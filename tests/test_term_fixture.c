#include "../src/term.h"
#include "../src/console.h"
#include "net/mock_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int console_has(const char *needle) {
    int n = console_line_count();
    for (int i = 0; i < n; i++)
        if (strstr(console_get_line(i), needle)) return 1;
    return 0;
}

int main(void) {
    FILE *f = fopen("tests/fixtures/multizork_banner.bin", "rb");
    assert(f != NULL);
    static uint8_t buf[8192];
    int len = (int)fread(buf, 1, sizeof(buf), f);
    fclose(f);
    assert(len > 0);

    console_init();
    TermState ts; term_init(&ts);
    MockTransport m; mock_transport_init(&m, buf, len);
    cui_transport_t t = mock_transport_iface(&m);

    /* drain the whole fixture (loop in case it exceeds one budget) */
    while (cui_transport_rx_ready(&t))
        term_service(&ts, &t, MOJOZORK_RX_BUDGET);

    assert(console_has("Hello sailor!"));
    printf("test_term_fixture: OK\n");
    return 0;
}
