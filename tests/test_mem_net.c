/* tests — Stage 1 exit: authoritative online Memory over loopback.
 *
 * One REAL server process (mem_server, spawned via argv[1]) + two
 * scripted client sessions (mem_client mirrors). Verifies:
 *   1. join/welcome/state flow, full game played to WIN, correct
 *      winner + scores on BOTH mirrors and in the WIN message
 *   2. strict turn alternation as observed by the clients
 *   3. robust to a client dropping mid-game: LEFT is broadcast, the
 *      remaining client cannot steal the absent player's turn, a
 *      REJOINING client receives a mid-game STATE snapshot and the
 *      game RESUMES and finishes correctly
 *   4. clean exit: SIGTERM -> BYE -> server exits 0
 */
#define _POSIX_C_SOURCE 200809L

#include "utest.h"
#include "mem_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int fd;
    mem_net_rx rx;
    mem_client c;
    int pending;      /* OPEN sent, echo not yet applied */
    int last_turn_ix; /* last TURN broadcast player, -1 none */
    int turn_flips;   /* TURN broadcasts seen */
    int alternation_ok;
} cli;

static void apply(cli *c, const mem_msgv *m) {
    mem_client_on(&c->c, m);
    if (m->type == MEM_MSG_OPENED)
        c->pending = 0;
    if (m->type == MEM_MSG_TURN) {
        if (c->last_turn_ix >= 0 && m->a == (uint8_t)c->last_turn_ix)
            c->alternation_ok = 0;   /* same player twice in a row */
        c->last_turn_ix = m->a;
        c->turn_flips++;
    }
}

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* apply everything currently readable; returns -1 when peer is gone */
static int pump(cli *c) {
    mem_msgv m;
    int r;
    while ((r = mem_net_rx_step(&c->rx, c->fd, &m)) == 1)
        apply(c, &m);
    return r < 0 ? -1 : 0;
}

static int wait_readable(int fd, int ms) {
    struct pollfd p = { .fd = fd, .events = POLLIN };
    return poll(&p, 1, ms) == 1 && (p.revents & POLLIN) ? 1 : 0;
}

/* pump until a message of `type` arrives (applying all messages) */
static int wait_for(cli *c, uint8_t type, mem_msgv *out, int timeout_ms) {
    long deadline = now_ms() + timeout_ms;
    for (;;) {
        mem_msgv m;
        int r;
        while ((r = mem_net_rx_step(&c->rx, c->fd, &m)) == 1) {
            apply(c, &m);
            if (m.type == type) {
                if (out)
                    *out = m;
                return 1;
            }
        }
        if (r < 0)
            return -1;
        if (now_ms() >= deadline)
            return 0;
        wait_readable(c->fd, 10);
    }
}

static uint16_t free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;
    struct sockaddr_in a;
    socklen_t l = sizeof a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0
        || getsockname(fd, (struct sockaddr *)&a, &l) < 0) {
        close(fd);
        return 0;
    }
    uint16_t port = ntohs(a.sin_port);
    close(fd);
    return port;
}

