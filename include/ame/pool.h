/* ame-next — static pool slot template (data.txt).
 *
 * A pool module OWNS its arrays in ONE .c file (never state in a header).
 * To get the spawn/despawn/valid bookkeeping without hand-copying bugs,
 * a module .c does:
 *
 *   #define AME_POOL_PREFIX  mylib_things      // mylib_ prefix, file-local
 *   #define AME_POOL_CAP     256
 *   #define AME_POOL_MAX_FREE 256
 *   #include <ame/pool.h>
 *
 * which expands to (in that .c only) a struct + functions:
 *   mylib_things_slots   — generation + alive arrays + free list (the ONLY
 *                          bookkeeping state; SoA field arrays stay separate
 *                          in the module's own struct)
 *   mylib_things_slots_reset(s)
 *   mylib_things_slots_alloc(s) -> ame_handle (INVALID when full; gen starts 1)
 *   mylib_things_slots_free(s, h)        deferred request (queues)
 *   mylib_things_slots_apply_frees(s)    call once at end of fixed step
 *   mylib_things_slots_valid(s, h)
 *
 * Rules baked in (data.txt): despawn is DEFERRED so a drained event handler
 * may despawn mid-walk; INVALID_HANDLE on full, never -1; generation 0 is
 * invalid so stale handles fail loudly.
 *
 * The template is only ever expanded inside a .c; no state lands in headers.
 */
/* This header is a TEMPLATE: it is included once per pool, with
 * AME_POOL_PREFIX / AME_POOL_CAP defined first, and re-expands on every
 * include (deliberately NO include guard). */
#include <ame/ame.h>

#ifndef AME_POOL_PREFIX
#error "define AME_POOL_PREFIX before including ame/pool.h"
#endif
#ifndef AME_POOL_CAP
#error "define AME_POOL_CAP before including ame/pool.h"
#endif
/* worst-case simultaneous deferred frees == cap; a smaller queue only
 * delays frees to the next step, it never drops them (requests coalesce). */
#ifndef AME_POOL_MAX_FREE
#define AME_POOL_MAX_FREE AME_POOL_CAP
#endif

#define AME_P_CAT_(a, b) a##b
#define AME_P_CAT(a, b)  AME_P_CAT_(a, b)
#define AME_P(name)      AME_P_CAT(AME_POOL_PREFIX, name)

typedef struct {
    uint16_t gen[AME_POOL_CAP];     /* generation per slot; 0 = never used */
    uint8_t  alive[AME_POOL_CAP];
    uint16_t free_list[AME_POOL_CAP];
    uint16_t free_head;             /* number of entries on free_list */
    ame_handle pend_free[AME_POOL_MAX_FREE];
    uint16_t pend_head;
    uint16_t overflow_drops;        /* deferred-queue overflow (debug view) */
} AME_P(slots);

static inline void AME_P(slots_reset)(AME_P(slots) *s) {
    for (uint16_t i = 0; i < (uint16_t)AME_POOL_CAP; i++) {
        s->gen[i] = 0;
        s->alive[i] = 0;
        s->free_list[i] = (uint16_t)(AME_POOL_CAP - 1 - i); /* pop ascending */
    }
    s->free_head = (uint16_t)AME_POOL_CAP;
    s->pend_head = 0;
    s->overflow_drops = 0;
}

/* allocate a slot; returns a valid handle or AME_HANDLE_INVALID when full */
static inline ame_handle AME_P(slots_alloc)(AME_P(slots) *s) {
    if (s->free_head == 0)
        return AME_HANDLE_INVALID;
    uint16_t idx = s->free_list[--s->free_head];
    if (s->gen[idx] == 0)
        s->gen[idx] = 1; /* first use: start at generation 1 (0 = invalid) */
    s->alive[idx] = 1;
    return (ame_handle){ .idx = idx, .gen = s->gen[idx] };
}

static inline bool AME_P(slots_valid)(const AME_P(slots) *s, ame_handle h) {
    return h.gen != 0 && h.idx < (uint16_t)AME_POOL_CAP
        && s->alive[h.idx] && s->gen[h.idx] == h.gen;
}

/* request a despawn (deferred; safe during iteration/drain) */
static inline void AME_P(slots_free)(AME_P(slots) *s, ame_handle h) {
    if (!AME_P(slots_valid)(s, h))
        return;
    if (s->pend_head >= (uint16_t)AME_POOL_MAX_FREE) {
        s->overflow_drops++; /* coalesce: a later apply will free it anyway */
        return;
    }
    s->pend_free[s->pend_head++] = h;
}

/* apply deferred frees — call ONCE at the end of the fixed step */
static inline void AME_P(slots_apply_frees)(AME_P(slots) *s) {
    while (s->pend_head > 0) {
        ame_handle h = s->pend_free[--s->pend_head];
        if (!AME_P(slots_valid)(s, h))
            continue;
        s->alive[h.idx] = 0;
        s->gen[h.idx] = (uint16_t)(s->gen[h.idx] + 1); /* bump: stale handles die */
        if (s->gen[h.idx] == 0)
            s->gen[h.idx] = 1;
        s->free_list[s->free_head++] = h.idx;
    }
}

#undef AME_POOL_PREFIX
#undef AME_POOL_CAP
#undef AME_POOL_MAX_FREE
#undef AME_P_CAT_
#undef AME_P_CAT
#undef AME_P

