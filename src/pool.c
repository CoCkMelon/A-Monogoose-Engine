#include "ame/pool.h"

#include <string.h>

/*
 * Bounds check on the *unsigned* slot index.
 *
 * The previous test was `if ((int)i >= p->cap) return 0;`. An index with the
 * high bit set (e.g. 0x80000000) converts to a negative int, so it passes the
 * test and the next line reads p->alive[i] far out of bounds. Handles come
 * from serialised/network input (see memnet payloads) and from saved state, so
 * they are not trusted values: the check has to be exact, not sign-flipped.
 * cap <= 0 covers an unbound/reset pool (bind with cap 0 is allowed).
 */
static int pool_index_ok(const ame_pool *p, uint32_t i)
{
    return p != NULL && p->cap > 0 && i < (uint32_t)p->cap;
}

ame_pool *ame_pool_bind(ame_pool *p,
                        uint32_t *generation, uint8_t *alive, uint32_t *pending,
                        int cap)
{
    if (!p) return p;
    p->generation = generation;
    p->alive = alive;
    p->pending = pending;
    p->pend_gen = NULL;
    p->free_list = NULL;
    p->cap = cap;
    p->n_pending = 0;
    p->free_top = 0;
    p->live = 0;
    p->free_built = 0;
    return p;
}

ame_pool *ame_pool_bind_fast(ame_pool *p, uint32_t *pend_gen, uint32_t *free_list)
{
    if (!p) return p;
    p->pend_gen = pend_gen;
    p->free_list = free_list;
    return p;
}

void ame_pool_reset(ame_pool *p)
{
    if (!p || p->cap <= 0) return;
    memset(p->generation, 0, (size_t)p->cap * sizeof(uint32_t));
    memset(p->alive, 0, (size_t)p->cap);
    if (p->pend_gen)
        memset(p->pend_gen, 0, (size_t)p->cap * sizeof(uint32_t));
    p->n_pending = 0;
    p->live = 0;
    /* Free stack high→low so spawn returns lowest index first
     * (matches the original linear-scan order; tests rely on it). */
    if (p->free_list) {
        for (int i = 0; i < p->cap; i++)
            p->free_list[i] = (uint32_t)(p->cap - 1 - i);
        p->free_top = p->cap;
        p->free_built = 1;
    } else {
        p->free_top = 0;
        p->free_built = 0;
    }
}

ame_handle ame_pool_spawn(ame_pool *p)
{
    if (!p) return AME_HANDLE_INVALID;

    if (p->free_built && p->free_list && p->free_top > 0) {
        uint32_t i = p->free_list[--p->free_top];
        if (pool_index_ok(p, i) && !p->alive[i]) {
            uint32_t g = p->generation[i] + 1u;
            if (g == 0) g = 1u;
            p->generation[i] = g;
            p->alive[i] = 1;
            p->live++;
            if (p->pend_gen) p->pend_gen[i] = 0;
            return ame_handle_make(i, g);
        }
        /* Corrupt free entry — fall through. */
    }

    for (int i = 0; i < p->cap; i++) {
        if (p->alive[i]) continue;
        uint32_t g = p->generation[i] + 1u;
        if (g == 0) g = 1u;
        p->generation[i] = g;
        p->alive[i] = 1;
        p->live++;
        if (p->pend_gen) p->pend_gen[i] = 0;
        return ame_handle_make((uint32_t)i, g);
    }
    return AME_HANDLE_INVALID;
}

int ame_pool_valid(const ame_pool *p, ame_handle h)
{
    if (!p || h == AME_HANDLE_INVALID) return 0;
    uint32_t i = ame_handle_index(h);
    uint32_t g = ame_handle_generation(h);
    if (!pool_index_ok(p, i)) return 0;
    if (g == 0) return 0;
    return p->alive[i] && p->generation[i] == g;
}

void ame_pool_despawn(ame_pool *p, ame_handle h)
{
    if (!ame_pool_valid(p, h)) return;
    uint32_t i = ame_handle_index(h);
    uint32_t g = ame_handle_generation(h);

    if (p->pend_gen) {
        if (p->pend_gen[i] == g) return; /* already queued this gen */
        if (p->n_pending >= p->cap) return;
        p->pend_gen[i] = g;
        p->pending[p->n_pending++] = i;
        return;
    }

    for (int k = 0; k < p->n_pending; k++)
        if (p->pending[k] == i) return;
    if (p->n_pending >= p->cap) return;
    p->pending[p->n_pending++] = i;
}

void ame_pool_apply_despawns(ame_pool *p)
{
    if (!p) return;
    int n = p->n_pending;
    if (n < 0 || n > p->cap) return;
    for (int k = 0; k < n; k++) {
        uint32_t i = p->pending[k];
        if (!pool_index_ok(p, i)) continue;
        if (p->alive[i]) {
            p->alive[i] = 0;
            p->live--;
            if (p->live < 0) p->live = 0;
            if (p->free_list && p->free_top < p->cap)
                p->free_list[p->free_top++] = i;
        }
        if (p->pend_gen) p->pend_gen[i] = 0;
    }
    p->n_pending = 0;
}

int ame_pool_live(const ame_pool *p)
{
    return p ? p->live : 0;
}
