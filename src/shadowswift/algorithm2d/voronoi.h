//
// Created by yuyttenh on 24/03/22.
//

#ifndef SWIFTSIM_SHADOWSWIFT_VORONOI_2D_H
#define SWIFTSIM_SHADOWSWIFT_VORONOI_2D_H

#include "../queues.h"
#include "./delaunay.h"
#include "./geometry.h"
#include "part.h"
#include "triangle.h"

/**
 * @brief Voronoi interface.
 *
 * An interface is a connection between two neighbouring Voronoi cells. It is
 * completely defined by the indices of the generators that generate the two
 * neighbouring cells, a surface area and a midpoint position.
 */
struct voronoi_pair {
  /*! idx of the particle on the right of this pair in its respective swift
   * cell. Since the left particle is always local this is also the index of the
   * corresponding cell in this voronoi tesselation. */
  int left_idx;

  /*! idx of the particle on the right of this pair in its respective swift cell
   * if that cell is the same as the cell holding this Voronoi tesselation (i.e.
   * the particle is local) or in the super cell of its respective swift cell if
   * that swift cell is foreign. For local particles, this is also the index of
   * the corresponding cell in this voronoi tesselation. */
  int right_idx;

  /*! Real sid of this pair (boundary faces are stored under sid 27) */
  int sid;

  /*! Surface area of the interface. */
  double surface_area;

  /*! Midpoint of the interface. */
  double midpoint[3];

#ifdef VORONOI_STORE_FACES
  /*! First vertex of the interface. */
  double a[2];

  /*! Second vertex of the interface. */
  double b[2];
#endif
};

/**
 * @brief Voronoi grid.
 *
 * The grid stores a copy of the coordinates of the grid generators, the
 * coordinates of the grid vertices and the edge connections that make up the
 * grid. For every generator, it stores the number of vertices for the cell
 * generated by it, and the offset of the cell edges in the edge array.
 */
struct voronoi {

  /*! @brief Voronoi cell pairs. We store these per (SWIFT) cell, i.e. pairs[0]
   *  contains all pairs that are completely contained within this cell, while
   *  pairs[1] corresponds to pairs crossing the boundary between this cell and
   *  the cell with coordinates that are lower in all coordinate directions (the
   *  cell to the left, front, bottom, sid=0), and so on. SID 27 is reserved
   *  for boundary condition particles (e.g. reflective boundary conditions). */
  struct voronoi_pair *pairs[28];

  /*! @brief Current number of pairs per cell index. */
  int pair_index[28];

  /*! @brief Allocated number of pairs per cell index. */
  int pair_size[28];

  /*! @brief cell pair connection. Queue of 2-tuples containing the index of
   * the pair and the sid of the pair */
  struct int2_lifo_queue cell_pair_connections;

  /*! @brief Flag indicating whether this voronoi struct is active (has memory
   * allocated) */
  int active;

  /*! @brief The absolute minimal surface area of faces in this voronoi
   * tessellation */
  double min_surface_area;
};

/* Forward declarations */
static inline void voronoi_check_grid(const struct voronoi *restrict v);

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
  /* currently only 2d implementation */
  result[2] = 0.;

  double sx = bx - ax;
  double sy = by - ay;

  return sqrt(sx * sx + sy * sy);
}

/**
 * @brief Add a two particle pair to the grid.
 *
 * This function also adds the correct tuple to the cell_pair_connections queue.
 *
 * The grid connectivity is stored per cell sid: sid=0 corresponds to particle
 * pairs encountered during a self task (both particles are within the local
 * cell), while sid=1-26 correspond to particle interactions for which the right
 * neighbour is part of one of the 26 neighbouring cells.
 *
 * For each pair, we compute and store all the quantities required to compute
 * fluxes between the Voronoi cells: the surface area and midpoint of the
 * interface.
 *
 * @param v Voronoi grid.
 * @param d Delaunay tesselation, dual of this Voronoi grid.
 * @param del_vert_idx The index in the delaunay tesselation of the left vertex
 * of the new pair.
 * @param ngb_del_vert_idx The index in the delaunay tesselation of the right
 * vertex of the new pair.
 * @param part_is_active Array of flags indicating whether the corresponding
 * hydro parts are active (we only construct voronoi cells for active
 * particles).
 * @param ax,ay,bx,by Vertices of the interface.
 * @return 1 if the face was valid (non degenerate surface area), else 0.
 */
