#include "ame/events.h"

#include <stdio.h>

static int n_enter;
static int last_kind;
static float last_x;

static void on_enter(const ame_event *e, void *u)
{
    (void)u;
    n_enter++;
    last_kind = e->kind;
    last_x = e->p[0];
}

static int fail(const char *m)
{
    fprintf(stderr, "FAIL events: %s\n", m);
    return 1;
}

int main(void)
{
    ame_events_reset();
    if (!ame_events_subscribe(AME_EV_OVERLAP_ENTER, on_enter, NULL))
        return fail("subscribe");

    float p[3] = {1.5f, 0, 0};
    float n[3] = {0, 0, 1};
    ame_events_push(AME_EV_OVERLAP_ENTER, ame_ref_none(), ame_ref_none(), p, n, 3.0f, 0);
    ame_events_push(AME_EV_OVERLAP_EXIT, ame_ref_none(), ame_ref_none(), p, n, 0, 0);
    int d = ame_events_drain();
    if (d != 2) return fail("drain count");
    if (n_enter != 1) return fail("handler once");
    if (last_kind != AME_EV_OVERLAP_ENTER) return fail("kind");
    if (last_x < 1.4f || last_x > 1.6f) return fail("point");

    /* overflow drops oldest */
    ame_events_reset();
    ame_events_subscribe(AME_EV_IMPACT, on_enter, NULL);
    n_enter = 0;
    for (int i = 0; i < AME_EV_QUEUE + 5; i++) {
        float q[3] = {(float)i, 0, 0};
        ame_events_push(AME_EV_IMPACT, ame_ref_none(), ame_ref_none(), q, n, 0, 0);
    }
    if (ame_events_overflows() < 5) return fail("overflow counter");
    ame_events_drain();
    if (n_enter != AME_EV_QUEUE) return fail("capped drain");

    printf("test_events ok\n");
    return 0;
}
