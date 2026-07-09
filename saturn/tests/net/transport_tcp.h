#ifndef TRANSPORT_TCP_H
#define TRANSPORT_TCP_H
#include "../../src/net/cui_transport.h"

#ifdef __cplusplus
extern "C" {
#endif
cui_transport_t transport_tcp_connect(const char *host, int port);
void transport_tcp_close(cui_transport_t *t);
#ifdef __cplusplus
}
#endif
#endif
