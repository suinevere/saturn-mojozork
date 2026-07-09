#include "transport_tcp.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { SOCKET s; int up; } TcpCtx;

static bool tc_rx_ready(void *ctx) {
    TcpCtx *c = (TcpCtx*)ctx;
    fd_set r; FD_ZERO(&r); FD_SET(c->s, &r);
    struct timeval tv = {0,0};
    return select(0, &r, NULL, NULL, &tv) > 0;
}
static uint8_t tc_rx_byte(void *ctx) {
    TcpCtx *c = (TcpCtx*)ctx; char b = 0;
    int n = recv(c->s, &b, 1, 0);
    if (n <= 0) c->up = 0;
    return (uint8_t)b;
}
static int tc_send(void *ctx, const uint8_t *d, int len) {
    TcpCtx *c = (TcpCtx*)ctx;
    return send(c->s, (const char*)d, len, 0);
}
static bool tc_is_connected(void *ctx) { return ((TcpCtx*)ctx)->up != 0; }

cui_transport_t transport_tcp_connect(const char *host, int port) {
    cui_transport_t t = {0,0,0,0,0};
    WSADATA w; WSAStartup(MAKEWORD(2,2), &w);
    struct addrinfo hints, *res = NULL;
    char portstr[16]; sprintf(portstr, "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return t;
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) { freeaddrinfo(res); return t; }
    freeaddrinfo(res);
    TcpCtx *c = (TcpCtx*)malloc(sizeof(TcpCtx)); c->s = s; c->up = 1;
    t.rx_ready = tc_rx_ready; t.rx_byte = tc_rx_byte; t.send = tc_send;
    t.is_connected = tc_is_connected; t.ctx = c;
    return t;
}
void transport_tcp_close(cui_transport_t *t) {
    if (t->ctx) { TcpCtx *c = (TcpCtx*)t->ctx; closesocket(c->s); free(c); t->ctx = 0; }
    WSACleanup();
}
