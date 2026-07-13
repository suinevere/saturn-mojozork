# MojoZork Saturn — Online Multizork Telnet Client — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Play Online" mode to the Saturn MojoZork project that turns the console into a NetLink telnet terminal to `multizork.icculus.org:23` via DreamPi.

**Architecture:** The Saturn is a dumb terminal (no Z-machine online). A pure-C terminal core (`term.c`) sits on coup's `cui_transport_t` byte-stream vtable: it pumps received bytes into the existing `console` and sends the existing `keyboard` line buffer on Enter. Two transports implement the vtable — UART over the NetLink modem (Saturn) and TCP (host tests). The core is host-tested against recorded server bytes; the modem/UART glue is Saturn-build- and hardware-verified.

**Tech Stack:** SaturnRingLib (C++ frontend), C for portable modules, MSYS2 mingw64 `gcc` (host tests), `sh2eb-elf` cross toolchain (Saturn build), DreamPi + eaudunord/Netlink tunnel (transport bridge).

## Global Constraints

- **Project directory:** `SaturnRingLib/Projects/mojozork/` — all paths below are relative to it unless noted. It is its own git repo; commit port work there.
- **Spec:** `docs/superpowers/specs/2026-07-08-mojozork-saturn-multizork-telnet-design.md` (in the outer repo).
- **Saturn build glob:** the makefile compiles every `src/**/*.c` and `src/**/*.cxx`. Therefore host-only C (`transport_tcp.c`, mock transport, tests) MUST live under `tests/`, never under `src/`, or it will be cross-compiled into the Saturn build and break it.
- **Host test toolchain:** `gcc` / `g++` from `/c/msys64/mingw64/bin` (on PATH). Each test is a self-contained `main()` using `assert` + `printf`, built ad hoc (there is no test runner), exactly like `tests/test_console.c`.
- **Existing reusable code (already built, do not rewrite):**
  - `src/console.{h,c}` — `console_init()`, `console_write(const char*, unsigned int)`, `console_line_count()`, `console_get_line(int)`. `console_write` already skips `\r`, flushes on `\n`, and word-wraps at 40 cols.
  - `src/keyboard.{h,c}` — `KeyboardState { int cursor_col, cursor_row; char input[KB_INPUT_MAX]; int input_len, submitted; }`; `keyboard_reset/move/type/type_char/backspace/submit`, `keyboard_current_char`. `KB_INPUT_MAX == 64`. `KB_LAYOUT` includes digits 0-9.
  - `src/saturn_keyboard.h` — `saturn_keyboard_poll()` → `SaturnKeyEvent { SaturnKeyKind kind; char ch; }` (NONE/CHAR/BACKSPACE/ENTER/ESCAPE); `saturn_keyboard_present()`.
  - `src/main.cxx` reusable statics: `render_console(void)`, `render_keyboard(const KeyboardState&)`, `menu_select(const char* title, const char* const* items, int count)` → chosen index or -1. Gamepad is `g_pad` (`SRL::Input::Digital*`), buttons via `g_pad->WasPressed(Button::X)`.
- **Vendored transport source:** clone **coup-saturn** locally (no longer a sibling checkout) — <https://github.com/likeagfeld/coup-saturn>. Headers to copy: `core/include/cui_transport.h`, `pal/saturn/saturn_uart16550.h`, `pal/saturn/modem.h`.
- **Dial code:** `#define ZATURN_DIAL_NUMBER "199403"` (coordinated `1994XX` code; `199403` is the free gap). Runtime dial-entry field pre-fills this default.
- **RX budget:** drain the transport each frame, capped at `ZATURN_RX_BUDGET` = 512 bytes (safety bound; at 9600 baud only ~16 bytes/frame actually arrive).
- **No new interpreter changes.** Online mode never touches `mojozork.c` / `mojozork_saturn.c`; the local Z-machine mode is unchanged.

---

## File Structure

| File | Status | Responsibility |
|------|--------|----------------|
| `src/net/cui_transport.h` | vendor | Byte-stream vtable (`rx_ready`/`rx_byte`/`send`/`is_connected` + `ctx`). |
| `src/net/saturn_uart16550.h` | vendor | Raw 16550 UART register I/O (Saturn-only; consumed by `transport_uart.c`). |
| `src/net/modem.h` | vendor | AT-command probe/init/dial/hangup (Saturn-only). |
| `src/term.h` / `src/term.c` | create | Pure-C terminal core: RX pump into console; line send + echo. No SRL/UART. |
| `src/net/transport_uart.h` / `.c` | create | `cui_transport_t` over the NetLink modem UART (Saturn). |
| `src/net/net_connect.h` / `.c` | create | C wrapper: probe→init→dial; owns the UART handle + connected transport. Isolates C-only modem headers from the C++ frontend. |
| `src/main.cxx` | modify | Mode menu (Local/Online); online dial-entry + terminal loop. Adds `ZATURN_DIAL_NUMBER`. |
| `tests/net/mock_transport.h` / `.c` | create | Host test double implementing `cui_transport_t` over in-memory buffers. |
| `tests/net/transport_tcp.h` / `.c` | create | Host `cui_transport_t` over a winsock TCP socket (optional live smoke). |
| `tests/fixtures/multizork_banner.bin` | create | Recorded server bytes for deterministic replay. |
| `tests/test_term.c` | create | Host unit tests for `term.c` (RX pump, line send/echo, IAC skip, cap). |
| `tests/test_term_fixture.c` | create | Host test: replay `multizork_banner.bin` through the core; assert banner renders. |

