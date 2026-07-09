# MojoZork → Sega Saturn (SaturnRingLib) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Zork I fully playable on the Sega Saturn — story loaded from CD, scrolling text output, and interactive input via an on-screen gamepad keyboard — by wrapping the MojoZork Z-Machine core with a SaturnRingLib frontend.

**Architecture:** Keep `mojozork.c` (the Z-Machine v3 interpreter) as C with a handful of `#if defined(MOJOZORK_SATURN)` guards. A C wrapper TU (`mojozork_saturn.c`) `#include`s it (mirroring the libretro core's single-TU pattern) to reach its file-static symbols and exposes `extern "C"` boot/run entry points. A C++ `main.cxx` implements the SRL frontend and the output/input/error hooks. Two pure-logic modules — text console (word-wrap ring buffer) and on-screen keyboard (cursor/edit state machine) — carry no SRL dependency and are unit-tested on the host with native `g++`.

**Tech Stack:** SaturnRingLib (C++), MojoZork (C), MSYS2 mingw64 `gcc`/`g++` (host tests), `sh2eb-elf` cross toolchain (Saturn build), Mednafen (emulator).

## Global Constraints

- **Project directory:** `SaturnRingLib/Projects/mojozork/` (all paths below are relative to it unless noted). This directory is its own git repo — commit port work here.
- **Build glob:** the makefile compiles `src/*.c` and `src/*.cxx`. Therefore `mojozork.c` MUST stay at the project root (NOT in `src/`) so it is compiled only via `#include` from `src/mojozork_saturn.c` (avoids duplicate symbols).
- **Integer types (from `mojozork.c`):** `uint8`=`uint8_t`, `uint32`=`uint32_t`, `sint16`=`int16_t`, `uintptr`=`size_t`. Hook signatures MUST match these exactly.
- **Screen budget:** 40 columns × 28 rows (SRL `Debug::Print` cell grid). Layout: rows 0–21 console, row 23 input line, rows 24–27 keyboard, row 28 help.
- **Host test compiler:** `/c/msys64/mingw64/bin/g++` (C++17) and `gcc`. Cross toolchain lives under `SaturnRingLib/Compiler/`.
- **MVP exclusions:** save/restore (stubbed to fail gracefully), Z-Machine v4+, sound, status bar (optional).
- **No change to interpreter logic:** only additive `#if defined(MOJOZORK_SATURN)` guards and one new `ZMachineState` field. The host build of `mojozork.c` must keep compiling unchanged.

---

## File Structure

| File | Status | Responsibility |
|------|--------|----------------|
| `mojozork.c` (root) | modify | Z-Machine core; add Saturn guards + `readline` hook field. |
| `src/console.h` / `src/console.c` | create | Word-wrap ring buffer. Pure C, no SRL. Host-tested. |
| `src/keyboard.h` / `src/keyboard.c` | create | On-screen keyboard state machine. Pure C, no SRL. Host-tested. |
| `src/saturn_glue.h` | create | `extern "C"` decls: hooks (`saturn_writestr/readline/die`) + entry points (`mojo_boot/mojo_run`). |
| `src/mojozork_saturn.c` | create | `#define MOJOZORK_SATURN` + `#include "../mojozork.c"`; defines `mojo_boot`/`mojo_run` using the core's statics. |
| `src/main.cxx` | replace | SRL frontend: CD load, title/seed screen, hook impls, console + keyboard rendering, main entry. |
| `cd/data/ZORK1.DAT` | create | Copy of `zork1.dat` (uppercase 8.3 CD name). |
| `makefile` | modify | Set `CD_NAME = mojozork`. |
| `tests/test_console.c` | create | Host unit tests for console. |
| `tests/test_keyboard.c` | create | Host unit tests for keyboard. |

---

## Task 1: Text console module (word-wrap ring buffer)

**Files:**
- Create: `src/console.h`, `src/console.c`
- Test: `tests/test_console.c`

**Interfaces:**
- Produces:
  - `void console_init(void)`
  - `void console_write(const char *str, unsigned int len)` — append, wrapping at `CONSOLE_COLS` and on `\n`
  - `int console_line_count(void)` — completed lines + current partial line
  - `const char *console_get_line(int index)` — line by absolute index `[0, count)`, newest last; pointer valid until next `console_write`

- [ ] **Step 1: Write the failing test**

Create `tests/test_console.c`:

```c
#include "../src/console.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

static void write_str(const char *s) { console_write(s, (unsigned int) strlen(s)); }

int main(void) {
    /* newline flushes a line */
    console_init();
    write_str("hello\n");
    assert(console_line_count() == 1);
    assert(strcmp(console_get_line(0), "hello") == 0);

    /* text without newline is the current partial line */
    console_init();
    write_str("west of house");
    assert(console_line_count() == 1);
    assert(strcmp(console_get_line(0), "west of house") == 0);

    /* hard-wrap a single long word at CONSOLE_COLS */
    console_init();
    { char buf[60]; memset(buf, 'a', 45); buf[45] = '\0'; write_str(buf); }
    assert(console_line_count() == 2);
    assert((int) strlen(console_get_line(0)) == CONSOLE_COLS);
    assert(strcmp(console_get_line(1), "aaaaa") == 0);   /* 45 - 40 */

    /* word-wrap at a space boundary, dropping the wrapping space */
    console_init();
    write_str("aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd eeee");
    /* 40-col width: "aaaaaaaaaa bbbbbbbbbb cccccccccc" is 32 chars, next word
       "dddddddddd" would reach 43 -> wrap before it. */
    assert(console_line_count() == 2);
    assert(strcmp(console_get_line(0), "aaaaaaaaaa bbbbbbbbbb cccccccccc") == 0);
    assert(strcmp(console_get_line(1), "dddddddddd eeee") == 0);

    printf("test_console: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails (no console.c yet)**

Run: `/c/msys64/mingw64/bin/gcc -std=c11 -I src tests/test_console.c src/console.c -o /tmp/test_console`
Expected: FAIL — `src/console.c: No such file` / undefined references.

- [ ] **Step 3: Write `src/console.h`**

```c
#ifndef CONSOLE_H
#define CONSOLE_H

#define CONSOLE_COLS 40
#define CONSOLE_MAX_LINES 128

#ifdef __cplusplus
extern "C" {
#endif

void console_init(void);
void console_write(const char *str, unsigned int len);
int  console_line_count(void);
const char *console_get_line(int index);

#ifdef __cplusplus
}
#endif
#endif /* CONSOLE_H */
```

- [ ] **Step 4: Write `src/console.c`**

```c
#include "console.h"
#include <string.h>

static char lines[CONSOLE_MAX_LINES][CONSOLE_COLS + 1];
static int  head;    /* index of oldest stored line */
static int  count;   /* number of completed lines in the ring */
static char cur[CONSOLE_COLS + 1];
static int  curlen;

static void push_line(const char *s, int len) {
    int slot;
    if (len > CONSOLE_COLS) len = CONSOLE_COLS;
    slot = (head + count) % CONSOLE_MAX_LINES;
    memcpy(lines[slot], s, (size_t) len);
    lines[slot][len] = '\0';
    if (count < CONSOLE_MAX_LINES) count++;
    else head = (head + 1) % CONSOLE_MAX_LINES;   /* overwrite oldest */
}

static void flush_cur(void) {
    push_line(cur, curlen);
    curlen = 0;
    cur[0] = '\0';
}

void console_init(void) {
    head = 0; count = 0; curlen = 0; cur[0] = '\0';
}

void console_write(const char *str, unsigned int len) {
    unsigned int i;
    for (i = 0; i < len; i++) {
        char c = str[i];
        if (c == '\r') continue;
        if (c == '\n') { flush_cur(); continue; }
        if (c == ' ' && curlen >= CONSOLE_COLS) { flush_cur(); continue; }
        if (c != ' ' && curlen >= CONSOLE_COLS) {
            int sp = -1, j;
            for (j = curlen - 1; j >= 0; j--) { if (cur[j] == ' ') { sp = j; break; } }
            if (sp > 0) {
                int carry = curlen - (sp + 1);
                push_line(cur, sp);                 /* line before the space */
                memmove(cur, cur + sp + 1, (size_t) carry);
                curlen = carry;
                cur[curlen] = '\0';
            } else {
                flush_cur();                        /* no space: hard wrap */
            }
        }
        cur[curlen++] = c;
        cur[curlen] = '\0';
    }
}

int console_line_count(void) {
    return count + (curlen > 0 ? 1 : 0);
}

const char *console_get_line(int index) {
    if (index < count) return lines[(head + index) % CONSOLE_MAX_LINES];
    return cur;   /* in-progress line is the last */
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `/c/msys64/mingw64/bin/gcc -std=c11 -I src tests/test_console.c src/console.c -o /tmp/test_console && /tmp/test_console`
Expected: `test_console: OK`

- [ ] **Step 6: Commit**

```bash
git add src/console.h src/console.c tests/test_console.c
git commit -m "feat(saturn): word-wrap text console ring buffer with host tests"
```

---

## Task 2: On-screen keyboard module (cursor/edit state machine)

**Files:**
- Create: `src/keyboard.h`, `src/keyboard.c`
- Test: `tests/test_keyboard.c`

**Interfaces:**
- Produces:
  - `KeyboardState` struct: `int cursor_col, cursor_row; char input[KB_INPUT_MAX]; int input_len; int submitted;`
  - `extern const char KB_LAYOUT[KB_ROWS][KB_COLS + 1]`
  - `void keyboard_reset(KeyboardState*)`
  - `void keyboard_move(KeyboardState*, int dcol, int drow)` — wraps around edges
  - `char keyboard_current_char(const KeyboardState*)`
  - `void keyboard_type(KeyboardState*)` — append current char
  - `void keyboard_backspace(KeyboardState*)`
  - `void keyboard_submit(KeyboardState*)`

- [ ] **Step 1: Write the failing test**

Create `tests/test_keyboard.c`:

```c
#include "../src/keyboard.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

int main(void) {
    KeyboardState k;
    keyboard_reset(&k);
    assert(k.cursor_col == 0 && k.cursor_row == 0);
    assert(keyboard_current_char(&k) == 'a');   /* KB_LAYOUT[0][0] */

    /* type 'a' */
    keyboard_type(&k);
    assert(k.input_len == 1 && strcmp(k.input, "a") == 0);

    /* move right twice -> 'c', type it */
    keyboard_move(&k, 1, 0);
    keyboard_move(&k, 1, 0);
    assert(keyboard_current_char(&k) == 'c');
    keyboard_type(&k);
    assert(strcmp(k.input, "ac") == 0);

    /* backspace */
    keyboard_backspace(&k);
    assert(strcmp(k.input, "a") == 0);

    /* left wraps from col 0 to col KB_COLS-1 */
    keyboard_reset(&k);
    keyboard_move(&k, -1, 0);
    assert(k.cursor_col == KB_COLS - 1);

    /* up wraps from row 0 to row KB_ROWS-1 */
    keyboard_reset(&k);
    keyboard_move(&k, 0, -1);
    assert(k.cursor_row == KB_ROWS - 1);

    /* submit */
    keyboard_submit(&k);
    assert(k.submitted == 1);

    printf("test_keyboard: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `/c/msys64/mingw64/bin/gcc -std=c11 -I src tests/test_keyboard.c src/keyboard.c -o /tmp/test_keyboard`
Expected: FAIL — `src/keyboard.c: No such file` / undefined references.

- [ ] **Step 3: Write `src/keyboard.h`**

```c
#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KB_COLS 10
#define KB_ROWS 4
#define KB_INPUT_MAX 64

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  cursor_col;
    int  cursor_row;
    char input[KB_INPUT_MAX];
    int  input_len;
    int  submitted;
} KeyboardState;

extern const char KB_LAYOUT[KB_ROWS][KB_COLS + 1];

void keyboard_reset(KeyboardState *k);
void keyboard_move(KeyboardState *k, int dcol, int drow);
char keyboard_current_char(const KeyboardState *k);
void keyboard_type(KeyboardState *k);
void keyboard_backspace(KeyboardState *k);
void keyboard_submit(KeyboardState *k);

#ifdef __cplusplus
}
#endif
#endif /* KEYBOARD_H */
```

- [ ] **Step 4: Write `src/keyboard.c`**

```c
#include "keyboard.h"

