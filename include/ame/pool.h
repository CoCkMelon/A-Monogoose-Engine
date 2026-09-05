#ifndef AME_POOL_H
#define AME_POOL_H

#include "ame/handle.h"

/*
 * HOT object: generation + alive bitset. Caller owns SoA field arrays
 * keyed by the same index. No malloc in spawn/despawn.
 *
 * Despawn is deferred: ame_pool_despawn queues; ame_pool_apply_despawns
 * runs once at the end of a sim step so a walk is not corrupted.
 */

typedef struct ame_pool {
    uint32_t *generation;
    uint8_t  *alive;
    uint32_t *pending;
    int cap;
    int n_pending;
    int live;
} ame_pool;

ame_pool *ame_pool_bind(ame_pool *p,
                        uint32_t *generation, uint8_t *alive, uint32_t *pending,
                        int cap);
void       ame_pool_reset(ame_pool *p);
ame_handle ame_pool_spawn(ame_pool *p);          /* INVALID if full */
void       ame_pool_despawn(ame_pool *p, ame_handle h);
int        ame_pool_valid(const ame_pool *p, ame_handle h);
void       ame_pool_apply_despawns(ame_pool *p);
int        ame_pool_live(const ame_pool *p);

#endif
