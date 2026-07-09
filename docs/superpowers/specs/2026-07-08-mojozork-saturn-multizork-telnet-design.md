# MojoZork Saturn — Online Multizork Telnet Client — Design Spec

**Date:** 2026-07-08
**Status:** Approved (design), pending spec review
**Target engine:** SaturnRingLib (SRL)
**Relationship to prior work:** Adds a second mode to the existing
`SaturnRingLib/Projects/mojozork/` project (see
[2026-07-03-mojozork-saturn-port-design.md](2026-07-03-mojozork-saturn-port-design.md)).

## Goal

Add an **online multiplayer** mode to the Saturn MojoZork project: a NetLink
telnet terminal that connects to icculus's hosted **multizork** server
(`multizork.icculus.org:23`) so players play networked multiplayer Zork on real
Saturn hardware over a NetLink modem + DreamPi.

The Saturn is a **dumb terminal**. It runs **no Z-machine** in this mode — the
multizork server runs the game. The Saturn dials out, receives text bytes and
renders them, and sends keyboard input back. This is the same architecture the
`coup-saturn` project uses for its networked play, and it reuses coup's proven
modem/UART transport stack.

The offline single-player local Z-machine port is unchanged and coexists as a
second selectable mode on the same disc (see Mode Selection below).

## Non-goals (MVP exclusions)

- **No PPP / TCP-IP stack on the Saturn.** The DreamPi-side tunnel does the TCP
  connect; the Saturn speaks raw bytes over the serial link.
- **No TLS.** multizork is plaintext telnet on port 23. TLS on a 28 MHz SH-2 is
  infeasible and unnecessary.
- **No telnet IAC negotiation.** The server sends zero IAC options (verified —
  see Server Behavior). A tiny defensive `IAC`-skip is included, nothing more.
- **No Saturn-side save/restore.** multizork persists progress server-side via
  an **access code** it prints; the player records/re-enters it. There is no
  local save concern in this mode.
- **No scrollback paging** beyond the console ring buffer's capacity.

## Server behavior (verified 2026-07-08)

Probed `multizork.icculus.org:23` directly:

- **Zero telnet IAC negotiation.** The server never sends `IAC DO/WILL/…`. It is
  a raw, line-oriented text stream.
- **No server echo.** Sent input is not echoed back → the **terminal must local
  echo** typed characters.
- **Line-oriented.** The server processes input on newline; sending a full line
  terminated by `\r\n` (or `\n`) advances the flow.
- **Flow:** banner → prompt for an existing **access code** (returning players)
  or Enter to start as a new player → name setup → game. The access code is the
  server-side persistence mechanism.
- Output is plain ASCII/latin-1 text with `> ` prompts inline.

These findings drive two firm decisions: (1) no telnet protocol layer is needed,
and (2) the terminal does local echo + line buffering, which is exactly the
line-edit model the existing on-screen keyboard already implements.

## Transport & routing

### The chain

```
Saturn NetLink modem  --ATDT 199403-->  DreamPi (eaudunord Netlink tunnel)
                                          --TCP-->  multizork.icculus.org:23
```

The DreamPi runs the **eaudunord/Netlink** tunnel (`netlink.py`), which maps a
dialed **dial code** to a TCP `host:port` via `netlink_config.ini`. When the
Saturn dials the code, the tunnel opens the TCP connection and relays bytes.

### Handler = transparent

The tunnel's `netlink_transparent_server()` is a pure bidirectional relay
(`sock.connect((host,port))` then `recv`/`sendall` both directions) with **no
auth and no framing** — unlike the default `server` handler, which performs a
`shared_secret` AUTH handshake (what coup uses). multizork requires **no
handshake** (verified: the raw banner arrives immediately), so it uses
`handler = transparent`. Therefore, after dial + `CONNECT`, the Saturn
reads/writes the raw telnet byte stream directly.

### Dial code assignment

Dial codes are hardcoded in the **tunnel config**, not in the game. In-use codes:
`199401` (Coup), `199402`, `199404`–`199407`. **`199403` is a free gap** and is
requested for multizork (final code is whatever the upstream config assigns).

### Config deliverable (infra, not in this repo)

