#include "ame/phys.h"
#include "ame/geo.h"
#include "ame/math.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

void ame_phys_world_clear(ame_phys_world *w)
{
    if (!w) return;
    w->n_plat = 0;
    w->n_seg = 0;
    w->segs_sorted = 0;
}

void ame_phys_add_plat(ame_phys_world *w, float cx, float cy, float width, float height)
{
    if (!w || w->n_plat >= AME_PHYS_MAX_PLAT) return;
    ame_phys_plat *p = &w->plat[w->n_plat++];
    p->cx = cx; p->cy = cy; p->hw = width * 0.5f; p->hh = height * 0.5f;
}

void ame_phys_add_seg(ame_phys_world *w, float x0, float y0, float x1, float y1,
                  float nx, float ny)
{
    if (!w || w->n_seg >= AME_PHYS_MAX_SEG) return;
    float ln = sqrtf(nx * nx + ny * ny);
    if (ln < 1e-8f) return;
    float dx = x1 - x0, dy = y1 - y0;
    if (dx * dx + dy * dy < 1e-8f) return;
    ame_phys_seg *s = &w->seg[w->n_seg++];
    s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1;
    s->nx = nx / ln; s->ny = ny / ln;
    s->minx = (x0 < x1) ? x0 : x1;
    s->maxx = (x0 > x1) ? x0 : x1;
    w->segs_sorted = 0;
}

/* Insertion sort is fine — N <= 512 and prepare runs once per reset. */
void ame_phys_world_prepare(ame_phys_world *w)
{
    if (!w || w->n_seg < 2) {
        if (w) w->segs_sorted = 1;
        return;
    }
    for (int i = 1; i < w->n_seg; i++) {
        ame_phys_seg key = w->seg[i];
        int j = i - 1;
        while (j >= 0 && w->seg[j].minx > key.minx) {
            w->seg[j + 1] = w->seg[j];
            j--;
        }
        w->seg[j + 1] = key;
    }
    w->segs_sorted = 1;
}

/* First index with minx >= lo  (lower_bound). */
static int seg_lower_bound(const ame_phys_world *w, float lo)
{
    int lo_i = 0, hi_i = w->n_seg;
    while (lo_i < hi_i) {
        int mid = lo_i + (hi_i - lo_i) / 2;
        if (w->seg[mid].minx < lo) lo_i = mid + 1;
        else hi_i = mid;
    }
    return lo_i;
}

void ame_phys_body_axes(const ame_phys_body *c, float *fx, float *fy, float *ux, float *uy)
{
    float cs = cosf(c->a), sn = sinf(c->a);
    *fx = cs; *fy = sn;
    *ux = -sn; *uy = cs;
}

void ame_phys_attach_of(const ame_phys_body *c, const ame_phys_wheel *w,
                    float *ax, float *ay, float *avx, float *avy)
{
    float fx, fy, ux, uy;
    ame_phys_body_axes(c, &fx, &fy, &ux, &uy);
    *ax = c->x + fx * w->lx;
    *ay = c->y + fy * w->lx;
    float rx = *ax - c->x, ry = *ay - c->y;
    *avx = c->vx - c->omega * ry;
    *avy = c->vy + c->omega * rx;
}

void ame_phys_apply_force_at(ame_phys_body *c, float px, float py, float fx, float fy, float dt)
{
    c->vx += fx / c->mass * dt;
    c->vy += fy / c->mass * dt;
    float rx = px - c->x, ry = py - c->y;
    float tau = rx * fy - ry * fx;
    c->omega += tau / c->I * dt;
}

static ame_aabb plat_aabb(const ame_phys_plat *p)
{
    return ame_aabb_make(p->cx, p->cy, 0.0f, p->hw, p->hh, 2.0f);
}

