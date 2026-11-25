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

/* ORACLE for camera_screen_ray's analytic path: unproject the SAME pixel through
 * the c->vp_inv that camera_build baked and take the direction from the eye. If
 * the analytic basis and the baked matrix ever disagree, this says so - which is
 * exactly how the degenerate-up collapse was found. There is no m4*v4 in the
 * public math API (the engine uploads the matrix and lets the shader transform),
 * so the multiply is hand-rolled, column-major per ame_m4_look_at's layout. The
 * 2D-style defaults use a NEGATIVE zn, so the NDC z=-1 plane sits behind the eye
 * and pos - world is the forward direction. */
static ame_v3 unproj_dir(const ame_camera *c, float sx, float sy) {
    const float *m = c->vp_inv.m;
    float nx = (sx / (float)c->vw) * 2.0f - 1.0f;
    float ny = 1.0f - (sy / (float)c->vh) * 2.0f;
    float px = m[0] * nx + m[4] * ny + m[8] * -1.0f + m[12];
    float py = m[1] * nx + m[5] * ny + m[9] * -1.0f + m[13];
    float pz = m[2] * nx + m[6] * ny + m[10] * -1.0f + m[14];
    float pw = m[3] * nx + m[7] * ny + m[11] * -1.0f + m[15];
    if (pw != 0.0f) { px /= pw; py /= pw; pz /= pw; }
    return ame_v3_norm(ame_v3_sub(c->pos, ame_v3_(px, py, pz)));
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
        /* world is world_y_up (coords.txt); screen is px_y_down. A 800x600
         * view centred at (400,300) covers world x 0..800, y 0..600, so the
         * screen's TOP-left pixel is world (0, 600), not (0,0). */
        ame_camera c;
        camera_viewport(camera_pos(camera_ortho2d(camera_desc(&c)), 400, 300, 0),
                        800, 600);
        camera_build(&c);
        float w2[2];
        camera_screen_y_down_to_world_y_up(&c, 0, 0, w2);
        UT_ASSERT_NEAR(w2[0], 0.0f, 1e-4);   /* screen x 0 = world x 0 */
        UT_ASSERT_NEAR(w2[1], 600.0f, 1e-4); /* screen TOP  = world TOP y */
        camera_screen_y_down_to_world_y_up(&c, 800, 600, w2);
        UT_ASSERT_NEAR(w2[0], 800.0f, 1e-3);
        UT_ASSERT_NEAR(w2[1], 0.0f, 1e-3);   /* screen BOTTOM = world y 0 */
        /* round trip through the inverse */
        float sx2[2];
        camera_world_y_up_to_screen_y_down(&c, 250, 100, sx2);
        UT_ASSERT_NEAR(sx2[0], 250.0f, 1e-3);
        UT_ASSERT_NEAR(sx2[1], 500.0f, 1e-3); /* 600 - 100 */
        camera_screen_y_down_to_world_y_up(&c, sx2[0], sx2[1], w2);
        UT_ASSERT_NEAR(w2[0], 250.0f, 1e-3);
        UT_ASSERT_NEAR(w2[1], 100.0f, 1e-3);
        /* higher world y is CLOSER TO THE TOP of the screen: smaller screen y */
        float lo[2], hi[2];
        camera_world_y_up_to_screen_y_down(&c, 0, 100, lo);
        camera_world_y_up_to_screen_y_down(&c, 0, 500, hi);
        UT_ASSERT(hi[1] < lo[1]);
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
            float ox, oy, tx2, ty2;
            camera_world_origin_y_up(&c2, &ox, &oy);      /* lower-left */
            camera_world_top_left_y_up(&c2, &tx2, &ty2);  /* top-left */
            UT_ASSERTF(ox == floorf(ox) && oy == floorf(oy),
                       "%dx%d lower-left (%.2f,%.2f) not integral", ws[k],
                       hs[k], (double)ox, (double)oy);
            UT_ASSERTF(tx2 == floorf(tx2) && ty2 == floorf(ty2),
                       "%dx%d top-left (%.2f,%.2f) not integral", ws[k],
                       hs[k], (double)tx2, (double)ty2);
            /* the two corners differ by exactly the view height */
            UT_ASSERTF(ty2 - oy == (float)hs[k],
                       "%dx%d corners differ by %.1f, not view height", ws[k],
                       hs[k], (double)(ty2 - oy));
            /* world<->window round trip at the corners stays exact */
            float out2[2];
            camera_screen_y_down_to_world_y_up(&c2, 0, 0, out2);
            UT_ASSERTF(out2[0] == floorf(out2[0]) && out2[1] == floorf(out2[1]),
                       "%dx%d corner world (%.2f,%.2f) not integral",
                       ws[k], hs[k], (double)out2[0], (double)out2[1]);
            UT_ASSERTF(out2[1] == ty2,
                       "%dx%d screen (0,0) should be the TOP-left y %.1f, got %.1f",
                       ws[k], hs[k], (double)ty2, (double)out2[1]);
        }
    }

    /* Regression: a camera that cannot be divided by must fail loudly instead
     * of raising SIGFPE. AME_CAM_ORTHO2D is 0, so a zero-initialised
     * ame_camera is a "valid" ortho camera with vw = vh = zoom = 0, and every
     * query below used to reach `c->vw / c->zoom` as 0 / 0 and kill the
     * process. Nine integer divisions across five functions; no golden image
     * can catch it, because the process dies before writing one. */
    UT_CASE("degenerate camera: all six entry points refuse instead of dividing by zero");
    {
        ame_camera z;
        memset(&z, 0, sizeof z);
        float ox = -1, oy = -1, out[2] = { -1, -1 };
        float o[3] = { -1, -1, -1 }, d[3] = { -1, -1, -1 };

        ame_clear_error();
        camera_world_origin_y_up(&z, &ox, &oy);
        UT_ASSERT(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT);
        UT_ASSERTF(ox == 0 && oy == 0, "origin not zeroed: (%.2f,%.2f)",
                   (double)ox, (double)oy);

        ox = oy = -1;
        ame_clear_error();
        camera_world_top_left_y_up(&z, &ox, &oy);
        UT_ASSERT(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT);
        UT_ASSERTF(ox == 0 && oy == 0, "top-left not zeroed: (%.2f,%.2f)",
                   (double)ox, (double)oy);

        out[0] = out[1] = -1;
        ame_clear_error();
        camera_screen_y_down_to_world_y_up(&z, 10, 20, out);
        UT_ASSERT(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT);
        UT_ASSERTF(out[0] == 0 && out[1] == 0, "screen->world not zeroed: (%.2f,%.2f)",
                   (double)out[0], (double)out[1]);

        out[0] = out[1] = -1;
        ame_clear_error();
        camera_world_y_up_to_screen_y_down(&z, 10, 20, out);
        UT_ASSERT(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT);
        UT_ASSERTF(out[0] == 0 && out[1] == 0, "world->screen not zeroed: (%.2f,%.2f)",
                   (double)out[0], (double)out[1]);

        ame_clear_error();
        camera_screen_ray(&z, 10, 20, o, d);
        UT_ASSERT(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT);
        UT_ASSERTF(o[0] == 0 && o[1] == 0 && o[2] == 0 && d[0] == 0 && d[1] == 0
                   && d[2] == 0, "ray not zeroed: o(%.2f,%.2f,%.2f) d(%.2f,%.2f,%.2f)",
                   (double)o[0], (double)o[1], (double)o[2],
                   (double)d[0], (double)d[1], (double)d[2]);

        ame_clear_error();
        camera_build(&z);   /* the ortho branch divided by zoom twice as well */
        UT_ASSERT(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT);
    }

    /* The guard must not change anything a real camera does: same vp bytes,
     * no error, and zoom = 0 reached by bypassing camera_zoom()'s clamp is
     * still refused. */
    UT_CASE("guard is inert for a valid camera and refuses zoom = 0 set directly");
    {
        ame_camera c;
        camera_viewport(camera_ortho2d(camera_desc(&c)), 800, 600);
        camera_pos(&c, 400.0f, 300.0f, 0.0f);
        ame_clear_error();
        camera_build(&c);
        UT_ASSERT(ame_last_error_code() == AME_ERR_OK);
        ame_m4 good_vp = c.vp, good_inv = c.vp_inv;

        float ox = 12345.0f, oy = 12345.0f;
        ame_clear_error();
        camera_world_origin_y_up(&c, &ox, &oy);
        UT_ASSERT(ame_last_error_code() == AME_ERR_OK);
        /* snapped, pos at the exact view centre: w*0.5 - floor(pos) floors to 0.
         * Assert the CONTRACT (integral, and the untouched sentinel is gone),
         * not a literal - the same reason the odd-viewport case above does. */
        UT_ASSERTF(ox != 12345.0f && oy != 12345.0f, "origin was not written");
        UT_ASSERTF(ox == floorf(ox) && oy == floorf(oy),
                   "valid camera lost integrality: origin (%.2f,%.2f)",
                   (double)ox, (double)oy);
        UT_ASSERTF(ox == 0.0f && oy == 0.0f,
                   "snapped centred camera should origin at (0,0), got (%.2f,%.2f)",
                   (double)ox, (double)oy);

        c.zoom = 0;   /* camera_zoom() would clamp this; a direct write does not */
        ame_clear_error();
        camera_world_origin_y_up(&c, &ox, &oy);
        UT_ASSERT(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT);
        UT_ASSERTF(ox == 0 && oy == 0, "zoom = 0 not refused: (%.2f,%.2f)",
                   (double)ox, (double)oy);

        c.zoom = 1;
        ame_clear_error();
        camera_build(&c);
        UT_ASSERT(ame_last_error_code() == AME_ERR_OK);
        UT_ASSERT(memcmp(&c.vp, &good_vp, sizeof c.vp) == 0
                  && memcmp(&c.vp_inv, &good_inv, sizeof c.vp_inv) == 0);
    }

    /* ame_m4_perspective computes 1.0f / tanf(fov_y * 0.5f) with no guard, so
     * fov_y == 0 baked inf into vp and NaN into vp_inv; the analytic ray path
     * multiplied by tanf(0) == 0 and silently collapsed every pick onto the view
     * axis. tanf(fov/2) is also 0 at pi and NEGATIVE past it, which flips the
     * projection's handedness rather than merely degenerating it - and a
     * degrees/radians mixup (50.0f where 0.872665f was meant) lands at 50 rad,
     * which the old `fov_y > 0` reading accepts. */
    UT_CASE("perspective fov_y outside (0, pi) is refused, not baked into vp");
    {
        ame_camera c;
        camera_desc(&c);
        camera_persp3d(&c);
        camera_viewport(&c, 1280, 720);
        camera_pos(&c, 0.0f, 3.2f, 3.4f);
        camera_look(&c, 0.0f, 0.0f, 0.0f);
        camera_depth_range(&c, 0.1f, 100.0f);

        /* the sane case first: 50 degrees, as examples/memory_game uses */
        camera_fov_deg(&c, 50.0f);
        ame_clear_error();
        camera_build(&c);
        UT_ASSERT(ame_last_error_code() == AME_ERR_OK);
        UT_ASSERTF(isfinite(c.vp.m[0]) && isfinite(c.vp.m[5]) && isfinite(c.vp.m[10]),
                   "valid fov produced a non-finite vp");
        ame_m4 good_vp = c.vp;
        float o[3], d[3];
        camera_screen_ray(&c, 640.0f, 360.0f, o, d);
        UT_ASSERTF(isfinite(d[0]) && isfinite(d[1]) && isfinite(d[2]),
                   "valid fov produced a non-finite ray");

        float bad[] = { 0.0f, -1.0f, (float)AME_PI, 50.0f, 6.2831853f };
        const char *why[] = { "0 (inf through 1/tan)", "negative", "exactly pi",
                              "50 RADIANS (degrees mixup)", "2 pi" };
        for (int i = 0; i < 5; i++) {
            c.vp = good_vp;             /* so a refused build cannot leave a stale pass */
            c.fov_y = bad[i];
            ame_clear_error();
            camera_build(&c);
            UT_ASSERTF(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT,
                       "fov_y %g (%s) was not refused", (double)bad[i], why[i]);
            UT_ASSERTF(memcmp(&c.vp, &good_vp, sizeof c.vp) == 0,
                       "fov_y %g (%s) overwrote vp despite being refused",
                       (double)bad[i], why[i]);
            ame_clear_error();
            camera_screen_ray(&c, 640.0f, 360.0f, o, d);
            UT_ASSERTF(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT,
                       "screen_ray accepted fov_y %g (%s)", (double)bad[i], why[i]);
            UT_ASSERTF(d[0] == 0 && d[1] == 0 && d[2] == 0,
                       "screen_ray left a non-zero direction for fov_y %g (%s)",
                       (double)bad[i], why[i]);
        }

        /* the bound must not reject a legitimate wide fov: 179 degrees is legal */
        camera_fov_deg(&c, 179.0f);
        ame_clear_error();
        camera_build(&c);
        UT_ASSERTF(ame_last_error_code() == AME_ERR_OK,
                   "179 deg is inside (0, pi) and must build");
        UT_ASSERTF(isfinite(c.vp.m[5]), "179 deg produced a non-finite vp");
    }

    UT_CASE("degenerate up: the analytic ray still matches the baked vp");
    {
        /* ame_m4_look_at substitutes a fallback axis when forward is parallel to
         * up (math.h:186-195), so a top-down 3D camera RENDERS - but
         * camera_screen_ray had no matching substitution. ame_v3_norm's own zero
         * guard (math.h:53) turned cross(f,up)==0 into s=(0,0,0), hence
         * u=(0,0,0), hence dir=norm(f) for EVERY pixel. The frame was right,
         * picking returned the same point everywhere, and nothing reported it.
         * Refusing these cameras would be wrong: a top-down camera with the
         * world-up convention is legitimate and works today, so the ray path has
         * to mirror the header's rule instead of rejecting the input. */
        const float px[4][2] = { { 480, 270 }, { 0, 0 }, { 960, 0 }, { 0, 540 } };
        const char *why[4] = { "baseline, up not parallel to f",
                               "top-down, f PARALLEL to up",
                               "zeroed up (a descriptor that omits .up)",
                               "top-down, up ANTIPARALLEL to f" };
        ame_v3 pos[4], look[4], up[4];
        pos[0] = ame_v3_(0, 0, 0);   look[0] = ame_v3_(0, 0, -1); up[0] = ame_v3_(0, 1, 0);
        pos[1] = ame_v3_(0, 10, 0);  look[1] = ame_v3_(0, 0, 0);  up[1] = ame_v3_(0, 1, 0);
        pos[2] = ame_v3_(0, 0, 0);   look[2] = ame_v3_(0, 0, -1); up[2] = ame_v3_(0, 0, 0);
        pos[3] = ame_v3_(0, 10, 0);  look[3] = ame_v3_(0, 0, 0);  up[3] = ame_v3_(0, -1, 0);
        for (int i = 0; i < 4; i++) {
            ame_camera c;
            camera_desc(&c);
            camera_persp3d(&c);
            camera_viewport(&c, 960, 540);
            camera_pos(&c, pos[i].x, pos[i].y, pos[i].z);
            camera_look(&c, look[i].x, look[i].y, look[i].z);
            camera_up(&c, up[i].x, up[i].y, up[i].z);
            ame_clear_error();
            camera_build(&c);
            UT_ASSERTF(ame_last_error_code() == AME_ERR_OK,
                       "%s: a degenerate up must still BUILD (look_at substitutes)",
                       why[i]);
            UT_ASSERTF(isfinite(c.vp.m[0]) && isfinite(c.vp.m[5])
                       && isfinite(c.vp_inv.m[5]),
                       "%s: build produced a non-finite vp/vp_inv", why[i]);
            ame_v3 centre = ame_v3_(0, 0, 0);
            int off_centre = 0;
            for (int p = 0; p < 4; p++) {
                float o[3], d[3];
                camera_screen_ray(&c, px[p][0], px[p][1], o, d);
                UT_ASSERTF(isfinite(d[0]) && isfinite(d[1]) && isfinite(d[2]),
                           "%s: ray dir not finite at px %g,%g",
                           why[i], (double)px[p][0], (double)px[p][1]);
                ame_v3 dv = ame_v3_(d[0], d[1], d[2]);
                float agree = ame_v3_dot(dv, unproj_dir(&c, px[p][0], px[p][1]));
                UT_ASSERTF(agree > 0.9999f,
                           "%s: analytic ray diverges from the baked vp at px %g,%g"
                           " (dot %g)", why[i], (double)px[p][0], (double)px[p][1],
                           (double)agree);
                if (p == 0) centre = dv;
                else if (ame_v3_dist(dv, centre) > 1e-4f) off_centre++;
            }
            /* the collapse detector: before the fix all three off-centre pixels
             * returned the CENTRE ray (dot 0.647, about 50 degrees off) */
            UT_ASSERTF(off_centre == 3,
                       "%s: rays COLLAPSED onto the view axis - only %d of 3 "
                       "off-centre pixels differ from the centre ray",
                       why[i], off_centre);
        }
    }

    UT_CASE("perspective look == pos is refused; ortho is not, near-misses still build");
    {
        /* look == pos is the one genuinely degenerate camera rather than an omitted
         * field. ame_v3_norm's guard (math.h:53) returns zeros, look_at builds an
         * all-zero view matrix, and measured at 960x540 the result is a c->vp with
         * ONE non-zero entry of 16, a vp_inv that comes back as the IDENTITY
         * (ame_m4_inverse of a singular matrix, no diagnostic), dir=(0,0,0) from
         * camera_screen_ray, and ame_last_error_code() == AME_ERR_OK throughout.
         * The whole world projects to a point and nothing reports it. */
        ame_camera c;
        camera_desc(&c);
        camera_persp3d(&c);
        camera_viewport(&c, 960, 540);
        camera_pos(&c, 1.0f, 2.0f, 3.0f);
        camera_look(&c, 0.0f, 0.0f, -1.0f);
        camera_fov_deg(&c, 50.0f);
        ame_clear_error();
        camera_build(&c);
        UT_ASSERTF(ame_last_error_code() == AME_ERR_OK, "the sane baseline must build");
        UT_ASSERTF(isfinite(c.vp.m[0]) && c.vp.m[0] != 0.0f, "baseline vp collapsed");
        ame_m4 good_vp = c.vp;

        const float pp[2][3] = { { 0, 0, 0 }, { 1.0f, 2.0f, 3.0f } };
        for (int i = 0; i < 2; i++) {
            c.vp = good_vp;         /* a refused build must not pass on a stale vp */
            camera_pos(&c, pp[i][0], pp[i][1], pp[i][2]);
            camera_look(&c, pp[i][0], pp[i][1], pp[i][2]);
            ame_clear_error();
            camera_build(&c);
            UT_ASSERTF(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT,
                       "look == pos at (%g,%g,%g) was not refused",
                       (double)pp[i][0], (double)pp[i][1], (double)pp[i][2]);
            UT_ASSERTF(memcmp(&c.vp, &good_vp, sizeof c.vp) == 0,
                       "look == pos at (%g,%g,%g) overwrote vp despite being refused",
                       (double)pp[i][0], (double)pp[i][1], (double)pp[i][2]);
            float o[3], d[3];
            ame_clear_error();
            camera_screen_ray(&c, 480.0f, 270.0f, o, d);
            UT_ASSERTF(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT,
                       "screen_ray accepted look == pos at (%g,%g,%g)",
                       (double)pp[i][0], (double)pp[i][1], (double)pp[i][2]);
            UT_ASSERTF(d[0] == 0 && d[1] == 0 && d[2] == 0,
                       "screen_ray left a non-zero direction for look == pos");
        }

        /* the threshold mirrors ame_v3_norm (l > 1e-12f) because that IS the
         * mechanism, so near-coincident cameras stay legitimate: measured fine at
         * 1e-1, 1e-3, 1e-5, 1e-7 and 1e-9, collapsing only at exact equality */
        camera_look(&c, 0, 0, 0);
        const float eps[3] = { 1e-5f, 1e-7f, 1e-9f };
        for (int i = 0; i < 3; i++) {
            camera_pos(&c, 0, 0, eps[i]);
            ame_clear_error();
            camera_build(&c);
            UT_ASSERTF(ame_last_error_code() == AME_ERR_OK,
                       "|look-pos| = %g is above ame_v3_norm's 1e-12f and must build",
                       (double)eps[i]);
            UT_ASSERTF(isfinite(c.vp.m[0]) && c.vp.m[0] != 0.0f,
                       "|look-pos| = %g produced a collapsed vp", (double)eps[i]);
        }

        /* ortho is out of scope: camera_build's 2D branch never calls look_at, so
         * look and up are unused and look == pos must keep building as before */
        camera_desc(&c);
        camera_ortho2d(&c);
        camera_viewport(&c, 960, 540);
        camera_pos(&c, 0, 0, 0);
        camera_look(&c, 0, 0, 0);
        ame_clear_error();
        camera_build(&c);
        UT_ASSERTF(ame_last_error_code() == AME_ERR_OK,
                   "ORTHO2D must not be refused for look == pos (unused in 2D)");
        UT_ASSERTF(isfinite(c.vp.m[0]) && c.vp.m[0] != 0.0f, "ORTHO2D vp collapsed");
    }

    UT_OK();
    return ut_done("test_camera");
}
