#ifndef NET_CONNECT_H
#define NET_CONNECT_H
#include "cui_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { NET_OK = 0, NET_NO_MODEM, NET_DIAL_FAIL } net_connect_result_t;

net_connect_result_t net_connect_open(const char *dial_number);
const cui_transport_t *net_connect_transport(void);
void net_connect_close(void);

#ifdef __cplusplus
}
#endif
#endif /* NET_CONNECT_H */
