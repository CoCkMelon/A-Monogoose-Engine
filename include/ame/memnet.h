#ifndef AME_MEMNET_H
#define AME_MEMNET_H

/*
 * Server-authoritative Memory over ame_conn.
 * SETUP: reset → seed → bind. Clients: reset → dial.
 */

#include "ame/memory.h"
#include "ame/net.h"

enum {
    AME_MSG_JOIN      = 1,
    AME_MSG_WELCOME   = 2,
    AME_MSG_OPEN      = 3,
    AME_MSG_REJECT    = 4,
    AME_MSG_STATE     = 5,
    AME_MSG_TURN      = 6,
    AME_MSG_MATCH     = 7,
    AME_MSG_MISMATCH  = 8,
    AME_MSG_WIN       = 9,
    AME_MSG_PEER_DROP = 10,
    AME_MSG_GOODBYE   = 11
};

enum {
    AME_REJ_NOT_YOUR_TURN = 1,
    AME_REJ_BUSY          = 2,
    AME_REJ_BAD_CARD      = 3,
    AME_REJ_GAME_OVER     = 4
};

enum { AME_STATE_BYTES = 16 * 6 + 6 }; /* pair,face,angle[4] × 16 + turn,s0,s1,nm,res,win */

typedef struct ame_mem_server {
    int listen_fd;
    ame_conn client[2];
    int joined[2];
    int n_joined;
    int started;
    int finished;
    uint32_t seed;
    unsigned char last_state[AME_STATE_BYTES];
    int have_last;
} ame_mem_server;

typedef struct ame_mem_client {
    ame_conn conn;
    int seat;       /* -1 until WELCOME */
    int peer_drop;
    int rejected;
    int last_reject;
    int saw_open;
    int saw_match;
    int saw_miss;
    int saw_win;
    int saw_turn;
    MemSnap snap;
} ame_mem_client;

ame_mem_server *ame_mem_server_reset(ame_mem_server *s);
ame_mem_server *ame_mem_server_seed(ame_mem_server *s, uint32_t seed);
ame_mem_server *ame_mem_server_bind(ame_mem_server *s, const char *host, uint16_t port);
uint16_t        ame_mem_server_port(const ame_mem_server *s);
void            ame_mem_server_step(ame_mem_server *s, float dt, double now);
void            ame_mem_server_shutdown(ame_mem_server *s);

ame_mem_client *ame_mem_client_reset(ame_mem_client *c);
int             ame_mem_client_dial(ame_mem_client *c, const char *host, uint16_t port);
int             ame_mem_client_open(ame_mem_client *c, int index);
void            ame_mem_client_poll(ame_mem_client *c);
void            ame_mem_client_close(ame_mem_client *c);

int ame_mem_pack_state(const MemSnap *s, unsigned char out[AME_STATE_BYTES]);
int ame_mem_unpack_state(MemSnap *s, const unsigned char in[AME_STATE_BYTES]);

#endif
