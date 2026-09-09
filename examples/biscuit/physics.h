#ifndef BF_PHYSICS_H
#define BF_PHYSICS_H

/*
 * Biscuit physics façade — thin aliases over library ame/phys.h.
 *
 * World collision + strut constraints live in the engine library.
 * Chassis / Wheel keep biscuit-only fields (hp, fuel, spin); their leading
 * layout matches ame_phys_body / ame_phys_wheel so casts are safe.
 *
 * Fixed-step rate comes from ame_logic / settings.yaml (game-owned).
 */

#include "ame/phys.h"
#include "entities/car.h"

enum { PHYS_MAX_PLAT = AME_PHYS_MAX_PLAT, PHYS_MAX_SEG = AME_PHYS_MAX_SEG };

typedef ame_phys_plat  PhysPlat;
typedef ame_phys_seg   PhysSeg;
typedef ame_phys_world PhysWorld;

/* Cast helpers: Chassis/Wheel share the ame_phys_* prefix. */
static inline ame_phys_body *bf_body(Chassis *c) { return (ame_phys_body *)(void *)c; }
static inline const ame_phys_body *bf_body_c(const Chassis *c)
{ return (const ame_phys_body *)(const void *)c; }
static inline ame_phys_wheel *bf_wheels(Wheel *w) { return (ame_phys_wheel *)(void *)w; }

void phys_world_clear(PhysWorld *w);
void phys_add_plat(PhysWorld *w, float cx, float cy, float width, float height);
void phys_add_seg(PhysWorld *w, float x0, float y0, float x1, float y1,
                  float nx, float ny);
void phys_world_prepare(PhysWorld *w);

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

/* Struts use biscuit REST_LEN / SUSP_* from car.h. */
void phys_strut_forces(Chassis *car, Wheel *wheels, int n, float dt);
void phys_strut_lateral(Chassis *car, Wheel *wheels, int n);
void phys_strut_limits(Chassis *car, Wheel *wheels, int n);

#endif
