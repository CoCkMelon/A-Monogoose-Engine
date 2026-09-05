/* ame-next — math for 2D and 3D from one core (principles rule 6).
 *
 * vec2 is always available. vec3/mat4 exist for both dimensions: the 2D
 * pixel-perfect camera uses an orthographic mat4, the 3D camera a
 * perspective mat4 (camera.txt has ONE camera module, not 2d/3d twins).
 * All header-only pure functions; no state, no allocation.
 *
 * Column-major mat4, right-handed, clip z in [-1,1] (GL convention).
 */
#ifndef AME_MATH_H
#define AME_MATH_H

#include <ame/ame.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AME_PI
#define AME_PI 3.14159265358979323846f
#endif

typedef struct { float x, y; }         ame_v2;
typedef struct { float x, y, z; }      ame_v3;
typedef struct { float x, y, z, w; }   ame_v4;
typedef struct { float m[16]; }        ame_m4; /* column-major */

static inline ame_v2 ame_v2_(float x, float y) { ame_v2 v = { x, y }; return v; }
static inline ame_v2 ame_v2_add(ame_v2 a, ame_v2 b) { return ame_v2_(a.x + b.x, a.y + b.y); }
static inline ame_v2 ame_v2_sub(ame_v2 a, ame_v2 b) { return ame_v2_(a.x - b.x, a.y - b.y); }
static inline ame_v2 ame_v2_scale(ame_v2 a, float s) { return ame_v2_(a.x * s, a.y * s); }
static inline float ame_v2_dot(ame_v2 a, ame_v2 b) { return a.x * b.x + a.y * b.y; }
static inline float ame_v2_len(ame_v2 a) { return sqrtf(ame_v2_dot(a, a)); }
static inline float ame_v2_dist(ame_v2 a, ame_v2 b) { return ame_v2_len(ame_v2_sub(a, b)); }
static inline ame_v2 ame_v2_norm(ame_v2 a) {
    float l = ame_v2_len(a);
    return l > 1e-12f ? ame_v2_scale(a, 1.0f / l) : ame_v2_(0, 0);
}