static inline int voronoi_add_pair(struct voronoi *v, const struct delaunay *d,
                                   int del_vert_idx, int ngb_del_vert_idx,
                                   struct part *parts,
                                   const int *part_is_active, double ax,
                                   double ay, double bx, double by) {
  int sid;
  int right_part_idx;
  /* Local pair? */
  if (ngb_del_vert_idx < d->ngb_offset) {
    right_part_idx = ngb_del_vert_idx - d->vertex_start;
    if (ngb_del_vert_idx < del_vert_idx && part_is_active[right_part_idx]) {
      /* Pair was already added. Find it and add it to the cell_pair_connections
       * if necessary. If no pair is found, the face must have been degenerate.
       * Return early. */
      struct part *ngb = &parts[right_part_idx];
      int left_part_idx = del_vert_idx - d->vertex_start;
      for (int i = 0; i < ngb->geometry.nface; i++) {
        int2 connection =
            v->cell_pair_connections
                .values[ngb->geometry.pair_connections_offset + i];
        if (v->pairs[connection._1][connection._0].right_idx == left_part_idx) {
          int2_lifo_queue_push(&v->cell_pair_connections, connection);
          return 1;
        }
      }
      return 0;
    }
    sid = 13;
  } else {
    sid = d->ngb_cell_sids[ngb_del_vert_idx - d->ngb_offset];
    right_part_idx = d->ngb_part_idx[ngb_del_vert_idx - d->ngb_offset];
  }

  /* Boundary particle? */
  int actual_sid = sid;
  if (sid & 1 << 5) {
    actual_sid &= ~(1 << 5);
    /* We store all boundary particles under fictive sid 27 */
    sid = 27;
  }

  /* Need to reallocate? */
  if (v->pair_index[sid] == v->pair_size[sid]) {
    v->pair_size[sid] <<= 1;
    v->pairs[sid] = (struct voronoi_pair *)swift_realloc(
        "voronoi", v->pairs[sid],
        v->pair_size[sid] * sizeof(struct voronoi_pair));
  }

  struct voronoi_pair *this_pair = &v->pairs[sid][v->pair_index[sid]];

  /* Skip degenerate faces (approximately 0 surface area) */
  double surface_area =
      voronoi_compute_midpoint_area_face(ax, ay, bx, by, this_pair->midpoint);
  if (surface_area < v->min_surface_area) {
    return 0;
  }

  this_pair->surface_area = surface_area;
  this_pair->left_idx = del_vert_idx - d->vertex_start;
  this_pair->right_idx = right_part_idx;
  this_pair->sid = actual_sid;

#ifdef VORONOI_STORE_FACES
  this_pair->a[0] = ax;
  this_pair->a[1] = ay;
  this_pair->b[0] = bx;
  this_pair->b[1] = by;
#endif

  /* Add cell_pair_connection */
  int2 connection = {._0 = v->pair_index[sid], ._1 = sid};
  int2_lifo_queue_push(&v->cell_pair_connections, connection);
  ++v->pair_index[sid];
  return 1;
}

/**
 * @brief Compute the circumcenter of the triangle through the given 3 points
 * */
inline static void voronoi_compute_circumcenter(double v0x, double v0y,
                                                double v1x, double v1y,
                                                double v2x, double v2y,
                                                double *circumcenter) {
  double ax = v1x - v0x;
  double ay = v1y - v0y;
  double bx = v2x - v0x;
  double by = v2y - v0y;

  double D = 2. * (ax * by - ay * bx);
  double a2 = ax * ax + ay * ay;
  double b2 = bx * bx + by * by;
  double Rx = (by * a2 - ay * b2) / D;
  double Ry = (ax * b2 - bx * a2) / D;

  circumcenter[0] = v0x + Rx;
  circumcenter[1] = v0y + Ry;
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

  geometry2d_compute_centroid_triangle(ax, ay, bx, by, cx, cy, result);

  double s10x = bx - ax;
  double s10y = by - ay;

  double s20x = cx - ax;
  double s20y = cy - ay;

  return 0.5 * fabs(s10x * s20y - s20x * s10y);
}

