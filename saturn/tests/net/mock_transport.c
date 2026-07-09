#include "mock_transport.h"

static bool mt_rx_ready(void *ctx) {
    MockTransport *m = (MockTransport*)ctx;
    return m->rx_pos < m->rx_len;
}
static uint8_t mt_rx_byte(void *ctx) {
    MockTransport *m = (MockTransport*)ctx;
    return (m->rx_pos < m->rx_len) ? m->rx[m->rx_pos++] : 0;
}
static int mt_send(void *ctx, const uint8_t *data, int len) {
    MockTransport *m = (MockTransport*)ctx;
    for (int i = 0; i < len && m->tx_len < (int)sizeof(m->tx); i++)
        m->tx[m->tx_len++] = data[i];
    return len;
}
static bool mt_is_connected(void *ctx) {
    MockTransport *m = (MockTransport*)ctx;
    return m->connected != 0;
}

void mock_transport_init(MockTransport *m, const uint8_t *rx, int rx_len) {
    m->rx = rx; m->rx_len = rx_len; m->rx_pos = 0;
    m->tx_len = 0; m->connected = 1;
}
cui_transport_t mock_transport_iface(MockTransport *m) {
    cui_transport_t t;
    t.rx_ready = mt_rx_ready;
    t.rx_byte = mt_rx_byte;
    t.send = mt_send;
    t.is_connected = mt_is_connected;
    t.ctx = m;
    return t;
}
