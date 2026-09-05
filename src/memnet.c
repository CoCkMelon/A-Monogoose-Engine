#include "ame/memnet.h"
#include "ame/events.h"

#include <string.h>

static ame_mem_server *g_live;
static int g_hooked;

static void put_f32(unsigned char *p, float f)
{
    union { float f; uint32_t u; } u;
    u.f = f;
    p[0] = (unsigned char)(u.u & 255);
    p[1] = (unsigned char)((u.u >> 8) & 255);
    p[2] = (unsigned char)((u.u >> 16) & 255);
    p[3] = (unsigned char)((u.u >> 24) & 255);
}

static float get_f32(const unsigned char *p)
{
    union { float f; uint32_t u; } u;
    u.u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
          ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return u.f;
}

int ame_mem_pack_state(const MemSnap *s, unsigned char out[AME_STATE_BYTES])
{
    if (!s || !out) return 0;
    int o = 0;
    for (int i = 0; i < MEM_COUNT; i++) {
        unsigned char pair = (unsigned char)s->cards[i].pair;
        if (s->cards[i].face == MEM_DOWN)
            pair = 255; /* hide face-down pairs from clients */
        out[o++] = pair;
        out[o++] = (unsigned char)s->cards[i].face;
        put_f32(out + o, s->cards[i].angle);
        o += 4;
    }
    out[o++] = (unsigned char)s->turn;
    out[o++] = (unsigned char)s->score[0];
    out[o++] = (unsigned char)s->score[1];
    out[o++] = (unsigned char)s->n_matched;
    out[o++] = (unsigned char)(s->resolving ? 1 : 0);
    out[o++] = (unsigned char)(s->winner + 1); /* 0 = still playing */
    return o == AME_STATE_BYTES;
}

int ame_mem_unpack_state(MemSnap *s, const unsigned char in[AME_STATE_BYTES])
{
    if (!s || !in) return 0;
    memset(s, 0, sizeof(*s));
    int o = 0;
    for (int i = 0; i < MEM_COUNT; i++) {
        unsigned char pair = in[o++];
        s->cards[i].pair = (pair == 255) ? 0 : (int)pair;
        s->cards[i].face = (int)in[o++];
        s->cards[i].angle = get_f32(in + o);
        o += 4;
        int col = i % MEM_COLS;
        int row = i / MEM_COLS;
        const float CARD_W = 1.55f, CARD_H = 2.10f, GAP = 0.22f;
        float gw = MEM_COLS * CARD_W + (MEM_COLS - 1) * GAP;
        float gh = MEM_ROWS * CARD_H + (MEM_ROWS - 1) * GAP;
        float ox = -gw * 0.5f;
        float oy = -gh * 0.5f - 0.35f;
        s->cards[i].x = ox + (float)col * (CARD_W + GAP) + CARD_W * 0.5f;
        s->cards[i].y = oy + (float)(MEM_ROWS - 1 - row) * (CARD_H + GAP) + CARD_H * 0.5f;
        s->cards[i].w = CARD_W;
        s->cards[i].h = CARD_H;
        s->cards[i].hover = 0;
    }
    s->turn = (int)in[o++];
    s->score[0] = (int)in[o++];
    s->score[1] = (int)in[o++];
    s->n_matched = (int)in[o++];
    s->resolving = (int)in[o++];
    s->winner = (int)in[o++] - 1;
    s->input_ok = 1;
    return 1;
}

static void broadcast(ame_mem_server *s, uint8_t type, const void *p, uint16_t n)
{
    for (int i = 0; i < 2; i++) {
        if (s->client[i].ok)
            ame_conn_send(&s->client[i], type, p, n);
    }
}

static void send_state(ame_mem_server *s, int force)
{
    MemSnap snap;
    mem_snapshot(&snap);
    unsigned char buf[AME_STATE_BYTES];
    ame_mem_pack_state(&snap, buf);
    if (!force && s->have_last && memcmp(buf, s->last_state, AME_STATE_BYTES) == 0)
        return;
    memcpy(s->last_state, buf, AME_STATE_BYTES);
    s->have_last = 1;
    broadcast(s, AME_MSG_STATE, buf, AME_STATE_BYTES);
}

