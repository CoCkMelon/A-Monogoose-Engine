/* ame-next — geometry implementation (physics.txt). One .c owns the world
 * state; pure primitive tests live here too. Deterministic order everywhere. */
#include <ame/geometry.h>
#include <ame/math.h>

#include <float.h>
#include <string.h>

/* broadphase grid: 64x64 head table in 2D, 16x16x16 in 3D (fixed budget) */
#if AME_DIM == 2
#  define AME_GEO_GRID_CELLS 64
#  define AME_GEO_HEAD_N     (64 * 64)
#else
#  define AME_GEO_GRID_CELLS 16
#  define AME_GEO_HEAD_N     (16 * 16 * 16)
#endif

/* ------------------------------------------------------------------ */
/* primitive tests                                                      */
/* ------------------------------------------------------------------ */

bool ame_geo_aabb_overlap(ame_aabb a, ame_aabb b) {
    for (int i = 0; i < AME_DIM; i++)
        if (a.c[i] - a.h[i] > b.c[i] + b.h[i] || b.c[i] - b.h[i] > a.c[i] + a.h[i])
            return false;
    return true;
}

bool ame_geo_sphere_overlap(ame_sphere a, ame_sphere b) {
    float d2 = 0.0f;
    for (int i = 0; i < AME_DIM; i++) {
        float d = a.c[i] - b.c[i];
        d2 += d * d;
    }
    return d2 <= (a.r + b.r) * (a.r + b.r);
}

bool ame_geo_aabb_sphere_overlap(ame_aabb b, ame_sphere s) {
    float q[AME_DIM];
    for (int i = 0; i < AME_DIM; i++)
        q[i] = ame_clampf(s.c[i], b.c[i] - b.h[i], b.c[i] + b.h[i]);
    return ame_geo_dist(q, s.c) <= s.r;
}

bool ame_geo_point_in_aabb(const float p[AME_DIM], ame_aabb b) {
    for (int i = 0; i < AME_DIM; i++)
        if (p[i] < b.c[i] - b.h[i] || p[i] > b.c[i] + b.h[i])
            return false;
    return true;
}

bool ame_geo_point_in_sphere(const float p[AME_DIM], ame_sphere s) {
    return ame_geo_dist(p, s.c) <= s.r;
}

float ame_geo_dist(const float a[AME_DIM], const float b[AME_DIM]) {
#if AME_DIM == 2
    return ame_v2_dist(ame_v2_(a[0], a[1]), ame_v2_(b[0], b[1]));
#else
    return ame_v3_dist(ame_v3_(a[0], a[1], a[2]), ame_v3_(b[0], b[1], b[2]));
#endif
}

float ame_geo_seg_closest_pt(ame_seg s, const float p[AME_DIM], float out[AME_DIM]) {
    float ab[AME_DIM], ap[AME_DIM];
    float ab2 = 0.0f, apab = 0.0f;
    for (int i = 0; i < AME_DIM; i++) {
        ab[i] = s.b[i] - s.a[i];
        ap[i] = p[i] - s.a[i];
        ab2 += ab[i] * ab[i];
        apab += ap[i] * ab[i];
    }
    float t = ab2 > 1e-12f ? ame_clampf(apab / ab2, 0.0f, 1.0f) : 0.0f;
    float d2 = 0.0f;
    for (int i = 0; i < AME_DIM; i++) {
        out[i] = s.a[i] + ab[i] * t;
        float d = p[i] - out[i];
        d2 += d * d;
    }
    return d2;
}

#if AME_DIM == 2
bool ame_geo_seg_intersect(ame_seg a, ame_seg b, float out[2]) {
    float p0x = a.a[0], p0y = a.a[1], p1x = a.b[0], p1y = a.b[1];
    float p2x = b.a[0], p2y = b.a[1], p3x = b.b[0], p3y = b.b[1];
    float s1x = p1x - p0x, s1y = p1y - p0y;
    float s2x = p3x - p2x, s2y = p3y - p2y;
    float denom = (-s2x * s1y + s1x * s2y);
    if (denom > -1e-12f && denom < 1e-12f)
        return false; /* parallel */
    float s = (-s1y * (p0x - p2x) + s1x * (p0y - p2y)) / denom;
    float t = ( s2x * (p0y - p2y) - s2y * (p0x - p2x)) / denom;
    if (s >= 0.0f && s <= 1.0f && t >= 0.0f && t <= 1.0f) {
        out[0] = p0x + t * s1x;
        out[1] = p0y + t * s1y;
        return true;
    }
    return false;
}
#endif

