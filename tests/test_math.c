#include "ame/math.h"
#include "ame/camera.h"

#include <math.h>
#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL math: %s\n", m);
    return 1;
}

static int close3(vec3 a, vec3 b, float eps)
{
    return fabsf(a.x - b.x) < eps && fabsf(a.y - b.y) < eps && fabsf(a.z - b.z) < eps;
}

int main(void)
{
    vec3 k = v3_cross(v3(1, 0, 0), v3(0, 1, 0));
    if (!close3(k, v3(0, 0, 1), 1e-5f)) return fail("cross i×j");
    if (fabsf(v3_dot(v3(1, 2, 3), v3(0, 1, 0)) - 2.0f) > 1e-5f)
        return fail("dot");

    quat q = quat_from_axis_angle(v3(0, 0, 1), 1.57079632679f);
    vec3 r = quat_rotate(q, v3(1, 0, 0));
    if (!close3(r, v3(0, 1, 0), 0.02f)) {
        fprintf(stderr, "quat got %.3f %.3f %.3f\n", r.x, r.y, r.z);
        return fail("quat 90 Z");
    }
    quat id = quat_mul(q, quat_ident());
    vec3 r2 = quat_rotate(id, v3(1, 0, 0));
    if (!close3(r2, r, 0.02f)) return fail("quat ident mul");

    ame_transform tr;
    ame_transform_identity(&tr);
    tr.position = v3(2, -1, 0.5f);
    tr.rotation = quat_from_euler_z(0);
    mat4 M = ame_transform_matrix(&tr);
    if (fabsf(M.m[12] - 2.0f) > 1e-4f) return fail("trs tx");
    if (fabsf(M.m[13] + 1.0f) > 1e-4f) return fail("trs ty");
    if (fabsf(M.m[0] - 1.0f) > 1e-4f) return fail("trs sx");

    mat4 down = m4_look_down_z(5.0f);
    mat4 at = m4_look_at(v3(0, 0, 5), v3(0, 0, 0), v3(0, 1, 0));
    for (int i = 0; i < 16; i++) {
        if (fabsf(down.m[i] - at.m[i]) > 1e-4f) {
            fprintf(stderr, "look_at[%d] %f vs %f\n", i, at.m[i], down.m[i]);
            return fail("look_at vs look_down_z");
        }
    }

    mat4 P = m4_perspective(1.04719755f, 16.0f / 9.0f, 0.1f, 100.0f);
    if (fabsf(P.m[11] + 1.0f) > 1e-5f) return fail("persp m11");
    if (P.m[15] != 0.0f) return fail("persp m15");

    ame_camera cam;
    ame_camera_look_at(
        ame_camera_perspective(ame_camera_reset(&cam), 60.0f, 1.6f, 0.1f, 80.0f),
        v3(0, 0, 8), v3(0, 0, 0), v3(0, 1, 0));
    if (cam.projection_mode != AME_CAM_PERSP) return fail("cam persp mode");
    if (!ame_camera_vp(&cam)) return fail("cam vp");
    ame_camera_center_xy(&cam, 3.0f, 1.0f);
    if (fabsf(cam.eye.x - 3.0f) > 1e-4f) return fail("persp center x");

    ame_camera_fit_height(
        ame_camera_look_z(ame_camera_reset(&cam), 16.0f),
        16.0f / 9.0f, 5.4f);
    if (cam.projection_mode != AME_CAM_ORTHO) return fail("reset ortho");
    ame_camera_center_xy(&cam, 8.0f, 1.0f);
    if (fabsf(0.5f * (cam.left + cam.right) - 8.0f) > 1e-3f)
        return fail("ortho center");

    {
        vec3 mid = v3_lerp(v3(0, 0, 0), v3(10, 0, 0), 0.5f);
        if (fabsf(mid.x - 5.0f) > 1e-5f) return fail("lerp");
        vec3 rf = v3_reflect(v3(1, -1, 0), v3(0, 1, 0));
        if (!close3(rf, v3(1, 1, 0), 1e-4f)) return fail("reflect");
        if (fabsf(v2_dot(v2(1, 0), v2(0, 1))) > 1e-6f) return fail("v2 dot");
        vec4 a = v4_from_v3(v3(1, 2, 3), 1);
        if (fabsf(a.w - 1.0f) > 1e-6f) return fail("v4");
    }

    {
        mat4 T = m4_translate(3, 4, 5);
        mat4 I = m4_mul(m4_inverse(T), T);
        if (fabsf(I.m[0] - 1.0f) > 1e-4f || fabsf(I.m[12]) > 1e-4f)
            return fail("m4 inverse");
        if (fabsf(m4_det(m4_ident()) - 1.0f) > 1e-4f) return fail("det I");
        vec3 tp = m4_mul_point(T, v3(1, 0, 0));
        if (!close3(tp, v3(4, 4, 5), 1e-4f)) return fail("mul point");
        mat3 id3 = m3_ident();
        if (fabsf(id3.m[0] - 1.0f) > 1e-5f) return fail("m3 ident");
        vec3 r90 = m3_mulv(m3_from_quat(quat_from_axis_angle(v3(0, 0, 1), ame_rad(90))),
                           v3(1, 0, 0));
        if (!close3(r90, v3(0, 1, 0), 0.02f)) return fail("m3 from quat");
    }

    {
        quat q0 = quat_ident();
        quat q1 = quat_from_axis_angle(v3(0, 0, 1), ame_rad(90));
        quat qh = quat_slerp(q0, q1, 0.5f);
        vec3 half = quat_rotate(qh, v3(1, 0, 0));
        if (fabsf(half.x - half.y) > 0.05f) return fail("slerp 45");
        quat qe = quat_from_euler(v3(0, 0, ame_rad(90)));
        vec3 er = quat_rotate(qe, v3(1, 0, 0));
        if (!close3(er, v3(0, 1, 0), 0.05f)) return fail("euler zxy yaw");
        quat qn = quat_normalize(quat_make(0, 0, 3, 4));
        if (fabsf(quat_dot(qn, qn) - 1.0f) > 1e-4f) return fail("quat norm");
        quat back = quat_mul(q1, quat_inverse(q1));
        vec3 idv = quat_rotate(back, v3(1, 0, 0));
        if (!close3(idv, v3(1, 0, 0), 0.02f)) return fail("quat inverse");
    }

    {
        mat4 P = m4_perspective(ame_rad(60), 1.0f, 0.1f, 100.0f);
        mat4 V = m4_look_at(v3(0, 0, 5), v3(0, 0, 0), v3(0, 1, 0));
        mat4 mvp = m4_mul(P, V);
        vec3 obj = v3(0, 0, 0);
        vec3 win = m4_project(obj, mvp, 0, 0, 100, 100);
        vec3 back = m4_unproject(win, mvp, 0, 0, 100, 100);
        if (!close3(back, obj, 0.05f)) {
            fprintf(stderr, "unproj %.3f %.3f %.3f\n", back.x, back.y, back.z);
            return fail("project roundtrip");
        }
    }

    printf("test_math ok\n");
    return 0;
}
