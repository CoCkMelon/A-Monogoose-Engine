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
    p->cap = cap;
    p->n_pending = 0;
    p->live = 0;
    return p;
}

void ame_pool_reset(ame_pool *p)
{
    if (!p || p->cap <= 0) return;
    memset(p->generation, 0, (size_t)p->cap * sizeof(uint32_t));
    memset(p->alive, 0, (size_t)p->cap);
    p->n_pending = 0;
    p->live = 0;
}

ame_handle ame_pool_spawn(ame_pool *p)
{
    if (!p) return AME_HANDLE_INVALID;
    for (int i = 0; i < p->cap; i++) {
        if (p->alive[i]) continue;
        uint32_t g = p->generation[i] + 1u;
        if (g == 0) g = 1u;
        p->generation[i] = g;
        p->alive[i] = 1;
        p->live++;
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
    for (int k = 0; k < p->n_pending; k++)
        if (p->pending[k] == i) return;
    if (p->n_pending >= p->cap) return;
    p->pending[p->n_pending++] = i;
}

void ame_pool_apply_despawns(ame_pool *p)
{
    if (!p) return;
    int n = p->n_pending;
    if (n < 0 || n > p->cap) return;      /* corrupt pending count: do nothing */
    for (int k = 0; k < n; k++) {
        uint32_t i = p->pending[k];
        if (!pool_index_ok(p, i)) continue;
        if (p->alive[i]) {
            p->alive[i] = 0;
            p->live--;
            if (p->live < 0) p->live = 0;
        }
    }
    p->n_pending = 0;
}

int ame_pool_live(const ame_pool *p)
{
    return p ? p->live : 0;
}
