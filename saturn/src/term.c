#include "term.h"
#include "console.h"
#include <string.h>

#define TELNET_IAC 0xFF

void term_init(TermState *t) {
    t->iac_skip = 0;
}

int term_service(TermState *t, const cui_transport_t *tr, int max_bytes) {
    int consumed = 0;
    while (consumed < max_bytes && cui_transport_rx_ready(tr)) {
        uint8_t c = cui_transport_rx_byte(tr);
        consumed++;
        if (t->iac_skip > 0) {          /* swallow the 2 bytes after IAC */
            t->iac_skip--;
            continue;
        }
        if (c == TELNET_IAC) {          /* defensive: server never sends these */
            t->iac_skip = 2;
            continue;
        }
        {
            char ch = (char)c;
            console_write(&ch, 1);      /* console handles \r, \n, wrap */
        }
    }
    return consumed;
}

void term_submit_line(const cui_transport_t *tr, KeyboardState *k) {
    int len = k->input_len;
    if (len > 0)
        console_write(k->input, (unsigned int)len);   /* echo onto prompt line */
    console_write("\n", 1);
    if (len > 0)
        cui_transport_send(tr, (const uint8_t*)k->input, len);
    cui_transport_send(tr, (const uint8_t*)"\n", 1);
    keyboard_reset(k);
}
