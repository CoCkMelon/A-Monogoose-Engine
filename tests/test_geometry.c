/* tests — geometry module (physics.txt), built per-dimension by CMake:
 * test_geometry_2d (AME_2D) and test_geometry_3d (AME_3D). */
#include "utest.h"
#include <math.h>
#include <string.h>
#include <ame/ame.h>
#include <ame/geometry.h>

static int t_prims(void) {
    UT_CASE("aabb overlap / containment");
    ame_aabb a, b, c;
    for (int i = 0; i < AME_DIM; i++) {
        a.c[i] = 0; a.h[i] = 1;
        b.c[i] = 2.5f; b.h[i] = 1;   /* nearest faces 0.5 apart */
        c.c[i] = 0.5f; c.h[i] = 1;   /* overlapping */
    }
    UT_ASSERT(!ame_geo_aabb_overlap(a, b));
    UT_ASSERT(ame_geo_aabb_overlap(a, c));

    float inside[AME_DIM], outside[AME_DIM];
    for (int i = 0; i < AME_DIM; i++) { inside[i] = 0.5f; outside[i] = 5.0f; }
    UT_ASSERT(ame_geo_point_in_aabb(inside, a));
    UT_ASSERT(!ame_geo_point_in_aabb(outside, a));

    UT_CASE("sphere overlap / dist");
    ame_sphere s1, s2;
    for (int i = 0; i < AME_DIM; i++) { s1.c[i] = 0; s2.c[i] = 1.0f; }
    s1.r = s2.r = 1.0f;              /* centers sqrt(D) apart; radii sum 2 */
    UT_ASSERT(ame_geo_sphere_overlap(s1, s2));
    s2.r = 0.4f;
    UT_ASSERT(!ame_geo_sphere_overlap(s1, s2));
    UT_ASSERT(ame_geo_aabb_sphere_overlap(a, s1));
    UT_ASSERT_NEAR(ame_geo_dist(s1.c, s2.c), sqrtf((float)AME_DIM), 1e-5);
    UT_OK();
    return 0;
}

static int t_ray(void) {
    UT_CASE("ray vs aabb (slab)");
    ame_aabb box;
    for (int i = 0; i < AME_DIM; i++) { box.c[i] = 5; box.h[i] = 1; }
    ame_ray r;
    r.o[0] = 0; r.o[1] = 5; if (AME_DIM == 3) r.o[2] = 5;
    r.d[0] = 1; r.d[1] = 0; if (AME_DIM == 3) r.d[2] = 0;
    r.tmax = 100;
    ame_hit h;
    UT_ASSERT(ame_geo_ray_aabb(r, box, &h));
    UT_ASSERT_NEAR(h.t, 4.0f, 1e-5);
    UT_ASSERT_NEAR(h.p[0], 4.0f, 1e-5);
    UT_ASSERT_NEAR(h.n[0], -1.0f, 1e-5);
    /* too short */
    r.tmax = 3;
    UT_ASSERT(!ame_geo_ray_aabb(r, box, &h));
    /* miss on y */
    r.tmax = 100; r.o[1] = 9;
    UT_ASSERT(!ame_geo_ray_aabb(r, box, &h));

    UT_CASE("ray vs sphere");
    ame_sphere s;
    for (int i = 0; i < AME_DIM; i++) s.c[i] = 5;
    s.r = 1;
    r.o[0] = 0; r.o[1] = 5; if (AME_DIM == 3) r.o[2] = 5;
    r.d[0] = 1; r.d[1] = 0; if (AME_DIM == 3) r.d[2] = 0;
    r.tmax = 100;
    UT_ASSERT(ame_geo_ray_sphere(r, s, &h));
    UT_ASSERT_NEAR(h.t, 4.0f, 1e-5); /* straight at the sphere center */
    UT_OK();
    return 0;
}

#if AME_DIM == 2
static int t_seg2d(void) {
    UT_CASE("2D segment intersection");
    ame_seg a = { .a = { 0, 0 }, .b = { 10, 10 } };
    ame_seg b = { .a = { 0, 10 }, .b = { 10, 0 } };
    float p[2];
    UT_ASSERT(ame_geo_seg_intersect(a, b, p));
    UT_ASSERT_NEAR(p[0], 5, 1e-5);
    UT_ASSERT_NEAR(p[1], 5, 1e-5);
    ame_seg c = { .a = { 20, 20 }, .b = { 30, 30 } };
    UT_ASSERT(!ame_geo_seg_intersect(a, c, p));
    UT_OK();
    return 0;
}
#endif

