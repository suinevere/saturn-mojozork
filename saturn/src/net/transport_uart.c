/*----------------------
 | transport_uart.c
 | Description: Adapts a Saturn 16550 UART to the generic cui_transport interface,
 |   so the telnet terminal can be written against the transport abstraction
 |   without knowing about the NetLink hardware. Each callback forwards to the
 |   matching saturn_uart operation over the UART held in the transport context.
 | Author: suinevere
 | Dependencies: transport_uart.h (cui_transport_t, saturn_uart16550_t and its ops)
 ----------------------*/
#include "transport_uart.h"

/*----------------------
 | tu_rx_ready / tu_rx_byte / tu_send / tu_is_connected
 | Description: The cui_transport callbacks over a UART: bytes-available check,
 |   blocking byte read, and a send that stops on the first UART write failure
 |   (returning a short count). tu_is_connected reports carrier via MSR bit 7
 |   (0x80, Data Carrier Detect). ctx is the saturn_uart16550_t.
 | Author: suinevere
 ----------------------*/
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
        if (!saturn_uart_putc(u, data[i])) return i;
    return len;
}
static bool tu_is_connected(void *ctx) {
    return (saturn_uart_read_msr((const saturn_uart16550_t*)ctx) & 0x80) != 0;
}

/*----------------------
 | transport_uart_make
 | Description: Builds a cui_transport whose callbacks are the tu_* adapters above
 |   and whose context is the given UART.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: uart -- the detected UART to wrap
 | Returns: a cui_transport bound to that UART
 ----------------------*/
cui_transport_t transport_uart_make(const saturn_uart16550_t *uart) {
    cui_transport_t t;
    t.rx_ready = tu_rx_ready;
    t.rx_byte = tu_rx_byte;
    t.send = tu_send;
    t.is_connected = tu_is_connected;
    t.ctx = (void*)uart;
    return t;
}
