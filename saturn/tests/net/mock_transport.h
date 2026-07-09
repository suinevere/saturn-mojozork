#ifndef MOCK_TRANSPORT_H
#define MOCK_TRANSPORT_H
#include "../../src/net/cui_transport.h"

typedef struct {
    const uint8_t *rx;
    int rx_len;
    int rx_pos;
    uint8_t tx[8192];
    int tx_len;
    int connected;
} MockTransport;

void mock_transport_init(MockTransport *m, const uint8_t *rx, int rx_len);
cui_transport_t mock_transport_iface(MockTransport *m);

#endif
