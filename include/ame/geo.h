#ifndef AME_GEO_H
#define AME_GEO_H

/*
 * Geometry queries only. No rigid-body solver, no BVH, no Box2D.
 * Memory uses point-in-AABB on XY for picking (cursor vs card rect).
 * Biscuit wheels use circle vs AABB / segment. Ray vs AABB/OBB/tri/quad
 * are for a later 3D picker. `t` is parametric in `ray.dir` (not metres
 * unless dir is unit).
 */

#include "ame/math.h"

typedef struct ame_aabb {
    vec3 min;
    vec3 max;
} ame_aabb;

typedef struct ame_obb {
    vec3 center;
    vec3 axis[3]; /* unit local axes in world (column i of the rotation) */
    vec3 half;
} ame_obb;

typedef struct ame_ray {
    vec3 origin;
    vec3 dir;
    float tmin;
    float tmax;
} ame_ray;

typedef struct ame_hit {
    int   hit;
    float t;
    vec3  p;
    vec3  n;
} ame_hit;

ame_aabb ame_aabb_make(float cx, float cy, float cz,
                       float hx, float hy, float hz);
ame_aabb ame_aabb_from_minmax(vec3 min, vec3 max);
vec3     ame_aabb_center(const ame_aabb *b);
vec3     ame_aabb_extents(const ame_aabb *b); /* half-size */
ame_aabb ame_aabb_inflate(ame_aabb b, float r);

ame_ray  ame_ray_make(float ox, float oy, float oz,
                      float dx, float dy, float dz, float tmax);

/* Axis-aligned OBB (identity rotation). */
ame_obb ame_obb_axis(float cx, float cy, float cz,
                     float hx, float hy, float hz);
/* Unity-like: centre, rotation, half-extents. */
ame_obb ame_obb_make(vec3 center, quat rotation, vec3 half);

int ame_geo_point_in_aabb_xy(const ame_aabb *b, float x, float y);
int ame_geo_point_in_aabb(const ame_aabb *b, vec3 p);
int ame_geo_aabb_overlap(const ame_aabb *a, const ame_aabb *b);
int ame_geo_aabb_overlap_xy(const ame_aabb *a, const ame_aabb *b);

/* MTV on XY to move *a* out of *b*. nx,ny unit; *pen* > 0. */
int ame_geo_aabb_aabb_xy(const ame_aabb *a, const ame_aabb *b,
                         float *nx, float *ny, float *pen);

/* Circle vs AABB on XY (z ignored). 1 if overlapping.
 * nx,ny = unit push from box toward circle centre; *pen = penetration. */
int ame_geo_circle_aabb_xy(const ame_aabb *b, float cx, float cy, float r,
                           float *nx, float *ny, float *pen);

/* Closest point on a finite segment. Writes *t in [0,1]. */
void ame_geo_closest_on_seg_xy(float x0, float y0, float x1, float y1,
                               float px, float py,
                               float *qx, float *qy, float *t);

/* Circle vs finite segment on XY. 1 if overlapping.
 * nx,ny = unit push from closest point toward circle centre; *pen = r - dist. */
int ame_geo_circle_seg_xy(float cx, float cy, float r,
                          float x0, float y0, float x1, float y1,
                          float *nx, float *ny, float *pen);

/* Circle vs circle on XY. nx,ny push c0 out of c1. */
int ame_geo_circle_circle_xy(float x0, float y0, float r0,
                             float x1, float y1, float r1,
                             float *nx, float *ny, float *pen);

int ame_geo_ray_aabb(const ame_ray *r, const ame_aabb *b, ame_hit *out);
int ame_geo_ray_obb(const ame_ray *r, const ame_obb *o, ame_hit *out);
int ame_geo_ray_tri(const ame_ray *r, vec3 a, vec3 b, vec3 c, ame_hit *out);
int ame_geo_ray_quad(const ame_ray *r, vec3 v0, vec3 v1, vec3 v2, vec3 v3,
                     ame_hit *out);

#endif
