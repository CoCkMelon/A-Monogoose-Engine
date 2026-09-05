/* memory_game — Stage 1 netcode implementation (see mem_net.h). */
#include "mem_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* --- sockets -------------------------------------------------------------- */

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0)
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int mem_net_listen(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY); /* loopback CI + LAN alike */
    a.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(fd, 4) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int mem_net_accept(int lst) {
    struct pollfd p = { .fd = lst, .events = POLLIN };
    if (poll(&p, 1, 0) != 1)
        return -1;
    int fd = accept(lst, NULL, NULL);
    if (fd < 0)
        return -1;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    set_nonblock(fd);
    return fd;
}

int mem_net_connect(const char *host, uint16_t port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (!host || !*host || strcmp(host, "localhost") == 0)
        host = "127.0.0.1";
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    set_nonblock(fd);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0
        && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    struct pollfd p = { .fd = fd, .events = POLLOUT };
    if (poll(&p, 1, timeout_ms) != 1 || !(p.revents & POLLOUT)) {
        close(fd);
        return -1;
    }
    int err = 0;
    socklen_t el = sizeof err;
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
    if (err != 0) {
        close(fd);
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

int mem_net_send(int fd, uint8_t type, uint8_t a, uint8_t b, uint8_t c,
                 const void *payload, uint16_t len) {
    uint8_t hdr[6] = { type, a, b, c,
                       (uint8_t)(len & 0xff), (uint8_t)(len >> 8) };
    struct pollfd p = { .fd = fd, .events = POLLOUT };
    int deadline = 40; /* x50ms = 2s */
    size_t sent = 0;
    const uint8_t *buf = hdr;
    uint16_t n = 6;
    while (sent < n) {
        ssize_t w = send(fd, buf + sent, n - sent, MSG_NOSIGNAL);
        if (w > 0) {
            sent += (size_t)w;
        } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (poll(&p, 1, 50) != 1)
                if (--deadline <= 0)
                    return -1;
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    sent = 0;
    buf = payload;
    n = len;
    while (sent < n) {
        ssize_t w = send(fd, buf + sent, n - sent, MSG_NOSIGNAL);
        if (w > 0) {
            sent += (size_t)w;
        } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (poll(&p, 1, 50) != 1)
                if (--deadline <= 0)
                    return -1;
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

/* --- frame reassembly ------------------------------------------------------ */

void mem_net_rx_init(mem_net_rx *rx) {
    rx->have = 0;
}

int mem_net_rx_step(mem_net_rx *rx, int fd, mem_msgv *out) {
    for (;;) {
        if (rx->have >= 6) {
            uint16_t len = (uint16_t)(rx->buf[4] | (rx->buf[5] << 8));
            if (len > MEM_NET_MAX_PAYLOAD)
                return -1; /* protocol garbage */
            if (rx->have >= 6u + len) {
                out->type = rx->buf[0];
                out->a = rx->buf[1];
                out->b = rx->buf[2];
                out->c = rx->buf[3];
                out->len = len;
                memcpy(out->payload, rx->buf + 6, len);
                rx->have -= 6u + len;
                memmove(rx->buf, rx->buf + 6 + len, rx->have);
                return 1;
            }
        }
        ssize_t r = recv(fd, rx->buf + rx->have,
                         sizeof rx->buf - rx->have, 0);
        if (r > 0) {
            rx->have += (unsigned)r;
            continue;
        }
        if (r == 0)
            return -1; /* orderly close */
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        if (errno == EINTR)
            continue;
        return -1;
    }
}

/* --- STATE codec + client mirror ------------------------------------------ */

/* payload: cols,rows,count,phase,turn,score0,score1,winner + per card
 * pair,state,matched,angle(f32 bits; same-host loopback, documented) */
void mem_net_encode_state(const mem_game *g, uint8_t *out, uint16_t *len) {
    out[0] = (uint8_t)g->cols;
    out[1] = (uint8_t)g->rows;
    out[2] = (uint8_t)g->count;
    out[3] = (uint8_t)g->phase;
    out[4] = (uint8_t)g->turn;
    out[5] = (uint8_t)g->score[0];
    out[6] = (uint8_t)g->score[1];
    out[7] = (uint8_t)(mem_over(g) ? mem_winner(g) : -2);
    for (int i = 0; i < g->count && i < MEM_MAX_CARDS; i++) {
        uint8_t *p = out + 8 + i * 7;
        p[0] = g->card[i].pair;
        p[1] = g->card[i].state;
        p[2] = g->card[i].matched;
        memcpy(p + 3, &g->card[i].angle, 4);
    }
    *len = (uint16_t)(8 + g->count * 7);
}

static void decode_state(mem_game *g, const uint8_t *in, uint16_t len) {
    int count = in[2];
    if (count < 0 || count > MEM_MAX_CARDS || (uint16_t)(8 + count * 7) != len)
        return; /* refuse malformed */
    mem_reset(g, in[0] > 0 ? in[0] : 4, in[1] > 0 ? in[1] : 4, 1);
    g->count = count;
    g->phase = (mem_phase)in[3];
    g->turn = in[4] < 2 ? in[4] : 0;
    g->score[0] = in[5];
    g->score[1] = in[6];
    g->first = g->second = -1;
    g->resolved = (g->phase == MEM_PHASE_RESOLVE);
    for (int i = 0; i < count; i++) {
        const uint8_t *p = in + 8 + i * 7;
        g->card[i].pair = p[0];
        g->card[i].state = p[1];
        g->card[i].matched = p[2];
        memcpy(&g->card[i].angle, p + 3, 4);
    }
}

void mem_client_init(mem_client *c) {
    memset(c, 0, sizeof *c);
    c->you = -1;
    c->opp_left = -1;
}

void mem_client_on(mem_client *c, const mem_msgv *m) {
    switch (m->type) {
    case MEM_MSG_WELCOME:
        c->you = m->a;
        break;
    case MEM_MSG_STATE:
        decode_state(&c->g, m->payload, m->len);
        c->states++;
        c->opp_left = -1;
        break;
    case MEM_MSG_OPENED:
        /* authoritative pick echo: replays the flip animation */
        (void)mem_pick(&c->g, m->b);
        break;
    case MEM_MSG_LEFT:
        c->opp_left = m->a;
        break;
    case MEM_MSG_BYE:
        c->bye = 1;
        break;
    default:
        break; /* TURN/MATCH/NOMATCH/WIN: mirror derives these itself */
    }
}
