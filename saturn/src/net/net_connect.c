#include "net_connect.h"
#include "saturn_uart16550.h"
#include "modem.h"
#include "transport_uart.h"

static saturn_uart16550_t g_uart;
static cui_transport_t    g_transport;
static int                g_open = 0;

#define MODEM_DIAL_TIMEOUT 105000000u   /* ~35s at 28.6MHz (trimmed for faster retry;
                                           successful V.34 training completes well
                                           under this, so a dead attempt gives up
                                           sooner and the caller can redial) */

/* Power on the modem via SMPC, then probe the two known NetLink cart-port
   base addresses (verbatim from coup examples/coup/saturn/main_saturn.c). */
static int detect_uart(void) {
    static const struct { uint32_t base; uint32_t stride; } addrs[] = {
        { 0x25895001, 4 },
        { 0x04895001, 4 },
    };
    int i;
    saturn_netlink_smpc_enable();
    for (i = 0; i < 2; i++) {
        g_uart.base = addrs[i].base;
        g_uart.stride = addrs[i].stride;
        if (saturn_uart_detect(&g_uart)) return 1;
    }
    return 0;
}

net_connect_result_t net_connect_open(const char *dial_number) {
    g_open = 0;
    if (!detect_uart())                    return NET_NO_MODEM;
    if (modem_probe(&g_uart) != MODEM_OK)  return NET_NO_MODEM;
    if (modem_init(&g_uart) != MODEM_OK)   return NET_NO_MODEM;
    if (modem_dial(&g_uart, dial_number, MODEM_DIAL_TIMEOUT) != MODEM_CONNECT)
        return NET_DIAL_FAIL;
    g_transport = transport_uart_make(&g_uart);
    g_open = 1;
    return NET_OK;
}

const cui_transport_t *net_connect_transport(void) {
    return g_open ? &g_transport : 0;
}

void net_connect_close(void) {
    if (g_open) { modem_hangup(&g_uart); g_open = 0; }
}