const char KB_LAYOUT[KB_ROWS][KB_COLS + 1] = {
    "abcdefghij",
    "klmnopqrst",
    "uvwxyz0123",
    "456789.,' "
};

void keyboard_reset(KeyboardState *k) {
    k->cursor_col = 0;
    k->cursor_row = 0;
    k->input_len = 0;
    k->input[0] = '\0';
    k->submitted = 0;
}

void keyboard_move(KeyboardState *k, int dcol, int drow) {
    k->cursor_col = (k->cursor_col + dcol + KB_COLS) % KB_COLS;
    k->cursor_row = (k->cursor_row + drow + KB_ROWS) % KB_ROWS;
}

char keyboard_current_char(const KeyboardState *k) {
    return KB_LAYOUT[k->cursor_row][k->cursor_col];
}

void keyboard_type(KeyboardState *k) {
    if (k->input_len < KB_INPUT_MAX - 1) {
        k->input[k->input_len++] = keyboard_current_char(k);
        k->input[k->input_len] = '\0';
    }
}

void keyboard_backspace(KeyboardState *k) {
    if (k->input_len > 0) {
        k->input[--k->input_len] = '\0';
    }
}

void keyboard_submit(KeyboardState *k) {
    k->submitted = 1;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `/c/msys64/mingw64/bin/gcc -std=c11 -I src tests/test_keyboard.c src/keyboard.c -o /tmp/test_keyboard && /tmp/test_keyboard`
Expected: `test_keyboard: OK`

- [ ] **Step 6: Commit**

```bash
git add src/keyboard.h src/keyboard.c tests/test_keyboard.c
git commit -m "feat(saturn): on-screen keyboard state machine with host tests"
```

---

## Task 3: MojoZork core integration edits

**Files:**
- Modify: `mojozork.c` (project root)

**Interfaces:**
- Produces: a new `ZMachineState` field `void (*readline)(char *buf, int maxlen);` and a `MOJOZORK_SATURN`-gated build that excludes the stdio `main`, routes `read` through `readline`, and stubs save/restore/reseed. Consumed by Task 4.

All edits are additive and guarded so the host build is unaffected.

- [ ] **Step 1: Add the `readline` hook field**

In `mojozork.c`, find (~line 125):

```c
    void (*writestr)(const char *str, const uintptr slen);
```

Add immediately after it:

```c
    void (*readline)(char *buf, int maxlen);
```

- [ ] **Step 2: Exclude the stdio `main`/`die`/`writestr_stdio` on Saturn**

Find (~line 2094):

```c
#if !defined(MULTIZORK) && !defined(MOJOZORK_LIBRETRO)
```

Replace with:

```c
#if !defined(MULTIZORK) && !defined(MOJOZORK_LIBRETRO) && !defined(MOJOZORK_SATURN)
```

- [ ] **Step 3: Route the `read` opcode through the hook**

Find (~lines 1302–1306):

```c
    } else if (script == NULL) {
        FIXME("fgets isn't really the right solution here.");
        if (!fgets((char *) input, inputlen, stdin)) {
            GState->die("EOF or error on stdin during read");
        }
    } else {
```

Replace with:

```c
    } else if (script == NULL) {
#if defined(MOJOZORK_SATURN)
        GState->readline((char *) input, (int) inputlen);
#else
        FIXME("fgets isn't really the right solution here.");
        if (!fgets((char *) input, inputlen, stdin)) {
            GState->die("EOF or error on stdin during read");
        }
#endif
    } else {
```

- [ ] **Step 4: Guard the time-based RNG reseed**

Find (~line 1113):

```c
        random_seed = (int) time(NULL);
```

Replace with:

```c
#if defined(MOJOZORK_SATURN)
        random_seed = 0x2A6D;   /* entropy comes from the title-screen frame count */
#else
        random_seed = (int) time(NULL);
#endif
```

- [ ] **Step 5: Stub save/restore on Saturn (no filesystem writes)**

Find the body of `opcode_save` (starts ~line 1423). Immediately after the opening `{`, insert:

```c
#if defined(MOJOZORK_SATURN)
    doBranch(0);   /* saving not supported yet on Saturn */
    return;
#endif
```

Find the body of `opcode_restore` (starts ~line 1443). Immediately after the opening `{`, insert:

```c
#if defined(MOJOZORK_SATURN)
    doBranch(0);   /* no save to restore on Saturn */
    return;
#endif
```

- [ ] **Step 6: Verify the host build still compiles (guards are inert off-Saturn)**

Run: `/c/msys64/mingw64/bin/gcc -std=c11 -w mojozork.c -o /tmp/mojozork_host`
Expected: PASS (compiles). This proves the edits didn't break the untouched interpreter path.

- [ ] **Step 7: Commit**

```bash
git add mojozork.c
git commit -m "feat(saturn): guard mojozork.c for a Saturn frontend (readline hook, stubs)"
```

---

## Task 4: C wrapper TU + glue header

**Files:**
- Create: `src/saturn_glue.h`, `src/mojozork_saturn.c`

**Interfaces:**
- Consumes: `mojozork.c` statics (`GState`, `random_seed`, `initStory`, `runInstruction`) via `#include`; hook impls `saturn_writestr/readline/die` from Task 5.
- Produces (for `main.cxx`):
  - `void mojo_boot(uint8_t *story, uint32_t len, int seed)`
  - `void mojo_run(void)`

> Verification of this task's compilation happens in Task 6 (full cross build), since it depends on the SRL headers reached through the build's include paths.

- [ ] **Step 1: Write `src/saturn_glue.h`**

```c
#ifndef SATURN_GLUE_H
#define SATURN_GLUE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hooks implemented in main.cxx (C++), called by the Z-Machine core (C).
   Signatures MUST match the ZMachineState function pointers:
     writestr: void(*)(const char*, size_t)   [uintptr == size_t]
     readline: void(*)(char*, int)
     die:      void(*)(const char*, ...)                                    */
void saturn_writestr(const char *str, size_t slen);
void saturn_readline(char *buf, int maxlen);
void saturn_die(const char *fmt, ...);

/* Entry points implemented in mojozork_saturn.c, called by main.cxx. */
void mojo_boot(uint8_t *story, uint32_t len, int seed);
void mojo_run(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_GLUE_H */
```

- [ ] **Step 2: Write `src/mojozork_saturn.c`**

```c
/* Single-TU wrapper: pull in the Z-Machine core so we can reach its
   file-static symbols (mirrors mojozork-libretro.c). MOJOZORK_SATURN excludes
   the core's stdio main/die and enables the Saturn guards. */
#define MOJOZORK_SATURN 1
#include "../mojozork.c"

#include "saturn_glue.h"

static ZMachineState g_zmachine;

void mojo_boot(uint8_t *story, uint32_t len, int seed) {
    GState = &g_zmachine;
    GState->startup_script = NULL;
    GState->die      = saturn_die;
    GState->writestr = saturn_writestr;
    GState->readline = saturn_readline;
    random_seed = (sint32) seed;
    initStory("ZORK1.DAT", story, len);
}

void mojo_run(void) {
    while (!GState->quit) {
        runInstruction();
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add src/saturn_glue.h src/mojozork_saturn.c
git commit -m "feat(saturn): C wrapper TU exposing mojo_boot/mojo_run + glue header"
```

---

## Task 5: SRL frontend (`main.cxx`)

**Files:**
- Replace: `src/main.cxx` (currently the copied gamepad-sample placeholder)

**Interfaces:**
- Consumes: `console.h`, `keyboard.h`, `saturn_glue.h`; SRL (`Core`, `Debug`, `Input::Digital`, `Cd::File`, `Memory::HighWorkRam`).
- Produces: `saturn_writestr`, `saturn_readline`, `saturn_die` (the hooks), and `main()`.

> Verified in Task 6 (full build + emulator).

- [ ] **Step 1: Replace `src/main.cxx` entirely**

```cpp
#include <srl.hpp>

extern "C" {
#include "console.h"
#include "keyboard.h"
#include "saturn_glue.h"
}

using namespace SRL::Types;
using Button = SRL::Input::Digital::Button;

// One shared gamepad on port 0, used by the readline hook.
static SRL::Input::Digital *g_pad = nullptr;

// ---- rendering -------------------------------------------------------------

static const int CONSOLE_ROWS = 22;   // rows 0..21

static void render_console(void) {
    int total = console_line_count();
    int start = (total > CONSOLE_ROWS) ? (total - CONSOLE_ROWS) : 0;
    for (int r = 0; r < CONSOLE_ROWS; r++) {
        SRL::Debug::PrintClearLine(r);
        int li = start + r;
        if (li < total) {
            SRL::Debug::Print(0, r, "%s", console_get_line(li));
        }
    }
}

static void render_keyboard(const KeyboardState &k) {
    SRL::Debug::PrintClearLine(23);
    SRL::Debug::Print(0, 23, "> %s_", k.input);
    for (int r = 0; r < KB_ROWS; r++) {
        char rowbuf[KB_COLS * 2 + 1];
        int p = 0;
        for (int c = 0; c < KB_COLS; c++) {
            rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
            rowbuf[p++] = KB_LAYOUT[r][c];
        }
        rowbuf[p] = '\0';
        SRL::Debug::PrintClearLine(24 + r);
        SRL::Debug::Print(2, 24 + r, "%s", rowbuf);
    }
    SRL::Debug::Print(0, 28, "A=type B=del START=enter");
}

// ---- hooks (extern "C" so the C core can call them) ------------------------

extern "C" void saturn_writestr(const char *str, size_t slen) {
    console_write(str, (unsigned int) slen);
}

extern "C" void saturn_readline(char *buf, int maxlen) {
    KeyboardState k;
    keyboard_reset(&k);
    while (!k.submitted) {
        if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
        if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
        if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
        if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
        if (g_pad->WasPressed(Button::A))     keyboard_type(&k);
        if (g_pad->WasPressed(Button::B))     keyboard_backspace(&k);
        if (g_pad->WasPressed(Button::START)) keyboard_submit(&k);
        render_console();
        render_keyboard(k);
        SRL::Core::Synchronize();
    }
    int n = k.input_len;
    if (n > maxlen - 2) n = maxlen - 2;
    for (int i = 0; i < n; i++) buf[i] = k.input[i];
    buf[n]     = '\n';   // opcode_read strips this, matching fgets contract
    buf[n + 1] = '\0';
}

extern "C" void saturn_die(const char *fmt, ...) {
    (void) fmt;
    console_write("\n*** interpreter halted ***\n", 28);
    while (1) { render_console(); SRL::Core::Synchronize(); }
}

// ---- boot ------------------------------------------------------------------

// Title screen: wait for START; use elapsed frames as an RNG seed.
static int title_and_seed(void) {
    int frames = 0;
    while (!g_pad->WasPressed(Button::START)) {
        SRL::Debug::Print(6, 12, "M O J O Z O R K   ---   Z O R K   I");
        SRL::Debug::Print(12, 15, "Press START to begin");
        SRL::Core::Synchronize();
        frames++;
    }
    return frames | 1;   // avoid a zero seed
}

int main(void) {
    SRL::Core::Initialize(HighColor::Colors::Black);
    console_init();

    static SRL::Input::Digital pad(0);
    g_pad = &pad;

    int seed = title_and_seed();

    // Load the story from CD into high work RAM.
    SRL::Cd::File file("ZORK1.DAT");
    file.Open();
    uint32_t len = (uint32_t) file.Size.Bytes;
    uint8_t *story = (uint8_t *) SRL::Memory::HighWorkRam::Malloc(len);
    file.Read((int32_t) len, story);

    mojo_boot(story, len, seed);
    mojo_run();

    // Game ended: keep the final screen up.
    while (1) { render_console(); SRL::Core::Synchronize(); }
    return 0;
}
```

- [ ] **Step 2: Commit**

```bash
git add src/main.cxx
git commit -m "feat(saturn): SRL frontend - CD load, hooks, console/keyboard render"
```

---

## Task 6: Build the ISO (assets + makefile + cross build)

**Files:**
- Create: `cd/data/ZORK1.DAT`
- Modify: `makefile` (`CD_NAME`)

- [ ] **Step 1: Copy the story into the CD image tree**

Run: `cp zork1.dat cd/data/ZORK1.DAT && ls -l cd/data/ZORK1.DAT`
Expected: a 92160-byte `ZORK1.DAT`.

- [ ] **Step 2: Set the disc name in `makefile`**

Find `CD_NAME = Input_gamepad` and replace with `CD_NAME = mojozork`.

- [ ] **Step 3: Remove the placeholder gamepad assets from the CD tree**

Run: `rm -f cd/data/GAMEPAD.TGA cd/data/WIRE.TGA cd/data/ARROW.TGA cd/data/BUTTON.TGA && ls cd/data/`
Expected: only `ZORK1.DAT` remains (plus any SRL-required files the build adds).

- [ ] **Step 4: Cross build**

Run: `cmd //c compile.bat` (from the project directory).
Expected: build completes and produces the ISO/CUE under `BuildDrop/`. If it fails:
- **Missing stdio symbols** (`fopen`, `fwrite`, `fread`, `rewind`) at link — confirm the Task 3 save/restore guards are present and that `MOJOZORK_SATURN` is defined before the `#include "../mojozork.c"`.
- **Duplicate symbols for the core** — confirm `mojozork.c` is at the project ROOT (not in `src/`); only `src/mojozork_saturn.c` should reference it.
- **Hook signature mismatch** on the `GState->writestr`/`readline`/`die` assignments — reconcile against `size_t`/`char*`/varargs in `saturn_glue.h`.

- [ ] **Step 5: Commit**

```bash
git add makefile cd/data/ZORK1.DAT
git commit -m "build(saturn): add ZORK1.DAT asset and set disc name; builds ISO"
```

---

## Task 7: Emulator smoke test + canned-script debug toggle

**Files:**
- Modify: `src/main.cxx` (add a compile-time canned-script toggle)

- [ ] **Step 1: Add a canned-script toggle for input-free smoke testing**

At the top of `src/main.cxx` (after the includes) add:

```cpp
// Define to auto-drive a fixed sequence of commands (no keyboard needed),
// for smoke-testing rendering in the emulator. Leave undefined for real play.
// #define MOJO_CANNED_SCRIPT
#ifdef MOJO_CANNED_SCRIPT
static const char *kCanned[] = { "open mailbox", "read leaflet", "go north", "look" };
static int kCannedIdx = 0;
#endif
```

Then at the very top of `saturn_readline`'s body, before the keyboard loop:

```cpp
#ifdef MOJO_CANNED_SCRIPT
    if (kCannedIdx < (int)(sizeof(kCanned)/sizeof(kCanned[0]))) {
        const char *s = kCanned[kCannedIdx++];
        int i = 0;
        for (; s[i] && i < maxlen - 2; i++) buf[i] = s[i];
        buf[i] = '\n'; buf[i + 1] = '\0';
        // Show the auto-typed command briefly.
        for (int f = 0; f < 60; f++) {
            render_console();
            SRL::Debug::PrintClearLine(23);
            SRL::Debug::Print(0, 23, "> %s", s);
            SRL::Core::Synchronize();
        }
        return;
    }
#endif
```

- [ ] **Step 2: Build with the canned script enabled**

Temporarily uncomment `#define MOJO_CANNED_SCRIPT`, then run: `cmd //c compile.bat`
Expected: builds.

- [ ] **Step 3: Run in the emulator and observe**

Run: `cmd //c run_with_mednafen.bat`
Expected: title screen → after START, the West-of-House intro text renders in the console area, and the four canned commands auto-run (mailbox/leaflet/north/look) with their responses scrolling. This proves CD load, interpreter execution, `writestr`→console, and the `read`→resume cycle end-to-end.

- [ ] **Step 4: Re-comment the toggle and rebuild for interactive play**

Re-comment `#define MOJO_CANNED_SCRIPT`, run `cmd //c compile.bat`, then `cmd //c run_with_mednafen.bat`.
Expected: after START, drive the on-screen keyboard with the D-pad, type `open mailbox`, press START to submit, and see the game respond. Manual playthrough of the opening confirms interactive input.

- [ ] **Step 5: Commit**

```bash
git add src/main.cxx
git commit -m "test(saturn): canned-script toggle; verified intro + input in Mednafen"
```

---

## Self-Review Notes

- **Spec coverage:** engine choice (SRL) → whole plan; three core edits → Task 3 (plus honest save/restore + reseed guards); console → Task 1; keyboard → Task 2; glue/hooks → Tasks 4–5; CD load → Task 5/6; memory (HighWorkRam, bypass `loadStory` malloc) → Task 4/5; deferred save/RNG/status bar → Task 3 stubs + Task 5 seed (status bar intentionally omitted for MVP); build/test strategy → Tasks 6–7.
- **Types:** hook signatures use `size_t`/`char*`/varargs matching `uintptr`/`ZMachineState` pointers; `mojo_boot` uses `uint8_t*`/`uint32_t`.
- **Naming consistency:** `console_write/console_line_count/console_get_line`, `keyboard_move/type/backspace/submit`, `saturn_writestr/readline/die`, `mojo_boot/mojo_run` used identically across tasks.
- **Known follow-ups (out of MVP scope):** status-bar row, save to Saturn backup RAM, scrollback paging, `[MORE]` prompts for long output bursts.