bool ame_geo_ray_aabb(ame_ray r, ame_aabb b, ame_hit *out) {
    float tmin = 0.0f, tmax = r.tmax;
    float nmin[AME_DIM];
    for (int i = 0; i < AME_DIM; i++) nmin[i] = 0.0f;

    for (int i = 0; i < AME_DIM; i++) {
        if (r.d[i] > -1e-12f && r.d[i] < 1e-12f) {
            if (r.o[i] < b.c[i] - b.h[i] || r.o[i] > b.c[i] + b.h[i])
                return false;
            continue;
        }
        float inv = 1.0f / r.d[i];
        float t1 = (b.c[i] - b.h[i] - r.o[i]) * inv;
        float t2 = (b.c[i] + b.h[i] - r.o[i]) * inv;
        float sign = -1.0f;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; sign = 1.0f; }
        if (t1 > tmin) {
            tmin = t1;
            for (int k = 0; k < AME_DIM; k++) nmin[k] = 0.0f;
            nmin[i] = sign;
        }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax)
            return false;
    }
    out->t = tmin;
    for (int i = 0; i < AME_DIM; i++) {
        out->p[i] = r.o[i] + r.d[i] * tmin;
        out->n[i] = nmin[i];
    }
    out->shape = -1;
    out->flags = 0;
    return true;
}

bool ame_geo_ray_sphere(ame_ray r, ame_sphere s, ame_hit *out) {
    float oc[AME_DIM];
    float b = 0.0f, c = 0.0f;
    for (int i = 0; i < AME_DIM; i++) {
        oc[i] = r.o[i] - s.c[i];
        b += oc[i] * r.d[i];
        c += oc[i] * oc[i];
    }
    c -= s.r * s.r;
    float disc = b * b - c;
    if (disc < 0.0f)
        return false;
    float sq = sqrtf(disc);
    float t = -b - sq;
    if (t < 0.0f)
        t = -b + sq;
    if (t < 0.0f || t > r.tmax)
        return false;
    out->t = t;
    for (int i = 0; i < AME_DIM; i++) {
        out->p[i] = r.o[i] + r.d[i] * t;
        out->n[i] = out->p[i] - s.c[i];
    }
    /* normalize normal */
    float len = 0.0f;
    for (int i = 0; i < AME_DIM; i++) len += out->n[i] * out->n[i];
    len = sqrtf(len);
    if (len > 1e-12f)
        for (int i = 0; i < AME_DIM; i++) out->n[i] /= len;
    out->shape = -1;
    out->flags = 0;
    return true;
}

/* ------------------------------------------------------------------ */
/* static world + uniform grid broadphase                               */
/* ------------------------------------------------------------------ */

typedef struct {
    ame_aabb  box;
    ame_sphere sph;
    bool      is_sphere;
    uint32_t  flags;
} ame_gshape;


static struct {
    ame_gshape shapes[AME_GEO_MAX_STATIC];
    int count;
    float world_min[AME_DIM];
    float world_max[AME_DIM];
    float cell;
    int  dim_n[3];
    int  head[AME_GEO_HEAD_N];   /* first shape index in cell, -1 empty */
    int  next[AME_GEO_MAX_STATIC]; /* linked list within cell */
} W;

void ame_geo_reset(void) {
    W.count = 0;
    for (int i = 0; i < AME_DIM; i++) { W.world_min[i] = 0; W.world_max[i] = 0; }
    W.cell = 1.0f;
    W.dim_n[0] = W.dim_n[1] = W.dim_n[2] = 0;
    for (int i = 0; i < AME_GEO_HEAD_N; i++) W.head[i] = -1;
    for (int i = 0; i < AME_GEO_MAX_STATIC; i++) W.next[i] = -1;
}

int ame_geo_add_aabb(ame_aabb box, uint32_t flags) {
    if (W.count >= AME_GEO_MAX_STATIC)
        return -1;
    ame_gshape *s = &W.shapes[W.count];
    s->box = box;
    s->is_sphere = false;
    s->flags = flags;
    return W.count++;
}

int ame_geo_add_sphere(ame_sphere sph, uint32_t flags) {
    if (W.count >= AME_GEO_MAX_STATIC)
        return -1;
    ame_gshape *s = &W.shapes[W.count];
    s->sph = sph;
    s->is_sphere = true;
    s->flags = flags;
    return W.count++;
}

static void shape_aabb(const ame_gshape *s, ame_aabb *b) {
    if (s->is_sphere) {
        for (int i = 0; i < AME_DIM; i++) {
            b->c[i] = s->sph.c[i];
            b->h[i] = s->sph.r;
        }
    } else {
        *b = s->box;
    }
}

