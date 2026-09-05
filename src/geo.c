#include "ame/geo.h"

#include <math.h>
#include <string.h>

static const float EPS = 1e-8f;

static void hit_clear(ame_hit *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
}

static float absf(float x) { return x < 0.0f ? -x : x; }

ame_aabb ame_aabb_make(float cx, float cy, float cz,
                       float hx, float hy, float hz)
{
    if (hx < 0.0f) hx = -hx;
    if (hy < 0.0f) hy = -hy;
    if (hz < 0.0f) hz = -hz;
    ame_aabb b;
    b.min = v3(cx - hx, cy - hy, cz - hz);
    b.max = v3(cx + hx, cy + hy, cz + hz);
    return b;
}

ame_aabb ame_aabb_from_minmax(vec3 min, vec3 max)
{
    ame_aabb b;
    b.min = v3(min.x < max.x ? min.x : max.x,
               min.y < max.y ? min.y : max.y,
               min.z < max.z ? min.z : max.z);
    b.max = v3(min.x > max.x ? min.x : max.x,
               min.y > max.y ? min.y : max.y,
               min.z > max.z ? min.z : max.z);
    return b;
}

vec3 ame_aabb_center(const ame_aabb *b)
{
    if (!b) return v3(0, 0, 0);
    return v3_scale(v3_add(b->min, b->max), 0.5f);
}

vec3 ame_aabb_extents(const ame_aabb *b)
{
    if (!b) return v3(0, 0, 0);
    return v3_scale(v3_sub(b->max, b->min), 0.5f);
}

ame_aabb ame_aabb_inflate(ame_aabb b, float r)
{
    vec3 e = v3(r, r, r);
    b.min = v3_sub(b.min, e);
    b.max = v3_add(b.max, e);
    return b;
}

ame_ray ame_ray_make(float ox, float oy, float oz,
                     float dx, float dy, float dz, float tmax)
{
    ame_ray r;
    r.origin = v3(ox, oy, oz);
    r.dir = v3(dx, dy, dz);
    r.tmin = 0.0f;
    r.tmax = tmax;
    return r;
}

ame_obb ame_obb_make(vec3 center, quat rotation, vec3 half)
{
    ame_obb o;
    o.center = center;
    o.half = v3(absf(half.x), absf(half.y), absf(half.z));
    o.axis[0] = quat_rotate(rotation, v3(1, 0, 0));
    o.axis[1] = quat_rotate(rotation, v3(0, 1, 0));
    o.axis[2] = quat_rotate(rotation, v3(0, 0, 1));
    return o;
}

ame_obb ame_obb_axis(float cx, float cy, float cz,
                     float hx, float hy, float hz)
{
    return ame_obb_make(v3(cx, cy, cz), quat_ident(), v3(hx, hy, hz));
}

int ame_geo_point_in_aabb_xy(const ame_aabb *b, float x, float y)
{
    if (!b) return 0;
    return x >= b->min.x && x <= b->max.x &&
           y >= b->min.y && y <= b->max.y;
}

int ame_geo_point_in_aabb(const ame_aabb *b, vec3 p)
{
    if (!b) return 0;
    return p.x >= b->min.x && p.x <= b->max.x &&
           p.y >= b->min.y && p.y <= b->max.y &&
           p.z >= b->min.z && p.z <= b->max.z;
}

int ame_geo_aabb_overlap(const ame_aabb *a, const ame_aabb *b)
{
    if (!a || !b) return 0;
    return a->min.x <= b->max.x && a->max.x >= b->min.x &&
           a->min.y <= b->max.y && a->max.y >= b->min.y &&
           a->min.z <= b->max.z && a->max.z >= b->min.z;
}

int ame_geo_aabb_overlap_xy(const ame_aabb *a, const ame_aabb *b)
{
    if (!a || !b) return 0;
    return a->min.x <= b->max.x && a->max.x >= b->min.x &&
           a->min.y <= b->max.y && a->max.y >= b->min.y;
}

int ame_geo_aabb_aabb_xy(const ame_aabb *a, const ame_aabb *b,
                         float *nx, float *ny, float *pen)
{
    if (!a || !b) return 0;
    vec3 ca = ame_aabb_center(a), cb = ame_aabb_center(b);
    vec3 ha = ame_aabb_extents(a), hb = ame_aabb_extents(b);
    float dx = ca.x - cb.x, dy = ca.y - cb.y;
    float ox = (ha.x + hb.x) - absf(dx);
    float oy = (ha.y + hb.y) - absf(dy);
    if (ox <= 0.0f || oy <= 0.0f) return 0;
    if (ox < oy) {
        float s = (dx < 0.0f) ? -1.0f : 1.0f;
        if (nx) *nx = s;
        if (ny) *ny = 0.0f;
        if (pen) *pen = ox;
    } else {
        float s = (dy < 0.0f) ? -1.0f : 1.0f;
        if (nx) *nx = 0.0f;
        if (ny) *ny = s;
        if (pen) *pen = oy;
    }
    return 1;
}