This is a **temporary** manual change on an already-running DreamPi (the one with
the Netlink tunnel image), applied until the entry is merged upstream into
[eaudunord/Netlink](https://github.com/eaudunord/Netlink) — after which DreamPi
auto-update distributes it. On the DreamPi: delete `/boot/noautoupdates.txt`, log
in as `pi` / `raspberry`, add the block below to `/dreampi/netlink_config.ini`,
and restart:

```ini
[server:199403]
name = MultiZork
host = multizork.icculus.org
port = 23
handler = transparent
```

Development is **not blocked** on the upstream merge: host tests hit
`multizork.icculus.org:23` directly, and on-hardware testing uses the manual
`/dreampi/netlink_config.ini` entry above.

## Approach: thin terminal over a vendored transport abstraction

The Saturn is a dumb terminal built on coup's `cui_transport_t` byte-stream
vtable. `term.c` (the terminal core) is pure C and depends only on the vtable, so
it is fully host-testable against the real server with a TCP backend and swaps to
UART on hardware. This isolates the one infra risk (dial routing) behind the
transport seam.

*Rejected alternatives:* a real PPP+TCP/IP stack on the SH-2 (enormous, high
risk, unnecessary for a text stream); an inline raw pipe with no abstraction
(UART-only, not host-testable — loses live-server verification).

## Module breakdown

All paths relative to `SaturnRingLib/Projects/mojozork/`.

| File | Status | Responsibility |
|------|--------|----------------|
| `src/main.cxx` | modify | Title menu (Play Local / Play Online); dispatch to `mojo_run` (local) or the online connect→terminal flow. |
| `src/console.{h,c}` | reuse | Word-wrap ring buffer (from local port). Renders console rows 0–21. |
| `src/keyboard.{h,c}` | reuse | On-screen keyboard + line-edit state machine (rows 24–27; input row 23). |
| `src/net/cui_transport.h` | vendor | Byte-stream vtable: `rx_ready`/`rx_byte`/`send`/`is_connected` + `ctx`. Copied from coup. |
| `src/net/saturn_uart16550.h` | vendor | Raw 16550 UART register I/O. Copied from coup. |
| `src/net/modem.h` | vendor | AT-command probe/init/dial/hangup. Copied from coup. |
| `src/net/transport_uart.{h,c}` | create | `cui_transport_t` over UART: `rx_ready`←LSR, `rx_byte`←getc, `send`←putc loop, `is_connected`←modem carrier (MSR DCD). `ctx` = UART handle. |
| `src/net/net_connect.{h,c}` | create | Connect state machine (probe→init→dial→connected); exposes a stage enum + status text for the SRL screen to render. |
| `src/term.{h,c}` | create | **Pure-C terminal core.** Drains transport RX into console; local-echoes keyboard input; on Enter sends `line + "\n"`. No SRL, no UART — takes a `cui_transport_t*`. |
| `tests/net/transport_tcp.{h,c}` | create | Host-only `cui_transport_t` over a socket (winsock/POSIX). For live/replay tests. |
| `tests/fixtures/multizork_banner.bin` | create | Recorded server bytes (banner + prompts) for deterministic replay. |
| `tests/test_term.c` | create | Host unit tests (mock transport + fixture): console assertions, line send, local echo, CR/LF, backspace. |

### Vendoring note

`cui_transport.h`, `saturn_uart16550.h`, and `modem.h` are copied from
**coup-saturn** — clone it locally (no longer vendored here):
<https://github.com/likeagfeld/coup-saturn>. In that repo they live at
`core/include/cui_transport.h`, `pal/saturn/modem.h`, and
`pal/saturn/saturn_uart16550.h`. They are standalone `static inline` headers
depending only on register access + a few SMPC/SGL bits, so they port to the SRL
project by copy. Record their source commit in a header comment so future syncs
are traceable.

## The `cui_transport_t` interface (from coup)

```c
typedef struct cui_transport {
    bool    (*rx_ready)(void* ctx);                              /* non-blocking */
    uint8_t (*rx_byte)(void* ctx);                               /* call when rx_ready */
    int     (*send)(void* ctx, const uint8_t* data, int len);    /* -1 on error */
    bool    (*is_connected)(void* ctx);                          /* NULL ⇒ assume up */
    void*   ctx;
} cui_transport_t;
```

- **UART impl (Saturn):** `rx_ready`←`saturn_uart_rx_ready` (LSR), `rx_byte`←
  `saturn_uart_getc`, `send`←`saturn_uart_putc` loop, `is_connected`←MSR carrier
  detect (DCD). `ctx` = `saturn_uart16550_t*`.
- **TCP impl (host):** non-blocking socket; `rx_ready`←`select`, `rx_byte`←1-byte
  `recv`, `send`←`send`, `is_connected`←peer-open check.

## Terminal core (`term.c`) behavior

`term_t` holds a `cui_transport_t*`, a pointer to the shared `console`, a pointer
to the shared `keyboard`/line buffer, and small state.

- `term_service(t)` — **RX pump.** While `rx_ready` and under the per-frame byte
  budget: read a byte and feed the console. Newline handling: `\n` → console
  newline; skip `\r`. Defensive: if a byte is `IAC` (0xFF), skip it and the next
  two bytes (never expected, but harmless if the server ever changes). Handle a
  server-sent backspace (0x08) as a console erase (rare).
- `term_handle_key(t, key)` — **input.** Char select → append to the line buffer
  + **local echo** into the input line. Backspace → pop line + erase echo. Enter
  → `cui_transport_send(t, line, "\n")`, echo the newline into the console, clear
  the line buffer.
- `term_connected(t)` — wraps `cui_transport_is_connected`.

### Per-frame RX budget

Drain the transport each frame, **capped at ~512 bytes** as a safety bound. At
9600 baud only ~16 bytes arrive per 60 Hz frame (16550 FIFO is 16 deep), so the
cap effectively never binds in normal play — it exists purely to prevent an
unbounded loop under a pathological burst. This mirrors coup's bounded recv.

## Mode selection & connect flow (`main.cxx`)

**Title menu:** Play Local (offline Z-machine) / Play Online (multizork telnet).

**Online path:**

1. **Dial-entry screen.** An input field pre-filled with `MOJOZORK_DIAL_NUMBER`
   ("199403"); the player accepts the default or edits it (reuses the on-screen
   keyboard/line-edit). Press connect to proceed.
2. **Connect screen** — `net_connect` FSM, one step per frame, rendering stage
   text and calling `Synchronize()`:
   `UART init → modem_probe → modem_init → modem_dial(code) → on MODEM_CONNECT → TERMINAL`.
   Stage strings: "Initializing modem…", "Dialing…", "Connected".
3. **Terminal loop** (per frame):
   1. `term_service()` — pump RX into console (capped).
   2. Read gamepad → `term_handle_key()` — keyboard nav, local echo, line send.
   3. Render: console (0–21), input line (23), keyboard (24–27), status/help row
      (shows connection state).
   4. If `!term_connected()` → "Connection lost" → return to menu (console
      scrollback preserved).
   5. `Synchronize()`.

`#define MOJOZORK_DIAL_NUMBER "199403"` — the compiled-in default; the dial-entry
field lets the player override it at runtime.

## Error handling

| Condition | Behavior |
|-----------|----------|
| Modem not detected (probe timeout) | "NetLink modem not found" → back to menu. |
| Dial fail: `NO CARRIER`/`BUSY`/`NO DIALTONE`/`NO ANSWER`/timeout | Specific message → retry / back. |
| Mid-session carrier loss or send error | "Connection lost" → menu; scrollback preserved. |
| Server sends unexpected `IAC` | Defensively skipped (IAC + 2 bytes). |

## Testing strategy

- **Host unit tests** (`gcc`/`g++`, pure C, deterministic):
  - `term.c` against a mock in-memory transport seeded from
    `tests/fixtures/multizork_banner.bin` — assert console contents after the
    banner, assert Enter sends `line+"\n"`, assert local echo, CR/LF handling,
    backspace.
  - Existing console/keyboard host tests continue to cover those modules.
- **Recorded fixture.** Capture real multizork bytes (banner, prompts) once into
  `multizork_banner.bin` so CI runs offline and deterministically.
- **Host integration test** (optional/manual, network): `transport_tcp` against
  `multizork.icculus.org:23` — assert we render "Hello sailor!" and can send a
  line. Kept optional so CI does not depend on the live server.
- **Hardware acceptance** (manual): real Saturn + NetLink + DreamPi running the
  tunnel with the multizork config entry. Mednafen's NetLink emulation is too
  limited to trust; this is the only non-automatable step.

## MVP boundary

**In:** title menu + mode dispatch; dial-entry field (defaults to `#define`);
connect FSM; terminal core (RX→console, keyboard→TX, local echo, line send,
capped RX); disconnect handling; vendored transport stack; upstream tunnel config
entry.

**Out:** PPP/TCP stack; TLS; full telnet IAC negotiation; scrollback paging;
multiple simultaneous connections; any Saturn-side save (server access code
handles persistence).

## Risks & assumptions

- **Dial routing (contained).** Requires the `[server:199403]` transparent entry
  in the tunnel config, distributed via DreamPi auto-update. The Saturn code is
  agnostic to this — the transport seam isolates it. Development proceeds via host
  TCP tests and a locally-run tunnel.
- **Vendored driver portability.** The coup UART/modem headers are assumed to
  build in the SRL project by copy; verify early (a minimal dial-and-echo spike
  on hardware/emulator) before building the full terminal.
- **Baud/DTE settings.** Reuse coup's proven modem init and baud values rather
  than re-deriving them.

## Deliverables checklist

- [ ] Vendor `cui_transport.h`, `saturn_uart16550.h`, `modem.h` (record source commit).
- [ ] `transport_uart.{h,c}`, `transport_tcp.{h,c}`.
- [ ] `term.{h,c}` (pure C).
- [ ] `net_connect.{h,c}` (connect FSM).
- [ ] `main.cxx`: title menu, dial-entry screen, online connect→terminal flow.
- [ ] `MOJOZORK_DIAL_NUMBER` define.
- [ ] Recorded fixture + `test_term.c` host tests.
- [ ] Add the `[server:199403]` entry to `/dreampi/netlink_config.ini` on the DreamPi (temporary), then PR it upstream to [eaudunord/Netlink](https://github.com/eaudunord/Netlink).
- [ ] Manual hardware acceptance pass.
