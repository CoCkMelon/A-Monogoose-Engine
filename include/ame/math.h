#ifndef AME_MATH_H
#define AME_MATH_H

#include <math.h>
#include <string.h>

#ifndef AME_DIM
#  if defined(AME_2D) && !defined(AME_3D)
#    define AME_DIM 2
#  else
#    define AME_DIM 3
#  endif
#endif

/*
 * Short names: v2/v3 construct vectors; m4_* construct column-major 4x4
 * matrices. Kept short because they show up in hot geometry code.
 *
 * Unity-like names (not ECS): vec3 ~ Vector3, quat ~ Quaternion,
 * ame_transform ~ Transform (position / rotation / scale).
 *
 * Non-trivial ops (inverse, slerp, euler, project) are recp/cglm in
 * src/math.c. Types stay structs so games never see cglm float[3].
 */

typedef struct { float x, y; } vec2;
typedef struct { float x, y, z; } vec3;
typedef struct { float x, y, z, w; } vec4;
typedef struct { float x, y, z, w; } quat; /* x,y,z imag, w real — Unity Quaternion */
typedef struct { float m[9]; } mat3;  /* column-major */
typedef struct { float m[16]; } mat4; /* column-major */

static inline vec2 v2(float x, float y) { vec2 r = {x, y}; return r; }
static inline vec3 v3(float x, float y, float z) { vec3 r = {x, y, z}; return r; }
static inline vec4 v4(float x, float y, float z, float w)
{
    vec4 r = {x, y, z, w};
    return r;
}
static inline quat quat_make(float x, float y, float z, float w)
{
    quat q; q.x = x; q.y = y; q.z = z; q.w = w; return q;
}

