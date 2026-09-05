#include "ame/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return 0;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
}

static int set_nodelay(int fd)
{
    int on = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    return 1;
}

ame_conn *ame_conn_reset(ame_conn *c)
{
    if (!c) return c;
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    return c;
}

ame_conn *ame_conn_attach(ame_conn *c, int fd)
{
    if (!c) return c;
    c->fd = fd;
    c->ok = (fd >= 0);
    c->in_n = 0;
    if (fd >= 0) {
        set_nonblock(fd);
        set_nodelay(fd);
    }
    return c;
}

void ame_conn_close(ame_conn *c)
{
    if (!c) return;
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->ok = 0;
    c->in_n = 0;
}

static int write_all(int fd, const unsigned char *b, int n)
{
    int off = 0;
    while (off < n) {
        struct pollfd p = { .fd = fd, .events = POLLOUT };
        int pr = poll(&p, 1, 200);
        if (pr <= 0) return 0;
        ssize_t r = send(fd, b + off, (size_t)(n - off), MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return 0;
        }
        if (r == 0) return 0;
        off += (int)r;
    }
    return 1;
}

int ame_conn_send(ame_conn *c, uint8_t type, const void *payload, uint16_t n)
{
    if (!c || !c->ok || c->fd < 0) return 0;
    if ((int)n + 1 > AME_NET_MAX) return 0;
    unsigned char hdr[3];
    uint16_t nbytes = (uint16_t)(n + 1);
    hdr[0] = (unsigned char)(nbytes & 255);
    hdr[1] = (unsigned char)((nbytes >> 8) & 255);
    hdr[2] = type;
    if (!write_all(c->fd, hdr, 3)) {
        c->ok = 0;
        return 0;
    }
    if (n > 0 && payload) {
        if (!write_all(c->fd, (const unsigned char *)payload, (int)n)) {
            c->ok = 0;
            return 0;
        }
    }
    return 1;
}

int ame_conn_recv(ame_conn *c, uint8_t *type, unsigned char *payload, uint16_t *n)
{
    if (!c || !c->ok || c->fd < 0) return -1;
    for (;;) {
        if (c->in_n >= 2) {
            uint16_t nbytes = (uint16_t)(c->in[0] | (c->in[1] << 8));
            if (nbytes < 1 || nbytes > AME_NET_MAX) {
                c->ok = 0;
                return -1;
            }
            int need = 2 + (int)nbytes;
            if (c->in_n >= need) {
                if (type) *type = c->in[2];
                uint16_t pn = (uint16_t)(nbytes - 1);
                if (n) *n = pn;
                if (payload && pn) memcpy(payload, c->in + 3, pn);
                int rest = c->in_n - need;
                if (rest > 0) memmove(c->in, c->in + need, (size_t)rest);
                c->in_n = rest;
                return 1;
            }
        }
        ssize_t r = recv(c->fd, c->in + c->in_n,
                         (size_t)((int)sizeof(c->in) - c->in_n), 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            c->ok = 0;
            return -1;
        }
        if (r == 0) {
            c->ok = 0;
            return -1;
        }
        c->in_n += (int)r;
    }
}

static int make_addr(const char *host, uint16_t port, struct sockaddr_in *a)
{
    memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_port = htons(port);
    if (!host || !host[0] || strcmp(host, "0.0.0.0") == 0)
        a->sin_addr.s_addr = htonl(INADDR_ANY);
    else if (inet_pton(AF_INET, host, &a->sin_addr) != 1)
        return 0;
    return 1;
}

int ame_net_listen(const char *host, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in a;
    if (!make_addr(host, port, &a)) {
        close(fd);
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 4) < 0) {
        close(fd);
        return -1;
    }
    set_nonblock(fd);
    return fd;
}

uint16_t ame_net_sock_port(int listen_fd)
{
    struct sockaddr_in a;
    socklen_t n = sizeof(a);
    if (getsockname(listen_fd, (struct sockaddr *)&a, &n) < 0) return 0;
    return ntohs(a.sin_port);
}

int ame_net_accept(int listen_fd)
{
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) return -1;
    set_nonblock(fd);
    set_nodelay(fd);
    return fd;
}

int ame_net_connect(const char *host, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    if (!make_addr(host ? host : "127.0.0.1", port, &a)) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    set_nonblock(fd);
    set_nodelay(fd);
    return fd;
}

void ame_net_close_fd(int fd)
{
    if (fd >= 0) close(fd);
}
