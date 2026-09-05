#ifndef AME_SNAP_H
#define AME_SNAP_H

/*
 * Published snapshot: seqlock publication with copy-out readers.
 *
 * Ported from the sibling engine branch (agent/ame-next-...), where it
 * replaced a two-buffer scheme that an external review + TSan caught:
 * the writer could overwrite the very buffer a slow reader still held a
 * pointer into. The rules this enforces:
 *
 *   - ONE writer publishes (the logic/sim thread), ANY number of readers
 *     (render thread, tests, net dump) copy out into PRIVATE storage.
 *   - A reader never receives a pointer into shared memory, so no read can
 *     be invalidated after it returns. On failure *out is left unchanged,
 *     so callers keep their previous frame instead of tearing.
 *   - Every access to the shared buffer is a relaxed ATOMIC copy (atomics
 *     never race under C11), while the seqlock version counter guarantees
 *     CONSISTENCY: a copy never mixes two publishes. Net effect: correct by
 *     the memory model and TSan-clean without suppressions, so future races
 *     in surrounding code stay detectable. On x86 the copies are plain movs.
 *
 * T must be a trivially copyable POD snapshot struct (no pointers to
 * mutable shared state — copy-out means the reader owns the bytes).
 *
 *   typedef struct { ... } GameSnap;
 *   AME_SNAP_DEFINE(GameSnap)          -> GameSnap_snap, GameSnap_publish,
 *                                         GameSnap_latest_copy, _snap_init
 *
 *   GameSnap_snap s; GameSnap_snap_init(&s);
 *
 *   GameSnap_publish(&s, &state);                        -- logic thread
 *   if (GameSnap_latest_copy(&s, &mine)) draw(&mine);    -- render thread
 */

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    _Atomic uint32_t ver; /* even = stable, odd = publish in progress */
} ame_snap_gate;

/* relaxed atomic chunk copies: word-atomic when both sides are aligned. */
static inline void ame_snap_store_relaxed(void *dst, const void *src, size_t n)
{
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

static inline void ame_snap_load_relaxed(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t w = (((uintptr_t)dst | (uintptr_t)src) & 3u) == 0 ? n / 4 : 0;
    for (size_t i = 0; i < w; i++)
        ((uint32_t *)d)[i] = __atomic_load_n((const uint32_t *)s + i,
                                             __ATOMIC_RELAXED);
    for (size_t i = w * 4; i < n; i++)
        d[i] = __atomic_load_n(s + i, __ATOMIC_RELAXED);
}

/*
 * GCC's TSan rejects C11 thread fences outright. Under GCC+TSan a compiler
 * barrier is enough: TSan derives happens-before from the acquire/release
 * VERSION operations and the data copies are atomic, so the fence adds
 * nothing to race detection. Real builds — and Clang TSan, which supports
 * fences — keep the true fence for hardware ordering.
 */
#if defined(__SANITIZE_THREAD__) && defined(__GNUC__) && !defined(__clang__)
#define AME_SNAP_FENCE(mo) __asm__ __volatile__("" ::: "memory")
#else
#define AME_SNAP_FENCE(mo) atomic_thread_fence(mo)
#endif

#define AME_SNAP_DEFINE(type)                                                  \
    typedef struct {                                                           \
        type buf;                                                              \
        ame_snap_gate gate;                                                    \
    } type##_snap;                                                             \
    static inline void type##_snap_init(type##_snap *s)                        \
    {                                                                          \
        atomic_init(&s->gate.ver, 0);                                          \
    }                                                                          \
    /* writer ONLY: publish a fresh snapshot (seqlock write side). */          \
    static inline void type##_publish(type##_snap *s, const type *src)         \
    {                                                                          \
        uint32_t v = atomic_load_explicit(&s->gate.ver, memory_order_relaxed); \
        atomic_store_explicit(&s->gate.ver, v + 1u, memory_order_relaxed);     \
        AME_SNAP_FENCE(memory_order_release);                                  \
        ame_snap_store_relaxed(&s->buf, src, sizeof(type));                    \
        AME_SNAP_FENCE(memory_order_release);                                  \
        atomic_store_explicit(&s->gate.ver, v + 2u, memory_order_release);     \
    }                                                                          \
    /* any thread: copy out the latest consistent snapshot. false only if      \
     * every attempt overlapped a publish (retry / keep last frame);           \
     * *out is left UNCHANGED on failure. */                                   \
    static inline bool type##_latest_copy(const type##_snap *s, type *out)     \
    {                                                                          \
        for (int attempt = 0; attempt < 64; attempt++) {                       \
            uint32_t v1 = atomic_load_explicit(&s->gate.ver,                   \
                                               memory_order_acquire);          \
            if ((v1 & 1u) == 0) {                                              \
                type tmp; /* copy first, validate after: *out never tears */   \
                ame_snap_load_relaxed(&tmp, &s->buf, sizeof(type));            \
                AME_SNAP_FENCE(memory_order_acquire);                          \
                uint32_t v2 = atomic_load_explicit(&s->gate.ver,               \
                                                   memory_order_acquire);      \
                if (v1 == v2) {                                                \
                    *out = tmp;                                                \
                    return true;                                               \
                }                                                              \
            }                                                                  \
        }                                                                      \
        return false;                                                          \
    }

#endif /* AME_SNAP_H */