ame_aabb ame_geo_static_aabb(int i) {
    ame_aabb b = {0};
    if (i >= 0 && i < W.count)
        shape_aabb(&W.shapes[i], &b);
    return b;
}

int ame_geo_static_count(void) { return W.count; }

void ame_geo_rebuild_broadphase(void) {
    ame_aabb b = {0};
    for (int i = 0; i < AME_DIM; i++) {
        W.world_min[i] = 1e30f;
        W.world_max[i] = -1e30f;
    }
    for (int i = 0; i < W.count; i++) {
        shape_aabb(&W.shapes[i], &b);
        for (int k = 0; k < AME_DIM; k++) {
            if (b.c[k] - b.h[k] < W.world_min[k]) W.world_min[k] = b.c[k] - b.h[k];
            if (b.c[k] + b.h[k] > W.world_max[k]) W.world_max[k] = b.c[k] + b.h[k];
        }
    }
    if (W.count == 0) {
        W.cell = 1.0f;
        W.dim_n[0] = W.dim_n[1] = W.dim_n[2] = 0;
        return;
    }
    float extent = 0.0f;
    for (int i = 0; i < AME_DIM; i++)
        extent = fmaxf(extent, W.world_max[i] - W.world_min[i] + 1e-3f);
    W.cell = extent / AME_GEO_GRID_CELLS;
    W.dim_n[0] = AME_GEO_GRID_CELLS;
#if AME_DIM == 3
    W.dim_n[1] = AME_GEO_GRID_CELLS;
    W.dim_n[2] = AME_GEO_GRID_CELLS;
#else
    W.dim_n[1] = AME_GEO_GRID_CELLS;
    W.dim_n[2] = 1;
#endif
    for (int i = 0; i < AME_GEO_HEAD_N; i++) W.head[i] = -1;
    for (int i = 0; i < W.count; i++) W.next[i] = -1;

    /* insert each shape into every cell its AABB touches */
    for (int i = 0; i < W.count; i++) {
        shape_aabb(&W.shapes[i], &b);
        int lo[3], hi[3];
        for (int k = 0; k < 3; k++) {
            if (k >= AME_DIM) { lo[k] = hi[k] = 0; continue; }
            lo[k] = (int)((b.c[k] - b.h[k] - W.world_min[k]) / W.cell);
            hi[k] = (int)((b.c[k] + b.h[k] - W.world_min[k]) / W.cell);
            lo[k] = lo[k] < 0 ? 0 : (lo[k] > AME_GEO_GRID_CELLS - 1 ? AME_GEO_GRID_CELLS - 1 : lo[k]);
            hi[k] = hi[k] < 0 ? 0 : (hi[k] > AME_GEO_GRID_CELLS - 1 ? AME_GEO_GRID_CELLS - 1 : hi[k]);
        }
        for (int z = lo[2]; z <= hi[2]; z++)
        for (int y = lo[1]; y <= hi[1]; y++)
        for (int x = lo[0]; x <= hi[0]; x++) {
            int cell = x + W.dim_n[0] * (y + W.dim_n[1] * z);
            if (cell < 0 || cell >= AME_GEO_HEAD_N) continue;
            /* keep per-cell lists in DESCENDING index order so a walk
             * visiting head->next yields ASCENDING index order (deterministic) */
            int *slot = &W.head[cell];
            while (*slot != -1 && *slot > i) slot = &W.next[*slot];
            W.next[i] = *slot;
            *slot = i;
        }
    }
}