static int t_world(void) {
    UT_CASE("static world: overlap + raycast via grid");
    ame_geo_reset();
    /* three boxes: ground at y=-1 (wide), wall at x=10, hazard small box */
    ame_aabb g, w, hz;
    for (int i = 0; i < AME_DIM; i++) { g.c[i] = 0; g.h[i] = 20; }
    g.c[1] = -20; g.h[1] = 0.5f;                 /* ground plane slab */
    for (int i = 0; i < AME_DIM; i++) { w.c[i] = 10; w.h[i] = 20; }
    w.h[0] = 0.5f;
    for (int i = 0; i < AME_DIM; i++) { hz.c[i] = 0; hz.h[i] = 0.5f; }
    hz.c[0] = 2; hz.c[1] = 2; if (AME_DIM == 3) hz.c[2] = 2;
    int ig = ame_geo_add_aabb(g, AME_GEO_FLAG_SOLID);
    int iw = ame_geo_add_aabb(w, AME_GEO_FLAG_SOLID);
    int ih = ame_geo_add_aabb(hz, AME_GEO_FLAG_HAZARD);
    UT_ASSERT(ig >= 0 && iw >= 0 && ih >= 0);
    ame_geo_rebuild_broadphase();
    UT_ASSERT(ame_geo_static_count() == 3);

    /* overlap query around the hazard */
    ame_aabb q;
    for (int i = 0; i < AME_DIM; i++) { q.c[i] = 2; q.h[i] = 1; }
    int hits[AME_GEO_MAX_HITS];
    int n = ame_geo_overlap_world(q, hits);
    UT_ASSERT(n >= 1);
    bool found_hz = false;
    for (int i = 0; i < n; i++)
        if (hits[i] == ih) found_hz = true;
    UT_ASSERT(found_hz);

    /* raycast +X from origin hits hazard first (t=1.5) */
    ame_ray r;
    r.o[0] = 0; r.o[1] = 2; if (AME_DIM == 3) r.o[2] = 2;
    r.d[0] = 1; r.d[1] = 0; if (AME_DIM == 3) r.d[2] = 0;
    r.tmax = 100;
    ame_hit hit;
    UT_ASSERT(ame_geo_raycast(r, &hit));
    UT_ASSERT(hit.shape == ih);
    UT_ASSERT_NEAR(hit.t, 1.5f, 1e-4);
    UT_ASSERT_NEAR(hit.n[0], -1.0f, 1e-4);
    UT_ASSERT((hit.flags & AME_GEO_FLAG_HAZARD) != 0);

    /* raycast -X from (2,2): no hit (nothing behind) */
    r.d[0] = -1;
    UT_ASSERT(!ame_geo_raycast(r, &hit));
    UT_OK();
    return 0;
}

/* --- audit fixes + spec additions (session 9) -------------------------- */
static ame_aabb box_at(float cx, float cy, float cz, float h) {
    ame_aabb b;
    float c[3] = { cx, cy, cz };
    for (int i = 0; i < AME_DIM; i++) { b.c[i] = c[i]; b.h[i] = h; }
    return b;
}

