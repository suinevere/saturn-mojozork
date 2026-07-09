#include "transport_tcp.h"
#include "../../src/term.h"
#include "../../src/console.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>   /* Sleep() — simulate ~60Hz frame pacing */

int main(void) {
    cui_transport_t t = transport_tcp_connect("multizork.icculus.org", 23);
    if (!t.ctx) { printf("connect failed\n"); return 1; }
    console_init();
    TermState ts; term_init(&ts);
    for (int frame = 0; frame < 120 && cui_transport_is_connected(&t); frame++) {
        term_service(&ts, &t, MOJOZORK_RX_BUDGET);
        Sleep(16);
    }
    int n = console_line_count();
    for (int i = 0; i < n; i++) printf("%s\n", console_get_line(i));
    transport_tcp_close(&t);
    return 0;
}
