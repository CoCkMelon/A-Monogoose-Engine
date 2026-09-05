/* memory_game — Stage 1 AUTHORITATIVE SERVER (README Stage 1).
 *
 * Owns the one true mem_game. Clients are thin views: they send
 * card-open intents (MEM_MSG_OPEN), the server validates via the sim
 * itself (mem_pick) and broadcasts the echo; both clients' mirrors
 * replay the identical deterministic animation. Discrete turns over
 * TCP: one request per card open, no prediction, no rollback.
 *
 * robustness (Stage 1 exit):
 *   - a client drop broadcasts LEFT; the game PAUSES while the turn
 *     belongs to an absent player and RESUMES when the slot rejoins
 *     (rejoin = JOIN -> full STATE snapshot)
 *   - rematch: after WIN, every present client must send JOIN again
 *   - clean exit on SIGINT/SIGTERM (BYE to clients) or when nobody is
 *     connected for MEM_IDLE_EXIT seconds (CI safety)
 *
 * usage: mem_server [port]        (default 7777)
 */
#define _POSIX_C_SOURCE 200809L

#include "mem_net.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

typedef struct {
    int        fd;   /* -1 = empty slot */
    mem_net_rx rx;
} slot_t;

static mem_game G;
static slot_t S[MEM_NET_PLAYERS];
static int started;              /* game running with 2 present players */
static int rematch;              /* bitmask: present clients wanting a rematch */
static int last_winner = -2;

static void bcast(uint8_t type, uint8_t a, uint8_t b, uint8_t c,
                  const void *payload, uint16_t len, int except) {
    for (int p = 0; p < MEM_NET_PLAYERS; p++) {
        if (S[p].fd < 0 || p == except)
            continue;
        if (mem_net_send(S[p].fd, type, a, b, c, payload, len) < 0) {
            printf("mem_server: send to p%d failed, dropping\n", p);
            close(S[p].fd);
            S[p].fd = -1;
        }
    }
}

static void send_state_to(int p) {
    uint8_t buf[MEM_NET_MAX_PAYLOAD];
    uint16_t len;
    mem_net_encode_state(&G, buf, &len);
    mem_net_send(S[p].fd, MEM_MSG_STATE, 0, 0, 0, buf, len);
    mem_net_send(S[p].fd, MEM_MSG_TURN, (uint8_t)G.turn, 0, 0, NULL, 0);
}

static void start_game(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /* time(NULL) alone repeats within the same second (two back-to-back
     * games got identical boards) - mix in monotonic nanos + a call
     * counter so consecutive games always differ. */
    static uint32_t games;
    uint32_t seed = (uint32_t)time(NULL) ^ 0xC0FFEEu
        ^ (uint32_t)ts.tv_nsec ^ (games++ << 13);
    mem_reset(&G, 4, 4, seed);
    started = 1;
    rematch = 0;
    last_winner = -2;
    printf("mem_server: game start (seed 0x%08x)\n", seed);
    for (int p = 0; p < MEM_NET_PLAYERS; p++)
        if (S[p].fd >= 0)
            send_state_to(p);
}

static int joined[MEM_NET_PLAYERS]; /* slot sent its JOIN message */

static void drop_slot(int p) {
    if (S[p].fd < 0)
        return;
    close(S[p].fd);
    S[p].fd = -1;
    joined[p] = 0;
    rematch &= ~(1 << p);
    printf("mem_server: player %d left\n", p);
    bcast(MEM_MSG_LEFT, (uint8_t)p, 0, 0, NULL, 0, -1);
    if (S[0].fd < 0 && S[1].fd < 0) {
        started = 0; /* fresh game when two rejoin */
        printf("mem_server: all clients gone, game reset\n");
    }
    /* one player left: game PAUSES if the turn is the absent player's
     * (turn check below rejects opens out of turn) and resumes on
     * rejoin via the full STATE snapshot. */
}

