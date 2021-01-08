/*******************************************************************************
 * This file is part of cVoronoi.
 * Copyright (c) 2020 Bert Vandenbroucke (bert.vandenbroucke@gmail.com)
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

/**
 * @file voronoi.h
 *
 * @brief 2D Voronoi grid.
 *
 * @author Bert Vandenbroucke (bert.vandenbroucke@ugent.be)
 */

#ifndef SWIFT_VORONOI_H
#define SWIFT_VORONOI_H

#include <string.h>

#include "voronoi.h"

/**
 * @brief Voronoi grid.
 *
 * The grid stores a copy of the coordinates of the grid generators, the
 * coordinates of the grid vertices and the edge connections that make up the
 * grid. For every generator, it stores the number of vertices for the cell
 * generated by it, and the offset of the cell edges in the edge array.
 */
struct voronoi {
  /*! @brief Cell generator positions. This is a copy of the array created in
   *  main() and we should get rid of this for SWIFT. */
  double *generators;

  /*! @brief Number of vertices per cell. */
  int *vertex_number;

  /*! @brief Offset of the first vertex for each cell in the connections
   *  array. */
  int *vertex_offset;

  /*! @brief Volume of each cell. */
  double *cell_volume;

  /*! @brief Centroid position of each cell. */
  double *cell_centroid;

  /*! @brief Number of cells (and number of generators). */
  int number_of_cells;

  /*! @brief Cell vertices. */
  double *vertices;

  /*! @brief Number of vertices. */
  int vertex_index;

  /*! @brief Size of the vertex array in memory. If vertex_size matches
   *  vertex_index, additional space needs to be allocated to fit more
   *  vertices. */
  int vertex_size;

  /*! @brief Cell connections. For each cell, we store the offset of the first
   *  vertex of that cell within this array. The cell edges then correspond to
   *  two consecutive elements in connections, with the last vertex for that
   *  cell (connections[vertex_offset[i]+vertex_number[i]-1]) wrapping around
   *  to make and edge with connections[vertex_offset[i]]. */
  int *connections;

  /*! @brief Midpoint of each edge connection. */
  double *face_midpoints;

  /*! @brief Length of each edge connection. */
  double *face_areas;

  /*! @brief Number of connections. */
  int connection_index;

  /*! @brief Size of the connection array in memory. If connection_size matches
   *  connection_index, additional space needs to be allocated to fit more
   *  connections. */
  int connection_size;
};

/**
 * @brief Add a new edge connection to the grid.
 *
 * @param v Voronoi grid.
 * @return Index of the new connection.
 */
static inline int voronoi_add_connection(struct voronoi *restrict v) {

  /* first check if we have valid elements */
  if (v->connection_index == v->connection_size) {
    /* no: double the size of the arrays */
    v->connection_size <<= 1;
    v->connections =
        (int *)realloc(v->connections, v->connection_size * sizeof(int));
    v->face_midpoints = (double *)realloc(
        v->face_midpoints, 2 * v->connection_size * sizeof(double));
    v->face_areas =
        (double *)realloc(v->face_areas, v->connection_size * sizeof(double));
  }
  /* return the old index and increment it. connection_index now corresponds
     to the size of the array and to the index of the next free element */
  return v->connection_index++;
}

/**
 * @brief Compute the volume and centroid of the triangle through the given 3
 * points.
 *
 * @param ax, ay, bx, by, cx, cy Point coordinates.
 * @param result Centroid of the triangle.
 * @return Volume of the triangle.
 */
static inline double voronoi_compute_centroid_volume_triangle(
    double ax, double ay, double bx, double by, double cx, double cy,
    double *result) {

  result[0] = (ax + bx + cx) / 3.;
  result[1] = (ay + by + cy) / 3.;

  double s10x = bx - ax;
  double s10y = by - ay;

  double s20x = cx - ax;
  double s20y = cy - ay;

  return 0.5 * fabs(s10x * s20y - s20x * s10y);
}

/**
 * @brief Compute the midpoint and surface area of the face with the given
 * vertices.
 *
 * @param ax, ay, bx, by Face vertices.
 * @param result Midpoint of the face.
 * @return Surface area of the face.
 */
static inline double voronoi_compute_midpoint_area_face(double ax, double ay,
                                                        double bx, double by,
                                                        double *result) {

  result[0] = 0.5 * (ax + bx);
  result[1] = 0.5 * (ay + by);

  double sx = bx - ax;
  double sy = by - ay;

  return sqrt(sx * sx + sy * sy);
}

/**
 * @brief Initialise the Voronoi grid based on the given Delaunay tessellation.
 *
 * This function allocates the memory for the Voronoi grid arrays and creates
 * the grid in linear time by
 *  1. Computing the grid vertices as the midpoints of the circumcircles of the
 *     Delaunay triangles.
 *  2. Looping over all vertices and for each vertex looping (in
 *     counterclockwise order) over all triangles that link to that vertex.
 *
 * During the second step, the geometrical properties (cell centroid, volume
 * and face midpoint, area) are computed as well.
 *
 * @param v Voronoi grid.
 * @param d Delaunay tessellation (read-only).
 */
