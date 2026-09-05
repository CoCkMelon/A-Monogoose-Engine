#include "ame/memnet.h"
#include "ame/memory.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL net: %s\n", m);
    return 1;
}

static void pump(ame_mem_server *s, ame_mem_client *a, ame_mem_client *b,
                 double *now, int n)
{
    for (int i = 0; i < n; i++) {
        *now += 0.05;
        ame_mem_server_step(s, 0.05f, *now);
        if (a) ame_mem_client_poll(a);
        if (b) ame_mem_client_poll(b);
    }
}

static int find_pair(const MemSnap *s, int pair, int *a, int *b)
{
    *a = *b = -1;
    for (int i = 0; i < MEM_COUNT; i++) {
        if (s->cards[i].pair != pair) continue;
        if (s->cards[i].face == MEM_MATCHED) continue;
        if (*a < 0) *a = i;
        else { *b = i; return 1; }
    }
    return 0;
}

static int play_full_game(void)
{
    ame_mem_server srv;
    ame_mem_client c0, c1;
    ame_mem_server_bind(ame_mem_server_seed(ame_mem_server_reset(&srv), 42),
                        "127.0.0.1", 0);
    if (srv.listen_fd < 0) return fail("listen");
    uint16_t port = ame_mem_server_port(&srv);
    if (port == 0) return fail("port");

    ame_mem_client_reset(&c0);
    ame_mem_client_reset(&c1);
    if (!ame_mem_client_dial(&c0, "127.0.0.1", port)) return fail("dial0");
    if (!ame_mem_client_dial(&c1, "127.0.0.1", port)) return fail("dial1");

    double now = 1.0;
    pump(&srv, &c0, &c1, &now, 40);
    if (c0.seat != 0) return fail("seat0");
    if (c1.seat != 1) return fail("seat1");
    if (!srv.started) return fail("started");

    /* Wrong-turn reject: P1 opens first. */
    ame_mem_client_open(&c1, 0);
    pump(&srv, &c0, &c1, &now, 10);
    if (c1.rejected < 1) return fail("expected reject");
    if (c1.last_reject != AME_REJ_NOT_YOUR_TURN) return fail("reject reason");

    for (int p = 0; p < MEM_PAIRS; p++) {
        MemSnap truth;
        mem_snapshot(&truth);
        int a, b;
        if (!find_pair(&truth, p, &a, &b)) return fail("find pair");
        int seat = truth.turn;
        ame_mem_client *who = (seat == 0) ? &c0 : &c1;
        ame_mem_client_open(who, a);
        pump(&srv, &c0, &c1, &now, 8);
        ame_mem_client_open(who, b);
        /* hold + flip */
        pump(&srv, &c0, &c1, &now, 40);
        mem_snapshot(&truth);
        if (truth.n_matched != p + 1) {
            fprintf(stderr, "matched %d want %d\n", truth.n_matched, p + 1);
            return fail("match progress");
        }
    }

    MemSnap end;
    mem_snapshot(&end);
    if (end.winner != 2) return fail("perfect play should tie");
    if (c0.snap.winner != 2 || c1.snap.winner != 2)
        return fail("clients did not see tie");
    if (c0.snap.score[0] != 4 || c0.snap.score[1] != 4)
        return fail("client scores");

    ame_mem_client_close(&c0);
    ame_mem_client_close(&c1);
    ame_mem_server_shutdown(&srv);
    return 0;
}

static int play_drop(void)
{
    ame_mem_server srv;
    ame_mem_client c0, c1;
    ame_mem_server_bind(ame_mem_server_seed(ame_mem_server_reset(&srv), 7),
                        "127.0.0.1", 0);
    if (srv.listen_fd < 0) return fail("listen drop");
    uint16_t port = ame_mem_server_port(&srv);
    ame_mem_client_reset(&c0);
    ame_mem_client_reset(&c1);
    if (!ame_mem_client_dial(&c0, "127.0.0.1", port)) return fail("dial0 drop");
    if (!ame_mem_client_dial(&c1, "127.0.0.1", port)) return fail("dial1 drop");
    double now = 1.0;
    pump(&srv, &c0, &c1, &now, 40);
    if (c0.seat != 0 || c1.seat != 1) return fail("seats drop");

    ame_mem_client_close(&c1);
    pump(&srv, &c0, NULL, &now, 20);
    if (!c0.peer_drop) return fail("peer_drop flag");
    if (c0.snap.winner != 0) return fail("forfeit winner P0");

    ame_mem_client_close(&c0);
    ame_mem_server_shutdown(&srv);
    return 0;
}

int main(void)
{
    if (play_full_game()) return 1;
    if (play_drop()) return 1;
    printf("test_net ok\n");
    return 0;
}
