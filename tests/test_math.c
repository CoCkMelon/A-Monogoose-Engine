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
    UT_CASE("vec2/vec3/vec4 ops");
    ame_v2 v = ame_v2_add(ame_v2_(3, 4), ame_v2_(1, -4));
    UT_ASSERT(v.x == 4.0f && v.y == 0.0f);
    ame_v2 vs = ame_v2_scale(ame_v2_(3.0f, -4.0f), 2.5f);
    UT_ASSERT_NEAR(vs.x, 7.5f, 1e-6);
    UT_ASSERT_NEAR(vs.y, -10.0f, 1e-6);
    ame_v2 vs0 = ame_v2_scale(ame_v2_(3.0f, -4.0f), 0.0f);
    UT_ASSERT(vs0.x == 0.0f && vs0.y == 0.0f);
    UT_ASSERT_NEAR(ame_v2_len(ame_v2_(3, 4)), 5, 1e-6);
    ame_v3 c = ame_v3_cross(ame_v3_(1, 0, 0), ame_v3_(0, 1, 0));
    UT_ASSERT(c.x == 0 && c.y == 0 && c.z == 1);
    ame_v3 n = ame_v3_norm(ame_v3_(0, 0, -7));
    UT_ASSERT_NEAR(n.z, -1, 1e-6);
    ame_v4 v4 = ame_v4_add(ame_v4_(1.0f, 2.0f, 3.0f, 4.0f),
                           ame_v4_(-1.0f, 0.5f, 2.0f, -4.0f));
    UT_ASSERT_NEAR(v4.x, 0.0f, 1e-6);
    UT_ASSERT_NEAR(v4.y, 2.5f, 1e-6);
    UT_ASSERT_NEAR(v4.z, 5.0f, 1e-6);
    UT_ASSERT_NEAR(v4.w, 0.0f, 1e-6);
    UT_OK();
    return 0;
}

static int t_quat_m3(void) {
    UT_CASE("m3_identity / quat_identity / quat_norm");
    ame_m3 m3 = ame_m3_identity();
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            UT_ASSERT_NEAR(m3.m[col * 3 + row], col == row ? 1.0f : 0.0f, 1e-6);
    ame_v3 v = ame_v3_(2.0f, -3.0f, 5.0f);
    ame_v3 mv = ame_m3_mulv(m3, v);
    UT_ASSERT_NEAR(mv.x, v.x, 1e-6);
    UT_ASSERT_NEAR(mv.y, v.y, 1e-6);
    UT_ASSERT_NEAR(mv.z, v.z, 1e-6);
    ame_m3 m3sq = ame_m3_mul(m3, m3);
    for (int k = 0; k < 9; k++)
        UT_ASSERT_NEAR(m3sq.m[k], m3.m[k], 1e-6);

    ame_quat qi = ame_quat_identity();
    UT_ASSERT(qi.x == 0.0f && qi.y == 0.0f && qi.z == 0.0f && qi.w == 1.0f);
    ame_v3 qr = ame_quat_rotate_v3(qi, v);
    UT_ASSERT_NEAR(qr.x, v.x, 1e-6);
    UT_ASSERT_NEAR(qr.y, v.y, 1e-6);
    UT_ASSERT_NEAR(qr.z, v.z, 1e-6);
    ame_m4 qm = ame_quat_mat4(qi);
    ame_m4 m4i = ame_m4_identity();
    for (int k = 0; k < 16; k++)
        UT_ASSERT_NEAR(qm.m[k], m4i.m[k], 1e-6);

    ame_quat qn = ame_quat_norm(ame_quat_(0.0f, 3.0f, 0.0f, 4.0f));
    UT_ASSERT_NEAR(qn.x, 0.0f, 1e-6);
    UT_ASSERT_NEAR(qn.y, 0.6f, 1e-6);
    UT_ASSERT_NEAR(qn.z, 0.0f, 1e-6);
    UT_ASSERT_NEAR(qn.w, 0.8f, 1e-6);
    UT_ASSERT_NEAR(ame_quat_dot(qn, qn), 1.0f, 1e-6);
    ame_quat qz = ame_quat_norm(ame_quat_(0.0f, 0.0f, 0.0f, 0.0f));
    UT_ASSERT(qz.x == 0.0f && qz.y == 0.0f && qz.z == 0.0f && qz.w == 1.0f);
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

    /* world_y_up projection (coords.txt): view pixel (0,0) is the BOTTOM-left
     * of the view rectangle and lands at NDC bottom-left; (w,h) at top-right. */
    ame_m4 o = ame_m4_ortho_px_y_up(800, 600, -1, 1);
    ame_v3 bl = ame_m4_xform_point(o, ame_v3_(0, 0, 0));
    ame_v3 tr = ame_m4_xform_point(o, ame_v3_(800, 600, 0));
    UT_ASSERT_NEAR(bl.x, -1, 1e-6);
    UT_ASSERT_NEAR(bl.y, -1, 1e-6); /* world y=0 -> NDC BOTTOM (y is up) */
    UT_ASSERT_NEAR(tr.x,  1, 1e-6);
    UT_ASSERT_NEAR(tr.y,  1, 1e-6); /* world y=h -> NDC top */
    /* y really grows UP: raising y raises NDC y */
    ame_v3 mid = ame_m4_xform_point(o, ame_v3_(400, 300, 0));
    ame_v3 up  = ame_m4_xform_point(o, ame_v3_(400, 450, 0));
    UT_ASSERT(up.y > mid.y);
    UT_ASSERT_NEAR(mid.y, 0, 1e-6);
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
    if (t_quat_m3()) return 1;
    if (t_mat())    return 1;
    if (t_snapshot()) return 1;
    return ut_done("test_math");
}
