/*----------------------
 | net_connect.c
 | Description: The dial-up connection front end for online play: powers on the
 |   NetLink modem, detects its 16550 UART, probes/initializes the modem, dials,
 |   and on success wraps the UART in a cui_transport the terminal reads/writes.
 |   Holds the single connection's state.
 | Author: suinevere
 | Dependencies: net_connect.h, saturn_uart16550.h (UART detect/SMPC enable),
 |   modem.h (probe/init/dial/hangup), transport_uart.h (the transport wrapper)
 ----------------------*/
#include "net_connect.h"
#include "saturn_uart16550.h"
#include "modem.h"
#include "transport_uart.h"

/*----------------------
 | g_uart / g_transport / g_open
 | Description: The one live connection: the detected UART, the transport built
 |   over it, and whether a connection is currently open.
 | Author: suinevere
 ----------------------*/
static saturn_uart16550_t g_uart;
static cui_transport_t    g_transport;
static int                g_open = 0;

/*----------------------
 | MODEM_DIAL_TIMEOUT
 | Description: ~35s at 28.6MHz. Trimmed for faster retry: a successful V.34
 |   training completes well under this, so a dead attempt gives up sooner and the
 |   caller can redial.
 | Author: suinevere
 ----------------------*/
#define MODEM_DIAL_TIMEOUT 105000000u

/*----------------------
 | detect_uart
 | Description: Powers the modem on via SMPC, then probes the two known NetLink
 |   cart-port base addresses (verbatim from the coup examples) until one responds.
 | Author: suinevere
 | Dependencies: saturn_uart16550.h
 | Globals: g_uart
 | Params: N/A
 | Returns: 1 if a UART was detected, 0 otherwise
 ----------------------*/
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

/*----------------------
 | net_connect_open
 | Description: Runs the full connect sequence -- detect UART, probe and init the
 |   modem, dial -- and on a carrier connect builds the transport and marks the
 |   link open. Distinguishes a missing/unresponsive modem from a failed dial so
 |   the caller can redial only when redialing might help.
 | Author: suinevere
 | Dependencies: saturn_uart16550.h, modem.h, transport_uart.h
 | Globals: g_uart, g_transport, g_open
 | Params: dial_number -- the phone number to dial
 | Returns: NET_OK, NET_NO_MODEM, or NET_DIAL_FAIL
 ----------------------*/
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

/*----------------------
 | net_connect_transport
 | Description: Returns the active transport, or NULL when no connection is open.
 | Author: suinevere
 ----------------------*/
const cui_transport_t *net_connect_transport(void) {
    return g_open ? &g_transport : 0;
}

/*----------------------
 | net_connect_close
 | Description: Hangs up the modem and marks the link closed. Idempotent -- safe
 |   to call when nothing is open (the soft reset calls it unconditionally).
 | Author: suinevere
 | Dependencies: modem.h
 | Globals: g_uart, g_open
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void net_connect_close(void) {
    if (g_open) { modem_hangup(&g_uart); g_open = 0; }
}
