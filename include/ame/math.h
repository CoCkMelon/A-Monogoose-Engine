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
static inline ame_m4 ame_m4_mul_scalar(ame_m4 a, ame_m4 b) {
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

#if defined(__SSE2__)
/* SSE2 twin: each dest element sums the SAME products in the SAME
 * order as the scalar loop (k = 0..3, += per term); packed float
 * mul/add are IEEE binary32 lane-exact and shuffle broadcasts carry
 * identical bits, so results are BIT-IDENTICAL to ame_m4_mul_scalar
 * (asserted by test_math_cglm on every oracle iteration). */
#include <emmintrin.h> /* SSE2: guaranteed on x86-64 */
static inline ame_m4 ame_m4_mul(ame_m4 a, ame_m4 b) {
    const __m128 a0 = _mm_loadu_ps(a.m);
    const __m128 a1 = _mm_loadu_ps(a.m + 4);
    const __m128 a2 = _mm_loadu_ps(a.m + 8);
    const __m128 a3 = _mm_loadu_ps(a.m + 12);
    ame_m4 r;
    for (int c = 0; c < 4; c++) {
        const __m128 bc = _mm_loadu_ps(b.m + c * 4); /* column c of b */
        __m128 acc = _mm_add_ps(
            _mm_add_ps(_mm_add_ps(
                _mm_mul_ps(_mm_shuffle_ps(bc, bc, _MM_SHUFFLE(0, 0, 0, 0)), a0),
                _mm_mul_ps(_mm_shuffle_ps(bc, bc, _MM_SHUFFLE(1, 1, 1, 1)), a1)),
                _mm_mul_ps(_mm_shuffle_ps(bc, bc, _MM_SHUFFLE(2, 2, 2, 2)), a2)),
            _mm_mul_ps(_mm_shuffle_ps(bc, bc, _MM_SHUFFLE(3, 3, 3, 3)), a3));
        _mm_storeu_ps(r.m + c * 4, acc);
    }
    return r;
}
#else
static inline ame_m4 ame_m4_mul(ame_m4 a, ame_m4 b) {
    return ame_m4_mul_scalar(a, b);
}
#endif

/* Pointer form for hot loops (transform hierarchies, batched draws):
 * same products in the same order as ame_m4_mul_scalar -> identical
 * results, without by-value struct traffic. */
static inline void ame_m4_mul_to(const ame_m4 *restrict a, const ame_m4 *restrict b,
                                 ame_m4 *restrict out) {
#if defined(__SSE2__)
    const __m128 a0 = _mm_loadu_ps(a->m);
    const __m128 a1 = _mm_loadu_ps(a->m + 4);
    const __m128 a2 = _mm_loadu_ps(a->m + 8);
    const __m128 a3 = _mm_loadu_ps(a->m + 12);
    for (int c = 0; c < 4; c++) {
        const __m128 bc = _mm_loadu_ps(b->m + c * 4); /* column c of b */
        __m128 acc = _mm_add_ps(
            _mm_add_ps(_mm_add_ps(
                _mm_mul_ps(_mm_shuffle_ps(bc, bc, _MM_SHUFFLE(0, 0, 0, 0)), a0),
                _mm_mul_ps(_mm_shuffle_ps(bc, bc, _MM_SHUFFLE(1, 1, 1, 1)), a1)),
                _mm_mul_ps(_mm_shuffle_ps(bc, bc, _MM_SHUFFLE(2, 2, 2, 2)), a2)),
            _mm_mul_ps(_mm_shuffle_ps(bc, bc, _MM_SHUFFLE(3, 3, 3, 3)), a3));
        _mm_storeu_ps(out->m + c * 4, acc);
    }
#else
    *out = ame_m4_mul_scalar(*a, *b);
#endif
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

/* =====================================================================
 * cglm-parity additions: v4, quat, m3, frustum, curves, axis-angle.
 * Same rules as above: static inline, no allocation, per-binary
 * deterministic (no FMA contraction - build uses -ffp-contract=off).
 * Verified against cglm 0.9 as a test oracle (tests/test_math_cglm.c).
 * ===================================================================== */

/* --- vec4 ------------------------------------------------------------------ */
static inline ame_v4 ame_v4_(float x, float y, float z, float w) {
    ame_v4 v = { x, y, z, w };
    return v;
}
static inline ame_v4 ame_v4_add(ame_v4 a, ame_v4 b) {
    return ame_v4_(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}
static inline ame_v4 ame_v4_scale(ame_v4 a, float s) {
    return ame_v4_(a.x * s, a.y * s, a.z * s, a.w * s);
}
static inline float ame_v4_dot(ame_v4 a, ame_v4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
static inline ame_v4 ame_v4_sub(ame_v4 a, ame_v4 b) {
    return ame_v4_(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}
static inline ame_v4 ame_v4_lerp(ame_v4 a, ame_v4 b, float t) {
    return ame_v4_add(a, ame_v4_scale(ame_v4_sub(b, a), t));
}

/* --- mat3 (normal matrices, rotations) ------------------------------------- */
typedef struct { float m[9]; } ame_m3; /* column-major */

static inline ame_m3 ame_m3_identity(void) {
    ame_m3 r;
    for (int i = 0; i < 9; i++) r.m[i] = 0.0f;
    r.m[0] = r.m[4] = r.m[8] = 1.0f;
    return r;
}
static inline ame_m3 ame_m3_mul(ame_m3 a, ame_m3 b) {
    ame_m3 r;
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++) {
            float s = 0.0f;
            for (int k = 0; k < 3; k++)
                s += a.m[k * 3 + row] * b.m[c * 3 + k];
            r.m[c * 3 + row] = s;
        }
    return r;
}
static inline ame_v3 ame_m3_mulv(ame_m3 m, ame_v3 v) {
    return ame_v3_(
        m.m[0] * v.x + m.m[3] * v.y + m.m[6] * v.z,
        m.m[1] * v.x + m.m[4] * v.y + m.m[7] * v.z,
        m.m[2] * v.x + m.m[5] * v.y + m.m[8] * v.z);
}
static inline ame_m3 ame_m3_transpose(ame_m3 m) {
    ame_m3 r;
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++)
            r.m[c * 3 + row] = m.m[row * 3 + c];
    return r;
}
/* upper-left 3x3 of an m4 (rotation/scale part: normal matrix input) */
static inline ame_m3 ame_m3_from_m4(ame_m4 m) {
    ame_m3 r;
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++)
            r.m[c * 3 + row] = m.m[c * 4 + row];
    return r;
}
/* general 3x3 inverse (adjugate / det); identity when singular */
static inline ame_m3 ame_m3_inverse(ame_m3 m) {
    float a = m.m[0], b = m.m[3], c = m.m[6];
    float d = m.m[1], e = m.m[4], f = m.m[7];
    float g = m.m[2], h = m.m[5], i = m.m[8];
    float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (det > -1e-12f && det < 1e-12f)
        return ame_m3_identity();
    float inv = 1.0f / det;
    ame_m3 r;
    r.m[0] = A * inv;            r.m[3] = -(b * i - c * h) * inv;
    r.m[6] = (b * f - c * e) * inv;
    r.m[1] = B * inv;            r.m[4] = (a * i - c * g) * inv;
    r.m[7] = -(a * f - c * d) * inv;
    r.m[2] = C * inv;            r.m[5] = -(a * h - b * g) * inv;
    r.m[8] = (a * e - b * d) * inv;
    return r;
}

/* --- quaternions (x, y, z, w) ---------------------------------------------- */
typedef struct { float x, y, z, w; } ame_quat;

static inline ame_quat ame_quat_(float x, float y, float z, float w) {
    ame_quat q = { x, y, z, w };
    return q;
}
static inline ame_quat ame_quat_identity(void) {
    return ame_quat_(0, 0, 0, 1);
}
static inline float ame_quat_dot(ame_quat a, ame_quat b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
static inline ame_quat ame_quat_norm(ame_quat q) {
    float l = sqrtf(ame_quat_dot(q, q));
    return l > 1e-12f ? ame_quat_(q.x / l, q.y / l, q.z / l, q.w / l)
                      : ame_quat_identity();
}
static inline ame_quat ame_quat_conj(ame_quat q) {
    return ame_quat_(-q.x, -q.y, -q.z, q.w);
}
/* Hamilton product a (x) b: apply b FIRST, then a (gl-matrix order) */
static inline ame_quat ame_quat_mul_scalar(ame_quat a, ame_quat b) {
    return ame_quat_(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

#if defined(__SSE2__)
/* lane-per-component twin of the scalar Hamilton product. Each lane
 * evaluates the SAME four products in the SAME order as the scalar
 * body (((t1 +/- t2) +/- t3) +/- t4); a - b == a + (-b) in IEEE754 so
 * signs fold into term negation. Bit-identical, ~2x faster. */
static inline ame_quat ame_quat_mul(ame_quat a, ame_quat b) {
    const __m128i sgn_odd = _mm_set_epi32((int)0x80000000, 0, (int)0x80000000, 0); /* t2: negate lanes 1,3 */
    const __m128i sgn_hi  = _mm_set_epi32((int)0x80000000, (int)0x80000000, 0, 0); /* t3: lanes 2,3 */
    const __m128i sgn_end = _mm_set_epi32((int)0x80000000, 0, 0, (int)0x80000000); /* t4: lanes 0,3 */
    __m128 vb = _mm_setr_ps(b.x, b.y, b.z, b.w);
    __m128 t1 = _mm_mul_ps(_mm_set1_ps(a.w), vb);                    /* aw*b(lane) */
    __m128 p2 = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(vb), _MM_SHUFFLE(0, 1, 2, 3))); /* bw,bz,by,bx */
    __m128 t2 = _mm_xor_ps(_mm_mul_ps(_mm_set1_ps(a.x), p2), _mm_castsi128_ps(sgn_odd));
    __m128 p3 = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(vb), _MM_SHUFFLE(1, 0, 3, 2))); /* bz,bw,bx,by */
    __m128 t3 = _mm_xor_ps(_mm_mul_ps(_mm_set1_ps(a.y), p3), _mm_castsi128_ps(sgn_hi));
    __m128 p4 = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(vb), _MM_SHUFFLE(2, 3, 0, 1))); /* by,bx,bw,bz */
    __m128 t4 = _mm_xor_ps(_mm_mul_ps(_mm_set1_ps(a.z), p4), _mm_castsi128_ps(sgn_end));
    __m128 r  = _mm_add_ps(_mm_add_ps(_mm_add_ps(t1, t2), t3), t4);
    ame_quat q;
    _mm_storeu_ps(&q.x, r);
    return q;
}
#else
static inline ame_quat ame_quat_mul(ame_quat a, ame_quat b) {
    return ame_quat_mul_scalar(a, b);
}
#endif
/* axis must be normalized; rad counter-clockwise around the axis */
static inline ame_quat ame_quat_axis_angle(float rad, ame_v3 axis) {
    float h = rad * 0.5f, s = sinf(h);
    return ame_quat_(axis.x * s, axis.y * s, axis.z * s, cosf(h));
}
/* rotate a vector: v' = q (x) v (x) q* (q must be unit) */
static inline ame_v3 ame_quat_rotate_v3(ame_quat q, ame_v3 v) {
    ame_quat p = ame_quat_(v.x, v.y, v.z, 0.0f);
    ame_quat r = ame_quat_mul(ame_quat_mul(q, p), ame_quat_conj(q));
    return ame_v3_(r.x, r.y, r.z);
}
/* unit quaternion -> rotation matrix (gl-matrix layout) */
static inline ame_m4 ame_quat_mat4(ame_quat q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    ame_m4 r = ame_m4_identity();
    r.m[0] = 1.0f - (yy + zz); r.m[4] = xy - wz;         r.m[8]  = xz + wy;
    r.m[1] = xy + wz;         r.m[5] = 1.0f - (xx + zz); r.m[9]  = yz - wx;
    r.m[2] = xz - wy;         r.m[6] = yz + wx;          r.m[10] = 1.0f - (xx + yy);
    return r;
}
/* rotation matrix (no scale/shear) -> unit quaternion */
static inline ame_quat ame_quat_from_m4(ame_m4 m) {
    float tr = m.m[0] + m.m[5] + m.m[10];
    ame_quat q;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f; /* s = 4w */
        q = ame_quat_((m.m[6] - m.m[9]) / s, (m.m[8] - m.m[2]) / s,
                      (m.m[1] - m.m[4]) / s, 0.25f * s);
    } else if (m.m[0] > m.m[5] && m.m[0] > m.m[10]) {
        float s = sqrtf(1.0f + m.m[0] - m.m[5] - m.m[10]) * 2.0f;
        q = ame_quat_(0.25f * s, (m.m[4] + m.m[1]) / s,
                      (m.m[9] + m.m[2]) / s, (m.m[6] - m.m[9]) / s);
    } else if (m.m[5] > m.m[10]) {
        float s = sqrtf(1.0f + m.m[5] - m.m[0] - m.m[10]) * 2.0f;
        q = ame_quat_((m.m[4] + m.m[1]) / s, 0.25f * s,
                      (m.m[9] + m.m[6]) / s, (m.m[8] - m.m[2]) / s);
    } else {
        float s = sqrtf(1.0f + m.m[10] - m.m[0] - m.m[5]) * 2.0f;
        q = ame_quat_((m.m[8] + m.m[2]) / s, (m.m[9] + m.m[6]) / s,
                      0.25f * s, (m.m[1] - m.m[4]) / s);
    }
    return ame_quat_norm(q);
}
/* spherical interpolation (shortest path); t in [0,1] */
static inline ame_quat ame_quat_slerp(ame_quat a, ame_quat b, float t) {
    float d = ame_quat_dot(a, b);
    if (d < 0.0f) { /* double cover: take the short way */
        b = ame_quat_(-b.x, -b.y, -b.z, -b.w);
        d = -d;
    }
    if (d > 0.9995f) { /* nearly parallel: normalized lerp */
        ame_quat r = ame_quat_(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y),
                               a.z + t * (b.z - a.z), a.w + t * (b.w - a.w));
        return ame_quat_norm(r);
    }
    float th0 = acosf(d), th = th0 * t;
    float s0 = cosf(th) - d * sinf(th) / sinf(th0);
    float s1 = sinf(th) / sinf(th0);
    return ame_quat_(a.x * s0 + b.x * s1, a.y * s0 + b.y * s1,
                     a.z * s0 + b.z * s1, a.w * s0 + b.w * s1);
}

