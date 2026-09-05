#include "ame/geo.h"

#include <math.h>
#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL geo: %s\n", m);
    return 1;
}

static int near(float a, float b, float e)
{
    return fabsf(a - b) <= e;
}

int main(void)
{
    ame_aabb card = ame_aabb_make(0, 0, 0, 0.775f, 1.05f, 0.045f);
    if (!ame_geo_point_in_aabb_xy(&card, 0, 0)) return fail("centre");
    if (ame_geo_point_in_aabb_xy(&card, 2, 0)) return fail("miss xy");
    if (!ame_geo_point_in_aabb(&card, v3(0, 0, 0))) return fail("in 3d");
    if (ame_geo_point_in_aabb(&card, v3(0, 0, 2))) return fail("out z");

    vec3 c = ame_aabb_center(&card);
    if (!near(c.x, 0, 1e-5f) || !near(c.z, 0, 1e-5f)) return fail("center");
    vec3 e = ame_aabb_extents(&card);
    if (!near(e.x, 0.775f, 1e-5f)) return fail("extents");

    ame_aabb flipped = ame_aabb_make(0, 0, 0, -1, 1, 1);
    if (!ame_geo_point_in_aabb_xy(&flipped, 0.5f, 0)) return fail("neg half");

    ame_aabb other = ame_aabb_make(3, 0, 0, 0.5f, 0.5f, 0.5f);
    if (ame_geo_aabb_overlap(&card, &other)) return fail("no overlap");
    ame_aabb nearb = ame_aabb_make(0.5f, 0, 0, 0.5f, 0.5f, 0.5f);
    if (!ame_geo_aabb_overlap(&card, &nearb)) return fail("overlap");

    ame_aabb high = ame_aabb_make(0, 0, 10, 1, 1, 0.1f);
    ame_aabb low  = ame_aabb_make(0, 0, 0,  1, 1, 0.1f);
    if (ame_geo_aabb_overlap(&high, &low)) return fail("z separated");
    if (!ame_geo_aabb_overlap_xy(&high, &low)) return fail("xy overlap");

    ame_aabb inflated = ame_aabb_inflate(ame_aabb_make(0, 0, 0, 1, 1, 1), 0.5f);
    if (!near(inflated.max.x, 1.5f, 1e-5f)) return fail("inflate");

    {
        ame_aabb a = ame_aabb_make(0.5f, 0, 0, 1, 1, 1);
        ame_aabb b = ame_aabb_make(0, 0, 0, 1, 1, 1);
        float nx, ny, pen;
        if (!ame_geo_aabb_aabb_xy(&a, &b, &nx, &ny, &pen)) return fail("aabb mtv");
        if (nx < 0.5f) return fail("aabb mtv +x");
    }

    ame_ray down = ame_ray_make(0, 0, 10, 0, 0, -1, 100);
    ame_hit h;
    if (!ame_geo_ray_aabb(&down, &card, &h) || !h.hit) return fail("ray aabb");
    if (!near(h.t, 10.0f - 0.045f, 0.02f)) return fail("ray t");
    if (h.n.z < 0.5f) return fail("ray n +z");

    ame_ray miss = ame_ray_make(5, 5, 10, 0, 0, -1, 100);
    if (ame_geo_ray_aabb(&miss, &card, &h)) return fail("ray miss");

    ame_ray away = ame_ray_make(0, 0, 10, 0, 0, 1, 100);
    if (ame_geo_ray_aabb(&away, &card, &h)) return fail("ray away");

    ame_aabb box1 = ame_aabb_make(0, 0, 0, 1, 1, 1);
    ame_ray inside = ame_ray_make(0, 0, 0, 1, 0, 0, 10);
    if (!ame_geo_ray_aabb(&inside, &box1, &h) || !h.hit) return fail("ray inside");
    if (h.t > 1e-5f) return fail("ray inside t");
    /* nearest face from centre prefers -X */
    if (h.n.x > -0.5f) return fail("ray inside n");

    ame_obb obb = ame_obb_axis(0, 0, 0, 0.775f, 1.05f, 0.045f);
    if (!ame_geo_ray_obb(&down, &obb, &h)) return fail("ray obb");

    const float pi_2 = 1.57079632679f;
    ame_obb spun = ame_obb_make(v3(0, 0, 0),
                                quat_from_axis_angle(v3(0, 1, 0), pi_2),
                                v3(0.775f, 1.05f, 0.045f));
    if (!ame_geo_ray_obb(&down, &spun, &h)) return fail("rotated obb");

    vec3 q0 = v3(-1, -1, 0), q1 = v3(1, -1, 0), q2 = v3(1, 1, 0), q3 = v3(-1, 1, 0);
    if (!ame_geo_ray_quad(&down, q0, q1, q2, q3, &h)) return fail("quad hit");
    if (!ame_geo_ray_tri(&down, q0, q1, q2, &h)) return fail("tri hit");
    ame_ray side = ame_ray_make(-5, 0, 0, 0, 0, -1, 100);
    if (ame_geo_ray_quad(&side, q0, q1, q2, q3, &h)) return fail("quad miss");

    {
        ame_aabb box = ame_aabb_make(0, 0, 0, 1, 1, 1);
        float nx, ny, pen;
        if (!ame_geo_circle_aabb_xy(&box, 0, 1.5f, 0.6f, &nx, &ny, &pen))
            return fail("circle aabb hit");
        if (ny < 0.5f) return fail("circle aabb normal");
        if (!near(pen, 0.1f, 0.02f)) return fail("circle aabb pen");
        if (ame_geo_circle_aabb_xy(&box, 0, 3.0f, 0.6f, &nx, &ny, &pen))
            return fail("circle aabb miss");
        if (!ame_geo_circle_aabb_xy(&box, 0, 0, 0.5f, &nx, &ny, &pen))
            return fail("circle aabb inside");
        if (pen < 1.4f) return fail("circle aabb inside pen");
        /* corner: centre (1.3, 1.3), closest (1,1), dist ~0.424, r=0.5 */
        if (!ame_geo_circle_aabb_xy(&box, 1.3f, 1.3f, 0.5f, &nx, &ny, &pen))
            return fail("circle aabb corner");
        if (nx < 0.3f || ny < 0.3f) return fail("circle aabb corner n");
    }

    {
        float nx, ny, pen, qx, qy, t;
        if (!ame_geo_circle_seg_xy(0, 0.2f, 0.32f, -2, 0, 2, 0, &nx, &ny, &pen))
            return fail("circle seg hit");
        if (ny < 0.5f) return fail("circle seg normal");
        if (ame_geo_circle_seg_xy(0, 3.0f, 0.32f, -2, 0, 2, 0, &nx, &ny, &pen))
            return fail("circle seg miss");
        if (!ame_geo_circle_seg_xy(0.5f, 0.7f, 0.32f, 0, 0, 2, 2, &nx, &ny, &pen))
            return fail("circle seg ramp");
        /* Lean: circleHitsSeg 4 0 1  0 0 4 0 */
        if (!ame_geo_circle_seg_xy(4, 0, 1, 0, 0, 4, 0, &nx, &ny, &pen))
            return fail("circle seg endpoint");
        if (ame_geo_circle_seg_xy(6, 0, 1, 0, 0, 4, 0, &nx, &ny, &pen))
            return fail("circle seg past end");
        if (!ame_geo_circle_seg_xy(0, 0, 1, 0, 0, 0, 0, &nx, &ny, &pen))
            return fail("circle seg point");
        ame_geo_closest_on_seg_xy(0, 0, 2, 0, 3, 1, &qx, &qy, &t);
        if (!near(qx, 2, 1e-5f) || !near(t, 1, 1e-5f)) return fail("closest clamp");
        ame_geo_closest_on_seg_xy(0, 0, 2, 0, 1, 1, &qx, &qy, &t);
        if (!near(qx, 1, 1e-5f) || !near(qy, 0, 1e-5f)) return fail("closest mid");
    }

    {
        float nx, ny, pen;
        if (!ame_geo_circle_circle_xy(0, 0, 1, 1.5f, 0, 1, &nx, &ny, &pen))
            return fail("circle circle hit");
        if (nx > -0.5f) return fail("circle circle n"); /* c0 left of c1, push -X */
        if (!near(pen, 0.5f, 0.02f)) return fail("circle circle pen");
        if (ame_geo_circle_circle_xy(0, 0, 0.4f, 5, 0, 0.4f, &nx, &ny, &pen))
            return fail("circle circle miss");
        if (!ame_geo_circle_circle_xy(0, 0, 1, 0, 0, 1, &nx, &ny, &pen))
            return fail("circle circle coincident");
    }

    printf("test_geo ok\n");
    return 0;
}
