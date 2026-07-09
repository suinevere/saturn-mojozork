/* Vendored from coup-saturn/core/include/cui_transport.h
 * Source commit: a1f798d
 * Do not edit here; re-sync from coup-saturn if the interface changes. */

/**
 * cui_transport.h - Transport Abstraction Layer
 *
 * Byte-stream transport interface for networked applications.
 * Platform implementations provide concrete transports:
 *   - Saturn: UART over NetLink modem
 *   - SDL: TCP sockets
 *   - Dreamcast: BBA or modem
 *
 * The interface is intentionally minimal -- just enough to support
 * framed binary protocols over any byte-oriented link.
 */

#ifndef CUI_TRANSPORT_H
#define CUI_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct cui_transport {
    /**
     * Check if at least one byte is available to read.
     * Non-blocking.
     *
     * @param ctx  Opaque context (e.g., UART handle, socket fd)
     * @return true if rx_byte() can be called without blocking
     */
    bool (*rx_ready)(void* ctx);

    /**
     * Read a single byte. Only call when rx_ready() returns true.
     *
     * @param ctx  Opaque context
     * @return the byte read
     */
    uint8_t (*rx_byte)(void* ctx);

    /**
     * Send a buffer of bytes.
     *
     * @param ctx   Opaque context
     * @param data  Bytes to send
     * @param len   Number of bytes
     * @return number of bytes sent, or -1 on error
     */
    int (*send)(void* ctx, const uint8_t* data, int len);

    /**
     * Check if the transport link is still active.
     * Optional -- may be NULL if the transport has no notion of
     * connection state (e.g., raw UART).
     *
     * @param ctx  Opaque context
     * @return true if connected / link is up
     */
    bool (*is_connected)(void* ctx);

    /** Opaque context pointer passed to all callbacks. */
    void* ctx;

} cui_transport_t;

/*============================================================================
 * Convenience Helpers
 *============================================================================*/

/** Safe rx_ready check (returns false if transport or callback is NULL). */
static inline bool cui_transport_rx_ready(const cui_transport_t* t)
{
    return (t && t->rx_ready) ? t->rx_ready(t->ctx) : false;
}

/** Safe rx_byte (returns 0 if transport or callback is NULL). */
static inline uint8_t cui_transport_rx_byte(const cui_transport_t* t)
{
    return (t && t->rx_byte) ? t->rx_byte(t->ctx) : 0;
}

/** Safe send (returns -1 if transport or callback is NULL). */
static inline int cui_transport_send(const cui_transport_t* t,
                                     const uint8_t* data, int len)
{
    return (t && t->send) ? t->send(t->ctx, data, len) : -1;
}

/** Safe is_connected check (returns true if callback is NULL -- assume up). */
static inline bool cui_transport_is_connected(const cui_transport_t* t)
{
    if (!t) return false;
    return t->is_connected ? t->is_connected(t->ctx) : true;
}

#endif /* CUI_TRANSPORT_H */