---

## Task 1: Vendor the transport interface header

**Files:**
- Create: `src/net/cui_transport.h` (copied from `coup-saturn/core/include/cui_transport.h`)
- Test: `tests/net/test_transport_iface.c`

**Interfaces:**
- Produces: `typedef struct cui_transport { bool (*rx_ready)(void*); uint8_t (*rx_byte)(void*); int (*send)(void*, const uint8_t*, int); bool (*is_connected)(void*); void* ctx; } cui_transport_t;` plus inline helpers `cui_transport_rx_ready/rx_byte/send/is_connected`.

- [ ] **Step 1: Copy the header and record provenance**

Copy the file verbatim, then prepend a provenance comment as the first lines:

```c
/* Vendored from coup-saturn core/include/cui_transport.h
 * Source: https://github.com/likeagfeld/coup-saturn @ <short commit>
 * Do not edit here; re-sync from coup-saturn if the interface changes. */
```

Clone coup-saturn (it is no longer checked out locally) and read the commit hash:
```bash
git clone https://github.com/likeagfeld/coup-saturn.git
git -C coup-saturn rev-parse --short HEAD
```

- [ ] **Step 2: Write the failing compile-check test**

Create `tests/net/test_transport_iface.c`:

```c
#include "../../src/net/cui_transport.h"
#include <stdio.h>
#include <assert.h>

static uint8_t g_byte = 'Z';
static bool r_ready(void *ctx) { (void)ctx; return true; }
static uint8_t r_byte(void *ctx) { (void)ctx; return g_byte; }
static int r_send(void *ctx, const uint8_t *d, int n) { (void)ctx; (void)d; return n; }

int main(void) {
    cui_transport_t t = { r_ready, r_byte, r_send, NULL, NULL };
    assert(cui_transport_rx_ready(&t) == true);
    assert(cui_transport_rx_byte(&t) == 'Z');
    assert(cui_transport_send(&t, (const uint8_t*)"hi", 2) == 2);
    assert(cui_transport_is_connected(&t) == true); /* NULL is_connected ⇒ assume up */
    printf("test_transport_iface: OK\n");
    return 0;
}
```

- [ ] **Step 3: Run it to verify it fails**

Run:
```bash
gcc tests/net/test_transport_iface.c -o tests/net/test_transport_iface && ./tests/net/test_transport_iface
```
Expected: FAIL — compile error, `cui_transport.h: No such file or directory` (before Step 1 is done) or a clean pass once the header exists. If Step 1 is already applied, this step confirms the header is self-contained on the host.

- [ ] **Step 4: Run it to verify it passes**

Run the same command. Expected: `test_transport_iface: OK`.

- [ ] **Step 5: Commit**

```bash
git add src/net/cui_transport.h tests/net/test_transport_iface.c
git commit -m "vendor cui_transport.h transport interface"
```

---

## Task 2: Mock transport (host test double)

**Files:**
- Create: `tests/net/mock_transport.h`, `tests/net/mock_transport.c`
- Test: `tests/net/test_mock_transport.c`

**Interfaces:**
- Consumes: `cui_transport_t` (Task 1).
- Produces:
  - `typedef struct { const uint8_t *rx; int rx_len; int rx_pos; uint8_t tx[8192]; int tx_len; int connected; } MockTransport;`
  - `void mock_transport_init(MockTransport *m, const uint8_t *rx, int rx_len);`
  - `cui_transport_t mock_transport_iface(MockTransport *m);`

- [ ] **Step 1: Write the failing test**

Create `tests/net/test_mock_transport.c`:

```c
#include "mock_transport.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

int main(void) {
    MockTransport m;
    mock_transport_init(&m, (const uint8_t*)"AB", 2);
    cui_transport_t t = mock_transport_iface(&m);

    assert(cui_transport_rx_ready(&t) == true);
    assert(cui_transport_rx_byte(&t) == 'A');
    assert(cui_transport_rx_byte(&t) == 'B');
    assert(cui_transport_rx_ready(&t) == false);   /* drained */

    assert(cui_transport_send(&t, (const uint8_t*)"north\n", 6) == 6);
    assert(m.tx_len == 6);
    assert(memcmp(m.tx, "north\n", 6) == 0);        /* send captured */

    assert(cui_transport_is_connected(&t) == true);
    printf("test_mock_transport: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run:
```bash
gcc -I src/net tests/net/test_mock_transport.c tests/net/mock_transport.c -o tests/net/test_mock_transport && ./tests/net/test_mock_transport
```
Expected: FAIL — `mock_transport.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `tests/net/mock_transport.h`:

