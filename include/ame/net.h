#ifndef AME_NET_H
#define AME_NET_H

#include <stdint.h>

/*
 * Framed TCP. SETUP objects chain (reset / attach).
 * Wire: uint16le nbytes of (type + payload), then those bytes.
 * Loopback-first. No malloc in send/recv.
 */

enum { AME_NET_MAX = 1024 };

typedef struct ame_conn {
    int fd;
    int ok;
    unsigned char in[AME_NET_MAX + 8];
    int in_n;
} ame_conn;

ame_conn *ame_conn_reset(ame_conn *c);
ame_conn *ame_conn_attach(ame_conn *c, int fd);
void      ame_conn_close(ame_conn *c);

int ame_conn_send(ame_conn *c, uint8_t type, const void *payload, uint16_t n);
/* 1 = one message, 0 = none yet, -1 = closed/error. */
int ame_conn_recv(ame_conn *c, uint8_t *type, unsigned char *payload, uint16_t *n);

int      ame_net_listen(const char *host, uint16_t port);
uint16_t ame_net_sock_port(int listen_fd);
int      ame_net_accept(int listen_fd); /* -1 if none */
int      ame_net_connect(const char *host, uint16_t port);
void     ame_net_close_fd(int fd);

#endif