static void on_ev(const ame_event *e, void *user)
{
    (void)user;
    ame_mem_server *s = g_live;
    if (!s) return;
    unsigned char p[2];
    switch (e->kind) {
    case MEM_EV_MATCH:
        p[0] = (unsigned char)e->a.index;
        p[1] = (unsigned char)e->b.index;
        broadcast(s, AME_MSG_MATCH, p, 2);
        break;
    case MEM_EV_MISMATCH:
        p[0] = (unsigned char)e->a.index;
        p[1] = (unsigned char)e->b.index;
        broadcast(s, AME_MSG_MISMATCH, p, 2);
        break;
    case MEM_EV_TURN: {
        MemSnap snap;
        mem_snapshot(&snap);
        p[0] = (unsigned char)snap.turn;
        broadcast(s, AME_MSG_TURN, p, 1);
        break;
    }
    case MEM_EV_WIN: {
        MemSnap snap;
        mem_snapshot(&snap);
        unsigned char w = (unsigned char)(snap.winner + 1);
        broadcast(s, AME_MSG_WIN, &w, 1);
        s->finished = 1;
        break;
    }
    default:
        break;
    }
}

static void hook_events(void)
{
    if (g_hooked) return;
    ame_events_subscribe(MEM_EV_MATCH, on_ev, NULL);
    ame_events_subscribe(MEM_EV_MISMATCH, on_ev, NULL);
    ame_events_subscribe(MEM_EV_TURN, on_ev, NULL);
    ame_events_subscribe(MEM_EV_WIN, on_ev, NULL);
    g_hooked = 1;
}

ame_mem_server *ame_mem_server_reset(ame_mem_server *s)
{
    if (!s) return s;
    memset(s, 0, sizeof(*s));
    s->listen_fd = -1;
    ame_conn_reset(&s->client[0]);
    ame_conn_reset(&s->client[1]);
    s->seed = 1;
    return s;
}

ame_mem_server *ame_mem_server_seed(ame_mem_server *s, uint32_t seed)
{
    if (s) s->seed = seed ? seed : 1u;
    return s;
}

ame_mem_server *ame_mem_server_bind(ame_mem_server *s, const char *host, uint16_t port)
{
    if (!s) return s;
    s->listen_fd = ame_net_listen(host ? host : "127.0.0.1", port);
    hook_events();
    return s;
}

uint16_t ame_mem_server_port(const ame_mem_server *s)
{
    if (!s || s->listen_fd < 0) return 0;
    return ame_net_sock_port(s->listen_fd);
}

static void try_accept(ame_mem_server *s)
{
    if (s->listen_fd < 0) return;
    for (;;) {
        int fd = ame_net_accept(s->listen_fd);
        if (fd < 0) break;
        int slot = -1;
        for (int i = 0; i < 2; i++) {
            if (!s->client[i].ok && s->client[i].fd < 0) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            ame_net_close_fd(fd);
            continue;
        }
        ame_conn_attach(&s->client[slot], fd);
    }
}

static void note_drop(ame_mem_server *s, int seat)
{
    int was = s->joined[seat];
    s->joined[seat] = 0;
    s->n_joined = (s->joined[0] ? 1 : 0) + (s->joined[1] ? 1 : 0);
    if (was && s->started && !s->finished) {
        int other = 1 - seat;
        if (s->client[other].ok)
            ame_conn_send(&s->client[other], AME_MSG_PEER_DROP, NULL, 0);
        mem_forfeit(other);
        if (s->client[other].ok) {
            unsigned char w = (unsigned char)(other + 1);
            ame_conn_send(&s->client[other], AME_MSG_WIN, &w, 1);
            send_state(s, 1);
        }
        s->finished = 1;
    }
    ame_conn_close(&s->client[seat]);
}

static void handle_open(ame_mem_server *s, int seat, int idx)
{
    MemSnap snap;
    mem_snapshot(&snap);
    unsigned char reason = 0;
    if (snap.winner >= 0) reason = AME_REJ_GAME_OVER;
    else if (snap.resolving) reason = AME_REJ_BUSY;
    else if (snap.turn != seat) reason = AME_REJ_NOT_YOUR_TURN;
    else if (idx < 0 || idx >= MEM_COUNT) reason = AME_REJ_BAD_CARD;
    if (reason) {
        ame_conn_send(&s->client[seat], AME_MSG_REJECT, &reason, 1);
        return;
    }
    if (!mem_open_index(idx)) {
        reason = AME_REJ_BAD_CARD;
        ame_conn_send(&s->client[seat], AME_MSG_REJECT, &reason, 1);
        return;
    }
    unsigned char ix = (unsigned char)idx;
    broadcast(s, AME_MSG_OPEN, &ix, 1);
    send_state(s, 1);
}

