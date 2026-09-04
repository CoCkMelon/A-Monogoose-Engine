/* tests — geometry module (physics.txt), built per-dimension by CMake:
 * test_geometry_2d (AME_2D) and test_geometry_3d (AME_3D). */
#include "utest.h"
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

int main(void) {
    printf("=== test_geometry (%dD) ===\n", AME_DIM);
    if (t_prims()) return 1;
    if (t_ray())  return 1;
#if AME_DIM == 2
    if (t_seg2d()) return 1;
#endif
    if (t_world()) return 1;
    return ut_done("test_geometry");
}