static int t_audit(void) {
    UT_CASE("ray/sphere accepts any direction length (t in |d| units)");
    {
        ame_ray r;
        memset(&r, 0, sizeof r);
        ame_sphere s;
        for (int i = 0; i < AME_DIM; i++) s.c[i] = 0;
        s.r = 1;
        r.o[0] = -5; /* 5 units back along x */
        r.d[0] = 2.0f; /* |d| = 2 -> entry at t = 2 (|d| units) */
        r.tmax = 10;
        ame_hit h;
        UT_ASSERT(ame_geo_ray_sphere(r, s, &h));
        UT_ASSERT_NEAR(h.t, 2.0f, 1e-4f); /* was 1.28 with the old bug */
        r.d[0] = 1.0f;
        r.tmax = 5;
        UT_ASSERT(ame_geo_ray_sphere(r, s, &h));
        UT_ASSERT_NEAR(h.t, 4.0f, 1e-4f);
    }

    UT_CASE("ray/aabb from inside: t=0 with a NONZERO normal");
    {
        ame_ray r;
        memset(&r, 0, sizeof r);
        ame_aabb b = box_at(0, 0, 0, 1);
        r.o[0] = 0.1f; /* inside */
        r.d[0] = 0.7071f; r.d[1] = 0.7071f; /* dominant axis tie: x wins */
        r.tmax = 10;
        ame_hit h;
        UT_ASSERT(ame_geo_ray_aabb(r, b, &h));
        UT_ASSERT(h.t == 0.0f);
        float len2 = 0;
        for (int i = 0; i < AME_DIM; i++) len2 += h.n[i] * h.n[i];
        UT_ASSERTF(len2 > 0.99f, "embedded normal must be unit (got %f)",
                   sqrtf(len2));
    }

    UT_CASE("distance queries: point-aabb/sphere");
    {
        ame_aabb b = box_at(0, 0, 0, 1);
        float p[AME_DIM];
        for (int i = 0; i < AME_DIM; i++) p[i] = 3.0f;
        UT_ASSERT_NEAR(ame_geo_point_aabb_dist2(p, b), AME_DIM * 4.0f,
                       1e-4f);
        for (int i = 0; i < AME_DIM; i++) p[i] = 0.5f;
        UT_ASSERT(ame_geo_point_aabb_dist2(p, b) == 0.0f); /* inside */
        ame_sphere s;
        for (int i = 0; i < AME_DIM; i++) s.c[i] = 0;
        s.r = 2;
        for (int i = 0; i < AME_DIM; i++) p[i] = 3.0f;
        UT_ASSERT_NEAR(ame_geo_point_sphere_dist(p, s),
                       ame_geo_dist(p, s.c) - 2.0f, 1e-4f);
        for (int i = 0; i < AME_DIM; i++) p[i] = 0;
        UT_ASSERT(ame_geo_point_sphere_dist(p, s) == -2.0f); /* center */
    }

    UT_CASE("capsule overlaps + point distance (spec shape)");
    {
        /* capsule ON THE X AXIS: a=(-2,0), b=(2,0), r=0.5 - the same
         * construction is valid in 2D and 3D */
        ame_capsule cap;
        memset(&cap, 0, sizeof cap);
        cap.seg.a[0] = -2;
        cap.seg.b[0] = 2;
        cap.r = 0.5f;

        ame_sphere s;
        memset(&s, 0, sizeof s);
        s.c[0] = 2.8f; /* 0.8 from the segment END along the axis */
        s.r = 0.4f;    /* 0.5 + 0.4 = 0.9 > 0.8 -> overlap */
        UT_ASSERT(ame_geo_capsule_overlap_sphere(cap, s));
        s.r = 0.2f; /* 0.7 < 0.8 -> clear */
        UT_ASSERT(!ame_geo_capsule_overlap_sphere(cap, s));

        UT_ASSERT(!ame_geo_capsule_overlap_aabb(cap, box_at(2.9f, 0, 0,
                                                            0.15f)));
        UT_ASSERT(ame_geo_capsule_overlap_aabb(cap, box_at(2.3f, 0, 0,
                                                           0.2f)));

        float p[AME_DIM];
        memset(p, 0, sizeof p);
        p[0] = 3.0f; /* 1 past the end; dist2 = (1 - 0.5)^2 = 0.25 */
        UT_ASSERT_NEAR(ame_geo_capsule_point_dist2(cap, p), 0.25f, 1e-3f);
    }

    UT_CASE("obb: containment + separating axis (spec shape)");
    {
        ame_obb o = ame_geo_obb_from_aabb(box_at(0, 0, 0, 1));
        float p[AME_DIM];
        for (int i = 0; i < AME_DIM; i++) p[i] = 0.5f;
        UT_ASSERT(ame_geo_point_in_obb(p, o));
        for (int i = 0; i < AME_DIM; i++) p[i] = 1.5f;
        UT_ASSERT(!ame_geo_point_in_obb(p, o));

        ame_obb a = ame_geo_obb_from_aabb(box_at(0, 0, 0, 1));
        ame_obb b2 = ame_geo_obb_from_aabb(box_at(0, 0, 0, 1));
        float cs = cosf(0.7853981634f), sn = sinf(0.7853981634f);
        b2.u[0][0] = cs; b2.u[0][1] = -sn; /* 45 deg in the xy plane */
        b2.u[1][0] = sn; b2.u[1][1] = cs;
        UT_ASSERT(ame_geo_obb_overlap(a, b2)); /* concentric */
        b2.c[0] = 2.6f;
        UT_ASSERT(!ame_geo_obb_overlap(a, b2)); /* separated */
        b2.c[0] = 1.9f;
        /* rotated corners reach to 1.9? half-diagonal is sqrt(2) ~1.414
         * plus 1.0 face -> SAT must report overlap here */
        UT_ASSERT(ame_geo_obb_overlap(a, b2));
    }
    return 0;
}