int ame_geo_circle_aabb_xy(const ame_aabb *b, float cx, float cy, float r,
                           float *nx, float *ny, float *pen)
{
    if (!b || r <= 0.0f) return 0;
    float px = cx, py = cy;
    if (px < b->min.x) px = b->min.x;
    if (px > b->max.x) px = b->max.x;
    if (py < b->min.y) py = b->min.y;
    if (py > b->max.y) py = b->max.y;
    float dx = cx - px, dy = cy - py;
    float d2 = dx * dx + dy * dy;
    if (d2 > r * r) return 0;
    if (d2 < 1e-12f) {
        float dl = cx - b->min.x, dr = b->max.x - cx;
        float db = cy - b->min.y, dt = b->max.y - cy;
        float m = dl;
        float sx = -1.0f, sy = 0.0f;
        if (dr < m) { m = dr; sx = 1.0f; sy = 0.0f; }
        if (db < m) { m = db; sx = 0.0f; sy = -1.0f; }
        if (dt < m) { m = dt; sx = 0.0f; sy = 1.0f; }
        if (nx) *nx = sx;
        if (ny) *ny = sy;
        if (pen) *pen = r + m;
        return 1;
    }
    float d = sqrtf(d2);
    if (nx) *nx = dx / d;
    if (ny) *ny = dy / d;
    if (pen) *pen = r - d;
    return 1;
}

