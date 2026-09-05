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

    printf("test_pool ok\n");
    return 0;
}
