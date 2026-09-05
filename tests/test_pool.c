/* tests — static pool template (data.txt): generation handles, deferred
 * despawn, INVALID_HANDLE on full. The template is expanded in THIS .c,
 * exactly like a game module would. */
#include "utest.h"
#include <ame/ame.h>

/* a pool module: slot bookkeeping from the template + SoA field arrays */
#define AME_POOL_PREFIX bullets_
#define AME_POOL_CAP    8
#include <ame/pool.h>

/* second instantiation: tiny deferred-free queue (2) to prove duplicate
 * coalescing - the exact repro from the external review (free(a), free(a),
 * free(b) used to fill the queue with a duplicate and DROP b). */
#define AME_POOL_PREFIX tiny_
#define AME_POOL_CAP    4
#define AME_POOL_MAX_FREE 2
#include <ame/pool.h>

typedef struct {
    float x[8], y[8];
    float vx[8], vy[8];
} bullets_data;

static bullets_slots  bullet_slots;
static bullets_data   bullet_fields;

int main(void) {
    printf("=== test_pool ===\n");
    bullets_slots *P = &bullet_slots;

    UT_CASE("reset + alloc all + full -> INVALID");
    bullets_slots_reset(P);
    ame_handle hs[8];
    for (int i = 0; i < 8; i++) {
        hs[i] = bullets_slots_alloc(P);
        UT_ASSERT(ame_handle_valid(hs[i]));
        UT_ASSERT(hs[i].gen == 1); /* first generation is 1 (0 = invalid) */
        bullet_fields.x[hs[i].idx] = (float)i;
    }
    UT_ASSERT(!ame_handle_valid(bullets_slots_alloc(P))); /* full: not -1 */

    UT_CASE("deferred free keeps slot alive until apply");
    bullets_slots_free(P, hs[3]);
    UT_ASSERT(bullets_slots_valid(P, hs[3])); /* still alive mid-walk */
    bullets_slots_apply_frees(P);
    UT_ASSERT(!bullets_slots_valid(P, hs[3])); /* stale now */

    UT_CASE("reuse bumps generation; stale handle fails");
    ame_handle h2 = bullets_slots_alloc(P);
    UT_ASSERT(h2.idx == hs[3].idx);
    UT_ASSERT(h2.gen == hs[3].gen + 1);
    UT_ASSERT(bullet_fields.x[h2.idx] == 3.0f); /* slot memory reused */

    UT_CASE("double free request is safe");
    bullets_slots_free(P, hs[3]); /* stale: ignored */
    bullets_slots_free(P, h2);
    bullets_slots_free(P, h2);    /* second request: harmless */
    bullets_slots_apply_frees(P);
    UT_ASSERT(!bullets_slots_valid(P, h2));
    UT_ASSERT(ame_handle_valid(bullets_slots_alloc(P)));

    UT_CASE("duplicate frees coalesce; distinct frees never lost");
    static tiny_slots T;
    tiny_slots_reset(&T);
    ame_handle a = tiny_slots_alloc(&T);
    ame_handle b = tiny_slots_alloc(&T);
    tiny_slots_free(&T, a);
    tiny_slots_free(&T, a); /* duplicate: coalesces, not queued twice */
    tiny_slots_free(&T, b); /* must NOT be dropped (queue cap is 2) */
    UT_ASSERT(T.coalesced == 1);
    UT_ASSERT(T.overflow_drops == 0);
    tiny_slots_apply_frees(&T);
    UT_ASSERT(!tiny_slots_valid(&T, a));
    UT_ASSERT(!tiny_slots_valid(&T, b)); /* the review's lost-despawn case */

    UT_CASE("overflow only beyond MAX_FREE DISTINCT frees (honest)");
    tiny_slots_reset(&T);
    ame_handle th[3];
    for (int i = 0; i < 3; i++)
        th[i] = tiny_slots_alloc(&T);
    for (int i = 0; i < 3; i++)
        tiny_slots_free(&T, th[i]);
    UT_ASSERT(T.overflow_drops == 1); /* 3rd DISTINCT free, queue cap 2 */
    UT_ASSERT(T.coalesced == 0);
    tiny_slots_apply_frees(&T);
    UT_ASSERT(!tiny_slots_valid(&T, th[0]) && !tiny_slots_valid(&T, th[1]));
    UT_ASSERT(tiny_slots_valid(&T, th[2])); /* documented drop, still alive */

    UT_OK();
    return ut_done("test_pool");
}