```c
#ifndef MOCK_TRANSPORT_H
#define MOCK_TRANSPORT_H
#include "../../src/net/cui_transport.h"

typedef struct {
    const uint8_t *rx;
    int rx_len;
    int rx_pos;
    uint8_t tx[8192];
    int tx_len;
    int connected;
} MockTransport;

void mock_transport_init(MockTransport *m, const uint8_t *rx, int rx_len);
cui_transport_t mock_transport_iface(MockTransport *m);

#endif
```

Create `tests/net/mock_transport.c`:

```c
#include "mock_transport.h"

static bool mt_rx_ready(void *ctx) {
    MockTransport *m = (MockTransport*)ctx;
    return m->rx_pos < m->rx_len;
}
static uint8_t mt_rx_byte(void *ctx) {
    MockTransport *m = (MockTransport*)ctx;
    return (m->rx_pos < m->rx_len) ? m->rx[m->rx_pos++] : 0;
}
static int mt_send(void *ctx, const uint8_t *data, int len) {
    MockTransport *m = (MockTransport*)ctx;
    for (int i = 0; i < len && m->tx_len < (int)sizeof(m->tx); i++)
        m->tx[m->tx_len++] = data[i];
    return len;
}
static bool mt_is_connected(void *ctx) {
    MockTransport *m = (MockTransport*)ctx;
    return m->connected != 0;
}

void mock_transport_init(MockTransport *m, const uint8_t *rx, int rx_len) {
    m->rx = rx; m->rx_len = rx_len; m->rx_pos = 0;
    m->tx_len = 0; m->connected = 1;
}
cui_transport_t mock_transport_iface(MockTransport *m) {
    cui_transport_t t;
    t.rx_ready = mt_rx_ready;
    t.rx_byte = mt_rx_byte;
    t.send = mt_send;
    t.is_connected = mt_is_connected;
    t.ctx = m;
    return t;
}
```

- [ ] **Step 4: Run to verify it passes**

Run the Step 2 command. Expected: `test_mock_transport: OK`.

- [ ] **Step 5: Commit**

```bash
git add tests/net/mock_transport.h tests/net/mock_transport.c tests/net/test_mock_transport.c
git commit -m "add host mock transport test double"
```

---

## Task 3: Terminal RX pump (`term_service`)

**Files:**
- Create: `src/term.h`, `src/term.c`
- Test: `tests/test_term.c`

**Interfaces:**
- Consumes: `cui_transport_t` (Task 1); `console_write`, `console_line_count`, `console_get_line`, `console_init` (existing `src/console.h`).
- Produces:
  - `#define ZATURN_RX_BUDGET 512`
  - `typedef struct { int iac_skip; } TermState;`
  - `void term_init(TermState *t);`
  - `int term_service(TermState *t, const cui_transport_t *tr, int max_bytes);` — reads up to `max_bytes` available bytes, strips telnet `IAC`(0xFF)+2-byte sequences (state carried across calls via `iac_skip`), feeds the rest to `console_write`. Returns bytes consumed from the transport.

- [ ] **Step 1: Write the failing test**

Create `tests/test_term.c`:

```c
#include "../src/term.h"
#include "../src/console.h"
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
    term_service(&ts, &t, ZATURN_RX_BUDGET);
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
    term_service(&ts, &t, ZATURN_RX_BUDGET);
    assert(strcmp(console_get_line(0), "hi!") == 0);

    /* per-frame cap: only max_bytes consumed, remainder left for next frame */
    console_init(); term_init(&ts);
    {
        static uint8_t big[1000];
        memset(big, 'x', sizeof(big));
        mock_transport_init(&m, big, (int)sizeof(big));
    }
    t = mock_transport_iface(&m);
    int used = term_service(&ts, &t, ZATURN_RX_BUDGET);
    assert(used == ZATURN_RX_BUDGET);
    assert(drain_ready(&t) == 1000 - ZATURN_RX_BUDGET);

    printf("test_term: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run:
```bash
gcc -I src tests/test_term.c src/term.c src/console.c tests/net/mock_transport.c -o tests/test_term && ./tests/test_term
```
Expected: FAIL — `term.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `src/term.h`:

```c
#ifndef TERM_H
#define TERM_H
#include "net/cui_transport.h"

#define ZATURN_RX_BUDGET 512

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int iac_skip; } TermState;

void term_init(TermState *t);
int  term_service(TermState *t, const cui_transport_t *tr, int max_bytes);

#ifdef __cplusplus
}
#endif
#endif /* TERM_H */
```

Create `src/term.c`:

```c
#include "term.h"
#include "console.h"

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
```

- [ ] **Step 4: Run to verify it passes**

Run the Step 2 command. Expected: `test_term: OK`.

- [ ] **Step 5: Commit**

