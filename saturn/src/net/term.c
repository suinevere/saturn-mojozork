/*----------------------
 | term.c
 | Description: A minimal telnet terminal over a cui_transport: drains received
 |   bytes to the console (defensively swallowing any telnet IAC command
 |   sequences, which the multizork server never actually sends) and sends a typed
 |   line to the server with its trailing newline. Platform-independent -- all I/O
 |   goes through the transport and the console.
 | Author: suinevere
 | Dependencies: term.h, console.h (console_write), string.h
 ----------------------*/
#include "term.h"
#include "console.h"
#include <string.h>

#define TELNET_IAC 0xFF

/*----------------------
 | term_init
 | Description: Clears the terminal state (the IAC byte-skip counter).
 | Author: suinevere
 ----------------------*/
void term_init(TermState *t) {
    t->iac_skip = 0;
}

/*----------------------
 | term_service
 | Description: Pulls up to `max_bytes` received bytes and writes them to the
 |   console, which handles \r, \n, and wrapping. A telnet IAC byte marks the next
 |   two bytes as a command to swallow (defensive; the server does not send them).
 |   Budgeted per call so a flood cannot starve the rest of the frame loop.
 | Author: suinevere
 | Dependencies: console.h, the transport rx callbacks
 | Globals: N/A
 | Params: t -- terminal state; tr -- transport to read; max_bytes -- read budget
 | Returns: the number of bytes consumed this call
 ----------------------*/
int term_service(TermState *t, const cui_transport_t *tr, int max_bytes) {
    int consumed = 0;
    while (consumed < max_bytes && cui_transport_rx_ready(tr)) {
        uint8_t c = cui_transport_rx_byte(tr);
        consumed++;
        if (t->iac_skip > 0) {
            t->iac_skip--;
            continue;
        }
        if (c == TELNET_IAC) {
            t->iac_skip = 2;
            continue;
        }
        {
            char ch = (char)c;
            console_write(&ch, 1);
        }
    }
    return consumed;
}

/*----------------------
 | term_submit_line
 | Description: Sends the keyboard's current line to the server, echoing it onto
 |   the prompt line first (there is no server echo) and appending a newline both
 |   on screen and on the wire, then resets the keyboard for the next line.
 | Author: suinevere
 | Dependencies: console.h, keyboard (keyboard_reset), the transport send callback
 | Globals: N/A
 | Params: tr -- transport to send on; k -- keyboard state holding the line
 | Returns: N/A
 ----------------------*/
void term_submit_line(const cui_transport_t *tr, KeyboardState *k) {
    int len = k->input_len;
    if (len > 0)
        console_write(k->input, (unsigned int)len);
    console_write("\n", 1);
    if (len > 0)
        cui_transport_send(tr, (const uint8_t*)k->input, len);
    cui_transport_send(tr, (const uint8_t*)"\n", 1);
    keyboard_reset(k);
}