static pid_t spawn_server(const char *path, uint16_t port) {
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        char p[16];
        snprintf(p, sizeof p, "%u", port);
        setenv("MEM_TIME_SCALE", "6", 1); /* authoritative sim runs
                                           * ahead of the mirrors */
        setenv("MEM_IDLE_EXIT", "30", 1);
        execl(path, "mem_server", p, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static int cli_connect(cli *c, uint16_t port) {
    memset(c, 0, sizeof *c);
    mem_client_init(&c->c);
    mem_net_rx_init(&c->rx);
    c->fd = -1;
    c->last_turn_ix = -1;
    c->alternation_ok = 1;
    for (int try = 0; try < 100 && c->fd < 0; try++) {
        c->fd = mem_net_connect("127.0.0.1", port, 250);
        if (c->fd < 0) {
            struct timespec ts = { 0, 50 * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
    }
    if (c->fd < 0)
        return -1;
    if (mem_net_send(c->fd, MEM_MSG_JOIN, 0xff, 0, 0, NULL, 0) < 0)
        return -1;
    mem_msgv m;
    if (wait_for(c, MEM_MSG_WELCOME, &m, 3000) != 1)
        return -1;
    if (wait_for(c, MEM_MSG_STATE, &m, 3000) != 1)
        return -1;
    return 0;
}

/* honest-memory policy: knows only what the protocol reveals - pairs
 * of cards opened at least once on THIS mirror. A cheating client
 * could not do better (never-opened pairs arrive as MEM_PAIR_HIDDEN),
 * so this doubles as the compliance proof: the game completes with
 * hidden pairs end-to-end. */
static int honest_pick(const mem_game *g, int slot) {
    int n = g->count;
    if (slot == 0) {
        /* a known pair among face-down cards? */
        for (int p = 0; p < 256; p++) {
            int a = -1, b = -1;
            for (int i = 0; i < n; i++) {
                if (g->card[i].matched
                    || g->card[i].state != MEM_CARD_DOWN)
                    continue;
                if (g->card[i].pair == (uint8_t)p) {
                    if (a < 0) a = i; else b = i;
                }
            }
            if (a >= 0 && b >= 0)
                return a; /* guaranteed match */
        }
        /* explore: prefer never-opened (hidden) cards */
        for (int i = 0; i < n; i++)
            if (!g->card[i].matched && g->card[i].state == MEM_CARD_DOWN
                && g->card[i].pair == MEM_PAIR_HIDDEN)
                return i;
        for (int i = 0; i < n; i++)
            if (!g->card[i].matched && g->card[i].state == MEM_CARD_DOWN)
                return i;
        return -1;
    }
    /* slot 1: complete the pair if the mate is down AND known */
    if (g->first >= 0) {
        int fp = g->card[g->first].pair;
        if (fp != MEM_PAIR_HIDDEN)
            for (int i = 0; i < n; i++)
                if (i != g->first && !g->card[i].matched
                    && g->card[i].state == MEM_CARD_DOWN
                    && g->card[i].pair == (uint8_t)fp)
                    return i;
        /* mate never opened: explore another hidden card (may be it) */
        for (int i = 0; i < n; i++)
            if (i != g->first && !g->card[i].matched
                && g->card[i].state == MEM_CARD_DOWN
                && g->card[i].pair == MEM_PAIR_HIDDEN)
                return i;
    }
    for (int i = 0; i < n; i++)
        if (!g->card[i].matched && g->card[i].state == MEM_CARD_DOWN)
            return i;
    return -1;
}

typedef struct {
    int winner;
    int s0, s1;
} game_log;

/* both clients play with the policy until the server broadcasts WIN */
static int play_until_win(cli *A, cli *B, game_log *lg, int timeout_ms) {
    long deadline = now_ms() + timeout_ms;
    memset(lg, 0, sizeof *lg);
    int won = 0;
    long last = now_ms();
    while (now_ms() < deadline) {
        cli *cs[2] = { A, B };
        long tnow = now_ms();
        float dt = (float)(tnow - last) / 1000.0f;
        if (dt > 0.05f)
            dt = 0.05f;
        last = tnow;
        mem_step(&A->c.g, dt * 4.0f); /* mirrors animate locally (4x:
                                       * purely cosmetic pacing); decisions
                                       * come only from server messages */
        mem_step(&B->c.g, dt * 4.0f);
        for (int k = 0; k < 2; k++) {
            cli *c = cs[k];
            mem_game *g = &c->c.g;
            if (c->pending || c->c.bye)
                continue;
            if ((g->phase == MEM_PHASE_PICK1 || g->phase == MEM_PHASE_PICK2)
                && g->turn == c->c.you && !mem_over(g)) {
                int i = honest_pick(g, g->phase == MEM_PHASE_PICK1 ? 0 : 1);
                if (i >= 0
                    && mem_net_send(c->fd, MEM_MSG_OPEN, (uint8_t)i, 0, 0,
                                    NULL, 0) == 0)
                    c->pending = 1;
            }
        }
        for (int k = 0; k < 2; k++) {
            if (wait_readable(cs[k]->fd, 2)) {
                if (pump(cs[k]) < 0) {
                    printf("    client gone mid-game\n");
                    return -1;
                }
            }
        }
        if (mem_over(&A->c.g) && mem_over(&B->c.g)) {
            won = 1;
            break;
        }
    }
    if (!won)
        return 0;
    /* the WIN broadcast was applied by pump; outcome from the mirrors */
    lg->winner = mem_winner(&A->c.g);
    lg->s0 = A->c.g.score[0];
    lg->s1 = A->c.g.score[1];
    return 1;
}

int main(int argc, char **argv) {
    printf("=== test_mem_net (Stage 1 exit) ===\n");
    if (argc < 2) {
        printf("usage: test_mem_net <mem_server-binary>\n");
        return ut_done("test_mem_net");
    }
    uint16_t port = free_port();
    UT_ASSERTF(port > 0, "no free port");

    pid_t srv = spawn_server(argv[1], port);
    UT_ASSERTF(srv > 0, "fork failed");
    UT_CASE("join + full game to WIN (two clients, one server process)");
    cli A, B;
    UT_ASSERTF(cli_connect(&A, port) == 0, "client A connect/join");
    UT_ASSERTF(A.c.you == 0, "first client is player 0");
    UT_ASSERTF(cli_connect(&B, port) == 0, "client B connect/join");
    UT_ASSERTF(B.c.you == 1, "second client is player 1");

    UT_CASE("anti-peep: never-opened pairs are hidden on the wire");
    {
        int hidden = 0;
        for (int i = 0; i < A.c.g.count; i++)
            if (A.c.g.card[i].pair == MEM_PAIR_HIDDEN)
                hidden++;
        UT_ASSERTF(hidden == A.c.g.count,
                   "fresh board must hide ALL pairs (hidden=%d/16)", hidden);
        int bhidden = 0;
        for (int i = 0; i < B.c.g.count; i++)
            if (B.c.g.card[i].pair == MEM_PAIR_HIDDEN)
                bhidden++;
        UT_ASSERTF(bhidden == B.c.g.count, "B fresh board fully hidden");
        printf("    fresh board: %d/%d pairs hidden\n", hidden, A.c.g.count);
    }

    game_log g1;
    int rc = play_until_win(&A, &B, &g1, 60000);
    UT_ASSERTF(rc == 1, "game did not finish in time (rc=%d)", rc);
    UT_ASSERTF(g1.s0 + g1.s1 == 8, "scores must sum to pairs (%d+%d)",
               g1.s0, g1.s1);
    UT_ASSERTF(g1.winner == (g1.s0 == g1.s1 ? -1 : (g1.s0 > g1.s1 ? 0 : 1)),
               "winner inconsistent with scores");
    UT_ASSERTF(A.alternation_ok && B.alternation_ok,
               "TURN broadcasts must alternate players");
    UT_ASSERTF(A.turn_flips > 0 && B.turn_flips > 0, "TURN msgs received");
    UT_ASSERTF(mem_over(&B.c.g) && mem_winner(&B.c.g) == g1.winner,
               "both mirrors agree on the winner");
    printf("    game 1: %d:%d winner=%d turn-msgs A=%d B=%d\n", g1.s0,
           g1.s1, g1.winner, A.turn_flips, B.turn_flips);

    UT_CASE("drop mid-game: LEFT broadcast, no turn theft, rejoin resumes");
    /* rematch: both vote JOIN -> fresh STATE for both */
    UT_ASSERTF(mem_net_send(A.fd, MEM_MSG_JOIN, 0, 0, 0, NULL, 0) == 0,
               "A rematch vote");
    UT_ASSERTF(mem_net_send(B.fd, MEM_MSG_JOIN, 0, 0, 0, NULL, 0) == 0,
               "B rematch vote");
    int a_states = A.c.states, b_states = B.c.states;
    UT_ASSERTF(wait_for(&A, MEM_MSG_STATE, NULL, 5000) == 1
               && A.c.states > a_states, "A got rematch STATE");
    UT_ASSERTF(wait_for(&B, MEM_MSG_STATE, NULL, 5000) == 1
               && B.c.states > b_states, "B got rematch STATE");
    UT_ASSERTF(!mem_over(&A.c.g) && !mem_over(&B.c.g), "fresh board");

    /* play until it is B's turn to move, then drop B */
    long deadline = now_ms() + 15000;
    long last = now_ms();
    while (!(A.c.g.phase == MEM_PHASE_PICK1 && A.c.g.turn == B.c.you
             && !B.pending)) {
        float dt = (float)(now_ms() - last) / 1000.0f;
        if (dt > 0.05f)
            dt = 0.05f;
        last = now_ms();
        mem_step(&A.c.g, dt * 4.0f);
        mem_step(&B.c.g, dt * 4.0f);
        UT_ASSERTF(now_ms() < deadline, "never became B's turn");
        if (B.c.g.phase == MEM_PHASE_PICK1 && B.c.g.turn == B.c.you
            && !B.pending) {
            break;
        }
        if ((B.c.g.phase == MEM_PHASE_PICK1 || B.c.g.phase == MEM_PHASE_PICK2)
            && B.c.g.turn == B.c.you && !B.pending) {
            int i = honest_pick(&B.c.g, B.c.g.phase == MEM_PHASE_PICK1 ? 0 : 1);
            if (i >= 0
                && mem_net_send(B.fd, MEM_MSG_OPEN, (uint8_t)i, 0, 0, NULL, 0)
                       == 0)
                B.pending = 1;
        }
        /* keep A moving too, or the turn never comes back to B */
        if ((A.c.g.phase == MEM_PHASE_PICK1 || A.c.g.phase == MEM_PHASE_PICK2)
            && A.c.g.turn == A.c.you && !A.pending) {
            int i = honest_pick(&A.c.g, A.c.g.phase == MEM_PHASE_PICK1 ? 0 : 1);
            if (i >= 0
                && mem_net_send(A.fd, MEM_MSG_OPEN, (uint8_t)i, 0, 0, NULL, 0)
                       == 0)
                A.pending = 1;
        }
        if (wait_readable(A.fd, 2)) {
            if (pump(&A) < 0) {
                printf("    A gone in drop-wait loop\n");
                break;
            }
        }
        if (wait_readable(B.fd, 2))
            UT_ASSERTF(pump(&B) == 0, "B pump");
    }

    close(B.fd);
    B.fd = -1;
    UT_ASSERTF(wait_for(&A, MEM_MSG_LEFT, NULL, 5000) == 1,
               "A must receive LEFT after B drops");
    UT_ASSERTF(A.c.opp_left == 1, "LEFT names player 1");

    /* A must NOT be able to steal the absent player's turn */
    int stolen = honest_pick(&A.c.g, 0);
    UT_ASSERTF(stolen >= 0, "a closed card exists");
    UT_ASSERTF(mem_net_send(A.fd, MEM_MSG_OPEN, (uint8_t)stolen, 0, 0, NULL, 0)
                   == 0, "A sends an out-of-turn open");
    int echo = 0;
    for (int i = 0; i < 40; i++) { /* 400 ms must stay silent */
        if (wait_readable(A.fd, 10)) {
            mem_msgv m;
            int r;
            while ((r = mem_net_rx_step(&A.rx, A.fd, &m)) == 1) {
                mem_client_on(&A.c, &m);
                if (m.type == MEM_MSG_OPENED)
                    echo = 1;
            }
            UT_ASSERTF(r >= 0, "A must stay connected");
        }
    }
    UT_ASSERTF(!echo, "server must ignore opens for the absent player's turn");
    UT_ASSERTF(A.c.g.card[stolen].state == MEM_CARD_DOWN
                   || A.c.g.card[stolen].matched,
               "the out-of-turn card did not flip");

    /* B2 rejoins into the free slot: mid-game STATE, then finish */
    cli B2;
    UT_ASSERTF(cli_connect(&B2, port) == 0, "B2 connect/join");
    UT_ASSERTF(B2.c.you == 1, "B2 reuses the free slot");
    UT_ASSERTF(B2.c.g.score[0] == A.c.g.score[0]
                   && B2.c.g.score[1] == A.c.g.score[1],
               "B2 STATE matches mid-game scores");
    int matched = 0;
    for (int i = 0; i < A.c.g.count; i++)
        matched += A.c.g.card[i].matched;
    int matched2 = 0;
    for (int i = 0; i < B2.c.g.count; i++)
        matched2 += B2.c.g.card[i].matched;
    UT_ASSERTF(matched == matched2, "B2 board matches A board");
    /* a fresh mirror must NOT receive pairs of never-opened cards */
    int hidden2 = 0, down2 = 0;
    for (int i = 0; i < B2.c.g.count; i++) {
        if (B2.c.g.card[i].state == MEM_CARD_DOWN) {
            down2++;
            if (B2.c.g.card[i].pair == MEM_PAIR_HIDDEN)
                hidden2++;
        }
    }
    UT_ASSERTF(down2 > 0 && hidden2 > 0,
               "rejoined mirror has hidden pairs among down cards");
    printf("    B2 snapshot: %d/%d down cards hidden\n", hidden2, down2);

    game_log g2;
    rc = play_until_win(&A, &B2, &g2, 60000);
    UT_ASSERTF(rc == 1, "resumed game did not finish (rc=%d)", rc);
    UT_ASSERTF(g2.s0 + g2.s1 == 8, "resumed scores sum to pairs");
    UT_ASSERTF(mem_winner(&B2.c.g) == mem_winner(&A.c.g),
               "A and B2 agree on the winner after resume");
    printf("    game 2 (after drop+rejoin): %d:%d winner=%d\n", g2.s0, g2.s1,
           mem_winner(&A.c.g));

    UT_CASE("clean exit: SIGTERM -> BYE -> exit 0");
    kill(srv, SIGTERM);
    int w = wait_for(&A, MEM_MSG_BYE, NULL, 5000);
    UT_ASSERTF(w == 1 || w == -1, "BYE or close on shutdown (got %d)", w);
    int st = 0;
    UT_ASSERTF(waitpid(srv, &st, 0) == srv, "server reaped");
    UT_ASSERTF(WIFEXITED(st) && WEXITSTATUS(st) == 0,
               "server exits 0 (st=%d)", st);
    close(A.fd);
    if (B2.fd >= 0)
        close(B2.fd);

    UT_OK();
    return ut_done("test_mem_net");
}