static inline void voronoi_init(struct voronoi *restrict v,
                                const struct delaunay *restrict d) {

  delaunay_assert(d->vertex_end > 0);

  /* the number of cells equals the number of non-ghost and non-dummy vertices
     in the Delaunay tessellation */
  v->number_of_cells = d->vertex_end - d->vertex_start;
  /* allocate memory for the generators and the vertices */
  v->generators = (double *)malloc(2 * v->number_of_cells * sizeof(double));
  v->vertex_number = (int *)malloc(v->number_of_cells * sizeof(int));
  v->vertex_offset = (int *)malloc(v->number_of_cells * sizeof(int));
  v->cell_volume = (double *)malloc(v->number_of_cells * sizeof(double));
  v->cell_centroid = (double *)malloc(2 * v->number_of_cells * sizeof(double));
  memcpy(v->generators, d->vertices + 2 * d->vertex_start,
         2 * v->number_of_cells * sizeof(double));

  /* loop over the triangles in the Delaunay tessellation and compute the
     midpoints of their circumcircles. These happen to be the vertices of the
     Voronoi grid (because they are the points of equal distance to 3
     generators, while the Voronoi edges are the lines of equal distance to 2
     generators) */
  /* FUTURE NOTE: we can add a check here to see if the triangle is linked to
     a non-ghost, non-dummy vertex. If it isn't, it is not a grid vertex and
     we can skip it. */
  v->vertex_index = d->triangle_index - 3;
  v->vertex_size = v->vertex_index;
  v->vertices = (double *)malloc(2 * v->vertex_index * sizeof(double));
  for (int i = 0; i < d->triangle_index - 3; ++i) {
    struct triangle *t = &d->triangles[i + 3];
    int v0 = t->vertices[0];
    int v1 = t->vertices[1];
    int v2 = t->vertices[2];

    double v0x = d->vertices[2 * v0];
    double v0y = d->vertices[2 * v0 + 1];
    double v1x = d->vertices[2 * v1];
    double v1y = d->vertices[2 * v1 + 1];
    double v2x = d->vertices[2 * v2];
    double v2y = d->vertices[2 * v2 + 1];

    double ax = v1x - v0x;
    double ay = v1y - v0y;
    double bx = v2x - v0x;
    double by = v2y - v0y;

    double D = 2. * (ax * by - ay * bx);
    double a2 = ax * ax + ay * ay;
    double b2 = bx * bx + by * by;
    double Rx = (by * a2 - ay * b2) / D;
    double Ry = (ax * b2 - bx * a2) / D;

    v->vertices[2 * i] = v0x + Rx;
    v->vertices[2 * i + 1] = v0y + Ry;
  }

  /* now set up the grid connections. We do not know the number of connections
     beforehand, so we have to guess the size of the connections array and
     update it as we go. */
  v->connections = (int *)malloc(v->number_of_cells * sizeof(int));
  v->face_midpoints = (double *)malloc(2 * v->number_of_cells * sizeof(double));
  v->face_areas = (double *)malloc(v->number_of_cells * sizeof(double));
  v->connection_index = 0;
  v->connection_size = v->number_of_cells;
  /* loop over all cell generators, and hence over all non-ghost, non-dummy
     Delaunay vertices */
  for (int i = 0; i < v->number_of_cells; ++i) {

    /* initialise the cell volume and centroid and the temporary variables used
       to compute the centroid */
    v->cell_volume[i] = 0.;
    v->cell_centroid[2 * i] = 0.;
    v->cell_centroid[2 * i + 1] = 0.;
    double centroid[2];

    /* get the generator position, we use it during centroid/volume
       calculations */
    double ax = v->generators[2 * i];
    double ay = v->generators[2 * i + 1];

    /* Get a triangle containing this generator and the index of the generator
       within that triangle */
    int t0 = d->vertex_triangles[i + d->vertex_start];
    int vi0 = d->vertex_triangle_index[i + d->vertex_start];
    /* Add the first vertex for this cell: the circumcircle midpoint of this
       triangle */
    v->vertex_number[i] = 1;
    int c0 = voronoi_add_connection(v);
    v->vertex_offset[i] = c0;
    v->connections[c0] = t0 - 3;

    /* store the current vertex position for geometry calculations */
    double cx = v->vertices[2 * v->connections[c0]];
    double cy = v->vertices[2 * v->connections[c0] + 1];

    /* now use knowledge of the triangle orientation convention to obtain the
       next neighbouring triangle that has this generator as vertex, in the
       counterclockwise direction */
    int vi0p1 = (vi0 + 1) % 3;
    int t1 = d->triangles[t0].neighbours[vi0p1];
    int vi1 = d->triangles[t0].index_in_neighbour[vi0p1];
    /* loop around until we arrive back at the original triangle */
    while (t1 != t0) {
      ++v->vertex_number[i];
      int c1 = voronoi_add_connection(v);
      v->connections[c1] = t1 - 3;

      /* get the current vertex position for geometry calculations.
         Each calculation involves the current and the previous vertex.
         The face geometry is completely determined by these (the face is in
         this case simply the line segment between (bx,by) and (cx,cy).
         The cell geometry is calculated by accumulating the centroid and
         "volume" for the triangle (ax, ay) - (bx, by) - (cx, cy). */
      double bx = cx;
      double by = cy;
      cx = v->vertices[2 * v->connections[c1]];
      cy = v->vertices[2 * v->connections[c1] + 1];

      double V = voronoi_compute_centroid_volume_triangle(ax, ay, bx, by, cx,
                                                          cy, centroid);
      v->cell_volume[i] += V;
      v->cell_centroid[2 * i] += V * centroid[0];
      v->cell_centroid[2 * i + 1] += V * centroid[1];

      v->face_areas[c1] = voronoi_compute_midpoint_area_face(
          bx, by, cx, cy, v->face_midpoints + 2 * c1);

      int vi1p2 = (vi1 + 2) % 3;
      vi1 = d->triangles[t1].index_in_neighbour[vi1p2];
      t1 = d->triangles[t1].neighbours[vi1p2];
    }

    /* don't forget the last edge for the geometry! */
    double bx = cx;
    double by = cy;
    cx = v->vertices[2 * v->connections[c0]];
    cy = v->vertices[2 * v->connections[c0] + 1];

    double V = voronoi_compute_centroid_volume_triangle(ax, ay, bx, by, cx, cy,
                                                        centroid);
    v->cell_volume[i] += V;
    v->cell_centroid[2 * i] += V * centroid[0];
    v->cell_centroid[2 * i + 1] += V * centroid[1];

    v->face_areas[c0] = voronoi_compute_midpoint_area_face(
        bx, by, cx, cy, v->face_midpoints + 2 * c0);

    /* now compute the actual centroid by dividing the volume-weighted
       accumulators by the cell volume */
    v->cell_centroid[2 * i] /= v->cell_volume[i];
    v->cell_centroid[2 * i + 1] /= v->cell_volume[i];
  }
}

