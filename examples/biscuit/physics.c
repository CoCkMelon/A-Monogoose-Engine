#include "physics.h"

#include <stddef.h>

/*
 * Thin wrappers — implementation is ame_phys_* in the library.
 * Wheel arrays pass sizeof(Wheel) stride so game-only spin fields stay safe.
 */

void phys_world_clear(PhysWorld *w) { ame_phys_world_clear(w); }

void phys_add_plat(PhysWorld *w, float cx, float cy, float width, float height)
{
    ame_phys_add_plat(w, cx, cy, width, height);
}

void phys_add_seg(PhysWorld *w, float x0, float y0, float x1, float y1,
                  float nx, float ny)
{
    ame_phys_add_seg(w, x0, y0, x1, y1, nx, ny);
}

void phys_world_prepare(PhysWorld *w) { ame_phys_world_prepare(w); }

void phys_body_axes(const Chassis *c, float *fx, float *fy, float *ux, float *uy)
{
    ame_phys_body_axes(bf_body_c(c), fx, fy, ux, uy);
}

void phys_attach_of(const Chassis *c, const Wheel *w,
                    float *ax, float *ay, float *avx, float *avy)
{
    ame_phys_attach_of(bf_body_c(c), (const ame_phys_wheel *)(const void *)w,
                       ax, ay, avx, avy);
}

void phys_apply_force_at(Chassis *c, float px, float py, float fx, float fy, float dt)
{
    ame_phys_apply_force_at(bf_body(c), px, py, fx, fy, dt);
}

int phys_circle_world(PhysWorld *w, float *x, float *y, float *vx, float *vy, float r,
                      int *grounded, float *nx, float *ny)
{
    return ame_phys_circle_world(w, x, y, vx, vy, r, grounded, nx, ny);
}

int phys_circle_segs(PhysWorld *w, float *x, float *y, float *vx, float *vy, float r,
                     int *grounded, float *nx, float *ny)
{
    return ame_phys_circle_segs(w, x, y, vx, vy, r, grounded, nx, ny);
}

int phys_aabb_world(PhysWorld *w, float *x, float *y, float *vx, float *vy,
                    float hw, float hh, int *grounded, int *wall, int floors)
{
    return ame_phys_aabb_world(w, x, y, vx, vy, hw, hh, grounded, wall, floors);
}

void phys_strut_forces(Chassis *car, Wheel *wheels, int n, float dt)
{
    ame_phys_strut_forces(bf_body(car), wheels, n, sizeof(Wheel),
                          REST_LEN, SUSP_K, SUSP_D, dt);
}

void phys_strut_lateral(Chassis *car, Wheel *wheels, int n)
{
    ame_phys_strut_lateral(bf_body(car), wheels, n, sizeof(Wheel));
}

void phys_strut_limits(Chassis *car, Wheel *wheels, int n)
{
    ame_phys_strut_limits(bf_body(car), wheels, n, sizeof(Wheel),
                          SUSP_MIN, SUSP_MAX);
}