/**
 * @brief Free up all memory used by the Voronoi grid.
 *
 * @param v Voronoi grid.
 */
static inline void voronoi_destroy(struct voronoi *restrict v) {
  v->active = 0;

  if (v->cell_pair_connections.values != NULL) {
    int2_lifo_queue_destroy(&v->cell_pair_connections);
  }

  for (int i = 0; i < 28; i++) {
    if (v->pairs[i] != NULL) {
      swift_free("voronoi", v->pairs[i]);
      v->pairs[i] = NULL;
      v->pair_index[i] = 0;
      v->pair_size[i] = 0;
    }
  }

  v->min_surface_area = -1;

  free(v);
}

inline static struct voronoi *voronoi_malloc(int number_of_cells, double dmin) {
  struct voronoi *v = malloc(sizeof(struct voronoi));

  /* Allocate memory for the voronoi pairs (faces). */
  for (int i = 0; i < 28; ++i) {
    v->pairs[i] = (struct voronoi_pair *)swift_malloc(
        "voronoi", 10 * sizeof(struct voronoi_pair));
    v->pair_index[i] = 0;
    v->pair_size[i] = 10;
  }

  /* Allocate memory for the cell_pair connections */
  int2_lifo_queue_init(&v->cell_pair_connections, 6 * number_of_cells);

  v->min_surface_area = MIN_REL_FACE_SIZE * dmin;
  v->active = 1;

  return v;
}

