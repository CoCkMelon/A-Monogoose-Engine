#ifndef AME_POOL_H
#define AME_POOL_H

#include "ame/handle.h"

/*
 * HOT object: generation + alive bitset. Caller owns SoA field arrays
 * keyed by the same index. No malloc in spawn/despawn.
 *
 * Despawn is deferred: ame_pool_despawn queues; ame_pool_apply_despawns
 * runs once at the end of a sim step so a walk is not corrupted.
 *
 * Free-list: free slots are pushed on apply and popped on spawn (O(1)
 * after the first fill). pending[] doubles as the free stack storage
 * between apply and next despawn batch (n_pending is zero after apply;
 * free_top tracks the free stack independently). Duplicate despawn tags
 * are rejected in O(1) via pend_gen[] matching the live generation.
 */

typedef struct ame_pool {
    uint32_t *generation;
    uint8_t  *alive;
    uint32_t *pending;   /* deferred despawn queue + free-list stack storage */
    uint32_t *pend_gen;  /* optional: gen at despawn-time for O(1) dedupe; may be NULL */
    uint32_t *free_list; /* optional dedicated free stack; falls back to pending[] */
    int cap;
    int n_pending;
    int free_top;        /* free_list stack size (slots available for spawn) */
    int live;
    int free_built;      /* 1 after first reset builds the free stack */
} ame_pool;

/* Bind SoA storage. pend_gen and free_list may be NULL:
 *   - pend_gen NULL → despawn dedupe is an O(n_pending) scan (tiny caps OK)
 *   - free_list NULL → free stack reuses pending[] after apply (same buffer) */
ame_pool *ame_pool_bind(ame_pool *p,
                        uint32_t *generation, uint8_t *alive, uint32_t *pending,
                        int cap);
/* Optional second bind for the O(1) helpers. Safe to skip for small pools. */
ame_pool *ame_pool_bind_fast(ame_pool *p, uint32_t *pend_gen, uint32_t *free_list);

void       ame_pool_reset(ame_pool *p);
ame_handle ame_pool_spawn(ame_pool *p);          /* INVALID if full */
void       ame_pool_despawn(ame_pool *p, ame_handle h);
int        ame_pool_valid(const ame_pool *p, ame_handle h);
void       ame_pool_apply_despawns(ame_pool *p);
int        ame_pool_live(const ame_pool *p);

#endif
