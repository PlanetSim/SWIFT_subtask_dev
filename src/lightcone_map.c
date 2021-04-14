/*******************************************************************************
 * This file is part of SWIFT.
 * Copyright (c) 2021 John Helly (j.c.helly@durham.ac.uk)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

/* Config parameters. */
#include "../config.h"

/* Some standard headers. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* This object's header. */
#include "lightcone_map.h"

/* Local headers */
#include "align.h"
#include "error.h"
#include "exchange_structs.h"
#include "memuse.h"
#include "particle_buffer.h"
#include "restart.h"

/* Healpix C API header */
#ifdef HAVE_CHEALPIX
#include <chealpix.h>
#endif


void lightcone_map_init(struct lightcone_map *map, int nside,
                        size_t elements_per_block) {
  
  int comm_rank, comm_size;
#ifdef WITH_MPI  
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
#else
  comm_rank = 0;
  comm_size = 1;
#endif

  /* Initialise the data buffer for this map */
  particle_buffer_init(&map->buffer, sizeof(struct lightcone_map_contribution),
                       elements_per_block, "lightcone_map");

  /* Determine number of pixels in the map */
#ifdef HAVE_CHEALPIX
  map->total_nr_pix = (size_t) nside2npix((long) nside);
#else
  error("Can't make healpix maps without healpix library!");
#endif

  /* Determine which pixels are stored on which rank:
     put pix_per_rank on each node with any extra assigned to
     the last node. This makes it easy to convert a pixel index
     to a node index. */
  map->pix_per_rank = map->total_nr_pix / comm_size;
  if(map->pix_per_rank==0)error("Must have healpix npix > number of MPI ranks!");
  if(comm_rank < comm_size-1)
    map->local_nr_pix = map->pix_per_rank;
  else
    map->local_nr_pix = map->total_nr_pix - (comm_size-1)*map->pix_per_rank;
  
  /* Pixel data is initially not allocated */
  map->data = NULL;

  /* Record block size so we can re-initialise particle_buffer on restarting */
  map->elements_per_block = elements_per_block;

}


/**
 * @brief Deallocate the lightcone_map struct contents
 *
 * @param map the #lightcone_map structure
 */
void lightcone_map_clean(struct lightcone_map *map) {
  
  particle_buffer_free(&map->buffer);
  if(map->data)free(map->data);
}


void lightcone_map_allocate_pixels(struct lightcone_map *map) {
  
  if(swift_memalign("lightcone_map_pixels", (void **) &map->data,
                    SWIFT_STRUCT_ALIGNMENT, sizeof(double)*map->local_nr_pix) != 0)
    error("Failed to allocate lightcone map pixel data");
}


void lightcone_map_free_pixels(struct lightcone_map *map) {
  
  swift_free("lightcone_map_pixels", (void **) &map->data);
  map->data = NULL;

}


/**
 * @brief Dump lightcone_map struct to the output stream.
 *
 * @param map the #lightcone_map structure
 * @param stream The stream to write to.
 */
void lightcone_map_struct_dump(const struct lightcone_map *map, FILE *stream) {

  /* Don't write the particle_buffer (must flush before dumping) */
  struct lightcone_map tmp = *map;
  memset(&tmp.buffer, 0, sizeof(struct particle_buffer));

  /* Write the struct */
  restart_write_blocks((void *) &tmp, sizeof(struct lightcone_map), 1, stream,
                       "lightcone_map", "lightcone_map");

  /* Write the pixel data if it is allocated */
  if(tmp.data)
    restart_write_blocks((void *) tmp.data, sizeof(double), tmp.local_nr_pix, 
                         stream, "lightcone_map_data", "lightcone_map_data");
}


/**
 * @brief Restore lightcone_map struct from the input stream.
 *
 * @param map the #lightcone_map structure
 * @param stream The stream to read from.
 */
void lightcone_map_struct_restore(struct lightcone_map *map, FILE *stream) {

  /* Read the struct */
  restart_read_blocks((void *)map, sizeof(struct lightcone_map), 1, stream,
                      NULL, "lightcone_map");

  /* Initialise the buffer for this map */
  particle_buffer_init(&map->buffer, sizeof(struct lightcone_map_contribution),
                       map->elements_per_block, "lightcone_map");
  
  /* Read the pixel data if it was allocated.
     map->data from the restart file is not a valid pointer now but we can
     check if it is not null to see if the pixel data block was written out. */
  if(map->data) {
    lightcone_map_allocate_pixels(map);
    restart_read_blocks((void *)map->data, sizeof(double), map->local_nr_pix,
                        stream, NULL, "lightcone_map");
  }

}