```bash
git add src/term.h src/term.c tests/test_term.c
git commit -m "add terminal RX pump with IAC skip and per-frame cap"
```

---

## Task 4: Terminal line send + local echo (`term_submit_line`)

**Files:**
- Modify: `src/term.h`, `src/term.c`
- Test: `tests/test_term.c` (extend)

**Interfaces:**
- Consumes: `KeyboardState`, `keyboard_reset` (existing `src/keyboard.h`).
- Produces: `void term_submit_line(const cui_transport_t *tr, KeyboardState *k);` — appends `k->input` then `"\n"` to the console (local echo; because the server's dangling `"> "` prompt is the console's current partial line, the command visually continues it into `"> north"`), sends `k->input` followed by `"\n"` over the transport, then `keyboard_reset(k)`.

- [ ] **Step 1: Write the failing test (extend `tests/test_term.c`)**

Add these includes near the top of `tests/test_term.c`:

```c
#include "../src/keyboard.h"
```

Add this block just before the final `printf("test_term: OK\n");`:

```c
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
```

- [ ] **Step 2: Run to verify it fails**

Run:
```bash
gcc -I src tests/test_term.c src/term.c src/console.c src/keyboard.c tests/net/mock_transport.c -o tests/test_term && ./tests/test_term
```
Expected: FAIL — `undefined reference to 'term_submit_line'`.

- [ ] **Step 3: Write the implementation**

In `src/term.h`, add the include and declaration:

```c
#include "keyboard.h"
```
```c
void term_submit_line(const cui_transport_t *tr, KeyboardState *k);
```
(Place the `#include "keyboard.h"` next to `#include "net/cui_transport.h"`, and the declaration inside the `extern "C"` block beside `term_service`.)

In `src/term.c`, add:

```c
#include <string.h>

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
```

- [ ] **Step 4: Run to verify it passes**

Run the Step 2 command. Expected: `test_term: OK`.

- [ ] **Step 5: Commit**

```bash
git add src/term.h src/term.c tests/test_term.c
git commit -m "add terminal line send with local echo"
```

---

## Task 5: Record server fixture + deterministic replay test

**Files:**
- Create: `tests/fixtures/multizork_banner.bin`
- Create: `tests/test_term_fixture.c`

**Interfaces:**
- Consumes: `term_service` (Task 3), mock transport (Task 2), console.

- [ ] **Step 1: Record the fixture from the live server**

Run (captures the banner + first prompt, IAC-free stream, into the fixture):

```bash
mkdir -p tests/fixtures
python - <<'PY'
import socket, time
s = socket.create_connection(("multizork.icculus.org", 23), timeout=10)
s.settimeout(4)
data = b""
try:
    while len(data) < 4096:
        c = s.recv(1024)
        if not c: break
        data += c
        time.sleep(0.3)
except socket.timeout:
    pass
s.close()
open("tests/fixtures/multizork_banner.bin","wb").write(data)
print("wrote", len(data), "bytes")
PY
```
Expected: prints e.g. `wrote 220 bytes`. The bytes include the `Hello sailor!` banner and a trailing `>` prompt.

- [ ] **Step 2: Write the failing test**

Create `tests/test_term_fixture.c`:

```c
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
        term_service(&ts, &t, ZATURN_RX_BUDGET);

    assert(console_has("Hello sailor!"));
    printf("test_term_fixture: OK\n");
    return 0;
}
```

- [ ] **Step 3: Run to verify it fails, then passes**

Run:
```bash
gcc -I src tests/test_term_fixture.c src/term.c src/console.c src/keyboard.c tests/net/mock_transport.c -o tests/test_term_fixture && ./tests/test_term_fixture
```
(`keyboard.c` is linked because `term.c` references `keyboard_reset` via `term_submit_line`.)
Expected before Step 1's fixture exists: FAIL (`assert(f != NULL)`). With the fixture present: `test_term_fixture: OK`.

- [ ] **Step 4: Commit**

```bash
git add tests/fixtures/multizork_banner.bin tests/test_term_fixture.c
git commit -m "add recorded multizork fixture and deterministic replay test"
```

---

## Task 6: UART transport + vendored modem drivers (Saturn)

**Files:**
- Create: `src/net/saturn_uart16550.h` (vendored), `src/net/modem.h` (vendored)
- Create: `src/net/transport_uart.h`, `src/net/transport_uart.c`

**Interfaces:**
- Consumes: `cui_transport_t` (Task 1); vendored `saturn_uart16550.h` (`saturn_uart16550_t`, `saturn_uart_rx_ready`, `saturn_uart_getc`, `saturn_uart_putc`, `saturn_uart_read_msr`).
- Produces:
  - `cui_transport_t transport_uart_make(const saturn_uart16550_t *uart);` — builds a `cui_transport_t` whose `ctx` is the UART handle. `rx_ready`←`saturn_uart_rx_ready`; `rx_byte`←`saturn_uart_getc`; `send`←`saturn_uart_putc` loop; `is_connected`←carrier-detect (MSR DCD bit `0x80`).

