/* tests — event queue (src/events.c, events.txt). HEADLESS, logic-thread
 * semantics only: bounded ring, drop-oldest overflow, fixed per-kind
 * handler table, drain-once-per-step dispatch order, re-entrancy guard,
 * diagnostic counters.
 *
 * Cases mined from B-rewrite tests/test_events.c (da69fb2), rewritten
 * against trunk's API: B called ame_events_reset/subscribe/push/drain with
 * ame_ref_none() and AME_EV_* kind names; trunk is events_init/clear,
 * EV_* kinds, AME_EV_QUEUE_CAP, plus three counters B did not have.
 */
#include <stdio.h>
#include <string.h>

#include <ame/events.h>

#include "utest.h"

/* --- recorders ------------------------------------------------------------ */
#define MAX_SEEN 512
static int   seen_kinds[MAX_SEEN];
static float seen_x[MAX_SEEN];
static float seen_rel[MAX_SEEN];
static uint32_t seen_flags[MAX_SEEN];
static ame_ref seen_a[MAX_SEEN];
static int   seen_n;

static void rec_reset(void) {
    seen_n = 0;
}

static void record(const ame_event *e, void *user) {
    int *hits = (int *)user;
    if (hits)
        (*hits)++;
    if (seen_n < MAX_SEEN) {
        seen_kinds[seen_n] = e->kind;
        seen_x[seen_n] = e->p[0];
        seen_rel[seen_n] = e->rel_speed;
        seen_flags[seen_n] = e->flags;
        seen_a[seen_n] = e->a;
        seen_n++;
    }
}

/* handler that pushes one follow-up event, once only */
static int pusher_fired;
static void pusher(const ame_event *e, void *user) {
    (void)e;
    (void)user;
    if (pusher_fired)
        return;
    pusher_fired = 1;
    float p[AME_DIM] = { 99.0f };
    events_push(EV_OVERLAP_EXIT, AME_REF_INVALID, AME_REF_INVALID, p, NULL,
                0.0f, 0);
}

/* handler that tries to drain from inside a drain (must be a no-op) */
static int nested_drain_returned;
static void reentrant(const ame_event *e, void *user) {
    (void)e;
    (void)user;
    uint32_t before = events_dispatched_count();
    events_drain();
    if (events_dispatched_count() == before)
        nested_drain_returned = 1;
}

/* increments the caller's counter; records nothing */
static void count_only(const ame_event *e, void *user) {
    (void)e;
    *(int *)user += 1;
}

