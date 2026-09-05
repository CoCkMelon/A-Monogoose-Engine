#ifndef BF_PHYSICS_H
#define BF_PHYSICS_H

/*
 * World collision + strut constraints. No Box2D, no 1000 Hz thread.
 * Chassis AABB resolves walls/ceiling only (floors=0); wheels are circles
 * vs AABB boxes and vs one-sided track segments (bezier level).
 */

#include "entities/car.h"

enum { PHYS_MAX_PLAT = 48, PHYS_MAX_SEG = 512 };

typedef struct PhysPlat {
    float cx, cy, hw, hh;
} PhysPlat;

typedef struct PhysSeg {
    float x0, y0, x1, y1;
    float nx, ny; /* driveable-side unit normal */
} PhysSeg;

typedef struct PhysWorld {
    PhysPlat plat[PHYS_MAX_PLAT];
    int n;
    PhysSeg seg[PHYS_MAX_SEG];
    int n_seg;
} PhysWorld;

void phys_world_clear(PhysWorld *w);
void phys_add_plat(PhysWorld *w, float cx, float cy, float width, float height);
void phys_add_seg(PhysWorld *w, float x0, float y0, float x1, float y1,
                  float nx, float ny);

void phys_body_axes(const Chassis *c, float *fx, float *fy, float *ux, float *uy);
void phys_attach_of(const Chassis *c, const Wheel *w,
                    float *ax, float *ay, float *avx, float *avy);
void phys_apply_force_at(Chassis *c, float px, float py, float fx, float fy, float dt);

int phys_circle_world(PhysWorld *w, float *x, float *y, float *vx, float *vy, float r,
                      int *grounded, float *nx, float *ny);
int phys_circle_segs(PhysWorld *w, float *x, float *y, float *vx, float *vy, float r,
                     int *grounded, float *nx, float *ny);
int phys_aabb_world(PhysWorld *w, float *x, float *y, float *vx, float *vy,
                    float hw, float hh, int *grounded, int *wall, int floors);

void phys_strut_forces(Chassis *car, Wheel *wheels, int n, float dt);
void phys_strut_lateral(Chassis *car, Wheel *wheels, int n);
void phys_strut_limits(Chassis *car, Wheel *wheels, int n);

#endif
