/* ame-next — common types shared by every module.
 *
 * Spec: principles.txt (two-layer rule, split threads), data.txt (handles),
 * agents.txt (LOGD), build.txt (AME_2D/AME_3D dimension macro).
 *
 * This header has NO state and NO platform headers; it is safe to include
 * everywhere (engine, game, tools, both dimensions, all targets).
 */
#ifndef AME_AME_H
#define AME_AME_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- dimension (principles rule 6) ---------------------------------------
 * A build defines exactly ONE of these. Shared modules (pools, events,
 * audio, text, UI, input) are dimension-agnostic; only math/camera/geometry
 * vary. Do not compile both dimensions into one game. */
#if defined(AME_3D)
#  if defined(AME_2D)
#    error "define AME_2D or AME_3D, not both"
#  endif
#  define AME_DIM 3
#elif defined(AME_2D)
#  define AME_DIM 2
#else
#  error "a build must define AME_2D or AME_3D (build.txt rule 6)"
#endif

/* --- logging (agents.txt) --------------------------------------------------
 * Non-essential logs compile to nothing in release. Errors and one-time init
 * lines may always log. Never per-frame/per-keypress spam, never from audio
 * callbacks. */
#ifdef DEBUG
#  include <SDL3/SDL_log.h>
#  define LOGD(...) SDL_Log(__VA_ARGS__)
#else
#  define LOGD(...) ((void)0)
#endif

/* --- handles (data.txt rule 3) ----------------------------------------------
 * Refer to pooled things by (index, generation). Generation 0 is INVALID
 * (slots start at generation 1), so a stale handle fails loudly.
 * "No slot" is always AME_HANDLE_INVALID, never a sentinel like -1. */
typedef struct ame_handle {
    uint16_t idx;
    uint16_t gen;
} ame_handle;

#define AME_HANDLE_INVALID ((ame_handle){ .idx = 0, .gen = 0 })

static inline bool ame_handle_eq(ame_handle a, ame_handle b) {
    return a.idx == b.idx && a.gen == b.gen;
}

static inline bool ame_handle_valid(ame_handle h) {
    return h.gen != 0;
}

/* A cross-pool reference is (pool_id, index, generation) — data.txt.
 * Events store contacts as this triple (events.txt). */
typedef struct ame_ref {
    uint8_t  pool; /* which pool module; 0 = world/other */
    uint16_t idx;
    uint16_t gen;
} ame_ref;

#define AME_REF_INVALID ((ame_ref){ .pool = 0, .idx = 0, .gen = 0 })

/* --- small helpers ------------------------------------------------------- */
static inline float ame_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* smooth linear interpolation */
static inline float ame_lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/* --- deterministic RNG ----------------------------------------------------
 * One engine RNG: xorshift32, seeded per level/run (levels.txt SEEDS).
 * Integer-only state: identical per-binary replays (principles DETERMINISM).
 * Returns 0..0x7fffffff. */