int main(void) {
    const float p0[AME_DIM] = { 1.5f };
    const float n0[AME_DIM] = { 0.0f };

    /* --- 1. counters start clean; a full push/drain round trip ---------- */
    UT_CASE("init -> subscribe -> push -> drain dispatches once");
    events_init();
    UT_ASSERT(events_pushed_count() == 0);
    UT_ASSERT(events_dispatched_count() == 0);
    UT_ASSERT(events_overflow_count() == 0);
    rec_reset();
    int hits = 0;
    UT_ASSERT(events_subscribe(EV_OVERLAP_ENTER, record, &hits));
    ame_ref a = { .pool = 1, .idx = 7, .gen = 3 };
    ame_ref b = { .pool = 2, .idx = 9, .gen = 5 };
    events_push(EV_OVERLAP_ENTER, a, b, p0, n0, 3.5f, 0x8u);
    UT_ASSERT(events_pushed_count() == 1);
    UT_ASSERT(events_dispatched_count() == 0); /* nothing runs until drain */
    events_drain();
    UT_ASSERT(hits == 1);
    UT_ASSERT(events_dispatched_count() == 1);
    UT_ASSERT(seen_n == 1);
    UT_ASSERT(seen_kinds[0] == EV_OVERLAP_ENTER);
    UT_ASSERT_NEAR(seen_x[0], 1.5f, 1e-6f);
    UT_ASSERT_NEAR(seen_rel[0], 3.5f, 1e-6f);
    UT_ASSERT(seen_flags[0] == 0x8u);
    /* the (pool, index, generation) triple survives the round trip */
    UT_ASSERT(seen_a[0].pool == a.pool);
    UT_ASSERT(seen_a[0].idx == a.idx);
    UT_ASSERT(seen_a[0].gen == a.gen);
    UT_OK();

    /* --- 2. a second drain with an empty ring is a no-op ---------------- */
    UT_CASE("drain on an empty queue does not re-dispatch");
    events_drain();
    UT_ASSERT(hits == 1);
    UT_ASSERT(events_dispatched_count() == 1);
    UT_OK();

    /* --- 3. dispatch order == push order, across interleaved kinds ------ */
    UT_CASE("mixed kinds dispatch in push order");
    events_init();
    rec_reset();
    UT_ASSERT(events_subscribe(EV_OVERLAP_ENTER, record, NULL));
    UT_ASSERT(events_subscribe(EV_IMPACT, record, NULL));
    for (int i = 0; i < 6; i++) {
        float q[AME_DIM] = { (float)i };
        events_push((i & 1) ? EV_IMPACT : EV_OVERLAP_ENTER, AME_REF_INVALID,
                    AME_REF_INVALID, q, NULL, 0.0f, 0);
    }
    events_drain();
    UT_ASSERT(seen_n == 6);
    UT_ASSERT(seen_kinds[0] == EV_OVERLAP_ENTER);
    UT_ASSERT(seen_kinds[1] == EV_IMPACT);
    UT_ASSERT(seen_kinds[5] == EV_IMPACT);
    for (int i = 0; i < 6; i++)
        UT_ASSERT_NEAR(seen_x[i], (float)i, 1e-6f);
    UT_OK();

    /* --- 4. two handlers on ONE kind both run, in subscription order ---- */
    UT_CASE("per-kind handler table runs every subscriber");
    events_init();
    int h1 = 0, h2 = 0;
    UT_ASSERT(events_subscribe(EV_HAZARD, record, &h1));
    UT_ASSERT(events_subscribe(EV_HAZARD, record, &h2));
    events_push(EV_HAZARD, AME_REF_INVALID, AME_REF_INVALID, p0, n0, 0.0f, 0);
    events_drain();
    UT_ASSERT(h1 == 1 && h2 == 1);
    UT_OK();

    /* --- 5. handler table capacity ------------------------------------- */
    UT_CASE("AME_EV_MAX_SUBS subscribers per kind, then false");
    events_init();
    for (int i = 0; i < AME_EV_MAX_SUBS; i++)
        UT_ASSERTF(events_subscribe(EV_IMPACT, count_only, &h1),
                   "subscriber %d of %d rejected", i + 1, AME_EV_MAX_SUBS);
    UT_ASSERT(!events_subscribe(EV_IMPACT, count_only, &h1)); /* table full */
    /* a DIFFERENT kind still has room */
    UT_ASSERT(events_subscribe(EV_HAZARD, count_only, &h1));
    UT_ASSERT(!events_subscribe(EV_IMPACT, NULL, &h1));      /* no fn */
    UT_ASSERT(!events_subscribe((ame_ev_kind)EV_KIND_COUNT, count_only, &h1));
    UT_ASSERT(!events_subscribe((ame_ev_kind)-1, count_only, &h1));
    UT_OK();

    /* --- 6. overflow: bounded ring drops the OLDEST -------------------- */
    UT_CASE("overflow drops oldest and counts the loss");
    events_init();
    rec_reset();
    UT_ASSERT(events_subscribe(EV_IMPACT, record, NULL));
    int total = AME_EV_QUEUE_CAP + 4;
    for (int i = 0; i < total; i++) {
        float q[AME_DIM] = { (float)i };
        events_push(EV_IMPACT, AME_REF_INVALID, AME_REF_INVALID, q, NULL, 0.0f,
                    0);
    }
    UT_ASSERT(events_pushed_count() == (uint32_t)total);
    UT_ASSERT(events_overflow_count() == 4u);
    events_drain();
    UT_ASSERT(seen_n == AME_EV_QUEUE_CAP);
    UT_ASSERT_NEAR(seen_x[0], 4.0f, 1e-6f);                    /* oldest gone */
    UT_ASSERT_NEAR(seen_x[AME_EV_QUEUE_CAP - 1], (float)(total - 1), 1e-6f);
    UT_ASSERT(events_dispatched_count() == (uint32_t)AME_EV_QUEUE_CAP);
    UT_OK();

    /* --- 7. NULL point/normal pointers are tolerated ------------------- */
    UT_CASE("NULL p / NULL n push zeroed vectors");
    events_init();
    rec_reset();
    UT_ASSERT(events_subscribe(EV_OVERLAP_EXIT, record, NULL));
    events_push(EV_OVERLAP_EXIT, AME_REF_INVALID, AME_REF_INVALID, NULL, NULL,
                0.0f, 0);
    events_drain();
    UT_ASSERT(seen_n == 1);
    UT_ASSERT_NEAR(seen_x[0], 0.0f, 1e-6f);
    UT_OK();

    /* --- 8. invalid kind is dropped, not counted ----------------------- */
    UT_CASE("out-of-range kind is ignored");
    events_init();
    events_push((ame_ev_kind)EV_KIND_COUNT, AME_REF_INVALID, AME_REF_INVALID,
                p0, n0, 0.0f, 0);
    UT_ASSERT(events_pushed_count() == 0);
    events_drain();
    UT_ASSERT(events_dispatched_count() == 0);
    UT_OK();

    /* --- 9. clear() drops queued events but keeps the counters -------- */
    UT_CASE("events_clear drops the queue, keeps diagnostics");
    events_init();
    int ch = 0;
    UT_ASSERT(events_subscribe(EV_IMPACT, count_only, &ch));
    events_push(EV_IMPACT, AME_REF_INVALID, AME_REF_INVALID, p0, n0, 0.0f, 0);
    events_clear();
    events_drain();
    UT_ASSERT(ch == 0);
    UT_ASSERT(events_pushed_count() == 1u);      /* push still counted */
    UT_ASSERT(events_dispatched_count() == 0u);
    UT_OK();

    /* --- 10. a push from inside a handler is drained by the SAME call --
     * events.txt: handlers may modify gameplay state; the drain loop runs
     * until the ring is empty, so a follow-up pushed by a handler is
     * dispatched before drain() returns. Pinned as the current contract. */
    UT_CASE("handler-pushed event is drained in the same pass");
    events_init();
    rec_reset();
    pusher_fired = 0;
    UT_ASSERT(events_subscribe(EV_IMPACT, pusher, NULL));
    UT_ASSERT(events_subscribe(EV_OVERLAP_EXIT, record, NULL));
    events_push(EV_IMPACT, AME_REF_INVALID, AME_REF_INVALID, p0, n0, 0.0f, 0);
    events_drain();
    UT_ASSERT(seen_n == 1);
    UT_ASSERT(seen_kinds[0] == EV_OVERLAP_EXIT);
    UT_ASSERT(events_dispatched_count() == 2u);
    UT_OK();

    /* --- 11. re-entrant drain() is a guarded no-op -------------------- */
    UT_CASE("drain() called from a handler does not recurse");
    events_init();
    nested_drain_returned = 0;
    UT_ASSERT(events_subscribe(EV_HAZARD, reentrant, NULL));
    events_push(EV_HAZARD, AME_REF_INVALID, AME_REF_INVALID, p0, n0, 0.0f, 0);
    events_push(EV_HAZARD, AME_REF_INVALID, AME_REF_INVALID, p0, n0, 0.0f, 0);
    events_drain();
    UT_ASSERT(nested_drain_returned == 1);
    UT_ASSERT(events_dispatched_count() == 2u); /* not re-dispatched */
    UT_OK();

    /* --- 12. init() forgets subscribers (fixed tables, no leak) ------- */
    UT_CASE("events_init resets the subscriber tables");
    events_init();
    int ih = 0;
    UT_ASSERT(events_subscribe(EV_IMPACT, count_only, &ih));
    events_init();
    events_push(EV_IMPACT, AME_REF_INVALID, AME_REF_INVALID, p0, n0, 0.0f, 0);
    events_drain();
    UT_ASSERT(ih == 0);
    UT_OK();

    return ut_done("events");
}
