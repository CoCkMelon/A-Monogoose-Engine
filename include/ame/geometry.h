/* ame-next — geometry / collision-query module (physics.txt).
 *
 * v0 has NO rigid-body physics: this module is pure geometry —
 * intersections, raycasts, overlap/containment, static-world queries with a
 * uniform-grid broadphase. Actors move themselves; games test shapes and
 * react in their own code. No solver, no bodies, no callbacks.
 *
 * Dimension-safe: AME_DIM decides 2D (x,y) or 3D (x,y,z); the SAME functions
 * exist in both builds. Pure C, no allocation, deterministic ordering.
 *
 * Static world: built once at level load (geo_add_static_*), then
 * geo_rebuild_broadphase(); never mutated during play. Dynamic shapes are
 * passed by value to the query functions.
 */
#ifndef AME_GEOMETRY_H
#define AME_GEOMETRY_H

#include <ame/ame.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AME_GEO_MAX_STATIC 1024
#define AME_GEO_MAX_HITS    64

/* user flags per static shape (events.txt composes EV_HAZARD from these) */
enum {
    AME_GEO_FLAG_SOLID   = 1u << 0,
    AME_GEO_FLAG_HAZARD  = 1u << 1,
    AME_GEO_FLAG_TRIGGER = 1u << 2,
};

typedef struct { float c[AME_DIM]; float h[AME_DIM]; } ame_aabb; /* center+half */
typedef struct { float c[AME_DIM]; float r; }          ame_sphere; /* circle in 2D */
typedef struct { float a[AME_DIM]; float b[AME_DIM]; } ame_seg;

typedef struct {
    float o[AME_DIM]; /* origin */
    float d[AME_DIM]; /* direction (normalized by caller or |d| scales tmax) */
    float tmax;       /* max distance along d (in units of |d|) */
} ame_ray;

typedef struct {
    float t;              /* hit distance along d (FLT_MAX = none) */
    float p[AME_DIM];     /* hit point */
    float n[AME_DIM];     /* hit normal */
    int   shape;          /* index into static world, -1 = none */
    uint32_t flags;       /* static shape flags */
} ame_hit;

/* --- primitive-vs-primitive (pure, no world state) ------------------------ */

bool ame_geo_aabb_overlap(ame_aabb a, ame_aabb b);
bool ame_geo_sphere_overlap(ame_sphere a, ame_sphere b);
bool ame_geo_aabb_sphere_overlap(ame_aabb b, ame_sphere s);
bool ame_geo_point_in_aabb(const float p[AME_DIM], ame_aabb b);
bool ame_geo_point_in_sphere(const float p[AME_DIM], ame_sphere s);
float ame_geo_dist(const float a[AME_DIM], const float b[AME_DIM]);
/* closest point on segment a to point p; out set, returns sq distance */
float ame_geo_seg_closest_pt(ame_seg s, const float p[AME_DIM], float out[AME_DIM]);

/* segment-segment intersection (2D): true if they cross, out = point */
#if AME_DIM == 2
bool ame_geo_seg_intersect(ame_seg a, ame_seg b, float out[2]);
#endif

/* ray vs primitives: fills hit (t=FLT_MAX when missed); returns true on hit */
bool ame_geo_ray_aabb(ame_ray r, ame_aabb b, ame_hit *out);
bool ame_geo_ray_sphere(ame_ray r, ame_sphere s, ame_hit *out);

/* --- static world (built once per level, queried every step) -------------- */

void  ame_geo_reset(void);
/* add a static shape; returns its index (>= 0) or -1 when the table is full */
int   ame_geo_add_aabb(ame_aabb box, uint32_t flags);
int   ame_geo_add_sphere(ame_sphere s, uint32_t flags);
/* compute bounds over everything added and build the uniform grid */
void  ame_geo_rebuild_broadphase(void);

ame_aabb ame_geo_static_aabb(int i); /* bounds of static shape i */

/* nearest raycast against the static world (broadphase + exact tests) */
bool ame_geo_raycast(ame_ray r, ame_hit *out);

/* collect up to AME_GEO_MAX_HITS static shapes overlapping `box`
 * (ascending index order — deterministic). returns count. */
int   ame_geo_overlap_world(ame_aabb box, int out_indices[AME_GEO_MAX_HITS]);

/* how many static shapes are in the world */
int   ame_geo_static_count(void);

#ifdef __cplusplus
}
#endif

#endif /* AME_GEOMETRY_H */