/* --- misc mat4 parity ------------------------------------------------------ */
static inline ame_m4 ame_m4_transpose(ame_m4 m) {
    ame_m4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r.m[c * 4 + row] = m.m[row * 4 + c];
    return r;
}
/* rotation about an ARBITRARY normalized axis (Rodrigues) */
static inline ame_m4 ame_m4_axis_angle(float rad, ame_v3 axis) {
    return ame_quat_mat4(ame_quat_axis_angle(rad, axis));
}

/* --- frustum culling (planes from a view-projection matrix) ---------------- */
typedef struct {
    /* [left,right,bottom,top,near,far] each (a,b,c,d):
     * inside the frustum <=> a*x + b*y + c*z + d > 0 (normalized) */
    float p[6][4];
} ame_frustum;

typedef enum {
    AME_FRUSTUM_OUTSIDE = 0,
    AME_FRUSTUM_INTERSECT,
    AME_FRUSTUM_INSIDE,
} ame_frustum_res;

/* Gribb-Hartmann extraction (rows of the combined vp matrix) */
static inline ame_frustum ame_frustum_from_vp(ame_m4 vp) {
    ame_frustum f;
    for (int pl = 0; pl < 6; pl++) {
        int row = pl >> 1;           /* 0,0,1,1,2,2 */
        float sign = (pl & 1) ? -1.0f : 1.0f;
        float r3[4], ri[4];
        r3[0] = vp.m[0 * 4 + 3]; r3[1] = vp.m[1 * 4 + 3];
        r3[2] = vp.m[2 * 4 + 3]; r3[3] = vp.m[3 * 4 + 3];
        ri[0] = vp.m[0 * 4 + row]; ri[1] = vp.m[1 * 4 + row];
        ri[2] = vp.m[2 * 4 + row]; ri[3] = vp.m[3 * 4 + row];
        float n[3], d;
        n[0] = r3[0] + sign * ri[0];
        n[1] = r3[1] + sign * ri[1];
        n[2] = r3[2] + sign * ri[2];
        d = r3[3] + sign * ri[3];
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-12f) {
            n[0] /= len; n[1] /= len; n[2] /= len; d /= len;
        }
        f.p[pl][0] = n[0];
        f.p[pl][1] = n[1];
        f.p[pl][2] = n[2];
        f.p[pl][3] = d;
    }
    return f;
}