static void handle_msg(ame_mem_server *s, int seat, uint8_t type,
                       const unsigned char *pay, uint16_t n)
{
    if (type == AME_MSG_JOIN) {
        if (!s->joined[seat]) {
            s->joined[seat] = 1;
            s->n_joined++;
            unsigned char st = (unsigned char)seat;
            ame_conn_send(&s->client[seat], AME_MSG_WELCOME, &st, 1);
        }
        if (s->n_joined == 2 && !s->started) {
            s->started = 1;
            mem_reset(s->seed);
            unsigned char t = 0;
            broadcast(s, AME_MSG_TURN, &t, 1);
            send_state(s, 1);
        } else if (s->started) {
            send_state(s, 1);
        }
        return;
    }
    if (type == AME_MSG_OPEN) {
        int idx = (n > 0) ? (int)pay[0] : -1;
        handle_open(s, seat, idx);
        return;
    }
    if (type == AME_MSG_GOODBYE)
        note_drop(s, seat);
}

void ame_mem_server_step(ame_mem_server *s, float dt, double now)
{
    if (!s) return;
    g_live = s;
    try_accept(s);
    unsigned char pay[AME_NET_MAX];
    for (int i = 0; i < 2; i++) {
        if (!s->client[i].ok) {
            if (s->client[i].fd >= 0) note_drop(s, i);
            continue;
        }
        for (;;) {
            uint8_t type = 0;
            uint16_t n = 0;
            int r = ame_conn_recv(&s->client[i], &type, pay, &n);
            if (r == 0) break;
            if (r < 0) {
                note_drop(s, i);
                break;
            }
            handle_msg(s, i, type, pay, n);
            if (!s->client[i].ok) break;
        }
    }
    if (s->started && !s->finished) {
        mem_tick(dt, now);
        ame_events_drain();
        send_state(s, 0);
    } else if (s->started) {
        /* keep pumping closed sockets so a late drop is noticed */
        ame_events_drain();
    }
}

void ame_mem_server_shutdown(ame_mem_server *s)
{
    if (!s) return;
    ame_conn_close(&s->client[0]);
    ame_conn_close(&s->client[1]);
    if (s->listen_fd >= 0) ame_net_close_fd(s->listen_fd);
    s->listen_fd = -1;
    if (g_live == s) g_live = NULL;
}

ame_mem_client *ame_mem_client_reset(ame_mem_client *c)
{
    if (!c) return c;
    memset(c, 0, sizeof(*c));
    ame_conn_reset(&c->conn);
    c->seat = -1;
    c->snap.winner = -1;
    return c;
}

int ame_mem_client_dial(ame_mem_client *c, const char *host, uint16_t port)
{
    if (!c) return 0;
    int fd = ame_net_connect(host ? host : "127.0.0.1", port);
    if (fd < 0) return 0;
    ame_conn_attach(&c->conn, fd);
    return ame_conn_send(&c->conn, AME_MSG_JOIN, NULL, 0);
}

int ame_mem_client_open(ame_mem_client *c, int index)
{
    if (!c || !c->conn.ok) return 0;
    if (index < 0 || index >= MEM_COUNT) return 0;
    unsigned char i = (unsigned char)index;
    return ame_conn_send(&c->conn, AME_MSG_OPEN, &i, 1);
}

void ame_mem_client_poll(ame_mem_client *c)
{
    if (!c || !c->conn.ok) return;
    unsigned char pay[AME_NET_MAX];
    for (;;) {
        uint8_t type = 0;
        uint16_t n = 0;
        int r = ame_conn_recv(&c->conn, &type, pay, &n);
        if (r == 0) break;
        if (r < 0) {
            c->peer_drop = 1;
            break;
        }
        switch (type) {
        case AME_MSG_WELCOME:
            if (n >= 1) c->seat = (int)pay[0];
            break;
        case AME_MSG_STATE:
            if (n >= AME_STATE_BYTES)
                ame_mem_unpack_state(&c->snap, pay);
            break;
        case AME_MSG_OPEN:
            c->saw_open = 1;
            break;
        case AME_MSG_MATCH:
            c->saw_match = 1;
            break;
        case AME_MSG_MISMATCH:
            c->saw_miss = 1;
            break;
        case AME_MSG_TURN:
            c->saw_turn = 1;
            break;
        case AME_MSG_REJECT:
            c->rejected++;
            if (n >= 1) c->last_reject = (int)pay[0];
            break;
        case AME_MSG_PEER_DROP:
            c->peer_drop = 1;
            break;
        case AME_MSG_WIN:
            c->saw_win = 1;
            if (n >= 1) c->snap.winner = (int)pay[0] - 1;
            break;
        default:
            break;
        }
    }
}

void ame_mem_client_close(ame_mem_client *c)
{
    if (!c) return;
    if (c->conn.ok)
        ame_conn_send(&c->conn, AME_MSG_GOODBYE, NULL, 0);
    ame_conn_close(&c->conn);
}