bool ame_geo_raycast(ame_ray r, ame_hit *best) {
    best->t = FLT_MAX;
    best->shape = -1;
    best->flags = 0;
    if (W.count == 0)
        return false;

    /* v0: walk cells along the ray's AABB span. The ray may be long, but the
     * slab test per shape is cheap and the grid prunes candidates; a DDA
     * march can replace this if profiling ever demands it. */
    ame_aabb ray_box;
    float end[AME_DIM];
    for (int i = 0; i < AME_DIM; i++)
        end[i] = r.o[i] + r.d[i] * r.tmax;
    for (int i = 0; i < AME_DIM; i++) {
        float lo = r.o[i] < end[i] ? r.o[i] : end[i];
        float hi = r.o[i] < end[i] ? end[i] : r.o[i];
        ray_box.c[i] = (lo + hi) * 0.5f;
        ray_box.h[i] = (hi - lo) * 0.5f;
    }
    int lo[3], hi[3];
    for (int k = 0; k < 3; k++) {
        if (k >= AME_DIM) { lo[k] = hi[k] = 0; continue; }
        lo[k] = (int)((ray_box.c[k] - ray_box.h[k] - W.world_min[k]) / W.cell);
        hi[k] = (int)((ray_box.c[k] + ray_box.h[k] - W.world_min[k]) / W.cell);
        lo[k] = lo[k] < 0 ? 0 : (lo[k] > AME_GEO_GRID_CELLS - 1 ? AME_GEO_GRID_CELLS - 1 : lo[k]);
        hi[k] = hi[k] < 0 ? 0 : (hi[k] > AME_GEO_GRID_CELLS - 1 ? AME_GEO_GRID_CELLS - 1 : hi[k]);
    }

    /* candidate stamp to avoid double-testing a shape across cells */
    static uint8_t stamp[AME_GEO_MAX_STATIC];
    static uint8_t cur_stamp = 0;
    cur_stamp++;
    if (cur_stamp == 0) { /* wrapped: clear */
        memset(stamp, 0, sizeof stamp);
        cur_stamp = 1;
    }

    ame_hit h;
    bool any = false;
    for (int z = lo[2]; z <= hi[2]; z++)
    for (int y = lo[1]; y <= hi[1]; y++)
    for (int x = lo[0]; x <= hi[0]; x++) {
        int cell = x + W.dim_n[0] * (y + W.dim_n[1] * z);
        if (cell < 0 || cell >= AME_GEO_HEAD_N) continue;
        for (int i = W.head[cell]; i != -1; i = W.next[i]) {
            if (i < 0 || i >= W.count || stamp[i] == cur_stamp)
                continue;
            stamp[i] = cur_stamp;
            ame_gshape *s = &W.shapes[i];
            bool hit = s->is_sphere ? ame_geo_ray_sphere(r, s->sph, &h)
                                    : ame_geo_ray_aabb(r, s->box, &h);
            if (hit && h.t < best->t) {
                *best = h;
                best->shape = i;
                best->flags = s->flags;
                any = true;
            }
        }
    }
    return any;
}

int ame_geo_overlap_world(ame_aabb box, int out_indices[AME_GEO_MAX_HITS]) {
    static uint8_t stamp[AME_GEO_MAX_STATIC];
    static uint8_t cur_stamp = 0;
    cur_stamp++;
    if (cur_stamp == 0) {
        memset(stamp, 0, sizeof stamp);
        cur_stamp = 1;
    }
    int n = 0;
    if (W.count == 0)
        return 0;
    int lo[3], hi[3];
    for (int k = 0; k < 3; k++) {
        if (k >= AME_DIM) { lo[k] = hi[k] = 0; continue; }
        lo[k] = (int)((box.c[k] - box.h[k] - W.world_min[k]) / W.cell);
        hi[k] = (int)((box.c[k] + box.h[k] - W.world_min[k]) / W.cell);
        lo[k] = lo[k] < 0 ? 0 : (lo[k] > AME_GEO_GRID_CELLS - 1 ? AME_GEO_GRID_CELLS - 1 : lo[k]);
        hi[k] = hi[k] < 0 ? 0 : (hi[k] > AME_GEO_GRID_CELLS - 1 ? AME_GEO_GRID_CELLS - 1 : hi[k]);
    }
    for (int z = lo[2]; z <= hi[2]; z++)
    for (int y = lo[1]; y <= hi[1]; y++)
    for (int x = lo[0]; x <= hi[0]; x++) {
        int cell = x + W.dim_n[0] * (y + W.dim_n[1] * z);
        if (cell < 0 || cell >= AME_GEO_HEAD_N) continue;
        for (int i = W.head[cell]; i != -1; i = W.next[i]) {
            if (i < 0 || i >= W.count || stamp[i] == cur_stamp)
                continue;
            stamp[i] = cur_stamp;
            ame_gshape *s = &W.shapes[i];
            bool ov = s->is_sphere ? ame_geo_aabb_sphere_overlap(box, s->sph)
                                   : ame_geo_aabb_overlap(box, s->box);
            if (ov) {
                if (n < AME_GEO_MAX_HITS)
                    out_indices[n] = i;
                n++;
            }
        }
    }
    if (n > AME_GEO_MAX_HITS)
        n = AME_GEO_MAX_HITS;
    /* per-cell lists walk descending; report ascending (deterministic) */
    for (int i = 1; i < n; i++) {
        int key = out_indices[i], j = i - 1;
        while (j >= 0 && out_indices[j] > key) { out_indices[j+1] = out_indices[j]; j--; }
        out_indices[j+1] = key;
    }
    return n;
}