static inline vec3 v3_add(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3 v3_sub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3 v3_scale(vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3 v3_cross(vec3 a, vec3 b)
{
    return v3(a.y * b.z - a.z * b.y,
              a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}
static inline float v3_len(vec3 a) { return sqrtf(v3_dot(a, a)); }
static inline vec3 v3_normalize(vec3 a)
{
    float l = v3_len(a);
    if (l < 1e-8f) return v3(0, 0, 1);
    return v3_scale(a, 1.0f / l);
}

static inline vec3 v3_mul(vec3 a, vec3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline vec3 v3_negate(vec3 a) { return v3(-a.x, -a.y, -a.z); }
static inline vec3 v3_abs(vec3 a) { return v3(fabsf(a.x), fabsf(a.y), fabsf(a.z)); }
static inline vec3 v3_min(vec3 a, vec3 b)
{
    return v3(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z);
}
static inline vec3 v3_max(vec3 a, vec3 b)
{
    return v3(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z);
}
static inline vec3 v3_lerp(vec3 a, vec3 b, float t)
{
    return v3_add(a, v3_scale(v3_sub(b, a), t));
}
static inline float v3_distance2(vec3 a, vec3 b)
{
    vec3 d = v3_sub(a, b);
    return v3_dot(d, d);
}
static inline float v3_distance(vec3 a, vec3 b) { return sqrtf(v3_distance2(a, b)); }
/* I - 2 dot(I,N) N. `n` should be unit. */
static inline vec3 v3_reflect(vec3 i, vec3 n)
{
    return v3_sub(i, v3_scale(n, 2.0f * v3_dot(i, n)));
}
static inline vec3 v3_clamp(vec3 a, vec3 lo, vec3 hi)
{
    return v3_min(v3_max(a, lo), hi);
}

static inline vec2 v2_add(vec2 a, vec2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline vec2 v2_sub(vec2 a, vec2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline vec2 v2_scale(vec2 a, float s) { return v2(a.x * s, a.y * s); }
static inline float v2_dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
static inline float v2_len(vec2 a) { return sqrtf(v2_dot(a, a)); }
static inline vec2 v2_normalize(vec2 a)
{
    float l = v2_len(a);
    if (l < 1e-8f) return v2(1, 0);
    return v2_scale(a, 1.0f / l);
}
static inline vec2 v2_lerp(vec2 a, vec2 b, float t)
{
    return v2_add(a, v2_scale(v2_sub(b, a), t));
}
static inline float v2_distance(vec2 a, vec2 b)
{
    vec2 d = v2_sub(a, b);
    return v2_len(d);
}

static inline vec4 v4_add(vec4 a, vec4 b) { return v4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
static inline vec4 v4_sub(vec4 a, vec4 b) { return v4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
static inline vec4 v4_scale(vec4 a, float s) { return v4(a.x * s, a.y * s, a.z * s, a.w * s); }
static inline float v4_dot(vec4 a, vec4 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
static inline float v4_len(vec4 a) { return sqrtf(v4_dot(a, a)); }
static inline vec4 v4_normalize(vec4 a)
{
    float l = v4_len(a);
    if (l < 1e-8f) return v4(0, 0, 0, 1);
    return v4_scale(a, 1.0f / l);
}
static inline vec4 v4_lerp(vec4 a, vec4 b, float t)
{
    return v4_add(a, v4_scale(v4_sub(b, a), t));
}
static inline vec4 v4_from_v3(vec3 a, float w) { return v4(a.x, a.y, a.z, w); }

static inline quat quat_ident(void) { return quat_make(0, 0, 0, 1); }

static inline quat quat_mul(quat a, quat b)
{
    return quat_make(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

static inline quat quat_from_axis_angle(vec3 axis, float radians)
{
    axis = v3_normalize(axis);
    float h = radians * 0.5f;
    float s = sinf(h);
    return quat_make(axis.x * s, axis.y * s, axis.z * s, cosf(h));
}

/* Unity Quaternion.Euler, degrees, applied Z * X * Y (Unity ZXY). We use
 * a single-axis helper for 2.5D yaw around Z. */
static inline quat quat_from_euler_z(float radians)
{
    return quat_from_axis_angle(v3(0, 0, 1), radians);
}

static inline vec3 quat_rotate(quat q, vec3 v)
{
    vec3 u = v3(q.x, q.y, q.z);
    vec3 t = v3_scale(v3_cross(u, v), 2.0f);
    return v3_add(v, v3_add(v3_scale(t, q.w), v3_cross(u, t)));
}

static inline mat4 m4_ident(void)
{
    mat4 r;
    memset(&r, 0, sizeof(r));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static inline mat4 m4_mul(mat4 a, mat4 b)
{
    mat4 r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            r.m[c * 4 + row] =
                a.m[0 * 4 + row] * b.m[c * 4 + 0] +
                a.m[1 * 4 + row] * b.m[c * 4 + 1] +
                a.m[2 * 4 + row] * b.m[c * 4 + 2] +
                a.m[3 * 4 + row] * b.m[c * 4 + 3];
        }
    }
    return r;
}

/* Affine: w=1. Same layout as gfx ame_transform_point. */
static inline vec3 m4_mul_point(mat4 m, vec3 p)
{
    return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12],
              m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13],
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}
static inline vec3 m4_mul_dir(mat4 m, vec3 d)
{
    return v3(m.m[0] * d.x + m.m[4] * d.y + m.m[8]  * d.z,
              m.m[1] * d.x + m.m[5] * d.y + m.m[9]  * d.z,
              m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z);
}

static inline mat4 m4_translate(float x, float y, float z)
{
    mat4 r = m4_ident();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

static inline mat4 m4_scale(float x, float y, float z)
{
    mat4 r = m4_ident();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}

static inline mat4 m4_rotate_x(float a)
{
    float c = cosf(a), s = sinf(a);
    mat4 r = m4_ident();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

static inline mat4 m4_rotate_y(float a)
{
    float c = cosf(a), s = sinf(a);
    mat4 r = m4_ident();
    r.m[0] = c;
    r.m[2] = s;
    r.m[8] = -s;
    r.m[10] = c;
    return r;
}

static inline mat4 m4_rotate_z(float a)
{
    float c = cosf(a), s = sinf(a);
    mat4 r = m4_ident();
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}

static inline mat4 m4_from_quat(quat q)
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    mat4 r = m4_ident();
    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + wz);
    r.m[2] = 2.0f * (xz - wy);
    r.m[4] = 2.0f * (xy - wz);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + wx);
    r.m[8] = 2.0f * (xz + wy);
    r.m[9] = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    return r;
}

/* Unity Transform.localToWorldMatrix: T * R * S. */
static inline mat4 m4_trs(vec3 position, quat rotation, vec3 scale)
{
    return m4_mul(m4_translate(position.x, position.y, position.z),
                  m4_mul(m4_from_quat(rotation),
                         m4_scale(scale.x, scale.y, scale.z)));
}

static inline mat4 m4_ortho(float l, float r, float b, float t, float n, float f)
{
    mat4 o;
    memset(&o, 0, sizeof(o));
    o.m[0] = 2.0f / (r - l);
    o.m[5] = 2.0f / (t - b);
    o.m[10] = -2.0f / (f - n);
    o.m[12] = -(r + l) / (r - l);
    o.m[13] = -(t + b) / (t - b);
    o.m[14] = -(f + n) / (f - n);
    o.m[15] = 1.0f;
    return o;
}

static inline mat4 m4_perspective(float fov_y_radians, float aspect, float n, float f)
{
    mat4 o;
    memset(&o, 0, sizeof(o));
    if (aspect < 1e-6f) aspect = 1e-6f;
    if (n < 1e-6f) n = 1e-6f;
    float t = tanf(fov_y_radians * 0.5f);
    if (t < 1e-6f) t = 1e-6f;
    o.m[0] = 1.0f / (aspect * t);
    o.m[5] = 1.0f / t;
    o.m[10] = -(f + n) / (f - n);
    o.m[11] = -1.0f;
    o.m[14] = -(2.0f * f * n) / (f - n);
    return o;
}

/* OpenGL look-at: camera looks from eye toward target, Y-ish up. */
static inline mat4 m4_look_at(vec3 eye, vec3 target, vec3 up)
{
    vec3 f = v3_normalize(v3_sub(target, eye));
    vec3 z = v3_scale(f, -1.0f); /* camera backward (+Z in view) */
    vec3 x = v3_normalize(v3_cross(up, z));
    vec3 y = v3_cross(z, x);
    mat4 r = m4_ident();
    r.m[0] = x.x; r.m[4] = x.y; r.m[8]  = x.z;
    r.m[1] = y.x; r.m[5] = y.y; r.m[9]  = y.z;
    r.m[2] = z.x; r.m[6] = z.y; r.m[10] = z.z;
    r.m[12] = -v3_dot(x, eye);
    r.m[13] = -v3_dot(y, eye);
    r.m[14] = -v3_dot(z, eye);
    return r;
}

/* Camera at +Z looking toward origin, Y up. Same as look_at((0,0,e),(0,0,0),(0,1,0)). */
static inline mat4 m4_look_down_z(float eye_z)
{
    mat4 r = m4_ident();
    r.m[14] = -eye_z;
    return r;
}

static inline float clampf(float x, float a, float b)
{
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static inline float smooth01(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline float ame_rad(float degrees) { return degrees * 0.017453292519943295f; }
static inline float ame_deg(float radians) { return radians * 57.29577951308232f; }

/* ---- cglm-backed (src/math.c) ---- */
mat4  m4_inverse(mat4 m);
mat4  m4_transpose(mat4 m);
float m4_det(mat4 m);

mat3  m3_ident(void);
mat3  m3_mul(mat3 a, mat3 b);
mat3  m3_inverse(mat3 m);
mat3  m3_transpose(mat3 m);
vec3  m3_mulv(mat3 m, vec3 v);
mat3  m3_from_m4(mat4 m);
mat3  m3_from_quat(quat q);

quat  quat_normalize(quat q);
quat  quat_conjugate(quat q);
quat  quat_inverse(quat q);
float quat_dot(quat a, quat b);
quat  quat_slerp(quat a, quat b, float t);
quat  quat_nlerp(quat a, quat b, float t);
float quat_angle(quat q);
vec3  quat_axis(quat q);
quat  quat_from_to(vec3 from_unit, vec3 to_unit);
/* Unity Quaternion.Euler order ZXY, radians. */
quat  quat_from_euler(vec3 radians_xyz);
quat  quat_look(vec3 dir, vec3 up);
quat  quat_from_mat4(mat4 m);

/* Window coords: viewport (vx, vy, vw, vh). mvp = proj * view [* model]. */
vec3  m4_project(vec3 obj, mat4 mvp, float vx, float vy, float vw, float vh);
vec3  m4_unproject(vec3 win, mat4 mvp, float vx, float vy, float vw, float vh);

/* Unity Transform. HOT object — not a SETUP chain. */
typedef struct ame_transform {
    vec3 position;
    quat rotation;
    vec3 scale;
} ame_transform;

static inline void ame_transform_identity(ame_transform *t)
{
    if (!t) return;
    t->position = v3(0, 0, 0);
    t->rotation = quat_ident();
    t->scale = v3(1, 1, 1);
}

static inline mat4 ame_transform_matrix(const ame_transform *t)
{
    if (!t) return m4_ident();
    return m4_trs(t->position, t->rotation, t->scale);
}

#endif