inline static void voronoi_reset(struct voronoi *restrict v,
                                 int number_of_cells, double dmin) {
  voronoi_assert(v->active);

  /* reset indices for the voronoi pairs (faces). */
  for (int i = 0; i < 28; ++i) {
    v->pair_index[i] = 0;
  }

  /* Reset the cell_pair connections */
  int2_lifo_queue_reset(&v->cell_pair_connections);

  v->min_surface_area = MIN_REL_FACE_SIZE * dmin;
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
 * @param parts Array of parts of the SWIFT cell for which we are building the
 * voronoi tesselation.
 * @param part_is_active Flags indicating whether the particle is active.
 * @param count The number of hydro particles in #parts
 */
static inline void voronoi_build(struct voronoi *v, struct delaunay *d,
                                 struct part *parts, const int *part_is_active,
                                 int count) {

  voronoi_assert(d->vertex_end > 0);
  voronoi_assert(d->active);
  voronoi_assert(v->active);

  /* the number of cells equals the number of non-ghost and non-dummy
     vertex_indices in the Delaunay tessellation */
  voronoi_assert(v->number_of_cells == d->vertex_end - d->vertex_start);

  /* loop over the triangles in the Delaunay tessellation and compute the
     midpoints of their circumcircles. These happen to be the vertices of the
     Voronoi grid (because they are the points of equal distance to 3
     generators, while the Voronoi edges are the lines of equal distance to 2
     generators) */
  double *vertices =
      (double *)malloc(2 * (d->triangle_index - 3) * sizeof(double));
  for (int i = 0; i < d->triangle_index - 3; ++i) {
    struct triangle *t = &d->triangles[i + 3];
    int v0 = t->vertices[0];
    int v1 = t->vertices[1];
    int v2 = t->vertices[2];

    /* if the triangle is not linked to a non-ghost, non-dummy vertex,
     * corresponding to an active particle, it is not a grid vertex and we can
     * skip it. */
    if ((v0 >= d->vertex_end || v0 < d->vertex_start ||
         !part_is_active[v0 - d->vertex_start]) &&
        (v1 >= d->vertex_end || v1 < d->vertex_start ||
         !part_is_active[v1 - d->vertex_start]) &&
        (v2 >= d->vertex_end || v2 < d->vertex_start ||
         !part_is_active[v2 - d->vertex_start]))
      continue;

    if (v0 >= d->vertex_end && v0 < d->ngb_offset) {
      /* This could mean that a neighbouring cell of this grids cell is empty!
       * Or that we did not add all the necessary ghost vertices to the delaunay
       * tesselation. */
      error(
          "Vertex is part of triangle with Dummy vertex! This could mean that "
          "one of the neighbouring cells is empty.");
    }
    double v0x = d->vertices[2 * v0];
    double v0y = d->vertices[2 * v0 + 1];

    if (v1 >= d->vertex_end && v1 < d->ngb_offset) {
      error(
          "Vertex is part of triangle with Dummy vertex! This could mean that "
          "one of the neighbouring cells is empty.");
    }
    double v1x = d->vertices[2 * v1];
    double v1y = d->vertices[2 * v1 + 1];

    if (v2 >= d->vertex_end && v2 < d->ngb_offset) {
      error(
          "Vertex is part of triangle with Dummy vertex! This could mean that "
          "one of the neighbouring cells is empty.");
    }
    double v2x = d->vertices[2 * v2];
    double v2y = d->vertices[2 * v2 + 1];

    voronoi_compute_circumcenter(v0x, v0y, v1x, v1y, v2x, v2y,
                                 &vertices[2 * i]);
  } /* loop over the Delaunay triangles and compute the circumcenters */

  /* loop over all cell generators, and hence over all non-ghost, non-dummy
     Delaunay vertices and create the voronoi cell. */
  for (int i = 0; i < count; ++i) {

    /* Don't create voronoi cells for inactive particles */
    if (!part_is_active[i]) continue;

    struct part *p = &parts[i];
    double cell_volume = 0.;
    double cell_centroid[2] = {0., 0.};
    double centroid[2];
    int nface = 0;
    int pair_connections_offset = v->cell_pair_connections.index;
    double min_ngb_dist2 = DBL_MAX;
    double min_ngb_dist_pos[2] = {0., 0.};
    double generator_pos[2] = {p->x[0], p->x[1]};

    int del_vert_ix = i + d->vertex_start;
    /* get the generator position, we use it during centroid/volume
       calculations */
    if (del_vert_ix >= d->vertex_end) {
      error(
          "Found a ghost particle while looping over non-ghost, non-dummy "
          "particles!");
    }
    double ax = d->vertices[2 * del_vert_ix];
    double ay = d->vertices[2 * del_vert_ix + 1];

    /* Get a triangle containing this generator and the index of the generator
       within that triangle */
    int t0 = d->vertex_triangles[del_vert_ix];
    int del_vert_ix_in_t0 = d->vertex_triangle_index[del_vert_ix];
    /* Add the first vertex for this cell: the circumcircle midpoint of this
       triangle */
    int vor_vert_ix = t0 - 3;
    int first_vor_vert_ix = vor_vert_ix;

    /* store the current vertex position for geometry calculations */
    double cx = vertices[2 * vor_vert_ix];
    double cy = vertices[2 * vor_vert_ix + 1];

    /* now use knowledge of the triangle orientation convention to obtain the
       next neighbouring triangle that has this generator as vertex, in the
       counterclockwise direction */
    int next_t_ix_in_cur_t = (del_vert_ix_in_t0 + 1) % 3;

    int first_ngb_del_vert_ix = d->triangles[t0].vertices[next_t_ix_in_cur_t];

    int t1 = d->triangles[t0].neighbours[next_t_ix_in_cur_t];
    int cur_t_ix_in_next_t =
        d->triangles[t0].index_in_neighbour[next_t_ix_in_cur_t];
    /* loop around the voronoi cell generator (delaunay vertex) until we arrive
     * back at the original triangle */
    while (t1 != t0) {
      vor_vert_ix = t1 - 3;

      /* get the current vertex position for geometry calculations.
         Each calculation involves the current and the previous vertex.
         The face geometry is completely determined by these (the face is in
         this case simply the line segment between (bx,by) and (cx,cy).
         The cell geometry is calculated by accumulating the centroid and
         "volume" for the triangle (ax, ay) - (bx, by) - (cx, cy). */
      double bx = cx;
      double by = cy;
      cx = vertices[2 * vor_vert_ix];
      cy = vertices[2 * vor_vert_ix + 1];

      double V = voronoi_compute_centroid_volume_triangle(ax, ay, bx, by, cx,
                                                          cy, centroid);
      cell_volume += V;
      cell_centroid[0] += V * centroid[0];
      cell_centroid[1] += V * centroid[1];

      next_t_ix_in_cur_t = (cur_t_ix_in_next_t + 2) % 3;

      /* the neighbour corresponding to the face is the same vertex that
         determines the next triangle */
      int ngb_del_vert_ix = d->triangles[t1].vertices[next_t_ix_in_cur_t];
      if (voronoi_add_pair(v, d, del_vert_ix, ngb_del_vert_ix, parts,
                           part_is_active, bx, by, cx, cy)) {
        nface++;
        /* Update the minimal dist to a neighbouring generator */
        double ngb_pos[2];
        delaunay_get_vertex_at(d, ngb_del_vert_ix, ngb_pos);
        double dx[2] = {ngb_pos[0] - generator_pos[0],
                        ngb_pos[1] - generator_pos[1]};
        double dist2 = dx[0] * dx[0] + dx[1] * dx[1];
        if (dist2 < min_ngb_dist2) {
          min_ngb_dist2 = dist2;
          min_ngb_dist_pos[0] = ngb_pos[0];
          min_ngb_dist_pos[1] = ngb_pos[1];
        }
      }

      cur_t_ix_in_next_t =
          d->triangles[t1].index_in_neighbour[next_t_ix_in_cur_t];
      t1 = d->triangles[t1].neighbours[next_t_ix_in_cur_t];
    } /* loop around the voronoi cell generator */

    /* don't forget the last edge for the geometry! */
    double bx = cx;
    double by = cy;
    cx = vertices[2 * first_vor_vert_ix];
    cy = vertices[2 * first_vor_vert_ix + 1];

    double V = voronoi_compute_centroid_volume_triangle(ax, ay, bx, by, cx, cy,
                                                        centroid);
    cell_volume += V;
    cell_centroid[0] += V * centroid[0];
    cell_centroid[1] += V * centroid[1];

    if (voronoi_add_pair(v, d, del_vert_ix, first_ngb_del_vert_ix, parts,
                         part_is_active, bx, by, cx, cy)) {
      nface++;
      /* Update the minimal dist to a neighbouring generator */
      double ngb_pos[2];
      delaunay_get_vertex_at(d, first_ngb_del_vert_ix, ngb_pos);
      double dx[2] = {ngb_pos[0] - generator_pos[0],
                      ngb_pos[1] - generator_pos[1]};
      double dist2 = dx[0] * dx[0] + dx[1] * dx[1];
      if (dist2 < min_ngb_dist2) {
        min_ngb_dist2 = dist2;
        min_ngb_dist_pos[0] = ngb_pos[0];
        min_ngb_dist_pos[1] = ngb_pos[1];
      }
    }

    /* now compute the actual centroid by dividing the volume-weighted
       accumulators by the cell volume */
    cell_centroid[0] /= cell_volume;
    cell_centroid[1] /= cell_volume;

    /* now compute an estimate for the distance of the centroid to the closest
     * face. */
    double face[2] = {0.5 * (min_ngb_dist_pos[0] + p->x[0]),
                      0.5 * (min_ngb_dist_pos[1] + p->x[1])};
    double dx_gen[2] = {face[0] - p->x[0], face[1] - p->x[1]};
    double dx_cen[2] = {face[0] - cell_centroid[0], face[1] - cell_centroid[1]};
    double dist = (dx_cen[0] * dx_gen[0] + dx_cen[1] * dx_gen[1]) /
                  (0.5 * sqrt(min_ngb_dist2));

    /* Store the voronoi cell in the particle. */
    p->geometry.volume = (float)cell_volume;
    p->geometry.centroid[0] = (float)(cell_centroid[0] - p->x[0]);
    p->geometry.centroid[1] = (float)(cell_centroid[1] - p->x[1]);
    p->geometry.centroid[2] = 0.f;
    p->geometry.nface = nface;
    p->geometry.pair_connections_offset = pair_connections_offset;
    p->geometry.min_face_dist = (float)dist;
  } /* loop over all cell generators */

  voronoi_check_grid(v);
  free(vertices);
}

static inline double voronoi_compute_volume(const struct voronoi *restrict v) {
  /* TODO */
  return 0.0;
}

/**
 * @brief Sanity checks on the grid.
 *
 * Right now, this only checks the total volume of the cells.
 */
static inline void voronoi_check_grid(const struct voronoi *restrict v) {
#ifdef VORONOI_CHECKS
  /* Check total volume */
  double total_volume = 0.;
  for (int i = 0; i < v->number_of_cells; i++) {
    total_volume += v->cells[i].volume;
  }
  //  fprintf(stderr, "Total volume: %g\n", total_volume);

  /* For each cell check that the total surface area is not bigger than the
   * surface area of a sphere with the same volume */
  double *surface_areas = malloc(v->number_of_cells * sizeof(double));
  for (int i = 0; i < v->number_of_cells; i++) {
    surface_areas[i] = 0;
  }
  for (int sid = 0; sid < 28; sid++) {
    for (int i = 0; i < v->pair_index[sid]; i++) {
      struct voronoi_pair *pair = &v->pairs[sid][i];
      surface_areas[pair->left_idx] += pair->surface_area;
      if (sid == 13) {
        assert(pair->right_idx >= 0);
        surface_areas[pair->right_idx] += pair->surface_area;
      } else {
        assert(pair->right_idx == -1);
      }
    }
  }
  for (int i = 0; i < v->number_of_cells; i++) {
    double sphere_surface_area =
        2. * M_PI * pow(v->cells[i].volume / M_PI, 0.5);
    voronoi_assert(sphere_surface_area < surface_areas[i]);
  }
  free(surface_areas);

#if defined(VORONOI_STORE_CONNECTIONS) && defined(VORONOI_STORE_GENERATORS)
  /* Check connectivity */
  for (int i = 0; i < v->number_of_cells; i++) {
    struct voronoi_cell *this_cell = &v->cells[i];
    for (int j = this_cell->pair_connections_offset;
         j < this_cell->pair_connections_offset + this_cell->nface; j++) {
      int2 connection = v->cell_pair_connections.values[j];
      int pair_idx = connection._0;
      int sid = connection._1;
      struct voronoi_pair *pair = &v->pairs[sid][pair_idx];
      assert(i == pair->left_idx || (sid == 13 && i == pair->right_idx));
    }
  }
#endif
#endif
}

/**
 * @brief Write the Voronoi grid information to the given file.
 *
 * The output depends on the configuration. The maximal output contains 3
 * different types of output lines:
 *  - "G\tgx\tgy: x and y position of a single grid generator (optional).
 *  - "C\tcx\tcy\tV\tnface": centroid position, volume and (optionally) number
 *    of faces for a single Voronoi cell.
 *  - "F\tax\tay\tbx\tby\tleft\tngb\tright\tA\tmx\tmy": edge positions
 *    (optional), left and right generator index (and ngb cell index), surface
 *    area and midpoint position for a single two-pair interface.
 *
 * @param v Voronoi grid.
 * @param file File to write to.
 */
static inline void voronoi_write_grid(const struct voronoi *restrict v,
                                      const struct part *parts, int count,
                                      FILE *file, size_t *offset) {

  /* Write the generator positions */
  for (int i = 0; i < count; i++)
    fprintf(file, "G\t%g\t%g\t%lu\n", parts[i].x[0], parts[i].x[1],
            *offset + i);

  /* Write the centroid positions */
  for (int i = 0; i < count; i++)
    fprintf(file, "C\t%g\t%g\tV\t%i\t%lu\n",
            parts[i].x[0] + parts[i].geometry.centroid[0],
            parts[i].x[1] + parts[i].geometry.centroid[1],
            parts[i].geometry.nface, *offset + i);

  /* now write the pairs */
  for (int sid = 0; sid < 28; ++sid) {
    for (int i = 0; i < v->pair_index[sid]; ++i) {
      struct voronoi_pair *pair = &v->pairs[sid][i];
      fprintf(file, "F\t");
#ifdef VORONOI_STORE_FACES
      fprintf(file, "%g\t%g\t%g\t%g\t", pair->a[0], pair->a[1], pair->b[0],
              pair->b[1]);
#endif
      fprintf(file, "%i\t%g\t%g\t%g\t%lu\n", sid, pair->surface_area,
              pair->midpoint[0], pair->midpoint[1], *offset + pair->left_idx);
    }
  }

  /* Update *offset */
  *offset += count;
}

#endif  // SWIFTSIM_SHADOWSWIFT_VORONOI_2D_H