static inline ame_frustum_res ame_frustum_aabb(const ame_frustum *f,
                                               const float mn[3],
                                               const float mx[3]) {
    bool intersecting = false;
    for (int pl = 0; pl < 6; pl++) {
        const float *P = f->p[pl];
        /* p-vertex: the corner FURTHEST along the plane normal */
        float px = P[0] > 0.0f ? mx[0] : mn[0];
        float py = P[1] > 0.0f ? mx[1] : mn[1];
        float pz = P[2] > 0.0f ? mx[2] : mn[2];
        if (P[0] * px + P[1] * py + P[2] * pz + P[3] < 0.0f)
            return AME_FRUSTUM_OUTSIDE;
        /* n-vertex: nearest corner; negative => the plane cuts the box */
        float nx = P[0] > 0.0f ? mn[0] : mx[0];
        float ny = P[1] > 0.0f ? mn[1] : mx[1];
        float nz = P[2] > 0.0f ? mn[2] : mx[2];
        if (P[0] * nx + P[1] * ny + P[2] * nz + P[3] < 0.0f)
            intersecting = true;
    }
    return intersecting ? AME_FRUSTUM_INTERSECT : AME_FRUSTUM_INSIDE;
}

static inline bool ame_frustum_sphere(const ame_frustum *f,
                                      const float c[3], float r) {
    for (int pl = 0; pl < 6; pl++) {
        const float *P = f->p[pl];
        if (P[0] * c[0] + P[1] * c[1] + P[2] * c[2] + P[3] < -r)
            return false;
    }
    return true;
}

/* --- curves (cameras, paths, particles) ------------------------------------ */
static inline ame_v3 ame_bezier3(ame_v3 p0, ame_v3 p1, ame_v3 p2,
                                 ame_v3 p3, float t) {
    float u = 1.0f - t;
    float a = u * u * u, b = 3.0f * u * u * t;
    float c = 3.0f * u * t * t, d = t * t * t;
    return ame_v3_(a * p0.x + b * p1.x + c * p2.x + d * p3.x,
                   a * p0.y + b * p1.y + c * p2.y + d * p3.y,
                   a * p0.z + b * p1.z + c * p2.z + d * p3.z);
}
static inline ame_v3 ame_catmull_rom(ame_v3 p0, ame_v3 p1, ame_v3 p2,
                                     ame_v3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return ame_v3_(
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t
                + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2
                + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t
                + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2
                + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
        0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t
                + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2
                + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3));
}

#ifdef __cplusplus
}
#endif

#endif /* AME_MATH_H */