/**
 * @brief Apply buffered updates to the healpix map
 *
 * @param map the #lightcone_map structure
 */
void lightcone_map_update_from_buffer(struct lightcone_map *map) {
  
#ifdef WITH_MPI

  /* Get MPI rank, number of ranks */
  int comm_rank, comm_size;
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);

  /* Array to store number of updates to send to each rank*/
  size_t *send_count = malloc(comm_size*sizeof(size_t));
  for(int i=0; i<comm_size; i+=1)
    send_count[i] = 0;

  /* Loop over buffered updates and count number to send to each rank */
  struct particle_buffer_block *block = NULL;
  struct lightcone_map_contribution *contr;
  size_t num_elements;
  do {
    particle_buffer_iterate(&map->buffer, &block, &num_elements, (void **) &contr);
    for(size_t i=0; i<num_elements; i+=1) {
      int dest = contr[i].pixel / map->pix_per_rank;
      if(dest >= comm_size) dest=comm_size-1;
      send_count[dest] += 1;
    }
  } while(block);
    
  /* Find total number of updates */
  size_t total_nr_send = particle_buffer_num_elements(&map->buffer);

  /* Allocate send buffer */
  struct lightcone_map_contribution *sendbuf = 
    malloc(sizeof(struct lightcone_map_contribution)*total_nr_send);

  /* Compute offsets into send buffer */
  size_t *send_offset = malloc(comm_size*sizeof(size_t));
  send_offset[0] = 0;
  for(int i=1; i<comm_size; i+=1)
    send_offset[i] = send_offset[i-1] + send_count[i-1];

  /* Populate the send buffer */
  for(int i=0; i<comm_size; i+=1)
    send_count[i] = 0;
  do {
    particle_buffer_iterate(&map->buffer, &block, &num_elements, (void **) &contr);
    for(size_t i=0; i<num_elements; i+=1) {
      int dest = contr[i].pixel / map->pix_per_rank;
      if(dest >= comm_size) dest=comm_size-1;

      /* Copy entry to the send buffer */
      struct lightcone_map_contribution *from = contr+i;
      struct lightcone_map_contribution *to   = sendbuf+send_offset[dest]+send_count[dest];
      memcpy(to, from, sizeof(struct lightcone_map_contribution));
      
      send_count[dest] += 1;
    }
  } while(block);
  
  /* Empty the buffer now that we copied the data from it */
  particle_buffer_empty(&map->buffer);

  /* Determine number of elements to receive */
  size_t *recv_count = malloc(comm_size*sizeof(size_t));
  MPI_Alltoall(send_count, sizeof(size_t), MPI_BYTE, recv_count, sizeof(size_t),
               MPI_BYTE, MPI_COMM_WORLD);
  size_t total_nr_recv = 0;
  for(int i=0; i<comm_size; i+=1)
    total_nr_recv += recv_count[i];
  
  /* Allocate receive buffer */
  struct lightcone_map_contribution *recvbuf = 
    malloc(sizeof(struct lightcone_map_contribution)*total_nr_recv);
  
  /* Exchange data */
  exchange_structs(send_count, sendbuf, recv_count, recvbuf,
                   sizeof(struct lightcone_map_contribution));

  /* Apply received updates to the healpix map */
  const size_t pixel_offset = map->pix_per_rank * comm_rank;
  for(size_t i=0; i<total_nr_recv; i+=1) {
    const size_t local_pixel = recvbuf[i].pixel - pixel_offset;
    if(recvbuf[i].pixel < pixel_offset || local_pixel >= map->local_nr_pix)
      error("pixel index out of range");
    map->data[local_pixel] += recvbuf[i].value;
  }

  /* Tidy up */
  free(send_count);
  free(send_offset);
  free(sendbuf);
  free(recv_count);
  free(recvbuf);

#else
  
  /* If not using MPI, we can update the map directly from the buffer */
  struct particle_buffer_block *block = NULL;
  struct lightcone_map_contribution *contr;
  size_t num_elements;
  do {
    particle_buffer_iterate(&map->buffer, &block, &num_elements, (void **) &contr);
    for(size_t i=0; i<num_elements; i+=1) {
      const size_t pixel = contr[i].pixel;
      map->data[pixel] += contr[i].value;
    }
  } while(block);
  particle_buffer_empty(&map->buffer);
    
#endif

}