- [ ] **Step 1: Vendor the two Saturn-only headers**

Copy `coup-saturn/pal/saturn/saturn_uart16550.h` → `src/net/saturn_uart16550.h` and `coup-saturn/pal/saturn/modem.h` → `src/net/modem.h`. Prepend the same provenance comment as Task 1 (source path + `git -C ../../../coup-saturn rev-parse --short HEAD`). Fix the `#include "saturn_uart16550.h"` in `modem.h` if the relative path differs (both now live in `src/net/`, so the existing bare include is correct).

- [ ] **Step 2: Write `transport_uart.h`**

```c
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
```

- [ ] **Step 3: Write `transport_uart.c`**

```c
#include "transport_uart.h"

static bool tu_rx_ready(void *ctx) {
    return saturn_uart_rx_ready((const saturn_uart16550_t*)ctx);
}
static uint8_t tu_rx_byte(void *ctx) {
    return saturn_uart_getc((const saturn_uart16550_t*)ctx);
}
static int tu_send(void *ctx, const uint8_t *data, int len) {
    const saturn_uart16550_t *u = (const saturn_uart16550_t*)ctx;
    int i;
    for (i = 0; i < len; i++)
        if (!saturn_uart_putc(u, data[i])) return i;   /* short write on failure */
    return len;
}
static bool tu_is_connected(void *ctx) {
    /* MSR bit 7 (0x80) = Data Carrier Detect */
    return (saturn_uart_read_msr((const saturn_uart16550_t*)ctx) & 0x80) != 0;
}

cui_transport_t transport_uart_make(const saturn_uart16550_t *uart) {
    cui_transport_t t;
    t.rx_ready = tu_rx_ready;
    t.rx_byte = tu_rx_byte;
    t.send = tu_send;
    t.is_connected = tu_is_connected;
    t.ctx = (void*)uart;
    return t;
}
```

> Note: `ctx` is `const`-cast to `void*` to fit the vtable; the UART callbacks only read through it plus touch device registers, matching how coup uses these `const saturn_uart16550_t*` accessors.

- [ ] **Step 4: Verify it compiles in the Saturn build**

There is no host test (UART registers are Saturn-only). Compile the Saturn project:
```bash
make
```
(from `SaturnRingLib/Projects/mojozork/`). Expected: `src/net/transport_uart.c`, `saturn_uart16550.h`, and `modem.h` compile with no errors (they are auto-globbed). If the cross toolchain is unavailable in this environment, defer to the environment that runs `make` and record the result.

- [ ] **Step 5: Commit**

```bash
git add src/net/saturn_uart16550.h src/net/modem.h src/net/transport_uart.h src/net/transport_uart.c
git commit -m "add UART cui_transport over NetLink modem (Saturn)"
```

---

## Task 7: Connection orchestration (`net_connect`, Saturn)

**Files:**
- Create: `src/net/net_connect.h`, `src/net/net_connect.c`

**Interfaces:**
- Consumes: `modem.h` (`modem_probe`, `modem_init`, `modem_dial`, `modem_hangup`, `modem_result_t`, `MODEM_CONNECT`), `saturn_uart16550.h`, `transport_uart_make` (Task 6).
- Produces (all `extern "C"`):
  - `typedef enum { NET_OK=0, NET_NO_MODEM, NET_DIAL_FAIL } net_connect_result_t;`
  - `net_connect_result_t net_connect_open(const char *dial_number);` — probes + inits the modem, dials, and on `MODEM_CONNECT` builds the module's static `cui_transport_t`. Uses `modem.h`'s long dial timeout.
  - `const cui_transport_t *net_connect_transport(void);` — the connected transport (valid after `NET_OK`).
  - `void net_connect_close(void);` — `modem_hangup`.

This module exists to keep the C-only modem/UART headers out of the C++ `main.cxx` translation unit. The caller renders progress around the (blocking) calls.

- [ ] **Step 1: Write `net_connect.h`**

```c
#ifndef NET_CONNECT_H
#define NET_CONNECT_H
#include "cui_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { NET_OK = 0, NET_NO_MODEM, NET_DIAL_FAIL } net_connect_result_t;

net_connect_result_t net_connect_open(const char *dial_number);
const cui_transport_t *net_connect_transport(void);
void net_connect_close(void);

#ifdef __cplusplus
}
#endif
#endif /* NET_CONNECT_H */
```

- [ ] **Step 2: Write `net_connect.c`**

```c
#include "net_connect.h"
#include "saturn_uart16550.h"
#include "modem.h"
#include "transport_uart.h"

static saturn_uart16550_t g_uart;
static cui_transport_t    g_transport;
static int                g_open = 0;

#define MODEM_DIAL_TIMEOUT 180000000u   /* ~60s at 28.6MHz (matches coup) */

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
```