static inline uint32_t ame_rand(uint32_t *state) {
    uint32_t x = *state ? *state : 0x9e3779b9u; /* never seed 0 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x & 0x7fffffffu;
}

/* uniform float [0,1) */
static inline float ame_randf(uint32_t *state) {
    return (float)(ame_rand(state) >> 8) * (1.0f / 8388608.0f); /* 23 bits */
}

/* uniform int [lo, hi] inclusive */
static inline int ame_rand_range(uint32_t *state, int lo, int hi) {
    return lo + (int)(ame_rand(state) % (uint32_t)(hi - lo + 1));
}

/* --- FNV-1a hash: golden tests, content checksums ------------------------- */
static inline uint32_t ame_fnv1a(uint32_t h, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* --- published snapshot (principles THREADING) -----------------------------
 * Seqlock publication: ONE writer copies the snapshot into the gate
 * buffer under an odd version, ANY number of readers copy OUT into
 * private storage and retry if a publish overlapped the read. Readers
 * own their copy for as long as they like - safe at any publish/read
 * rate ratio. (Replaces the double-buffer scheme an external review
 * caught with TSan: its back buffer could be overwritten while a slow
 * reader still held the pointer.)
 *
 * T must be a trivially-copyable POD snapshot struct. */
typedef struct {
    _Atomic uint32_t ver; /* even = stable, odd = publish in progress */
} ame_snap_gate;

/* relaxed atomic chunk copies: EVERY access to the shared snapshot
 * buffer is atomic (atomics never race under C11), while the seqlock
 * version check guarantees CONSISTENCY (a copy never mixes two
 * publishes). Net effect: correct by the memory model, and TSan sees
 * no plain-access race - no suppressions needed, future races in
 * surrounding code stay detectable. x86 codegen: plain movs. */
static inline void ame_snap_store_relaxed(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t w = (((uintptr_t)dst | (uintptr_t)src) & 3u) == 0 ? n / 4 : 0;
    for (size_t i = 0; i < w; i++)
        __atomic_store_n((uint32_t *)d + i,
                         __atomic_load_n((const uint32_t *)s + i,
                                         __ATOMIC_RELAXED), __ATOMIC_RELAXED);
    for (size_t i = w * 4; i < n; i++)
        __atomic_store_n(d + i, __atomic_load_n(s + i, __ATOMIC_RELAXED),
                         __ATOMIC_RELAXED);
}

static inline void ame_snap_load_relaxed(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t w = (((uintptr_t)dst | (uintptr_t)src) & 3u) == 0 ? n / 4 : 0;
    for (size_t i = 0; i < w; i++)
        ((uint32_t *)d)[i] = __atomic_load_n((const uint32_t *)s + i,
                                             __ATOMIC_RELAXED);
    for (size_t i = w * 4; i < n; i++)
        d[i] = __atomic_load_n(s + i, __ATOMIC_RELAXED);
}

/* GCC's TSan rejects C11 thread fences outright. Under GCC+TSan a
 * compiler barrier suffices: TSan derives happens-before from the
 * acquire/release VERSION operations and the data copies are atomic
 * (atomics never race), so fences add nothing to race detection.
 * Real builds - and Clang TSan, which supports fences - use the true
 * fence for hardware ordering. */
#if defined(__SANITIZE_THREAD__) && defined(__GNUC__) && !defined(__clang__)
#define AME_SNAP_FENCE(mo) __asm__ __volatile__("" ::: "memory")
#else
#define AME_SNAP_FENCE(mo) atomic_thread_fence(mo)
#endif

#define AME_SNAP_DEFINE(type)                                                 \
    typedef struct {                                                          \
        type buf;                                                             \
        ame_snap_gate gate;                                                   \
    } type##_snap;                                                            \
    static inline void type##_snap_init(type##_snap *s) {                     \
        atomic_init(&s->gate.ver, 0);                                         \
    }                                                                         \
    /* writer ONLY: publish a fresh snapshot (seqlock write side). */         \
    static inline void type##_publish(type##_snap *s, const type *src) {      \
        uint32_t v = atomic_load_explicit(&s->gate.ver, memory_order_relaxed);\
        atomic_store_explicit(&s->gate.ver, v + 1u, memory_order_relaxed);    \
        AME_SNAP_FENCE(memory_order_release);                                  \
        ame_snap_store_relaxed(&s->buf, src, sizeof(type));                       \
        AME_SNAP_FENCE(memory_order_release);                                  \
        atomic_store_explicit(&s->gate.ver, v + 2u, memory_order_release);    \
    }                                                                         \
    /* any thread: copy out the latest consistent snapshot. false only if    \
     * every attempt overlapped a publish (retry / keep last frame);         \
     * *out is left UNCHANGED on failure. */                                  \
    static inline bool type##_latest_copy(const type##_snap *s, type *out) {  \
        for (int attempt = 0; attempt < 64; attempt++) {                      \
            uint32_t v1 = atomic_load_explicit(&s->gate.ver,                  \
                                               memory_order_acquire);         \
            if ((v1 & 1u) == 0) {                                             \
                type tmp; /* atomic copy, then validate: never tear *out */ \
                ame_snap_load_relaxed(&tmp, &s->buf, sizeof(type));              \
                AME_SNAP_FENCE(memory_order_acquire);                         \
                uint32_t v2 = atomic_load_explicit(&s->gate.ver,              \
                                                   memory_order_acquire);     \
                if (v1 == v2) {                                               \
                    *out = tmp;                                               \
                    return true;                                              \
                }                                                             \
            }                                                                 \
        }                                                                     \
        return false;                                                         \
    }

#ifdef __cplusplus
}
#endif

#endif /* AME_AME_H */