/* first JOIN message from slot p (the accept only reserved the slot) */
static void on_join(int p) {
    mem_net_send(S[p].fd, MEM_MSG_WELCOME, (uint8_t)p, 4, 4, NULL, 0);
    rematch &= ~(1 << p);
    if (!started) {
        if (joined[0] && joined[1] && S[0].fd >= 0 && S[1].fd >= 0)
            start_game();
        else {
            printf("mem_server: p%d in, waiting for opponent\n", p);
            /* board preview until the opponent joins: STATE only, no
             * TURN (a pre-game TURN would double the game-start TURN
             * and look like a broken alternation to clients) */
            uint8_t buf[MEM_NET_MAX_PAYLOAD];
            uint16_t len;
            mem_net_encode_state(&G, buf, &len);
            mem_net_send(S[p].fd, MEM_MSG_STATE, 0, 0, 0, buf, len);
        }
        return;
    }
    send_state_to(p); /* mid-game (re)join: snapshot + whose turn */
}

/* JOIN again after the game is over: a rematch vote. Restart when all
 * seats are filled and everyone present voted. */
static void on_rematch_vote(int p) {
    rematch |= 1 << p;
    int present = 0, votes = 0;
    for (int q = 0; q < MEM_NET_PLAYERS; q++) {
        if (S[q].fd >= 0) {
            present++;
            if (rematch & (1 << q))
                votes++;
        }
    }
    if (present == MEM_NET_PLAYERS && votes == present) {
        start_game();
        return;
    }
    printf("mem_server: rematch vote %d/%d\n", votes, present);
}