> The `saturn_uart16550_t` is `{ uint32_t base; uint32_t stride; }`; `saturn_netlink_smpc_enable()` and `saturn_uart_detect()` are provided by the vendored `saturn_uart16550.h`. The two base/stride pairs are coup's exact NetLink probe table.

- [ ] **Step 3: Verify it compiles in the Saturn build**

```bash
make
```
Expected: `src/net/net_connect.c` compiles clean. Defer to the Saturn-build environment if the toolchain is unavailable here; record the result.

- [ ] **Step 4: Commit**

```bash
git add src/net/net_connect.h src/net/net_connect.c
git commit -m "add net_connect modem dial orchestration (Saturn)"
```

---

## Task 8: Online mode in `main.cxx` (menu, dial-entry, terminal loop)

**Files:**
- Modify: `src/main.cxx`

**Interfaces:**
- Consumes: `net_connect_open/transport/close` (Task 7), `term_init/term_service/term_submit_line`, `ZATURN_RX_BUDGET` (Tasks 3–4), and existing `render_console`, `render_keyboard`, `menu_select`, `KeyboardState`, `keyboard_*`, `saturn_keyboard_poll`, `g_pad`.

- [ ] **Step 1: Add includes and the dial-number define**

Near the top of `src/main.cxx`, after the existing includes (the file already includes `console.h`/`keyboard.h`/`saturn_keyboard.h`), add:

```c
extern "C" {
#include "term.h"
#include "net/net_connect.h"
}

#define ZATURN_DIAL_NUMBER "199403"
```

- [ ] **Step 2: Add a dial-entry helper**

Add this static function above `main()` (it reuses the on-screen keyboard, pre-filled with the default code; A/START connects, B cancels):

```c
// Edit the dial code with the on-screen keyboard. Returns false if cancelled.
static bool online_dial_entry(char *out, int maxlen) {
    KeyboardState k;
    keyboard_reset(&k);
    // pre-fill with the compiled-in default
    const char *def = ZATURN_DIAL_NUMBER;
    for (int i = 0; def[i] && k.input_len < KB_INPUT_MAX - 1; i++)
        keyboard_type_char(&k, def[i]);

    SRL::Core::Synchronize();   // consume the menu-pick edge
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind == SATURN_KEY_CHAR)           keyboard_type_char(&k, ke.ch);
        else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
        else if (ke.kind == SATURN_KEY_ENTER)     { break; }
        else if (ke.kind == SATURN_KEY_ESCAPE)    return false;

        if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
        if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
        if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
        if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
        if (g_pad->WasPressed(Button::C))     keyboard_type(&k);
        if (g_pad->WasPressed(Button::B))     keyboard_backspace(&k);
        if (g_pad->WasPressed(Button::A) ||
            g_pad->WasPressed(Button::START)) break;

        for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
        SRL::Debug::Print(2, 2, "Dial code (server number):");
        SRL::Debug::Print(2, 4, "> %s_", k.input);
        for (int r = 0; r < KB_ROWS; r++) {
            char rowbuf[KB_COLS + 1];
            for (int c = 0; c < KB_COLS; c++) rowbuf[c] = KB_LAYOUT[r][c];
            rowbuf[KB_COLS] = '\0';
            SRL::Debug::Print(4, 6 + r, "%s", rowbuf);
        }
        SRL::Debug::Print(2, 11, "C=type B=del  A/Ent=connect  Esc=back");
        SRL::Core::Synchronize();
    }
    int n = k.input_len; if (n > maxlen - 1) n = maxlen - 1;
    for (int i = 0; i < n; i++) out[i] = k.input[i];
    out[n] = '\0';
    return n > 0;
}
```

- [ ] **Step 3: Add the online-mode entry point (connect + terminal loop)**

Add this static function above `main()`:

```c
static void online_mode(void) {
    char number[KB_INPUT_MAX];
    if (!online_dial_entry(number, sizeof(number))) return;

    // ---- connect (blocking; render stage text first) ----
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
    SRL::Debug::Print(2, 4, "Dialing %s ...", number);
    SRL::Core::Synchronize();

    net_connect_result_t rc = net_connect_open(number);
    if (rc != NET_OK) {
        for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
        SRL::Debug::Print(2, 4, "%s",
            rc == NET_NO_MODEM ? "NetLink modem not found." : "Connection failed.");
        SRL::Debug::Print(2, 6, "(press any button)");
        for (;;) {
            if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
                g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) break;
            if (saturn_keyboard_poll().kind != SATURN_KEY_NONE) break;
            SRL::Core::Synchronize();
        }
        return;
    }

    const cui_transport_t *tr = net_connect_transport();
    TermState ts; term_init(&ts);
    KeyboardState k; keyboard_reset(&k);
    console_init();
    SRL::Core::Synchronize();   // consume the connect edge

    // ---- terminal loop ----
    for (;;) {
        term_service(&ts, tr, ZATURN_RX_BUDGET);   // RX -> console

        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind == SATURN_KEY_CHAR)           keyboard_type_char(&k, ke.ch);
        else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
        else if (ke.kind == SATURN_KEY_ENTER)     keyboard_submit(&k);

        if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
        if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
        if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
        if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
        if (g_pad->WasPressed(Button::C))     keyboard_type(&k);
        if (g_pad->WasPressed(Button::X))     keyboard_type_char(&k, ' ');
        if (g_pad->WasPressed(Button::B))     keyboard_backspace(&k);
        if (g_pad->WasPressed(Button::A) ||
            g_pad->WasPressed(Button::START)) keyboard_submit(&k);

        if (k.submitted) {
            term_submit_line(tr, &k);     // echo + send line; resets keyboard
        }

        if (!cui_transport_is_connected(tr)) {
            console_write("\n*** connection lost ***\n", 25);
            render_console();
            SRL::Core::Synchronize();
            break;
        }

        render_console();
        render_keyboard(k);
        SRL::Core::Synchronize();
    }
    net_connect_close();
}
```

