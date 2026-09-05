#include "ame/events.h"
#include "ame/memory.h"

#include <stdio.h>
#include <stdlib.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL: %s\n", m);
    return 1;
}

static int n_open, n_match, n_turn, n_miss;

static void on_ev(const ame_event *e, void *u)
{
    (void)u;
    if (e->kind == MEM_EV_OPEN) n_open++;
    if (e->kind == MEM_EV_MATCH) n_match++;
    if (e->kind == MEM_EV_TURN) n_turn++;
    if (e->kind == MEM_EV_MISMATCH) n_miss++;
}

int main(void)
{
    ame_events_reset();
    ame_events_subscribe(MEM_EV_OPEN, on_ev, NULL);
    ame_events_subscribe(MEM_EV_MATCH, on_ev, NULL);
    ame_events_subscribe(MEM_EV_TURN, on_ev, NULL);
    ame_events_subscribe(MEM_EV_MISMATCH, on_ev, NULL);
    mem_reset(42);
    MemSnap s;
    mem_snapshot(&s);

    int count[MEM_PAIRS];
    for (int i = 0; i < MEM_PAIRS; i++) count[i] = 0;
    for (int i = 0; i < MEM_COUNT; i++) {
        if (s.cards[i].pair < 0 || s.cards[i].pair >= MEM_PAIRS)
            return fail("pair range");
        count[s.cards[i].pair]++;
        if (s.cards[i].face != MEM_DOWN) return fail("start face");
    }
    for (int i = 0; i < MEM_PAIRS; i++)
        if (count[i] != 2) return fail("not pairs");

    /* pick both cards of pair 0 */
    int a = -1, b = -1;
    for (int i = 0; i < MEM_COUNT; i++) {
        if (s.cards[i].pair == 0) {
            if (a < 0) a = i;
            else b = i;
        }
    }
    mem_on_click(s.cards[a].x, s.cards[a].y);
    mem_on_click(s.cards[b].x, s.cards[b].y);
    double t = 0;
    for (int k = 0; k < 90; k++) {
        t += 0.05;
        mem_tick(0.05f, t);
    }
    mem_snapshot(&s);
    if (s.score[0] != 1) return fail("match score");
    if (s.n_matched != 1) return fail("n_matched");
    if (s.turn != 1) return fail("turn did not pass after match");
    if (s.cards[a].face != MEM_MATCHED || s.cards[b].face != MEM_MATCHED)
        return fail("matched faces");
    ame_events_drain();
    if (n_open != 2) return fail("open events");
    if (n_match != 1) return fail("match event");
    if (n_turn != 1) return fail("turn event after match");

    /* mismatch: two different remaining cards */
    int c = -1, d = -1;
    for (int i = 0; i < MEM_COUNT; i++) {
        if (s.cards[i].face != MEM_DOWN) continue;
        if (c < 0) c = i;
        else if (s.cards[i].pair != s.cards[c].pair) { d = i; break; }
    }
    if (c < 0 || d < 0) return fail("no mismatch pair");
    mem_on_click(s.cards[c].x, s.cards[c].y);
    mem_on_click(s.cards[d].x, s.cards[d].y);
    t += 0.01;
    mem_tick(0.01f, t);
    mem_snapshot(&s);
    if (!s.resolving) return fail("expected resolving");
    /* click during resolve ignored */
    for (int i = 0; i < MEM_COUNT; i++) {
        if (s.cards[i].face == MEM_DOWN) {
            mem_on_click(s.cards[i].x, s.cards[i].y);
            break;
        }
    }
    t += 1.0;
    mem_tick(1.0f, t);
    mem_snapshot(&s);
    if (s.cards[c].face != MEM_DOWN || s.cards[d].face != MEM_DOWN)
        return fail("mismatch should close");
    if (s.turn != 0) return fail("turn should be P1 again");
    if (s.score[1] != 0) return fail("no score on mismatch");
    ame_events_drain();
    if (n_miss != 1) return fail("mismatch event");
    if (n_turn != 2) return fail("turn event after mismatch");

    printf("test_memory ok\n");
    return 0;
}
