#ifndef AME_PHYS_H
#define AME_PHYS_H

/*
 * Lightweight 2D physics helpers (library).
 *
 * Not a full rigid-body engine / not Box2D. Provides:
 *   - static world of AABB platforms + one-sided segments (sorted broadphase)
 *   - circle / AABB resolve against that world
 *   - spring-damper strut helpers for a chassis + wheel pair
 *
 * Games own vehicle/character state (masses, motors, HP, spin). This module
 * only moves positions/velocities the game passes in.
 *
 * Side-view convention: XY, Y up, gravity is the game's job.
 *
 * Layout note: ame_phys_body / ame_phys_wheel are the shared prefix of biscuit's
 * Chassis / Wheel. Games may embed extra fields after the prefix and cast.
 */

#include "ame/math.h"

#include <stddef.h>

enum { AME_PHYS_MAX_PLAT = 48, AME_PHYS_MAX_SEG = 512 };

typedef struct ame_phys_plat {
    float cx, cy, hw, hh;
} ame_phys_plat;

typedef struct ame_phys_seg {
    float x0, y0, x1, y1;
    float nx, ny;       /* driveable-side unit normal */
    float minx, maxx;   /* broadphase bounds */
} ame_phys_seg;

typedef struct ame_phys_world {
    ame_phys_plat plat[AME_PHYS_MAX_PLAT];
    int n_plat;
    ame_phys_seg seg[AME_PHYS_MAX_SEG];
    int n_seg;
    int segs_sorted;
} ame_phys_world;

/* Shared rigid-body prefix (x..I). Games may append game fields after. */
typedef struct ame_phys_body {
    float x, y, vx, vy;
    float a, omega;     /* angle + angular vel (rad) */
    float w, h;         /* full width/height (AABB); unused by struts */
    float mass, I;
} ame_phys_body;

/* Shared wheel prefix. Games may append spin etc. after. */
typedef struct ame_phys_wheel {
    float lx;           /* body-space axle x */
    float x, y, vx, vy;
    float r, mass;
    int   grounded;
    float nx, ny;
} ame_phys_wheel;

void ame_phys_world_clear(ame_phys_world *w);
void ame_phys_add_plat(ame_phys_world *w, float cx, float cy, float width, float height);
void ame_phys_add_seg(ame_phys_world *w, float x0, float y0, float x1, float y1,
                      float nx, float ny);
void ame_phys_world_prepare(ame_phys_world *w); /* sort segs by minx */

void ame_phys_body_axes(const ame_phys_body *b,
                        float *fx, float *fy, float *ux, float *uy);
void ame_phys_attach_of(const ame_phys_body *b, const ame_phys_wheel *w,
                        float *ax, float *ay, float *avx, float *avy);
void ame_phys_apply_force_at(ame_phys_body *b, float px, float py,
                             float fx, float fy, float dt);

int ame_phys_circle_segs(ame_phys_world *w,
                         float *x, float *y, float *vx, float *vy, float r,
                         int *grounded, float *nx, float *ny);
int ame_phys_circle_world(ame_phys_world *w,
                          float *x, float *y, float *vx, float *vy, float r,
                          int *grounded, float *nx, float *ny);
int ame_phys_aabb_world(ame_phys_world *w,
                        float *x, float *y, float *vx, float *vy,
                        float hw, float hh, int *grounded, int *wall, int floors);

/* Strut constraints: rest_len / min / max / k / d are game params. */
/* wheels may be a larger game struct whose LEADING fields match ame_phys_wheel.
 * Pass stride = sizeof(GameWheel). stride 0 means sizeof(ame_phys_wheel). */
void ame_phys_strut_forces(ame_phys_body *body, void *wheels, int n, size_t stride,
                           float rest_len, float k, float d, float dt);
void ame_phys_strut_lateral(ame_phys_body *body, void *wheels, int n, size_t stride);
void ame_phys_strut_limits(ame_phys_body *body, void *wheels, int n, size_t stride,
                           float susp_min, float susp_max);

#endif