/**
 * @brief Free up all memory used by the Voronoi grid.
 *
 * @param v Voronoi grid.
 */
static inline void voronoi_destroy(struct voronoi *restrict v) {
  free(v->generators);
  free(v->vertex_number);
  free(v->vertex_offset);
  free(v->cell_volume);
  free(v->cell_centroid);
  free(v->vertices);
  free(v->connections);
  free(v->face_midpoints);
  free(v->face_areas);
}

/**
 * @brief Sanity checks on the grid.
 *
 * Right now, this only checks the total volume of the cells.
 */
static inline void voronoi_check_grid(const struct voronoi *restrict v) {
  double V = 0.;
  for (int i = 0; i < v->number_of_cells; ++i) {
    V += v->cell_volume[i];
  }

  printf("Total volume: %g\n", V);
}

/**
 * @brief Print the Voronoi grid to a file with the given name.
 *
 * The grid is output as follows:
 *  1. First, each generator is output, together with all its connections.
 *     The generator is output as "G\tx\ty\n", where x and y are the coordinates
 *     of the generator. The centroid of the corresponding cell is output as
 *     "M\tx\ty\n".
 *     The connections are output as "C\tindex\tindex\n", where the two indices
 *     are the indices of two vertices of the grid, in the order output by 2.
 *     The midpoint of each edge is output as "F\tx\ty\n".
 *  2. Next, all vertices of the grid are output, in the format "V\tx\ty\n".
 *
 * @param v Voronoi grid (read-only).
 * @param file_name Name of the output file.
 */
static inline void voronoi_print_grid(const struct voronoi *restrict v,
                                      const char *file_name) {

  FILE *file = fopen(file_name, "w");

  for (int i = 0; i < v->number_of_cells; ++i) {
    fprintf(file, "G\t%g\t%g\n", v->generators[2 * i],
            v->generators[2 * i + 1]);
    fprintf(file, "M\t%g\t%g\n", v->cell_centroid[2 * i],
            v->cell_centroid[2 * i + 1]);
    for (int j = 1; j < v->vertex_number[i]; ++j) {
      int cjm1 = v->vertex_offset[i] + j - 1;
      int cj = v->vertex_offset[i] + j;
      fprintf(file, "C\t%i\t%i\n", v->connections[cjm1], v->connections[cj]);
      fprintf(file, "F\t%g\t%g\n", v->face_midpoints[2 * cj],
              v->face_midpoints[2 * cj + 1]);
    }
    fprintf(file, "C\t%i\t%i\n",
            v->connections[v->vertex_offset[i] + v->vertex_number[i] - 1],
            v->connections[v->vertex_offset[i]]);
    fprintf(file, "F\t%g\t%g\n", v->face_midpoints[2 * v->vertex_offset[i]],
            v->face_midpoints[2 * v->vertex_offset[i] + 1]);
  }
  for (int i = 0; i < v->vertex_index; ++i) {
    fprintf(file, "V\t%g\t%g\n", v->vertices[2 * i], v->vertices[2 * i + 1]);
  }

  fclose(file);
}

#endif /* SWIFT_VORONOI_H */
