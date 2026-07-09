#include "../src/term.h"
#include "../src/console.h"
#include "../src/keyboard.h"
#include "net/mock_transport.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* count how many bytes are still readable */
static int drain_ready(cui_transport_t *t) {
    int n = 0;
    while (cui_transport_rx_ready(t)) { cui_transport_rx_byte(t); n++; }
    return n;
}

int main(void) {
    TermState ts;
    MockTransport m;
    cui_transport_t t;

    /* plain text + CRLF flows into console lines */
    console_init(); term_init(&ts);
    mock_transport_init(&m, (const uint8_t*)"West of House\r\n>", 16);
    t = mock_transport_iface(&m);
    term_service(&ts, &t, MOJOZORK_RX_BUDGET);
    assert(console_line_count() == 2);
    assert(strcmp(console_get_line(0), "West of House") == 0);
    assert(strcmp(console_get_line(1), ">") == 0);

    /* defensive IAC(0xFF) + 2 option bytes are skipped, not rendered */
    console_init(); term_init(&ts);
    {
        static const uint8_t buf[] = { 'h','i', 0xFF, 0xFB, 0x01, '!' }; /* IAC WILL ECHO */
        mock_transport_init(&m, buf, (int)sizeof(buf));
    }
    t = mock_transport_iface(&m);
    term_service(&ts, &t, MOJOZORK_RX_BUDGET);
    assert(strcmp(console_get_line(0), "hi!") == 0);

    /* per-frame cap: only max_bytes consumed, remainder left for next frame */
    console_init(); term_init(&ts);
    {
        static uint8_t big[1000];
        memset(big, 'x', sizeof(big));
        mock_transport_init(&m, big, (int)sizeof(big));
    }
    t = mock_transport_iface(&m);
    int used = term_service(&ts, &t, MOJOZORK_RX_BUDGET);
    assert(used == MOJOZORK_RX_BUDGET);
    assert(drain_ready(&t) == 1000 - MOJOZORK_RX_BUDGET);

    /* submit: echoes command into console, sends line + newline, resets keyboard */
    console_init(); term_init(&ts);
    console_write("> ", 2);                 /* simulate server's dangling prompt */
    {
        KeyboardState k;
        keyboard_reset(&k);
        keyboard_type_char(&k, 'n');
        keyboard_type_char(&k, 'o');
        keyboard_type_char(&k, 'w');
        mock_transport_init(&m, (const uint8_t*)"", 0);
        t = mock_transport_iface(&m);
        term_submit_line(&t, &k);
        assert(m.tx_len == 4);
        assert(memcmp(m.tx, "now\n", 4) == 0);          /* sent line + \n */
        assert(k.input_len == 0);                        /* keyboard reset */
        assert(strcmp(console_get_line(0), "> now") == 0); /* echoed onto the prompt */
    }

    printf("test_term: OK\n");
    return 0;
}