int ame_phys_circle_segs(ame_phys_world *w, float *x, float *y, float *vx, float *vy, float r,
                     int *grounded, float *nx, float *ny)
{
    int hit = 0;
    int gnd = grounded ? *grounded : 0;
    float bnx = nx ? *nx : 0.0f, bny = ny ? *ny : 1.0f;
    if (!w) return 0;

    /* Ensure sorted (no-op if already prepared). */
    if (!w->segs_sorted) ame_phys_world_prepare(w);

    for (int pass = 0; pass < 3; pass++) {
        int any = 0;
        float lo = *x - r - 0.05f;
        float hi = *x + r + 0.05f;
        /* Start a little before lower_bound so segs that begin left of `lo`
         * but still overlap [lo,hi] are not missed: walk back while maxx>=lo. */
        int start = seg_lower_bound(w, lo);
        while (start > 0 && w->seg[start - 1].maxx >= lo) start--;

        for (int i = start; i < w->n_seg; i++) {
            ame_phys_seg *s = &w->seg[i];
            if (s->minx > hi) break;
            if (s->maxx < lo) continue;
            if (!ame_geo_circle_seg_xy(*x, *y, r, s->x0, s->y0, s->x1, s->y1,
                                       NULL, NULL, NULL))
                continue;
            float qx, qy;
            ame_geo_closest_on_seg_xy(s->x0, s->y0, s->x1, s->y1, *x, *y,
                                      &qx, &qy, NULL);
            float sd = (*x - qx) * s->nx + (*y - qy) * s->ny;
            if (sd < -0.10f) continue; /* deep underside of a one-sided ribbon */
            float push = r - sd;
            if (push <= 0.0f) continue;
            *x += s->nx * push;
            *y += s->ny * push;
            float vn = (*vx) * s->nx + (*vy) * s->ny;
            if (vn < 0.0f) {
                *vx -= vn * s->nx;
                *vy -= vn * s->ny;
            }
            hit = 1;
            any = 1;
            if (s->ny > 0.25f) {
                gnd = 1;
                bnx = s->nx;
                bny = s->ny;
            }
        }
        if (!any) break;
    }
    if (grounded) *grounded = gnd;
    if (nx) *nx = bnx;
    if (ny) *ny = bny;
    return hit;
}

int ame_phys_circle_world(ame_phys_world *w, float *x, float *y, float *vx, float *vy, float r,
                      int *grounded, float *nx, float *ny)
{
    int hit = 0;
    *grounded = 0;
    *nx = 0; *ny = 1;
    for (int i = 0; i < w->n_plat; i++) {
        ame_aabb b = plat_aabb(&w->plat[i]);
        float pnx, pny, pen;
        if (!ame_geo_circle_aabb_xy(&b, *x, *y, r, &pnx, &pny, &pen))
            continue;
        if (pen < 0.0f) continue;
        *x += pnx * pen;
        *y += pny * pen;
        float vn = (*vx) * pnx + (*vy) * pny;
        if (vn < 0.0f) {
            *vx -= vn * pnx;
            *vy -= vn * pny;
        }
        hit = 1;
        if (pny > 0.35f) {
            *grounded = 1;
            *nx = pnx; *ny = pny;
        }
    }
    if (ame_phys_circle_segs(w, x, y, vx, vy, r, grounded, nx, ny))
        hit = 1;
    return hit;
}

int ame_phys_aabb_world(ame_phys_world *w, float *x, float *y, float *vx, float *vy,
                    float hw, float hh, int *grounded, int *wall, int floors)
{
    int gnd = 0, wl = 0;
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < w->n_plat; i++) {
            ame_phys_plat *p = &w->plat[i];
            float dx = *x - p->cx, dy = *y - p->cy;
            float ox = (hw + p->hw) - fabsf(dx);
            float oy = (hh + p->hh) - fabsf(dy);
            if (ox <= 0.0f || oy <= 0.0f) continue;
            if (ox < oy) {
                float s = (dx < 0.0f) ? -1.0f : 1.0f;
                *x += ox * s;
                *vx = 0.0f;
                wl = (s < 0.0f) ? -1 : 1;
            } else {
                float s = (dy < 0.0f) ? -1.0f : 1.0f;
                if (s > 0.0f && !floors)
                    continue; /* car floor is the wheels */
                *y += oy * s;
                if (s > 0.0f && *vy <= 0.0f) {
                    gnd = 1;
                    *vy = 0.0f;
                } else if (s < 0.0f && *vy > 0.0f) {
                    *vy = 0.0f;
                }
            }
        }
    }
    if (grounded) *grounded = gnd;
    if (wall) *wall = wl;
    return gnd;
}

