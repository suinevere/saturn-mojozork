#include "../../src/net/cui_transport.h"
#include <stdio.h>
#include <assert.h>

static uint8_t g_byte = 'Z';
static bool r_ready(void *ctx) { (void)ctx; return true; }
static uint8_t r_byte(void *ctx) { (void)ctx; return g_byte; }
static int r_send(void *ctx, const uint8_t *d, int n) { (void)ctx; (void)d; return n; }

int main(void) {
    cui_transport_t t = { r_ready, r_byte, r_send, NULL, NULL };
    assert(cui_transport_rx_ready(&t) == true);
    assert(cui_transport_rx_byte(&t) == 'Z');
    assert(cui_transport_send(&t, (const uint8_t*)"hi", 2) == 2);
    assert(cui_transport_is_connected(&t) == true); /* NULL is_connected => assume up */
    printf("test_transport_iface: OK\n");
    return 0;
}