- [ ] **Step 4: Wire the mode menu into `main()`**

In `main()`, after `int seed = title_and_seed();` and before `const char* game_file = game_select();`, insert a top-level mode choice; route "Play Online" into `online_mode()` and loop back to the menu when it returns; fall through to the existing local flow for "Play Local":

```c
    static const char *modes[] = { "Play Local (single player)", "Play Online (multizork)" };
    for (;;) {
        int mode = menu_select("Z-ATURN", modes, 2);
        if (mode == 1) { online_mode(); continue; }   // returns to menu on disconnect
        break;                                          // 0 or cancelled -> local
    }
```

(The existing `game_select()` / CD load / `mojo_boot` / `mojo_run` below stays unchanged and runs only for local mode.)

- [ ] **Step 5: Verify it compiles in the Saturn build**

```bash
make
```
Expected: clean build; `BuildDrop/` produces the disc image. Defer to the Saturn-build environment if the toolchain is unavailable here; record the result.

- [ ] **Step 6: Commit**

```bash
git add src/main.cxx
git commit -m "add Play Online mode: menu, dial entry, telnet terminal loop"
```

---

## Task 9 (optional): TCP transport + live smoke test (host)

**Files:**
- Create: `tests/net/transport_tcp.h`, `tests/net/transport_tcp.c`
- Create: `tests/net/smoke_tcp.c`

**Interfaces:**
- Produces: `cui_transport_t transport_tcp_connect(const char *host, int port);` (winsock; `ctx` holds the socket). `is_connected` returns false once the peer closes.

This is a manual, network-dependent smoke test — not part of CI. It verifies the same `term` core against the live server end-to-end on the host.

- [ ] **Step 1: Write `transport_tcp.h`**

```c
#ifndef TRANSPORT_TCP_H
#define TRANSPORT_TCP_H
#include "../../src/net/cui_transport.h"

#ifdef __cplusplus
extern "C" {
#endif
cui_transport_t transport_tcp_connect(const char *host, int port);
void transport_tcp_close(cui_transport_t *t);
#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Write `transport_tcp.c`**

```c
#include "transport_tcp.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>

typedef struct { SOCKET s; int up; } TcpCtx;

static bool tc_rx_ready(void *ctx) {
    TcpCtx *c = (TcpCtx*)ctx;
    fd_set r; FD_ZERO(&r); FD_SET(c->s, &r);
    struct timeval tv = {0,0};
    return select(0, &r, NULL, NULL, &tv) > 0;
}
static uint8_t tc_rx_byte(void *ctx) {
    TcpCtx *c = (TcpCtx*)ctx; char b = 0;
    int n = recv(c->s, &b, 1, 0);
    if (n <= 0) c->up = 0;
    return (uint8_t)b;
}
static int tc_send(void *ctx, const uint8_t *d, int len) {
    TcpCtx *c = (TcpCtx*)ctx;
    return send(c->s, (const char*)d, len, 0);
}
static bool tc_is_connected(void *ctx) { return ((TcpCtx*)ctx)->up != 0; }

