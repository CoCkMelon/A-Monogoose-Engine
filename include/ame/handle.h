#ifndef AME_HANDLE_H
#define AME_HANDLE_H

#include <stdint.h>

/*
 * Packed handle: 64 bits
 *   bits  0..31  index
 *   bits 32..63  generation
 * Generation 0 is INVALID (never allocated; slots start at generation 1).
 * A stale handle fails ame_pool_valid after the slot is reused.
 */

typedef uint64_t ame_handle;

#define AME_HANDLE_INVALID ((ame_handle)0)

static inline uint32_t ame_handle_index(ame_handle h)
{
    return (uint32_t)(h & 0xffffffffu);
}

static inline uint32_t ame_handle_generation(ame_handle h)
{
    return (uint32_t)(h >> 32);
}

static inline ame_handle ame_handle_make(uint32_t index, uint32_t generation)
{
    if (generation == 0) return AME_HANDLE_INVALID;
    return ((uint64_t)generation << 32) | (uint64_t)index;
}

/* Cross-pool reference stored in events. pool_id 0 = none/world. */
typedef struct ame_ref {
    uint16_t pool_id;
    uint16_t index;
    uint32_t generation;
} ame_ref;

static inline ame_ref ame_ref_none(void)
{
    ame_ref r = {0, 0, 0};
    return r;
}

static inline ame_ref ame_ref_make(uint16_t pool_id, ame_handle h)
{
    ame_ref r;
    r.pool_id = pool_id;
    r.index = (uint16_t)ame_handle_index(h);
    r.generation = ame_handle_generation(h);
    return r;
}

static inline int ame_ref_ok(ame_ref r)
{
    return r.generation != 0;
}

#endif
