#include "transport_uart.h"

static bool tu_rx_ready(void *ctx) {
    return saturn_uart_rx_ready((const saturn_uart16550_t*)ctx);
}
static uint8_t tu_rx_byte(void *ctx) {
    return saturn_uart_getc((const saturn_uart16550_t*)ctx);
}
static int tu_send(void *ctx, const uint8_t *data, int len) {
    const saturn_uart16550_t *u = (const saturn_uart16550_t*)ctx;
    int i;
    for (i = 0; i < len; i++)
        if (!saturn_uart_putc(u, data[i])) return i;   /* short write on failure */
    return len;
}
static bool tu_is_connected(void *ctx) {
    /* MSR bit 7 (0x80) = Data Carrier Detect */
    return (saturn_uart_read_msr((const saturn_uart16550_t*)ctx) & 0x80) != 0;
}

cui_transport_t transport_uart_make(const saturn_uart16550_t *uart) {
    cui_transport_t t;
    t.rx_ready = tu_rx_ready;
    t.rx_byte = tu_rx_byte;
    t.send = tu_send;
    t.is_connected = tu_is_connected;
    t.ctx = (void*)uart;
    return t;
}