static ame_phys_wheel *wheel_at(void *wheels, int i, size_t stride)
{
    if (stride == 0) stride = sizeof(ame_phys_wheel);
    return (ame_phys_wheel *)((char *)wheels + (size_t)i * stride);
}

void ame_phys_strut_forces(ame_phys_body *car, void *wheels, int n, size_t stride,
                           float rest_len, float k, float d, float dt)
{
    float fx, fy, ux, uy;
    ame_phys_body_axes(car, &fx, &fy, &ux, &uy);
    for (int i = 0; i < n; i++) {
        ame_phys_wheel *w = wheel_at(wheels, i, stride);
        float ax, ay, avx, avy;
        ame_phys_attach_of(car, w, &ax, &ay, &avx, &avy);
        float dx = w->x - ax, dy = w->y - ay;
        float along = dx * ux + dy * uy;
        float err = along + rest_len;
        float rel = (w->vx - avx) * ux + (w->vy - avy) * uy;
        float F = -k * err - d * rel;
        float wfx = ux * F, wfy = uy * F;
        w->vx += wfx / w->mass * dt;
        w->vy += wfy / w->mass * dt;
        ame_phys_apply_force_at(car, ax, ay, -wfx, -wfy, dt);
    }
}

void ame_phys_strut_lateral(ame_phys_body *car, void *wheels, int n, size_t stride)
{
    float fx, fy, ux, uy;
    ame_phys_body_axes(car, &fx, &fy, &ux, &uy);
    (void)ux; (void)uy;
    for (int i = 0; i < n; i++) {
        ame_phys_wheel *w = wheel_at(wheels, i, stride);
        float ax, ay, avx, avy;
        ame_phys_attach_of(car, w, &ax, &ay, &avx, &avy);
        float dx = w->x - ax, dy = w->y - ay;
        float lat = dx * fx + dy * fy;
        float mb = car->mass, mw = w->mass;
        float tw = mb / (mb + mw);
        float tb = mw / (mb + mw);
        w->x -= lat * fx * tw;
        w->y -= lat * fy * tw;
        car->x += lat * fx * tb;
        car->y += lat * fy * tb;

        ame_phys_attach_of(car, w, &ax, &ay, &avx, &avy);
        float rlv = (w->vx - avx) * fx + (w->vy - avy) * fy;
        float rx = ax - car->x, ry = ay - car->y;
        float cr = rx * fy - ry * fx;
        float inv = 1.0f / mw + 1.0f / mb + (cr * cr) / car->I;
        if (inv < 1e-8f) continue;
        float J = -rlv / inv;
        w->vx += (J / mw) * fx;
        w->vy += (J / mw) * fy;
        car->vx -= (J / mb) * fx;
        car->vy -= (J / mb) * fy;
        car->omega -= cr * J / car->I;
    }
}

void ame_phys_strut_limits(ame_phys_body *car, void *wheels, int n, size_t stride,
                           float susp_min, float susp_max)
{
    float fx, fy, ux, uy;
    ame_phys_body_axes(car, &fx, &fy, &ux, &uy);
    (void)fx; (void)fy;
    for (int i = 0; i < n; i++) {
        ame_phys_wheel *w = wheel_at(wheels, i, stride);
        float ax, ay, avx, avy;
        ame_phys_attach_of(car, w, &ax, &ay, &avx, &avy);
        float dx = w->x - ax, dy = w->y - ay;
        float along = dx * ux + dy * uy;
        float len = -along;
        float rv = (w->vx - avx) * ux + (w->vy - avy) * uy;
        if (len < susp_min) {
            float need = susp_min - len;
            car->x += ux * need;
            car->y += uy * need;
            if (rv > 0.0f) {
                float imp = rv;
                car->vx += ux * imp;
                car->vy += uy * imp;
            }
        } else if (len > susp_max) {
            w->x = ax - ux * susp_max;
            w->y = ay - uy * susp_max;
            if (rv < 0.0f) {
                w->vx -= rv * ux;
                w->vy -= rv * uy;
            }
        }
    }
}
