/* tests — math + common (utest, headless). */
#include "utest.h"
#include <ame/ame.h>
#include <ame/math.h>

static int t_common(void) {
    UT_CASE("rand determinism + range");
    uint32_t a = 1234, b = 1234;
    for (int i = 0; i < 1000; i++)
        UT_ASSERT(ame_rand(&a) == ame_rand(&b));
    for (int i = 0; i < 1000; i++) {
        int v = ame_rand_range(&a, 3, 9);
        UT_ASSERT(v >= 3 && v <= 9);
    }
    UT_OK();
    return 0;
}

static int t_vec(void) {
    UT_CASE("vec2/vec3 ops");
    ame_v2 v = ame_v2_add(ame_v2_(3, 4), ame_v2_(1, -4));
    UT_ASSERT(v.x == 4.0f && v.y == 0.0f);
    UT_ASSERT_NEAR(ame_v2_len(ame_v2_(3, 4)), 5, 1e-6);
    ame_v3 c = ame_v3_cross(ame_v3_(1, 0, 0), ame_v3_(0, 1, 0));
    UT_ASSERT(c.x == 0 && c.y == 0 && c.z == 1);
    ame_v3 n = ame_v3_norm(ame_v3_(0, 0, -7));
    UT_ASSERT_NEAR(n.z, -1, 1e-6);
    UT_OK();
    return 0;
}

static int t_mat(void) {
    UT_CASE("mat4 identity/mul/lookat/persp");
    ame_m4 i = ame_m4_identity();
    ame_m4 d = ame_m4_mul(i, i);
    for (int k = 0; k < 16; k++)
        UT_ASSERT_NEAR(d.m[k], i.m[k], 1e-6);

    ame_v3 p = ame_v3_(1, 2, 3);
    ame_v3 q = ame_m4_xform_point(ame_m4_translate(ame_v3_(10, 20, 30)), p);
    UT_ASSERT_NEAR(q.x, 11, 1e-6);
    UT_ASSERT_NEAR(q.y, 22, 1e-6);
    UT_ASSERT_NEAR(q.z, 33, 1e-6);

    ame_m4 vp = ame_m4_mul(
        ame_m4_perspective(AME_PI / 4.0f, 16.0f / 9.0f, 0.1f, 100.0f),
        ame_m4_look_at(ame_v3_(0, 0, 5), ame_v3_(0, 0, 0), ame_v3_(0, 1, 0)));
    ame_v3 clip = ame_m4_xform_point(vp, ame_v3_(0, 0, 0));
    UT_ASSERT_NEAR(clip.x, 0, 1e-5); /* on axis -> clip center */
    UT_ASSERT_NEAR(clip.y, 0, 1e-5);
    /* clip.w = -z_view = 5; ndc z = clip.z/clip.w ~ 0.962 (5 units away) */
    UT_ASSERT_NEAR(clip.z / 5.0f, 0.96196f, 1e-4);

    ame_m4 o = ame_m4_ortho_px(800, 600, -1, 1);
    ame_v3 tl = ame_m4_xform_point(o, ame_v3_(0, 0, 0));
    ame_v3 br = ame_m4_xform_point(o, ame_v3_(800, 600, 0));
    UT_ASSERT_NEAR(tl.x, -1, 1e-6);
    UT_ASSERT_NEAR(tl.y,  1, 1e-6); /* top-left px -> NDC top-left */
    UT_ASSERT_NEAR(br.x,  1, 1e-6);
    UT_ASSERT_NEAR(br.y, -1, 1e-6);
    UT_OK();
    return 0;
}

/* snapshot macro must expand at file scope (defines functions) */
typedef struct { int v; } snap_t;
AME_SNAP_DEFINE(snap_t)

static int t_snapshot(void) {
    UT_CASE("snapshot seqlock publish/copy: latest wins");
    snap_t_snap s;
    snap_t_snap_init(&s);
    snap_t w = { .v = 1 }, r;
    snap_t_publish(&s, &w);
    UT_ASSERT(snap_t_latest_copy(&s, &r) && r.v == 1);
    w.v = 2;
    snap_t_publish(&s, &w);
    UT_ASSERT(snap_t_latest_copy(&s, &r) && r.v == 2);
    w.v = 3;
    snap_t_publish(&s, &w);
    UT_ASSERT(snap_t_latest_copy(&s, &r) && r.v == 3);
    UT_OK();
    return 0;
}

int main(void) {
    printf("=== test_math ===\n");
    if (t_common()) return 1;
    if (t_vec())    return 1;
    if (t_mat())    return 1;
    if (t_snapshot()) return 1;
    return ut_done("test_math");
}
