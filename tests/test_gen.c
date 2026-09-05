#include "ame_gen.h"

#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL gen: %s\n", m);
    return 1;
}

int main(void)
{
    if (ame_gen_handle_make(9, 0) != 0)
        return fail("gen 0");
    uint64_t h = ame_gen_handle_make(3, 7);
    if (ame_gen_handle_index(h) != 3)
        return fail("index");
    if (ame_gen_handle_generation(h) != 7)
        return fail("generation");

    if (!ame_gen_point_in_aabb_xy(-1, -1, 1, 1, 0, 0))
        return fail("point in");
    if (ame_gen_point_in_aabb_xy(-1, -1, 1, 1, 5, 0))
        return fail("point out");

    uint32_t gen[4] = {0, 0, 0, 0};
    uint8_t alive[4] = {0, 0, 0, 0};
    uint32_t oi = 99, og = 0;
    if (!ame_gen_pool_spawn(gen, alive, 4, &oi, &og))
        return fail("spawn");
    if (oi != 0 || og != 1 || !alive[0])
        return fail("spawn slot");

    ame_gen_mem s;
    ame_gen_mem_reset(&s);
    ame_gen_mem_click(&s, 0);
    ame_gen_mem_click(&s, 1);
    ame_gen_mem_resolve(&s);
    if (s.score0 != 1 || s.turn != 1 || s.n_matched != 1)
        return fail("match score/turn");
    if (s.face[0] != 2 || s.face[1] != 2)
        return fail("matched faces");

    ame_gen_mem_reset(&s);
    ame_gen_mem_click(&s, 0);
    ame_gen_mem_click(&s, 2);
    ame_gen_mem_resolve(&s);
    if (s.score0 != 0 || s.face[0] != 0 || s.face[2] != 0 || s.turn != 1)
        return fail("mismatch");

    /* click during resolve ignored */
    ame_gen_mem_reset(&s);
    ame_gen_mem_click(&s, 0);
    ame_gen_mem_click(&s, 1);
    ame_gen_mem_click(&s, 3);
    if (s.phase != 2 || s.face[3] != 0)
        return fail("click while resolving");

    printf("test_gen ok\n");
    return 0;
}
