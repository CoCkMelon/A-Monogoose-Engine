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
 * Double-buffer publish/subscribe for "writer publishes, readers read a
 * const snapshot, readers never write back". The writer writes the back
 * buffer, then flips the index with release ordering; readers acquire the
 * index and get a stable const pointer until the NEXT flip. With exactly one
 * writer and flip-separated reads this is race-free without locks.
 *
 * T must be a trivially-copyable POD snapshot struct. The writer owns the
 * struct; readers must finish reading before the writer's next publish
 * (a 1000 Hz publisher vs 60 Hz readers gives ~16 frames of slack; copy out
 * if a reader must hold it longer). */
typedef struct {
    _Atomic uint32_t idx; /* 0 or 1 */
} ame_snap_gate;

#define AME_SNAP_DEFINE(type)                                                 \
    typedef struct {                                                          \
        type buf[2];                                                          \
        ame_snap_gate gate;                                                   \
    } type##_snap;                                                            \
    static inline void type##_snap_init(type##_snap *s) {                     \
        atomic_init(&s->gate.idx, 0);                                         \
    }                                                                         \
    /* writer ONLY: publish a fresh snapshot (back buffer), then flip. */     \
    static inline const type *type##_publish(type##_snap *s, const type *src) {\
        uint32_t cur = atomic_load_explicit(&s->gate.idx, memory_order_relaxed);\
        uint32_t nxt = cur ^ 1u;                                              \
        s->buf[nxt] = *src;                                                   \
        atomic_store_explicit(&s->gate.idx, nxt, memory_order_release);       \
        return &s->buf[nxt];                                                  \
    }                                                                         \
    /* any thread: latest published const snapshot. */                        \
    static inline const type *type##_latest(const type##_snap *s) {           \
        uint32_t i = atomic_load_explicit(&s->gate.idx, memory_order_acquire);\
        return &s->buf[i];                                                    \
    }

#ifdef __cplusplus
}
#endif

#endif /* AME_AME_H */