int main(int argc, char **argv) {
    uint16_t port = 7777;
    if (argc > 1)
        port = (uint16_t)strtoul(argv[1], NULL, 0);
    long idle_exit = 120;
    const char *ie = getenv("MEM_IDLE_EXIT");
    if (ie && ie[0])
        idle_exit = strtol(ie, NULL, 0);
    /* sim time scale: the server only ever WAITS for client intents
     * (turns are discrete), so running the authoritative animation
     * faster than the clients' mirrors is safe and keeps tests snappy.
     * Mirror-side pacing lags the server; the server never rejects a
     * slow intent. */
    float time_scale = 1.0f;
    const char *ts = getenv("MEM_TIME_SCALE");
    if (ts && ts[0])
        time_scale = strtof(ts, NULL);
    if (time_scale < 0.25f)
        time_scale = 0.25f;
    if (time_scale > 16.0f)
        time_scale = 16.0f;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    int lst = mem_net_listen(port);
    if (lst < 0) {
        fprintf(stderr, "mem_server: cannot listen on %u\n", port);
        return 1;
    }
    memset(&G, 0, sizeof G);
    mem_reset(&G, 4, 4, 1); /* pre-start board for solo joiners */
    for (int p = 0; p < MEM_NET_PLAYERS; p++)
        S[p].fd = -1;
    printf("mem_server: listening on %u (authoritative, 4x4)\n", port);
    fflush(stdout);

    long next_tick = now_ms();
    long empty_since = now_ms();

    while (!g_stop) {
        struct pollfd pfds[1 + MEM_NET_PLAYERS];
        int n = 0;
        pfds[n].fd = lst;
        pfds[n].events = POLLIN;
        int lst_ix = n++;
        int slot_ix[MEM_NET_PLAYERS] = { -1, -1 };
        for (int p = 0; p < MEM_NET_PLAYERS; p++) {
            if (S[p].fd < 0)
                continue;
            slot_ix[p] = n;
            pfds[n].fd = S[p].fd;
            pfds[n].events = POLLIN;
            n++;
        }
        /* NOTE: slot occupancy must be read LIVE (an accept happens
         * mid-iteration, after pfds was built) — the idle check below
         * used to see a stale "no clients" and exit on the very
         * iteration that accepted the first player. */
        int empty_now = S[0].fd < 0 && S[1].fd < 0;
        if (empty_now && empty_since == 0)
            empty_since = now_ms();

        long timeout = next_tick - now_ms();
        if (timeout < 0)
            timeout = 0;
        if (timeout > 50)
            timeout = 50;
        if (empty_now && idle_exit > 0) {
            long left = idle_exit * 1000 - (now_ms() - empty_since);
            if (left < 0)
                left = 0;
            if (left < timeout)
                timeout = left;
        }
        int rc = poll(pfds, (nfds_t)n, (int)timeout);
        if (rc < 0 && errno != EINTR)
            break;
        if (rc > 0) {
            if (pfds[lst_ix].revents & POLLIN) {
                int fd = mem_net_accept(lst);
                if (fd >= 0) {
                    int p = S[0].fd < 0 ? 0 : (S[1].fd < 0 ? 1 : -1);
                    if (p < 0) {
                        printf("mem_server: full, refusing\n");
                        close(fd);
                    } else {
                        S[p].fd = fd;
                        mem_net_rx_init(&S[p].rx);
                        empty_since = 0;
                        printf("mem_server: player %d connected\n", p);
                        /* wait for the JOIN message; the accept only
                         * reserves the transport slot */
                    }
                }
            }
            for (int p = 0; p < MEM_NET_PLAYERS; p++) {
                if (S[p].fd < 0 || slot_ix[p] < 0)
                    continue; /* fd not in this iteration's poll set */
                if (pfds[slot_ix[p]].revents & (POLLHUP | POLLERR)) {
                    drop_slot(p);
                    continue;
                }
                if (!(pfds[slot_ix[p]].revents & POLLIN))
                    continue;
                mem_msgv m;
                int r;
                while ((r = mem_net_rx_step(&S[p].rx, S[p].fd, &m)) == 1) {
                    if (m.type == MEM_MSG_JOIN) {
                        if (!joined[p]) {
                            joined[p] = 1;
                            on_join(p);
                        } else if (started && mem_over(&G)) {
                            on_rematch_vote(p);
                        }
                        /* else: redundant JOIN while playing: ignore */
                    } else if (m.type == MEM_MSG_QUIT) {
                        drop_slot(p);
                        break;
                    } else if (m.type == MEM_MSG_OPEN) {
                        if (!started || mem_over(&G) || G.turn != p
                            || !mem_pick(&G, m.a)) {
                            continue; /* not your turn / illegal: ignored */
                        }
                        bcast(MEM_MSG_OPENED, (uint8_t)p, (uint8_t)m.a,
                              G.card[m.a].pair, NULL, 0, -1);
                    }
                }
                if (r < 0)
                    drop_slot(p);
            }
        }

        /* authoritative sim tick (16 ms); flip animation runs here and
         * on the clients' mirrors at their own rate (decisions are
         * event-driven, so tick-rate differences are cosmetic only) */
        long now = now_ms();
        while (now - next_tick >= 16) {
            next_tick += 16;
            if (!started)
                continue;
            int prev_phase = (int)G.phase;
            bool prev_resolved = G.resolved;
            mem_step(&G, 0.016f * time_scale);
            if (G.phase == MEM_PHASE_RESOLVE && prev_phase == MEM_PHASE_RESOLVE
                && G.resolved && !prev_resolved) {
                if (G.was_match)
                    bcast(MEM_MSG_MATCH, (uint8_t)G.turn,
                          (uint8_t)G.first, (uint8_t)G.second, NULL, 0, -1);
                else
                    bcast(MEM_MSG_NOMATCH, (uint8_t)G.turn,
                          (uint8_t)G.first, (uint8_t)G.second, NULL, 0, -1);
            }
            if (G.phase == MEM_PHASE_PICK1 && prev_phase != MEM_PHASE_PICK1)
                bcast(MEM_MSG_TURN, (uint8_t)G.turn, 0, 0, NULL, 0, -1);
            if (mem_over(&G) && last_winner != mem_winner(&G)) {
                last_winner = mem_winner(&G);
                printf("mem_server: game over, winner=%d (%d:%d)\n",
                       last_winner, G.score[0], G.score[1]);
                bcast(MEM_MSG_WIN, (uint8_t)last_winner,
                      (uint8_t)G.score[0], (uint8_t)G.score[1], NULL, 0, -1);
            }
        }

        int empty_end = S[0].fd < 0 && S[1].fd < 0;
        /* empty_since == 0 means "activity this very iteration, not
         * re-armed yet" (the re-arm happens at the top of the NEXT
         * loop pass) - never treat that as "idle since epoch 0". */
        if (empty_end && idle_exit > 0 && empty_since != 0
            && now_ms() - empty_since >= idle_exit * 1000) {
            printf("mem_server: idle exit\n");
            break;
        }
        fflush(stdout);
    }

    bcast(MEM_MSG_BYE, 0, 0, 0, NULL, 0, -1);
    for (int p = 0; p < MEM_NET_PLAYERS; p++)
        if (S[p].fd >= 0)
            close(S[p].fd);
    close(lst);
    printf("mem_server: bye\n");
    return 0;
}