cui_transport_t transport_tcp_connect(const char *host, int port) {
    cui_transport_t t = {0,0,0,0,0};
    WSADATA w; WSAStartup(MAKEWORD(2,2), &w);
    struct addrinfo hints, *res = NULL;
    char portstr[16]; sprintf(portstr, "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return t;
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) { freeaddrinfo(res); return t; }
    freeaddrinfo(res);
    TcpCtx *c = (TcpCtx*)malloc(sizeof(TcpCtx)); c->s = s; c->up = 1;
    t.rx_ready = tc_rx_ready; t.rx_byte = tc_rx_byte; t.send = tc_send;
    t.is_connected = tc_is_connected; t.ctx = c;
    return t;
}
void transport_tcp_close(cui_transport_t *t) {
    if (t->ctx) { TcpCtx *c = (TcpCtx*)t->ctx; closesocket(c->s); free(c); t->ctx = 0; }
    WSACleanup();
}
```

- [ ] **Step 3: Write the smoke driver `tests/net/smoke_tcp.c`**

```c
#include "transport_tcp.h"
#include "../../src/term.h"
#include "../../src/console.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    cui_transport_t t = transport_tcp_connect("multizork.icculus.org", 23);
    if (!t.ctx) { printf("connect failed\n"); return 1; }
    console_init();
    TermState ts; term_init(&ts);
    for (int frame = 0; frame < 120 && cui_transport_is_connected(&t); frame++) {
        term_service(&ts, &t, ZATURN_RX_BUDGET);
    }
    int n = console_line_count();
    for (int i = 0; i < n; i++) printf("%s\n", console_get_line(i));
    transport_tcp_close(&t);
    return 0;
}
```

- [ ] **Step 4: Build and run manually (requires network)**

```bash
gcc -I src tests/net/smoke_tcp.c tests/net/transport_tcp.c src/term.c src/console.c -lws2_32 -o tests/net/smoke_tcp && ./tests/net/smoke_tcp
```
Expected: prints the multizork banner including `Hello sailor!`. This confirms the identical `term` core works over a real TCP transport.

- [ ] **Step 5: Commit**

```bash
git add tests/net/transport_tcp.h tests/net/transport_tcp.c tests/net/smoke_tcp.c
git commit -m "add optional TCP transport and live smoke test"
```

---

## Task 10: Tunnel routing entry (infra — outside this repo)

**Files:**
- Modify (on the DreamPi): `/dreampi/netlink_config.ini`
- Upstream (later): `eaudunord/Netlink` → `netlink_config.ini`

This is the one infra deliverable. It is **not** committed to this repo. It is a
**temporary** manual edit on an already-running DreamPi (the one with the Netlink
tunnel image), applied until the entry is merged upstream into
[eaudunord/Netlink](https://github.com/eaudunord/Netlink) — after which DreamPi
auto-update distributes it and no manual edit is needed.

- [ ] **Step 1: Enable updates + log in**

On the DreamPi's SD card delete `/boot/noautoupdates.txt`, then SSH in (or log in at the console) as user `pi` (password `raspberry`).

- [ ] **Step 2: Add the transparent server entry**

Add to `/dreampi/netlink_config.ini`:

```ini
[server:199403]
name = MultiZork
host = multizork.icculus.org
port = 23
handler = transparent
```

`handler = transparent` is required — multizork does no AUTH handshake (verified: the raw banner arrives immediately).

- [ ] **Step 3: Restart + verify routing**

Restart the DreamPi. From a terminal, confirm that dialing `199403` reaches the multizork banner. No repo commit for this task.

- [ ] **Step 4: Upstream (later)**

Open a PR to `eaudunord/Netlink` adding the same entry so the route ships to all DreamPis via auto-update, retiring the manual step.

---

## Task 11: Manual hardware acceptance

**Files:** none (verification only).

- [ ] **Step 1: Prepare** — DreamPi updated with the `199403` transparent entry (Task 10); Saturn + NetLink modem connected to the DreamPi phone line; disc built from Task 8 (`make` → `BuildDrop/`).
- [ ] **Step 2: Boot** the disc (real hardware; Mednafen NetLink emulation is not reliable enough to trust).
- [ ] **Step 3: Title → select "Play Online"** → confirm the dial-entry field shows `199403` by default.
- [ ] **Step 4: Connect** → confirm "Dialing…" then the multizork banner (`Hello sailor!`) renders in the console.
- [ ] **Step 5: Play** — press Enter (new player), type a name via the on-screen keyboard, confirm input is echoed locally and the server responds; type a game command and confirm the round-trip.
- [ ] **Step 6: Disconnect handling** — end the session (hang up / server disconnect) and confirm "connection lost" returns to the mode menu with scrollback intact.
- [ ] **Step 7: Regression** — select "Play Local" and confirm the offline Z-machine still boots and plays unchanged.

---

## Self-Review Notes

- **Spec coverage:** transport abstraction (T1), UART transport (T6), TCP transport (T9), terminal core RX+echo (T3–T4), connect FSM (T7), mode menu + dial entry (T8), no-Saturn-save (nothing to build — server access code), recorded fixture + deterministic test (T5), tunnel `transparent` routing entry (T10), hardware acceptance (T11). All spec sections map to a task.
- **Type consistency:** `cui_transport_t`, `TermState`, `KeyboardState`, `net_connect_result_t`, and function names (`term_service`, `term_submit_line`, `transport_uart_make`, `net_connect_open/transport/close`, `ZATURN_RX_BUDGET`, `ZATURN_DIAL_NUMBER`) are used identically across tasks.
- **No unresolved placeholders:** the T7 UART probe uses coup's exact `{0x25895001,4}` / `{0x04895001,4}` base table and `saturn_netlink_smpc_enable()` + `saturn_uart_detect()` from the vendored header — concrete, not a placeholder.
- **Verify-on-vendor:** Tasks 1 and 6 record the coup source commit in each vendored header so a future coup change is a traceable re-sync, not a silent drift.
