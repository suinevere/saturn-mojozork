#ifndef TRANSPORT_UART_H
#define TRANSPORT_UART_H
#include "cui_transport.h"
#include "saturn_uart16550.h"

#ifdef __cplusplus
extern "C" {
#endif

cui_transport_t transport_uart_make(const saturn_uart16550_t *uart);

#ifdef __cplusplus
}
#endif
#endif /* TRANSPORT_UART_H */
