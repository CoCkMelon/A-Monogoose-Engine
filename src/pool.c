#include "ame/pool.h"

#include <string.h>

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
    if ((int)i >= p->cap) return 0;
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
    for (int k = 0; k < p->n_pending; k++) {
        uint32_t i = p->pending[k];
        if ((int)i >= p->cap) continue;
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
