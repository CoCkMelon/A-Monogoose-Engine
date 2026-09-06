/* tests — camera math (pure CPU): the picking round-trip that was broken
 * (bad vp_inv) and is now analytic. Project world -> px -> ray; the ray must
 * pass EXACTLY through the world point. Guards hover/click alignment. */
#include "utest.h"
#include <ame/ame.h>
#include <ame/camera.h>
#include <ame/math.h>

static ame_v3 proj(const ame_camera *c, ame_v3 p) {
    float x = c->vp.m[0]*p.x + c->vp.m[4]*p.y + c->vp.m[8]*p.z + c->vp.m[12];
    float y = c->vp.m[1]*p.x + c->vp.m[5]*p.y + c->vp.m[9]*p.z + c->vp.m[13];
    float w = c->vp.m[3]*p.x + c->vp.m[7]*p.y + c->vp.m[11]*p.z + c->vp.m[15];
    return ame_v3_((x / w * 0.5f + 0.5f) * (float)c->vw,
                   (1.0f - (y / w * 0.5f + 0.5f)) * (float)c->vh, 0);
}

static float ray_miss(const ame_camera *c, ame_v3 world, float sx, float sy) {
    float o[3], d[3];
    camera_screen_ray(c, sx, sy, o, d);
    ame_v3 ov = ame_v3_(o[0], o[1], o[2]), dv = ame_v3_(d[0], d[1], d[2]);
    float t = ame_v3_dot(ame_v3_sub(world, ov), dv);
    return ame_v3_dist(ame_v3_add(ov, ame_v3_scale(dv, t)), world);
}

int main(void) {
    printf("=== test_camera ===\n");

    UT_CASE("matrix inverse: M * inv(M) == I");
    {
        ame_m4 mats[5];
        mats[0] = ame_m4_translate(ame_v3_(3, -4, 5));
        mats[1] = ame_m4_mul(ame_m4_rot_y(0.7f), ame_m4_scale(ame_v3_(2, 2, 2)));
        mats[2] = ame_m4_perspective(0.9f, 16.0f / 9.0f, 0.1f, 100.0f);
        ame_camera c;
        camera_viewport(camera_pos(camera_persp3d(camera_desc(&c)), 0, 3.2f, 3.4f),
                        1280, 720);
        camera_look(&c, 0, 0, 0);
        camera_fov_deg(&c, 50);
        camera_depth_range(&c, 0.1f, 100);
        camera_build(&c);
        mats[3] = c.vp;
        mats[4] = ame_m4_look_at(ame_v3_(1, 2, 3), ame_v3_(-2, 0, 4),
                                 ame_v3_(0, 1, 0));
        for (int i = 0; i < 5; i++) {
            ame_m4 prod = ame_m4_mul(mats[i], ame_m4_inverse(mats[i]));
            for (int k = 0; k < 16; k++) {
                float want = (k % 5 == 0) ? 1.0f : 0.0f;
                UT_ASSERTF(fabsf(prod.m[k] - want) < 1e-3f,
                           "mat %d: m[%d]=%.5f want %.1f", i, k, prod.m[k], want);
            }
        }
    }

    UT_CASE("picking round-trip: px ray passes through the world point");
    {
        ame_camera c;
        camera_viewport(camera_pos(camera_persp3d(camera_desc(&c)), 0, 3.2f, 3.4f),
                        1280, 720);
        camera_look(&c, 0, 0, 0);
        camera_fov_deg(&c, 50);
        camera_depth_range(&c, 0.1f, 100);
        camera_build(&c);
        /* the game's 4x4 board spots + midpoints */
        for (int gy = 0; gy < 4; gy++)
            for (int gx = 0; gx < 4; gx++) {
                ame_v3 w = ame_v3_(-1.875f + 0.625f + gx * 1.25f, 0.0f,
                                   -1.875f + 0.625f + gy * 1.25f);
                ame_v3 s = proj(&c, w);
                float miss = ray_miss(&c, w, s.x, s.y);
                UT_ASSERTF(miss < 1e-3f, "card(%d,%d) px(%.0f,%.0f) miss %.4f",
                           gx, gy, s.x, s.y, miss);
            }
        /* off-center screen points too */
        float pts[][2] = { {1,1}, {1279,719}, {640,360}, {213,537} };
        for (unsigned i = 0; i < 4; i++) {
            float o[3], d[3];
            camera_screen_ray(&c, pts[i][0], pts[i][1], o, d);
            float len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
            UT_ASSERT_NEAR(len, 1.0f, 1e-5); /* normalized direction */
        }
    }

    UT_CASE("2D: screen<->world corners + straight ray");
    {
        ame_camera c;
        camera_viewport(camera_pos(camera_ortho2d(camera_desc(&c)), 400, 300, 0),
                        800, 600);
        camera_build(&c);
        float w2[2];
        camera_screen_to_world2d(&c, 0, 0, w2);
        UT_ASSERT_NEAR(w2[0], 0.0f, 1e-4);   /* top-left = center - half */
        UT_ASSERT_NEAR(w2[1], 0.0f, 1e-4);
        camera_screen_to_world2d(&c, 800, 600, w2);
        UT_ASSERT_NEAR(w2[0], 800.0f, 1e-3);
        UT_ASSERT_NEAR(w2[1], 600.0f, 1e-3);
        float o[3], d[3];
        camera_screen_ray(&c, 400, 300, o, d);
        UT_ASSERT_NEAR(o[0], 400.0f, 1e-3);
        UT_ASSERT_NEAR(o[1], 300.0f, 1e-3);
        UT_ASSERT_NEAR(d[2], -1.0f, 1e-6);
    }

    UT_CASE("odd viewports: world origin stays INTEGRAL (crisp text)");
    {
        /* regression (audit): 1281x721 centered camera used to put the
         * origin at (-0.5,-0.5) - every glyph half a px off the grid,
         * soft at any font size. The translation floors; the leftover
         * half px falls at the far edge. */
        int ws[] = { 1280, 1281, 1365, 1023, 641 };
        int hs[] = { 720, 721, 767, 767, 641 };
        for (int k = 0; k < 5; k++) {
            ame_camera c2;
            camera_viewport(
                camera_ortho2d(camera_snap(camera_desc(&c2), true)),
                ws[k], hs[k]);
            camera_pos(&c2, (float)ws[k] * 0.5f, (float)hs[k] * 0.5f, 0);
            camera_build(&c2);
            float ox, oy;
            camera_world_origin(&c2, &ox, &oy);
            UT_ASSERTF(ox == floorf(ox) && oy == floorf(oy),
                       "%dx%d origin (%.2f,%.2f) not integral", ws[k],
                       hs[k], (double)ox, (double)oy);
            /* world<->window round trip at the corners stays exact */
            float out2[2];
            camera_screen_to_world2d(&c2, 0, 0, out2);
            UT_ASSERTF(out2[0] == floorf(out2[0]) && out2[1] == floorf(out2[1]),
                       "%dx%d corner world (%.2f,%.2f) not integral",
                       ws[k], hs[k], (double)out2[0], (double)out2[1]);
        }
    }

    UT_OK();
    return ut_done("test_camera");
}