void ame_geo_closest_on_seg_xy(float x0, float y0, float x1, float y1,
                               float px, float py,
                               float *qx, float *qy, float *t_out)
{
    float dx = x1 - x0, dy = y1 - y0;
    float l2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (l2 >= 1e-12f) {
        t = ((px - x0) * dx + (py - y0) * dy) / l2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    if (t_out) *t_out = t;
    if (qx) *qx = x0 + t * dx;
    if (qy) *qy = y0 + t * dy;
}

int ame_geo_circle_seg_xy(float cx, float cy, float r,
                          float x0, float y0, float x1, float y1,
                          float *nx, float *ny, float *pen)
{
    if (r <= 0.0f) return 0;
    float px, py;
    ame_geo_closest_on_seg_xy(x0, y0, x1, y1, cx, cy, &px, &py, NULL);
    float ex = cx - px, ey = cy - py;
    float d2 = ex * ex + ey * ey;
    if (d2 > r * r) return 0;
    if (d2 < 1e-12f) {
        float dx = x1 - x0, dy = y1 - y0;
        float ln = sqrtf(dx * dx + dy * dy);
        if (ln < 1e-8f) { if (nx) *nx = 0; if (ny) *ny = 1; }
        else { if (nx) *nx = -dy / ln; if (ny) *ny = dx / ln; }
        if (pen) *pen = r;
        return 1;
    }
    float d = sqrtf(d2);
    if (nx) *nx = ex / d;
    if (ny) *ny = ey / d;
    if (pen) *pen = r - d;
    return 1;
}

int ame_geo_circle_circle_xy(float x0, float y0, float r0,
                             float x1, float y1, float r1,
                             float *nx, float *ny, float *pen)
{
    if (r0 <= 0.0f || r1 <= 0.0f) return 0;
    float dx = x0 - x1, dy = y0 - y1;
    float d2 = dx * dx + dy * dy;
    float rr = r0 + r1;
    if (d2 > rr * rr) return 0;
    if (d2 < 1e-12f) {
        if (nx) *nx = 0;
        if (ny) *ny = 1;
        if (pen) *pen = rr;
        return 1;
    }
    float d = sqrtf(d2);
    if (nx) *nx = dx / d;
    if (ny) *ny = dy / d;
    if (pen) *pen = rr - d;
    return 1;
}

static void hit_at(const ame_ray *r, float t, vec3 n, ame_hit *out)
{
    out->hit = 1;
    out->t = t;
    out->p = v3_add(r->origin, v3_scale(r->dir, t));
    out->n = n;
}

static vec3 nearest_aabb_normal(const ame_aabb *b, vec3 p)
{
    float dl = p.x - b->min.x, dr = b->max.x - p.x;
    float db = p.y - b->min.y, dt = b->max.y - p.y;
    float dn = p.z - b->min.z, df = b->max.z - p.z;
    float m = dl;
    vec3 n = v3(-1, 0, 0);
    if (dr < m) { m = dr; n = v3(1, 0, 0); }
    if (db < m) { m = db; n = v3(0, -1, 0); }
    if (dt < m) { m = dt; n = v3(0, 1, 0); }
    if (dn < m) { m = dn; n = v3(0, 0, -1); }
    if (df < m) { n = v3(0, 0, 1); }
    return n;
}

int ame_geo_ray_aabb(const ame_ray *r, const ame_aabb *b, ame_hit *out)
{
    hit_clear(out);
    if (!r || !b) return 0;

    float tmin = r->tmin;
    float tmax = r->tmax;
    int axis = -1;
    float nsign = 0.0f;
    float o[3] = { r->origin.x, r->origin.y, r->origin.z };
    float d[3] = { r->dir.x, r->dir.y, r->dir.z };
    float mn[3] = { b->min.x, b->min.y, b->min.z };
    float mx[3] = { b->max.x, b->max.y, b->max.z };

    for (int i = 0; i < 3; i++) {
        if (d[i] > -EPS && d[i] < EPS) {
            if (o[i] < mn[i] || o[i] > mx[i]) return 0;
            continue;
        }
        float inv = 1.0f / d[i];
        float t1 = (mn[i] - o[i]) * inv;
        float t2 = (mx[i] - o[i]) * inv;
        float ns = -1.0f;
        if (t1 > t2) {
            float tmp = t1; t1 = t2; t2 = tmp;
            ns = 1.0f;
        }
        if (t1 > tmin) {
            tmin = t1;
            axis = i;
            nsign = ns;
        }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return 0;
    }
    if (out) {
        vec3 n = v3(0, 0, 0);
        if (axis >= 0) {
            if (axis == 0) n.x = nsign;
            else if (axis == 1) n.y = nsign;
            else n.z = nsign;
        } else {
            n = nearest_aabb_normal(b, r->origin);
        }
        hit_at(r, tmin, n, out);
    }
    return 1;
}

int ame_geo_ray_obb(const ame_ray *r, const ame_obb *o, ame_hit *out)
{
    hit_clear(out);
    if (!r || !o) return 0;

    vec3 diff = v3_sub(r->origin, o->center);
    ame_ray local;
    local.tmin = r->tmin;
    local.tmax = r->tmax;
    local.origin = v3(v3_dot(diff, o->axis[0]),
                      v3_dot(diff, o->axis[1]),
                      v3_dot(diff, o->axis[2]));
    local.dir = v3(v3_dot(r->dir, o->axis[0]),
                   v3_dot(r->dir, o->axis[1]),
                   v3_dot(r->dir, o->axis[2]));
    ame_aabb box;
    box.min = v3(-o->half.x, -o->half.y, -o->half.z);
    box.max = v3( o->half.x,  o->half.y,  o->half.z);
    ame_hit h;
    if (!ame_geo_ray_aabb(&local, &box, &h)) return 0;
    if (out) {
        out->hit = 1;
        out->t = h.t;
        out->p = v3_add(r->origin, v3_scale(r->dir, h.t));
        out->n = v3_add(v3_scale(o->axis[0], h.n.x),
                        v3_add(v3_scale(o->axis[1], h.n.y),
                               v3_scale(o->axis[2], h.n.z)));
    }
    return 1;
}

int ame_geo_ray_tri(const ame_ray *r, vec3 a, vec3 b, vec3 c, ame_hit *out)
{
    hit_clear(out);
    if (!r) return 0;
    vec3 e1 = v3_sub(b, a);
    vec3 e2 = v3_sub(c, a);
    vec3 pvec = v3_cross(r->dir, e2);
    float det = v3_dot(e1, pvec);
    if (det > -EPS && det < EPS) return 0;
    float inv = 1.0f / det;
    vec3 tv = v3_sub(r->origin, a);
    float u = v3_dot(tv, pvec) * inv;
    if (u < 0.0f || u > 1.0f) return 0;
    vec3 qvec = v3_cross(tv, e1);
    float v = v3_dot(r->dir, qvec) * inv;
    if (v < 0.0f || u + v > 1.0f) return 0;
    float t = v3_dot(e2, qvec) * inv;
    if (t < r->tmin || t > r->tmax) return 0;
    if (out)
        hit_at(r, t, v3_normalize(v3_cross(e1, e2)), out);
    return 1;
}

int ame_geo_ray_quad(const ame_ray *r, vec3 v0, vec3 v1, vec3 v2, vec3 v3,
                     ame_hit *out)
{
    hit_clear(out);
    ame_hit h0, h1;
    int a = ame_geo_ray_tri(r, v0, v1, v2, &h0);
    int b = ame_geo_ray_tri(r, v0, v2, v3, &h1);
    if (!a && !b) return 0;
    ame_hit *best = NULL;
    if (a && b) best = (h0.t < h1.t) ? &h0 : &h1;
    else if (a) best = &h0;
    else best = &h1;
    if (out) *out = *best;
    return 1;
}
