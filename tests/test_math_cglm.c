/* math parity vs cglm: CORRECTNESS oracle (asserted) + perf table
 * (informational). Built only when cglm is installed (pkg-config);
 * cglm never becomes an engine dependency - it is a test oracle, the
 * same way the golden screenshots are.
 *
 * Conventions that must line up: both are gl-matrix style (column-
 * major mat4, -z forward, +y up, NDC z in [-1,1]); versor = (x,y,z,w).
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <time.h>

#include <cglm/cglm.h>

#include "ame/math.h"
#include "utest.h"

/* deterministic LCG so failures reproduce */
static uint32_t rng = 0x12345678u;
static float frand(void) {
    rng = rng * 1664525u + 1013904223u;
    return (float)(rng >> 8 & 0xFFFF) / 16384.0f - 2.0f; /* [-2,2) */
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

#define N_BENCH 200000
static volatile float g_sink;

static bool mat4_close(ame_m4 a, mat4 b, float eps) {
    const float *fb = &b[0][0];
    for (int i = 0; i < 16; i++) {
        float d = fabsf(a.m[i] - fb[i]);
        if (d > eps * (1.0f + fabsf(fb[i])))
            return false;
    }
    return true;
}

int main(void) {
    UT_CASE("oracle: m4 mul / point transform / inverse / look_at / persp");
    {
        for (int iter = 0; iter < 64; iter++) {
            ame_m4 a, b;
            mat4 ca, cb, cdest;
            for (int i = 0; i < 16; i++) {
                a.m[i] = frand();
                b.m[i] = frand();
                ca[i / 4][i % 4] = a.m[i];
                cb[i / 4][i % 4] = b.m[i];
            }
            /* make a invertible: bump the diagonal */
            for (int i = 0; i < 4; i++) {
                a.m[i * 4 + i] += 4.0f;
                ca[i][i] = a.m[i * 4 + i];
            }

            ame_m4 ab = ame_m4_mul(a, b);
            {   /* ortho vs cglm (gl-matrix convention, NDC [-1,1]) */
                ame_m4 o = ame_m4_ortho(-2.5f, 3.5f, -1.5f, 2.5f, 0.1f, 10.0f);
                mat4 co;
                glm_ortho(-2.5f, 3.5f, -1.5f, 2.5f, 0.1f, 10.0f, co);
                UT_ASSERTF(mat4_close(o, co, 1e-6f), "m4_ortho mismatch iter %d", iter);
                ame_m4 mul_o = ame_m4_mul(o, a);
                mat4 cmul_o;
                glm_mat4_mul(co, ca, cmul_o);
                UT_ASSERTF(mat4_close(mul_o, cmul_o, 1e-4f),
                           "m4_ortho*mul mismatch iter %d", iter);
            }
            glm_mat4_mul(ca, cb, cdest);
            UT_ASSERTF(mat4_close(ab, cdest, 1e-4f),
                       "m4_mul mismatch iter %d", iter);
#if defined(__SSE2__)
            {   /* SSE path must be BIT-identical to the scalar body */
                ame_m4 twin = ame_m4_mul_scalar(a, b);
                UT_ASSERTF(memcmp(twin.m, ab.m, sizeof ab.m) == 0,
                           "m4_mul SSE != scalar iter %d", iter);
            }
#endif
            {   /* pointer form agrees bit-for-bit with the value form */
                ame_m4 via_ptr;
                ame_m4_mul_to(&a, &b, &via_ptr);
                UT_ASSERTF(memcmp(via_ptr.m, ab.m, sizeof ab.m) == 0,
                           "m4_mul_to != m4_mul iter %d", iter);
            }

            ame_v3 p = ame_v3_(frand(), frand(), frand());
            ame_v3 q = ame_m4_xform_point(a, p);
            vec4 cp = { p.x, p.y, p.z, 1.0f }, cq;
            glm_mat4_mulv(ca, cp, cq);
            UT_ASSERTF(fabsf(q.x - cq[0]) < 1e-4f && fabsf(q.y - cq[1]) < 1e-4f
                           && fabsf(q.z - cq[2]) < 1e-4f,
                       "xform_point mismatch iter %d", iter);

            ame_m4 ia = ame_m4_inverse(a);
            glm_mat4_inv(ca, cdest);
            UT_ASSERTF(mat4_close(ia, cdest, 2e-4f),
                       "m4_inverse mismatch iter %d", iter);

            ame_v3 eye = ame_v3_(frand(), frand(), frand());
            ame_v3 at = ame_v3_(frand(), frand(), frand());
            ame_v3 up = ame_v3_norm(ame_v3_(frand(), 1.0f + fabsf(frand()),
                                            frand()));
            ame_m4 la = ame_m4_look_at(eye, at, up);
            vec3 ce = { eye.x, eye.y, eye.z }, cat = { at.x, at.y, at.z },
                 cup = { up.x, up.y, up.z };
            glm_lookat(ce, cat, cup, cdest);
            UT_ASSERTF(mat4_close(la, cdest, 1e-4f),
                       "look_at mismatch iter %d", iter);

            ame_m4 pe = ame_m4_perspective(1.1f, 1.6f, 0.1f, 100.0f);
            glm_perspective(1.1f, 1.6f, 0.1f, 100.0f, cdest);
            UT_ASSERTF(mat4_close(pe, cdest, 1e-4f), "perspective mismatch");
        }
    }

    UT_CASE("oracle: quaternion axis-angle, mul, rotate, slerp");
    {
        for (int iter = 0; iter < 64; iter++) {
            ame_v3 axis = ame_v3_norm(ame_v3_(frand(), frand(), frand()));
            float ang = frand();
            ame_quat q = ame_quat_axis_angle(ang, axis);
            versor cq;
            mat4 cdest;
            glm_quatv(cq, ang, (vec3){ axis.x, axis.y, axis.z });
            UT_ASSERTF(fabsf(q.x - cq[0]) < 1e-5f && fabsf(q.y - cq[1]) < 1e-5f
                           && fabsf(q.z - cq[2]) < 1e-5f
                           && fabsf(q.w - cq[3]) < 1e-5f,
                       "quat_axis_angle mismatch iter %d", iter);

            ame_quat r = ame_quat_axis_angle(frand(), axis);
            ame_quat qr = ame_quat_mul(q, r);
#if defined(__SSE2__)
            {   /* SSE path must be BIT-identical to the scalar body */
                ame_quat twin = ame_quat_mul_scalar(q, r);
                UT_ASSERTF(qr.x == twin.x && qr.y == twin.y
                               && qr.z == twin.z && qr.w == twin.w,
                           "quat_mul SSE != scalar iter %d", iter);
            }
#endif
            versor cr;
            glm_quat_mul(cq, (versor){ r.x, r.y, r.z, r.w }, cr);
            /* q and -q are the same rotation: compare with sign fix */
            float s = (qr.w * cr[3] < 0) ? -1.0f : 1.0f;
            UT_ASSERTF(fabsf(qr.x - s * cr[0]) < 1e-4f
                           && fabsf(qr.y - s * cr[1]) < 1e-4f
                           && fabsf(qr.z - s * cr[2]) < 1e-4f
                           && fabsf(qr.w - s * cr[3]) < 1e-4f,
                       "quat_mul mismatch iter %d", iter);

            ame_v3 v = ame_v3_(frand(), frand(), frand());
            ame_v3 rq = ame_quat_rotate_v3(q, v);
            vec3 cro;
            glm_quat_rotatev(cq, (vec3){ v.x, v.y, v.z }, cro);
            UT_ASSERTF(ame_v3_dist(rq, ame_v3_(cro[0], cro[1], cro[2]))
                           < 1e-4f,
                       "quat_rotate_v3 mismatch iter %d (%f)",
                       iter,
                       ame_v3_dist(rq, ame_v3_(cro[0], cro[1], cro[2])));

            float t = 0.3f;
            ame_quat sq = ame_quat_slerp(q, r, t);
            versor cs;
            glm_quat_slerp(cq, (versor){ r.x, r.y, r.z, r.w }, t, cs);
            s = (sq.w * cs[3] < 0) ? -1.0f : 1.0f;
            UT_ASSERTF(fabsf(sq.x - s * cs[0]) < 1e-4f
                           && fabsf(sq.y - s * cs[1]) < 1e-4f
                           && fabsf(sq.z - s * cs[2]) < 1e-4f,
                       "quat_slerp mismatch iter %d", iter);

            ame_m4 qm = ame_quat_mat4(q);
            glm_quat_mat4(cq, cdest);
            UT_ASSERTF(mat4_close(qm, cdest, 1e-4f),
                       "quat_mat4 mismatch iter %d", iter);
        }
    }

    UT_CASE("oracle: frustum culling agrees with cglm");
    {
        for (int iter = 0; iter < 32; iter++) {
            ame_v3 eye = ame_v3_(frand() * 3, frand() * 3, frand() * 3);
            ame_v3 at = ame_v3_(frand() * 3, frand() * 3, frand() * 3);
            ame_v3 up = ame_v3_(0, 1, 0);
            ame_m4 vp = ame_m4_mul(ame_m4_perspective(1.0f, 1.5f, 0.2f,
                                                      60.0f),
                                   ame_m4_look_at(eye, at, up));
            ame_frustum f = ame_frustum_from_vp(vp);
            /* planes themselves must match cglm's extraction */
            vec4 cplanes[6];
            mat4 cvp;
            for (int i = 0; i < 16; i++)
                cvp[i / 4][i % 4] = vp.m[i];
            glm_frustum_planes(cvp, cplanes);
            for (int pl = 0; pl < 6; pl++) {
                float s = f.p[pl][0] * cplanes[pl][0]
                          + f.p[pl][1] * cplanes[pl][1]
                          + f.p[pl][2] * cplanes[pl][2]
                          + f.p[pl][3] * cplanes[pl][3] < 0
                              ? -1.0f
                              : 1.0f; /* double-cover sign */
                for (int k = 0; k < 4; k++)
                    UT_ASSERTF(fabsf(f.p[pl][k] - s * cplanes[pl][k])
                                   < 1e-4f,
                               "plane %d mismatch iter %d", pl, iter);
            }
            for (int k = 0; k < 16; k++) {
                ame_v3 c = ame_v3_(frand() * 8, frand() * 8, frand() * 8);
                float hr = 0.3f + fabsf(frand());
                float mn[3] = { c.x - hr, c.y - hr, c.z - hr };
                float mx[3] = { c.x + hr, c.y + hr, c.z + hr };
                ame_frustum_res ours = ame_frustum_aabb(&f, mn, mx);
                vec3 cbox[2] = { { mn[0], mn[1], mn[2] },
                                 { mx[0], mx[1], mx[2] } };
                bool visible = glm_aabb_frustum(cbox, cplanes);
                UT_ASSERTF((ours != AME_FRUSTUM_OUTSIDE) == visible,
                           "frustum aabb disagreement iter %d k %d "
                           "(ours=%d cglm visible=%d)",
                           iter, k, (int)ours, (int)visible);
                /* spheres: our sphere test must agree with the box of
                 * the same radius at center level (conservative) */
                bool sv = ame_frustum_sphere(
                    &f, (float[3]){ c.x, c.y, c.z }, hr);
                UT_ASSERTF(!sv || visible,
                           "sphere visible while box culled (iter %d k %d)",
                           iter, k);
            }
        }
    }

    /* --- perf table (informational; same binary, same flags) --------
     * Serial dependency on BOTH sides so no iteration can be dead-code
     * eliminated (ours: value feedback; cglm: pointer ping-pong). */
    UT_CASE("perf: ns/op vs cglm (informational)");
    {
        ame_m4 a, b;
        mat4 ca, cb, cd;
        for (int i = 0; i < 16; i++) {
            a.m[i] = frand();
            b.m[i] = frand();
        }
        for (int i = 0; i < 4; i++)
            a.m[i * 4 + i] += 4.0f;
        for (int i = 0; i < 16; i++) {
            ca[i / 4][i % 4] = a.m[i];
            cb[i / 4][i % 4] = b.m[i];
        }
        ame_v3 p = ame_v3_(1, 2, 3);
        vec4 cp4 = { 1, 2, 3, 1 };
        ame_quat q = ame_quat_axis_angle(0.7f, ame_v3_norm(ame_v3_(1, 2, 3)));
        ame_quat r = ame_quat_axis_angle(1.3f, ame_v3_norm(ame_v3_(3, 2, 1)));
        versor cq, cr, cqs;
        cq[0] = q.x; cq[1] = q.y; cq[2] = q.z; cq[3] = q.w;
        cr[0] = r.x; cr[1] = r.y; cr[2] = r.z; cr[3] = r.w;

        double t0, t1;

        t0 = now_ms();
        for (int i = 0; i < N_BENCH; i++)
            a = ame_m4_mul(a, b);
        t1 = now_ms();
        g_sink = a.m[0];
        double mul_ours = (t1 - t0) * 1e6 / N_BENCH;

        {
            ame_m4 x = a, y = b, z = a;
            ame_m4 *src = &x, *dst = &z;
            t0 = now_ms();
            for (int i = 0; i < N_BENCH; i++) {
                ame_m4_mul_to(src, &y, dst);
                ame_m4 *sw = src; src = dst; dst = sw;
            }
            t1 = now_ms();
            g_sink = z.m[0];
        }
        double mul_ours_ptr = (t1 - t0) * 1e6 / N_BENCH;

        float (*src)[4] = ca, (*dst)[4] = cb;
        t0 = now_ms();
        for (int i = 0; i < N_BENCH; i++) {
            glm_mat4_mul(src, cd, dst); /* cd = constant RHS */
            float (*sw)[4] = src; src = dst; dst = sw;
        }
        t1 = now_ms();
        g_sink = cb[0][0];
        double mul_cglm = (t1 - t0) * 1e6 / N_BENCH;

        t0 = now_ms();
        for (int i = 0; i < N_BENCH; i++)
            p = ame_m4_xform_point(a, p);
        t1 = now_ms();
        g_sink = p.x;
        double mulv_ours = (t1 - t0) * 1e6 / N_BENCH;

        t0 = now_ms();
        for (int i = 0; i < N_BENCH; i++)
            glm_mat4_mulv(ca, cp4, cp4);
        t1 = now_ms();
        g_sink = cp4[0];
        double mulv_cglm = (t1 - t0) * 1e6 / N_BENCH;

        t0 = now_ms();
        for (int i = 0; i < N_BENCH / 4; i++)
            a = ame_m4_inverse(a);
        t1 = now_ms();
        g_sink = a.m[0];
        double inv_ours = (t1 - t0) * 1e6 / (N_BENCH / 4);

        t0 = now_ms();
        for (int i = 0; i < N_BENCH / 4; i++) {
            glm_mat4_inv(src, dst);
            float (*sw)[4] = src; src = dst; dst = sw;
        }
        t1 = now_ms();
        g_sink = cb[0][0];
        double inv_cglm = (t1 - t0) * 1e6 / (N_BENCH / 4);

        t0 = now_ms();
        for (int i = 0; i < N_BENCH; i++)
            q = ame_quat_mul(q, r);
        t1 = now_ms();
        g_sink = q.x;
        double qm_ours = (t1 - t0) * 1e6 / N_BENCH;

        float *vq = cq, *vtmp = cqs;
        t0 = now_ms();
        for (int i = 0; i < N_BENCH; i++) {
            glm_quat_mul(vq, cr, vtmp);
            float *sw = vq; vq = vtmp; vtmp = sw;
        }
        t1 = now_ms();
        g_sink = vq[0];
        double qm_cglm = (t1 - t0) * 1e6 / N_BENCH;

        printf("    mat4_mul   : ours %6.1f ns  (ptr %6.1f ns)  cglm %6.1f ns  (%.2fx)\n",
               mul_ours, mul_ours_ptr, mul_cglm, mul_ours_ptr / mul_cglm);
        printf("    mulv point : ours %6.1f ns  cglm %6.1f ns  (%.2fx)\n",
               mulv_ours, mulv_cglm, mulv_ours / mulv_cglm);
        printf("    mat4_inv   : ours %6.1f ns  cglm %6.1f ns  (%.2fx)\n",
               inv_ours, inv_cglm, inv_ours / inv_cglm);
        printf("    quat_mul   : ours %6.1f ns  cglm %6.1f ns  (%.2fx)\n",
               qm_ours, qm_cglm, qm_ours / qm_cglm);
        UT_ASSERT(mul_ours > 0 && mul_cglm > 0);
        UT_OK();
        return ut_done("test_math_cglm");
    }
}
