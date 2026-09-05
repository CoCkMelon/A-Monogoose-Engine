#include "ame/pool.h"

#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL pool: %s\n", m);
    return 1;
}

int main(void)
{
    enum { N = 4 };
    uint32_t gen[N], pend[N];
    uint8_t alive[N];
    ame_pool pool;
    ame_pool_bind(&pool, gen, alive, pend, N);
    ame_pool_reset(&pool);

    ame_handle h0 = ame_pool_spawn(&pool);
    ame_handle h1 = ame_pool_spawn(&pool);
    if (h0 == AME_HANDLE_INVALID || h1 == AME_HANDLE_INVALID)
        return fail("spawn");
    if (ame_handle_index(h0) != 0 || ame_handle_index(h1) != 1)
        return fail("index order");
    if (ame_handle_generation(h0) == 0) return fail("gen0");
    if (!ame_pool_valid(&pool, h0) || !ame_pool_valid(&pool, h1))
        return fail("valid");
    if (ame_pool_live(&pool) != 2) return fail("live 2");

    ame_pool_despawn(&pool, h0);
    if (!ame_pool_valid(&pool, h0)) return fail("still valid until apply");
    ame_pool_apply_despawns(&pool);
    if (ame_pool_valid(&pool, h0)) return fail("stale after apply");
    if (ame_pool_live(&pool) != 1) return fail("live 1");

    ame_handle h0b = ame_pool_spawn(&pool);
    if (ame_handle_index(h0b) != 0) return fail("reuse slot 0");
    if (ame_handle_generation(h0b) == ame_handle_generation(h0))
        return fail("generation must bump");
    if (ame_pool_valid(&pool, h0)) return fail("old handle must fail");
    if (!ame_pool_valid(&pool, h0b)) return fail("new handle");

    ame_handle extra[4];
    extra[0] = ame_pool_spawn(&pool);
    extra[1] = ame_pool_spawn(&pool);
    extra[2] = ame_pool_spawn(&pool);
    if (extra[2] != AME_HANDLE_INVALID) return fail("full should be INVALID");

    /*
     * Regression: handle validation used to bounds-check with
     * `if ((int)i >= p->cap)`, so an index with the high bit set became a
     * negative int, passed the check, and read p->alive[i] out of bounds
     * (ASan: SEGV in ame_pool_valid). Every handle below must be rejected
     * without touching memory — run this test under ASan/UBSan.
     */
    const ame_handle bad[] = {
        ame_handle_make(0x80000000u, 1u),      /* sign-flip: (int)i < 0        */
        ame_handle_make(0xFFFFFFFFu, 1u),      /* index -1                     */
        ame_handle_make((uint32_t)N, 1u),      /* one past the end             */
        ame_handle_make((uint32_t)N + 64u, 1u),/* far past the end             */
        ame_handle_make(1u, 0u),               /* generation 0 is never live   */
        ame_handle_make(0u, 0u),               /* AME_HANDLE_INVALID           */
        ((ame_handle)0x12345678u << 32) | 0x9abcdef0u,
    };
    int live_before = ame_pool_live(&pool);
    for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        if (ame_pool_valid(&pool, bad[k])) return fail("bad handle accepted");
        ame_pool_despawn(&pool, bad[k]);       /* must not queue anything */
    }
    ame_pool_apply_despawns(&pool);
    if (ame_pool_live(&pool) != live_before)
        return fail("bad handle changed live count");

    if (ame_pool_valid(NULL, h1)) return fail("NULL pool accepted");

    /* A bound-but-empty pool must reject everything, including index 0. */
    ame_pool empty;
    ame_pool_bind(&empty, gen, alive, pend, 0);
    if (ame_pool_valid(&empty, ame_handle_make(0u, 1u)))
        return fail("cap 0 pool accepted a handle");
    ame_pool_reset(&empty);

    /* Sweep: any accepted handle must name a slot that is really in range. */
    uint64_t x = 0x9e3779b97f4a7c15ull;
    for (int k = 0; k < 200000; k++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;   /* xorshift64 */
        if (ame_handle_make((uint32_t)x, 1u) == AME_HANDLE_INVALID) continue;
        if (ame_pool_valid(&pool, (ame_handle)x) &&
            ame_handle_index((ame_handle)x) >= (uint32_t)N)
            return fail("out-of-range handle accepted during sweep");
    }

    /*
     * Deferred-despawn invariants under a random op mix:
     *   - ame_pool_live() always equals the number of set alive flags;
     *   - a handle stays valid until the queued free is applied;
     *   - a duplicate despawn request must not consume a second queue slot;
     *   - after apply, every previously despawned handle stays invalid.
     */
    ame_pool_reset(&pool);
    ame_handle live_h[N];
    ame_handle dead_h[64];
    int n_live = 0, n_dead = 0;
    uint64_t r = 0x243f6a8885a308d3ull;

    for (int step = 0; step < 20000; step++) {
        r ^= r << 13; r ^= r >> 7; r ^= r << 17;          /* xorshift64 */
        switch ((int)((r >> 4) & 3u)) {
        case 0: {
            ame_handle h = ame_pool_spawn(&pool);
            if (h != AME_HANDLE_INVALID) {
                if (n_live >= N) return fail("spawned past capacity");
                live_h[n_live++] = h;
            } else if (n_live + pool.n_pending != N) {
                /* a queued-but-unapplied free still holds its slot */
                return fail("spawn failed while the pool had room");
            }
            break;
        }
        case 1:
        case 2:
            if (n_live == 0) break;
            {
                int k = (int)((r >> 8) % (uint64_t)n_live);
                ame_pool_despawn(&pool, live_h[k]);
                if (((r >> 4) & 3u) == 2)
                    ame_pool_despawn(&pool, live_h[k]);   /* duplicate */
                if (n_dead < (int)(sizeof dead_h / sizeof dead_h[0]))
                    dead_h[n_dead++] = live_h[k];
                live_h[k] = live_h[--n_live];
            }
            break;
        default:
            ame_pool_apply_despawns(&pool);
            for (int k = 0; k < n_dead; k++)
                if (ame_pool_valid(&pool, dead_h[k]))
                    return fail("despawned handle became valid again");
            break;
        }

        int counted = 0;
        for (int i = 0; i < N; i++) counted += alive[i] ? 1 : 0;
        if (counted != ame_pool_live(&pool))
            return fail("live count drifted from the alive flags");
        if (pool.n_pending < 0 || pool.n_pending > N)
            return fail("pending count out of range");
        if (counted != n_live + pool.n_pending)
            return fail("slots freed without a request, or frees dropped");
        for (int k = 0; k < n_live; k++)
            if (!ame_pool_valid(&pool, live_h[k]))
                return fail("live handle rejected before apply");
    }

    /* Drain: everything queued plus everything still live must end up dead. */
    for (int k = 0; k < n_live; k++) ame_pool_despawn(&pool, live_h[k]);
    ame_pool_apply_despawns(&pool);
    if (ame_pool_live(&pool) != 0) return fail("live should drain to 0");
    for (int i = 0; i < N; i++)
        if (alive[i]) return fail("slot stuck alive after drain");

    printf("test_pool ok\n");
    return 0;
}
