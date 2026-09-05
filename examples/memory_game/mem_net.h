/* memory_game — Stage 1 netcode (README FIRST GAME / Stage 1):
 * server-authoritative Memory over TCP. Pure POSIX sockets + the pure
 * sim; no SDL, no GL (the server and the headless net test use this
 * file directly, the SDL client links it too).
 *
 * Transport: TCP frames [type u8][a u8][b u8][c u8][len u16 LE][payload].
 * Loopback/LAN turn game: reliable ordered bytes is the right tool; a
 * card open is ONE request (no prediction/rollback needed).
 *
 * AUTHORITY: the server owns the one true mem_game. Clients run a
 * RENDER-ONLY mirror (the same deterministic sim) driven entirely by
 * server messages: OPENED -> mem_pick on the mirror replays the exact
 * flip animation; STATE overwrites the whole mirror (join / rejoin /
 * rematch). Clients never decide anything.
 *
 * ANTI-PEEP (competitive fairness): pairs of cards that were NEVER
 * OPENED travel as MEM_PAIR_HIDDEN (0xFF). A card's true pair is
 * revealed exactly when the server echoes its OPEN (and in STATE for
 * cards already open/matched/closing). This is precisely the
 * information a HONEST player has: you see a face when it flips open,
 * you may remember it, and never before. TCP ordering guarantees the
 * mirror holds both true pairs before its sim enters RESOLVE, so the
 * deterministic mirror still computes match/no-match exactly.
 */
#ifndef MEM_NET_H
#define MEM_NET_H

#include "mem_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- message set (README Stage 1: join/state/your-turn/open-card/  --- */
/* ---                 match/no-match/win)                          --- */
enum {
    MEM_MSG_JOIN    = 1,  /* C->S a=slot hint(-1 any): join / rematch  */
    MEM_MSG_QUIT    = 2,  /* C->S leaving cleanly                      */
    MEM_MSG_OPEN    = 3,  /* C->S a=card idx: open-card intent         */

    MEM_MSG_WELCOME = 16, /* S->C a=you b=cols c=rows                  */
    MEM_MSG_STATE   = 17, /* S->C full snapshot (join/rejoin/rematch)  */
    MEM_MSG_TURN    = 18, /* S->C a=turn: your-turn / whose turn       */
    MEM_MSG_OPENED  = 19, /* S->C a=player b=card c=PAIR: validated open */
    MEM_MSG_MATCH   = 20, /* S->C a=player b,c=cards (scored)          */
    MEM_MSG_NOMATCH = 21, /* S->C a=player b,c=cards (flip back)       */
    MEM_MSG_WIN     = 22, /* S->C a=winner(-1 tie) b,c=scores          */
    MEM_MSG_LEFT    = 23, /* S->C a=player that dropped                */
    MEM_MSG_BYE     = 24, /* S->C server shutting down                 */
};

#define MEM_NET_MAX_PAYLOAD 512
#define MEM_NET_PLAYERS 2
#define MEM_PAIR_HIDDEN 0xFF /* never-opened card: pair withheld */

typedef struct {
    uint8_t  type, a, b, c;
    uint16_t len;
    uint8_t  payload[MEM_NET_MAX_PAYLOAD];
} mem_msgv;

/* --- sockets (POSIX; blocking connect, nonblocking data) ------------- */

int mem_net_listen(uint16_t port);            /* listen fd or -1        */
int mem_net_accept(int lst);                  /* client fd or -1        */
int mem_net_connect(const char *host, uint16_t port, int timeout_ms);
int mem_net_send(int fd, uint8_t type, uint8_t a, uint8_t b, uint8_t c,
                 const void *payload, uint16_t len);   /* 0 ok, -1 gone */

/* streaming reassembly per fd */
typedef struct {
    unsigned have;
    uint8_t  buf[6 + MEM_NET_MAX_PAYLOAD];
} mem_net_rx;

void mem_net_rx_init(mem_net_rx *rx);
/* 1 = one message decoded into *out, 0 = nothing yet, -1 = peer gone */
int mem_net_rx_step(mem_net_rx *rx, int fd, mem_msgv *out);

/* --- client mirror: applies server messages to a local mem_game ----- */

typedef struct {
    mem_game g;        /* render-only mirror of the authoritative game */
    int      you;      /* our player slot, -1 until WELCOME            */
    int      opp_left; /* player that dropped, -1 none                 */
    int      bye;      /* server said BYE                              */
    int      states;   /* STATE snapshots received                     */
} mem_client;

void mem_client_init(mem_client *c);
/* STATE payload codec (shared by server encode / mirror decode)      */
void mem_net_encode_state(const mem_game *g, uint8_t *out, uint16_t *len);
void mem_client_on(mem_client *c, const mem_msgv *m);

#ifdef __cplusplus
}
#endif

#endif /* MEM_NET_H */