static inline ame_v3 ame_v3_(float x, float y, float z) { ame_v3 v = { x, y, z }; return v; }
static inline ame_v3 ame_v3_add(ame_v3 a, ame_v3 b) { return ame_v3_(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline ame_v3 ame_v3_sub(ame_v3 a, ame_v3 b) { return ame_v3_(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline ame_v3 ame_v3_scale(ame_v3 a, float s) { return ame_v3_(a.x * s, a.y * s, a.z * s); }
static inline float ame_v3_dot(ame_v3 a, ame_v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline ame_v3 ame_v3_cross(ame_v3 a, ame_v3 b) {
    return ame_v3_(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float ame_v3_len(ame_v3 a) { return sqrtf(ame_v3_dot(a, a)); }
static inline float ame_v3_dist(ame_v3 a, ame_v3 b) { return ame_v3_len(ame_v3_sub(a, b)); }
static inline ame_v3 ame_v3_norm(ame_v3 a) {
    float l = ame_v3_len(a);
    return l > 1e-12f ? ame_v3_scale(a, 1.0f / l) : ame_v3_(0, 0, 0);
}

/* --- mat4 ---------------------------------------------------------------- */

static inline ame_m4 ame_m4_identity(void) {
    ame_m4 r;
    for (int i = 0; i < 16; i++) r.m[i] = 0.0f;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

/* r = a * b (apply b first, then a) */
static inline ame_m4 ame_m4_mul(ame_m4 a, ame_m4 b) {
    ame_m4 r;
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++)
                s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}

static inline ame_v3 ame_m4_xform_point(ame_m4 m, ame_v3 p) {
    return ame_v3_(
        m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}

static inline ame_v3 ame_m4_xform_dir(ame_m4 m, ame_v3 d) {
    return ame_v3_(
        m.m[0] * d.x + m.m[4] * d.y + m.m[8]  * d.z,
        m.m[1] * d.x + m.m[5] * d.y + m.m[9]  * d.z,
        m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z);
}

static inline ame_m4 ame_m4_translate(ame_v3 t) {
    ame_m4 r = ame_m4_identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
static inline ame_m4 ame_m4_scale(ame_v3 s) {
    ame_m4 r = ame_m4_identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}

static inline ame_m4 ame_m4_rot_y(float rad) {
    float c = cosf(rad), s = sinf(rad);
    ame_m4 r = ame_m4_identity();
    r.m[0] = c;  r.m[8]  = s;
    r.m[2] = -s; r.m[10] = c;
    return r;
}

static inline ame_m4 ame_m4_rot_z(float rad) {
    float c = cosf(rad), s = sinf(rad);
    ame_m4 r = ame_m4_identity();
    r.m[0] = c;  r.m[4] = -s;
    r.m[1] = s;  r.m[5] =  c;
    return r;
}

static inline ame_m4 ame_m4_rot_x(float rad) {
    float c = cosf(rad), s = sinf(rad);
    ame_m4 r = ame_m4_identity();
    r.m[5] = c;  r.m[9]  = -s;
    r.m[6] = s;  r.m[10] =  c;
    return r;
}

/* Right-handed look-at view matrix (eye -> target). */
static inline ame_m4 ame_m4_look_at(ame_v3 eye, ame_v3 at, ame_v3 up) {
    ame_v3 f = ame_v3_norm(ame_v3_sub(at, eye));
    /* audit fix: f parallel to up (looking straight down/up with a
     * world-up convention) zeroed the cross product and produced a
     * degenerate view matrix (nothing rendered). Substitute a fallback
     * axis; every NON-degenerate case is bit-identical to before (the
     * guard only fires where the old result was zeros). */
    ame_v3 cr = ame_v3_cross(f, up);
    if (cr.x * cr.x + cr.y * cr.y + cr.z * cr.z < 1e-8f) {
        up = fabsf(f.z) < 0.9f ? ame_v3_(0, 0, 1) : ame_v3_(0, 1, 0);
        cr = ame_v3_cross(f, up);
    }
    ame_v3 s = ame_v3_norm(cr);
    ame_v3 u = ame_v3_cross(s, f);
    ame_m4 r = ame_m4_identity();
    r.m[0] =  s.x; r.m[4] =  s.y; r.m[8]  =  s.z;
    r.m[1] =  u.x; r.m[5] =  u.y; r.m[9]  =  u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -ame_v3_dot(s, eye);
    r.m[13] = -ame_v3_dot(u, eye);
    r.m[14] =  ame_v3_dot(f, eye);
    return r;
}

/* Perspective projection, fov_y in radians, z in [-1,1]. */
static inline ame_m4 ame_m4_perspective(float fov_y, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fov_y * 0.5f);
    ame_m4 r;
    for (int i = 0; i < 16; i++) r.m[i] = 0.0f;
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zf * zn) / (zn - zf);
    return r;
}

/* Orthographic projection (2D pixel space: x right, y DOWN, origin top-left,
 * 1 unit = 1 pixel at zoom 1). This is what the pixel-perfect 2D camera
 * uses (render.txt rule 4). */
static inline ame_m4 ame_m4_ortho_px(float w, float h, float zn, float zf) {
    ame_m4 r = ame_m4_identity();
    r.m[0]  =  2.0f / w;
    r.m[5]  = -2.0f / h;
    r.m[10] =  1.0f / (zf - zn);
    r.m[12] = -1.0f;   /* x=0 -> NDC -1 (left) */
    r.m[13] =  1.0f;   /* y=0 -> NDC +1 (top; y axis points down in px space) */
    r.m[14] = -zn / (zf - zn);
    return r;
}

/* General 4x4 inverse (adjugate method, gl-matrix formula). Column-major.
 * Returns identity when singular. Unit-tested: M * inv(M) == I. */
static inline ame_m4 ame_m4_inverse(ame_m4 m) {
    const float *a = m.m;
    float a00=a[0],a01=a[1],a02=a[2],a03=a[3];
    float a10=a[4],a11=a[5],a12=a[6],a13=a[7];
    float a20=a[8],a21=a[9],a22=a[10],a23=a[11];
    float a30=a[12],a31=a[13],a32=a[14],a33=a[15];
    float b0 = a00*a11 - a01*a10,  b1 = a00*a12 - a02*a10;
    float b2 = a00*a13 - a03*a10,  b3 = a01*a12 - a02*a11;
    float b4 = a01*a13 - a03*a11,  b5 = a02*a13 - a03*a12;
    float b6 = a20*a31 - a21*a30,  b7 = a20*a32 - a22*a30;
    float b8 = a20*a33 - a23*a30,  b9 = a21*a32 - a22*a31;
    float b10= a21*a33 - a23*a31,  b11= a22*a33 - a23*a32;
    float det = b0*b11 - b1*b10 + b2*b9 + b3*b8 - b4*b7 + b5*b6;
    ame_m4 r;
    if (det > -1e-12f && det < 1e-12f)
        return ame_m4_identity();
    float id = 1.0f / det;
    float *o = r.m;
    o[0]  = (a11*b11 - a12*b10 + a13*b9 ) * id;
    o[1]  = (a02*b10 - a01*b11 - a03*b9 ) * id;
    o[2]  = (a31*b5  - a32*b4  + a33*b3 ) * id;
    o[3]  = (a22*b4  - a21*b5  - a23*b3 ) * id;
    o[4]  = (a12*b8  - a10*b11 - a13*b7 ) * id;
    o[5]  = (a00*b11 - a02*b8  + a03*b7 ) * id;
    o[6]  = (a32*b2  - a30*b5  - a33*b1 ) * id;
    o[7]  = (a20*b5  - a22*b2  + a23*b1 ) * id;
    o[8]  = (a10*b10 - a11*b8  + a13*b6 ) * id;
    o[9]  = (a01*b8  - a00*b10 - a03*b6 ) * id;
    o[10] = (a30*b4  - a31*b2  + a33*b0 ) * id;
    o[11] = (a21*b2  - a20*b4  - a23*b0 ) * id;
    o[12] = (a11*b7  - a10*b9  - a12*b6 ) * id;
    o[13] = (a00*b9  - a01*b7  + a02*b6 ) * id;
    o[14] = (a31*b1  - a30*b3  - a32*b0 ) * id;
    o[15] = (a20*b3  - a21*b1  + a22*b0 ) * id;
    return r;
}

#ifdef __cplusplus
}
#endif

#endif /* AME_MATH_H */