static int t_world2(void) {
    UT_CASE("per-shape exact ray test (spheres are not boxes)");
    {
        ame_geo_reset();
        ame_sphere sph;
        memset(&sph, 0, sizeof sph);
        sph.c[0] = 2;
        sph.r = 0.5f;
        int id = ame_geo_add_sphere(sph, 0);
        ame_geo_rebuild_broadphase();
        ame_ray r;
        memset(&r, 0, sizeof r);
        r.o[1] = 1.5f; /* crosses the bbox, MISSES the sphere */
        r.d[0] = 1;
        r.tmax = 10;
        ame_hit h;
        UT_ASSERT(!ame_geo_ray_shape(id, r, &h)); /* exact: no hit */
        r.o[1] = 0.3f; /* inside the bbox too -> real hit */
        UT_ASSERT(ame_geo_ray_shape(id, r, &h));
        UT_ASSERT(id == h.shape);
    }

    UT_CASE("mesh -> primitive proxies (spec: per-part approximation)");
    {
        ame_geo_reset();
        float v[8 * AME_DIM];
        int vi = 0;
        for (int k = 0; k < 4; k++) {
            for (int i = 0; i < AME_DIM; i++)
                v[vi * AME_DIM + i] = i == 0 ? -2.0f - 0.1f * k : 0;
            vi++;
        }
        for (int k = 0; k < 4; k++) {
            for (int i = 0; i < AME_DIM; i++)
                v[vi * AME_DIM + i] = i == 0 ? 2.0f + 0.1f * k : 0;
            vi++;
        }
        int added = ame_geo_add_mesh_proxies(v, vi,
                                             AME_DIM * sizeof(float), 4);
        ame_geo_rebuild_broadphase();
        printf("    proxies added: %d\n", added);
        UT_ASSERTF(added >= 2, "two blobs -> at least two proxies");
        ame_ray r;
        memset(&r, 0, sizeof r);
        r.d[0] = 1;
        r.tmax = 20;
        ame_hit h;
        UT_ASSERT(ame_geo_raycast(r, &h));
        UT_ASSERT(h.shape >= 0);
        UT_ASSERT(h.t < 3.0f); /* enters the -2 blob first */
        ame_aabb mid = box_at(0, 0, 0, 0.4f);
        UT_ASSERT(ame_geo_overlap_world(mid,
                                        (int[AME_GEO_MAX_HITS]){ 0 }) == 0);
    }

    UT_CASE("broadphase early-out: outside rays miss, edge rays hit");
    {
        ame_geo_reset();
        ame_geo_add_aabb(box_at(0, 0, 0, 1), 0);
        ame_geo_rebuild_broadphase();
        ame_ray r;
        memset(&r, 0, sizeof r);
        r.o[0] = -100;
        r.o[1] = 50;
        r.d[0] = 1;
        r.tmax = 10; /* far outside the world */
        ame_hit h;
        UT_ASSERT(!ame_geo_raycast(r, &h));
        r.o[0] = -50;
        r.o[1] = 0;
        r.tmax = 60; /* long edge ray hits */
        UT_ASSERT(ame_geo_raycast(r, &h));
        UT_ASSERT_NEAR(h.t, 49.0f, 1e-3f);
    }
    return 0;
}

int main(void) {
    printf("=== test_geometry (%dD) ===\n", AME_DIM);
    if (t_prims()) return 1;
    if (t_ray())  return 1;
#if AME_DIM == 2
    if (t_seg2d()) return 1;
#endif
    if (t_world()) return 1;
    if (t_audit()) return 1;
    if (t_world2()) return 1;
    return ut_done("test_geometry");
}
