/*----------------------
 | transport_uart.h
 | Description: Adapts a Saturn 16550 UART to the generic cui_transport interface,
 |   so the terminal can be written against the transport abstraction without
 |   knowing about the NetLink hardware. Implemented in transport_uart.c.
 | Author: suinevere
 | Dependencies: cui_transport.h, saturn_uart16550.h
 ----------------------*/
#ifndef TRANSPORT_UART_H
#define TRANSPORT_UART_H
#include "cui_transport.h"
#include "saturn_uart16550.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | transport_uart_make
 | Description: Builds a cui_transport whose callbacks forward to the given UART.
 | Author: suinevere
 ----------------------*/
cui_transport_t transport_uart_make(const saturn_uart16550_t *uart);

#ifdef __cplusplus
}
#endif
#endif /* TRANSPORT_UART_H */
