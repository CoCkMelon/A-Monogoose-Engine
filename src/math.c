#include "ame/math.h"
#include "ame/detail/cglm.h"

#include <string.h>

static void load_m4(mat4 m, cglm_mat4 o) { memcpy(o, m.m, 16 * sizeof(float)); }
static mat4 store_m4(cglm_mat4 o)
{
    mat4 r;
    memcpy(r.m, o, 16 * sizeof(float));
    return r;
}
static void load_m3(mat3 m, cglm_mat3 o) { memcpy(o, m.m, 9 * sizeof(float)); }
static mat3 store_m3(cglm_mat3 o)
{
    mat3 r;
    memcpy(r.m, o, 9 * sizeof(float));
    return r;
}
static void load_q(quat q, cglm_versor o)
{
    o[0] = q.x; o[1] = q.y; o[2] = q.z; o[3] = q.w;
}
static quat store_q(cglm_versor o)
{
    return quat_make(o[0], o[1], o[2], o[3]);
}
static void load_v3(vec3 v, cglm_vec3 o)
{
    o[0] = v.x; o[1] = v.y; o[2] = v.z;
}
static vec3 store_v3(cglm_vec3 o) { return v3(o[0], o[1], o[2]); }

mat4 m4_inverse(mat4 m)
{
    cglm_mat4 a, d;
    load_m4(m, a);
    glm_mat4_inv(a, d);
    return store_m4(d);
}

mat4 m4_transpose(mat4 m)
{
    cglm_mat4 a, d;
    load_m4(m, a);
    glm_mat4_transpose_to(a, d);
    return store_m4(d);
}

float m4_det(mat4 m)
{
    cglm_mat4 a;
    load_m4(m, a);
    return glm_mat4_det(a);
}

mat3 m3_ident(void)
{
    cglm_mat3 d;
    glm_mat3_identity(d);
    return store_m3(d);
}

mat3 m3_mul(mat3 a, mat3 b)
{
    cglm_mat3 A, B, D;
    load_m3(a, A);
    load_m3(b, B);
    glm_mat3_mul(A, B, D);
    return store_m3(D);
}

mat3 m3_inverse(mat3 m)
{
    cglm_mat3 a, d;
    load_m3(m, a);
    glm_mat3_inv(a, d);
    return store_m3(d);
}

mat3 m3_transpose(mat3 m)
{
    cglm_mat3 a, d;
    load_m3(m, a);
    glm_mat3_transpose_to(a, d);
    return store_m3(d);
}

vec3 m3_mulv(mat3 m, vec3 v)
{
    cglm_mat3 a;
    cglm_vec3 in, out;
    load_m3(m, a);
    load_v3(v, in);
    glm_mat3_mulv(a, in, out);
    return store_v3(out);
}

mat3 m3_from_m4(mat4 m)
{
    cglm_mat4 a;
    cglm_mat3 d;
    load_m4(m, a);
    glm_mat4_pick3(a, d);
    return store_m3(d);
}

mat3 m3_from_quat(quat q)
{
    cglm_versor qq;
    cglm_mat3 d;
    load_q(q, qq);
    glm_quat_mat3(qq, d);
    return store_m3(d);
}

quat quat_normalize(quat q)
{
    cglm_versor a, d;
    load_q(q, a);
    glm_quat_normalize_to(a, d);
    return store_q(d);
}

quat quat_conjugate(quat q)
{
    cglm_versor a, d;
    load_q(q, a);
    glm_quat_conjugate(a, d);
    return store_q(d);
}

quat quat_inverse(quat q)
{
    cglm_versor a, d;
    load_q(q, a);
    glm_quat_inv(a, d);
    return store_q(d);
}

float quat_dot(quat a, quat b)
{
    cglm_versor A, B;
    load_q(a, A);
    load_q(b, B);
    return glm_quat_dot(A, B);
}

quat quat_slerp(quat a, quat b, float t)
{
    cglm_versor A, B, D;
    load_q(a, A);
    load_q(b, B);
    glm_quat_slerp(A, B, t, D);
    return store_q(D);
}

quat quat_nlerp(quat a, quat b, float t)
{
    cglm_versor A, B, D;
    load_q(a, A);
    load_q(b, B);
    glm_quat_nlerp(A, B, t, D);
    return store_q(D);
}

float quat_angle(quat q)
{
    cglm_versor a;
    load_q(q, a);
    return glm_quat_angle(a);
}

vec3 quat_axis(quat q)
{
    cglm_versor a;
    cglm_vec3 d;
    load_q(q, a);
    glm_quat_axis(a, d);
    return store_v3(d);
}

quat quat_from_to(vec3 from_unit, vec3 to_unit)
{
    cglm_vec3 a, b;
    cglm_versor d;
    load_v3(from_unit, a);
    load_v3(to_unit, b);
    glm_quat_from_vecs(a, b, d);
    return store_q(d);
}

quat quat_from_euler(vec3 radians_xyz)
{
    cglm_vec3 a;
    cglm_versor d;
    load_v3(radians_xyz, a);
    glm_euler_zxy_quat(a, d);
    return store_q(d);
}

quat quat_look(vec3 dir, vec3 up)
{
    cglm_vec3 d, u;
    cglm_versor q;
    load_v3(dir, d);
    load_v3(up, u);
    glm_quat_for(d, u, q);
    return store_q(q);
}

quat quat_from_mat4(mat4 m)
{
    cglm_mat4 a;
    cglm_versor d;
    load_m4(m, a);
    glm_mat4_quat(a, d);
    return store_q(d);
}

vec3 m4_project(vec3 obj, mat4 mvp, float vx, float vy, float vw, float vh)
{
    cglm_vec3 p, d;
    cglm_mat4 m;
    cglm_vec4 vp = {vx, vy, vw, vh};
    load_v3(obj, p);
    load_m4(mvp, m);
    glm_project(p, m, vp, d);
    return store_v3(d);
}

vec3 m4_unproject(vec3 win, mat4 mvp, float vx, float vy, float vw, float vh)
{
    cglm_vec3 p, d;
    cglm_mat4 m;
    cglm_vec4 vp = {vx, vy, vw, vh};
    load_v3(win, p);
    load_m4(mvp, m);
    glm_unproject(p, m, vp, d);
    return store_v3(d);
}
