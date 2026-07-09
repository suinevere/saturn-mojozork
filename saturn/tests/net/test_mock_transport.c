#include "mock_transport.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

int main(void) {
    MockTransport m;
    mock_transport_init(&m, (const uint8_t*)"AB", 2);
    cui_transport_t t = mock_transport_iface(&m);

    assert(cui_transport_rx_ready(&t) == true);
    assert(cui_transport_rx_byte(&t) == 'A');
    assert(cui_transport_rx_byte(&t) == 'B');
    assert(cui_transport_rx_ready(&t) == false);   /* drained */

    assert(cui_transport_send(&t, (const uint8_t*)"north\n", 6) == 6);
    assert(m.tx_len == 6);
    assert(memcmp(m.tx, "north\n", 6) == 0);        /* send captured */

    assert(cui_transport_is_connected(&t) == true);
    printf("test_mock_transport: OK\n");
    return 0;
}
