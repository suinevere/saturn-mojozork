# Boxed Menus and Numbered Selection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every menu in the Saturn client a centered box with identical
chrome, and show digit selection only when a keyboard is the active input device.

**Architecture:** The pure layout logic — box sizing arithmetic and digit-to-row
mapping — moves into a new SRL-free C unit, `saturn/src/menu_layout.c`, which is
unit-tested on the host. `main.cxx` keeps all drawing and input handling and
calls into that unit. This is what makes any of this testable: `main.cxx` depends
on SRL, `g_pad`, and the VDP2 registers, so it cannot be compiled off-target.

**Tech Stack:** C99 for the layout unit, C++ for `main.cxx`, SaturnRingLib (SRL)
for drawing and VDP2 windowing, host gcc for the unit tests.

## Global Constraints

- Digits render only when `!g_kbd_visible` (`main.cxx:205`). No new device-tracking state.
- The `"N) "` prefix occupies **3 columns unconditionally**, drawn or not, so nothing shifts when the player switches input device. How those columns are found differs by menu:
  - **Auto-sized menus** (`menu_select`, `pick_slot_and_name`, `menu_confirm`) add `MENU_DIGIT_COLS` to the content width passed to `menu_box_fit`.
  - **The six option pages** hardcode their box dimensions and are **not resized**. The 3 columns come out of interior padding they already have, and their value columns shift right by `MENU_DIGIT_COLS`. Task 7 shows the measurements proving this fits.
- Screen is 40 columns x 28 rows. Every box must fit inside it.
- Content origin inside a box stays `(x0 + 2, y0 + 3)` — the convention `menu_frame` (`main.cxx:1319`) already documents.
- `menu_frame` itself is **not** modified.
- `toc_dump_page` (`main.cxx:1682`) is **not** boxed.
- Digit selection is suppressed wherever a text field is being edited.
- Shift+digit is detected by the shifted character (`!@#$%^&*(` for 1-9), because `SaturnKeyEvent` is `{kind, ch}` (`saturn_keyboard.h:42`) with no modifier flag. US layout assumed.
- Do **not** run `saturn/compile.bat` — the user runs the Saturn build. Host unit tests are yours to run.

### Deviation from the spec

The spec sketched `menu_row_digit(const SaturnKeyEvent &ke, ...)`. This plan
takes `char ch` instead, so the function lives in the SRL-free unit and is
host-testable. Callers in `main.cxx` unwrap `ke.ch` themselves after checking
`ke.kind == SATURN_KEY_CHAR`. Same behavior, testable boundary.

---

### Task 1: Box-sizing arithmetic

**Files:**
- Create: `saturn/src/menu_layout.h`
- Create: `saturn/src/menu_layout.c`
- Test: `saturn/tests/test_menu_layout.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `void menu_box_fit(const char *title, int content_w, int rows, int *x0, int *y0, int *w, int *h);` and the constants `MENU_SCREEN_COLS` (40), `MENU_SCREEN_ROWS` (28), `MENU_DIGIT_COLS` (3).

`saturn/makefile:30` globs `src/` for `*.c`, so no makefile change is needed for
the Saturn build to pick this up.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_menu_layout.c`:

```c
#include "../src/menu_layout.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

static void test_fit_centers_a_normal_box(void) {
    int x0, y0, w, h;
    /* title 5 cols, content 20 cols, 5 rows -> w = 20+4, h = 5+4 */
    menu_box_fit("SOUND", 20, 5, &x0, &y0, &w, &h);
    assert(w == 24);
    assert(h == 9);
    assert(x0 == (40 - 24) / 2);   /* 8 */
    assert(y0 == (28 - 9) / 2);    /* 9 */
}

static void test_fit_widens_for_a_long_title(void) {
    int x0, y0, w, h;
    /* title is 22 cols and beats the 4-col content */
    menu_box_fit("A VERY LONG TITLE HERE", 4, 2, &x0, &y0, &w, &h);
    assert(w == 26);
    assert(h == 6);
    assert(x0 == 7);
    assert(y0 == 11);
}

static void test_fit_clamps_to_the_screen(void) {
    int x0, y0, w, h;
    menu_box_fit("X", 50, 40, &x0, &y0, &w, &h);
    assert(w == 40);            /* clamped to MENU_SCREEN_COLS */
    assert(h == 28);            /* clamped to MENU_SCREEN_ROWS */
    assert(x0 == 0);
    assert(y0 == 0);
}

static void test_fit_never_goes_negative(void) {
    int x0, y0, w, h;
    menu_box_fit("", 0, 0, &x0, &y0, &w, &h);
    assert(w >= 4);             /* two borders + two pads */
    assert(h >= 4);
    assert(x0 >= 0 && y0 >= 0);
    assert(x0 + w <= 40);
    assert(y0 + h <= 28);
}

int main(void) {
    test_fit_centers_a_normal_box();
    test_fit_widens_for_a_long_title();
    test_fit_clamps_to_the_screen();
    test_fit_never_goes_negative();
    printf("test_menu_layout: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
gcc -o /tmp/tml saturn/tests/test_menu_layout.c saturn/src/menu_layout.c -I saturn/src
```

Expected: FAIL — `saturn/src/menu_layout.c: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/menu_layout.h`:

```c
#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

/* Pure layout arithmetic for the menu system. Deliberately free of any SRL or
   Saturn dependency so it can be unit-tested on the host; main.cxx owns all the
   drawing and input handling and calls in here for the geometry. */

#define MENU_SCREEN_COLS 40
#define MENU_SCREEN_ROWS 28

/* Columns reserved for a "N) " row-number prefix. Reserved unconditionally,
   whether or not the digits are currently drawn, so a box does not resize when
   the player switches between the pad and a real keyboard mid-menu. */
#define MENU_DIGIT_COLS  3

/* Fit a centered box around `content_w` columns and `rows` rows of content.
   Width is the wider of the content and the title, plus two border columns and
   one pad column each side; height is the content plus a top border, a title
   row, a blank row, and a bottom border. Both are clamped to the screen, and
   the result is always fully on-screen. */
void menu_box_fit(const char *title, int content_w, int rows,
                  int *x0, int *y0, int *w, int *h);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/menu_layout.c`:

```c
#include "menu_layout.h"

void menu_box_fit(const char *title, int content_w, int rows,
                  int *x0, int *y0, int *w, int *h) {
    int tlen = 0;
    int bw, bh;

    if (title != 0) while (title[tlen] != '\0') tlen++;

    if (content_w < 0) content_w = 0;
    if (rows < 0)      rows = 0;

    bw = (tlen > content_w ? tlen : content_w) + 4;   /* borders + pads */
    bh = rows + 4;                                    /* borders + title + blank */

    if (bw > MENU_SCREEN_COLS) bw = MENU_SCREEN_COLS;
    if (bh > MENU_SCREEN_ROWS) bh = MENU_SCREEN_ROWS;

    *w  = bw;
    *h  = bh;
    *x0 = (MENU_SCREEN_COLS - bw) / 2;
    *y0 = (MENU_SCREEN_ROWS - bh) / 2;
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
gcc -o /tmp/tml saturn/tests/test_menu_layout.c saturn/src/menu_layout.c -I saturn/src && /tmp/tml
```

Expected: `test_menu_layout: OK`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/menu_layout.h saturn/src/menu_layout.c saturn/tests/test_menu_layout.c
git commit -m "Add host-testable menu box sizing arithmetic"
```

---

### Task 2: Digit-to-row mapping

**Files:**
- Modify: `saturn/src/menu_layout.h`
- Modify: `saturn/src/menu_layout.c`
- Test: `saturn/tests/test_menu_layout.c`

**Interfaces:**
- Consumes: the header and unit from Task 1.
- Produces: `int menu_row_digit(char ch, int nrows, int *dir);` and `int menu_visible_digit(char ch, int top, int visible, int count);`

- [ ] **Step 1: Write the failing tests**

Add to `saturn/tests/test_menu_layout.c`, above `main`:

```c
static void test_plain_digit_picks_a_row_forward(void) {
    int dir = 0;
    assert(menu_row_digit('3', 5, &dir) == 2);
    assert(dir == 1);
    assert(menu_row_digit('1', 5, &dir) == 0);
    assert(dir == 1);
}

static void test_shifted_digit_picks_a_row_backward(void) {
    int dir = 0;
    assert(menu_row_digit('#', 5, &dir) == 2);   /* Shift+3 */
    assert(dir == -1);
    assert(menu_row_digit('!', 5, &dir) == 0);   /* Shift+1 */
    assert(dir == -1);
    assert(menu_row_digit('(', 9, &dir) == 8);   /* Shift+9 */
    assert(dir == -1);
}

static void test_digit_past_the_row_count_is_rejected(void) {
    int dir = 0;
    assert(menu_row_digit('7', 5, &dir) == -1);
    assert(menu_row_digit('&', 5, &dir) == -1);  /* Shift+7 */
}

static void test_non_selecting_characters_are_rejected(void) {
    int dir = 0;
    assert(menu_row_digit('0', 5, &dir) == -1);  /* rows are 1-9, never 0 */
    assert(menu_row_digit('a', 5, &dir) == -1);
    assert(menu_row_digit(' ', 5, &dir) == -1);
}

static void test_visible_digit_maps_through_the_scroll_window(void) {
    /* 30 games, window of 16 starting at 10: "3" is the third visible row. */
    assert(menu_visible_digit('3', 10, 16, 30) == 12);
    assert(menu_visible_digit('1', 25, 16, 30) == 25);
}

static void test_visible_digit_rejects_rows_past_the_end(void) {
    /* only 5 items, so the 9th visible row does not exist */
    assert(menu_visible_digit('9', 0, 16, 5) == -1);
    assert(menu_visible_digit('6', 0, 16, 5) == -1);
}

static void test_visible_digit_ignores_shift(void) {
    /* a list pick has no "backward"; only plain digits select */
    assert(menu_visible_digit('#', 0, 16, 30) == -1);
}
```

Add these calls inside `main`, before the `printf`:

```c
    test_plain_digit_picks_a_row_forward();
    test_shifted_digit_picks_a_row_backward();
    test_digit_past_the_row_count_is_rejected();
    test_non_selecting_characters_are_rejected();
    test_visible_digit_maps_through_the_scroll_window();
    test_visible_digit_rejects_rows_past_the_end();
    test_visible_digit_ignores_shift();
```

- [ ] **Step 2: Run to verify it fails**

```bash
gcc -o /tmp/tml saturn/tests/test_menu_layout.c saturn/src/menu_layout.c -I saturn/src
```

Expected: FAIL — implicit declaration of `menu_row_digit` and `menu_visible_digit`.

- [ ] **Step 3: Declare the functions**

Append to `saturn/src/menu_layout.h`, before the final `#endif`:

```c
/* Map a printable character to a 0-based row index, or -1 if it selects no row.
   A plain digit 1-9 sets *dir to +1; the matching shifted symbol (!@#$%^&*(, US
   layout) sets it to -1. Callers use *dir to decide whether the row's value
   cycles forward or backward; rows that are actions rather than value cyclers
   ignore it and simply activate. SaturnKeyEvent carries no modifier flag, which
   is why the shifted character is what gets matched. */
int menu_row_digit(char ch, int nrows, int *dir);

/* Which absolute list index a digit selects, given a scroll window of `visible`
   rows starting at `top` in a list of `count` items. Only plain digits select --
   a list pick has no backward direction. Returns -1 if the digit names no
   visible row. */
int menu_visible_digit(char ch, int top, int visible, int count);
```

- [ ] **Step 4: Write the implementation**

Append to `saturn/src/menu_layout.c`:

```c
/* Shifted digits on a US layout, index 0 == Shift+1. */
static const char MENU_SHIFTED_DIGITS[] = "!@#$%^&*(";

/* 0-based row for a plain digit, or -1. */
static int menu_plain_digit_row(char ch) {
    if (ch >= '1' && ch <= '9') return (int) (ch - '1');
    return -1;
}

/* 0-based row for a shifted digit, or -1. */
static int menu_shifted_digit_row(char ch) {
    int i;
    for (i = 0; i < 9; i++) if (MENU_SHIFTED_DIGITS[i] == ch) return i;
    return -1;
}

int menu_row_digit(char ch, int nrows, int *dir) {
    int row = menu_plain_digit_row(ch);
    int d   = 1;

    if (row < 0) {
        row = menu_shifted_digit_row(ch);
        d   = -1;
    }
    if (row < 0 || row >= nrows) return -1;

    if (dir != 0) *dir = d;
    return row;
}

int menu_visible_digit(char ch, int top, int visible, int count) {
    int row = menu_plain_digit_row(ch);
    int idx;

    if (row < 0 || row >= visible) return -1;
    idx = top + row;
    if (idx < 0 || idx >= count) return -1;
    return idx;
}
```

- [ ] **Step 5: Run to verify it passes**

```bash
gcc -o /tmp/tml saturn/tests/test_menu_layout.c saturn/src/menu_layout.c -I saturn/src && /tmp/tml
```

Expected: `test_menu_layout: OK`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/menu_layout.h saturn/src/menu_layout.c saturn/tests/test_menu_layout.c
git commit -m "Add digit-to-row mapping with shifted-digit reverse cycling"
```

---

### Task 3: Box and number `menu_select`

**Files:**
- Modify: `saturn/src/main.cxx:1352-1391` (`menu_select`)
- Modify: `saturn/src/main.cxx:4-8` (the `extern "C"` include block)

**Interfaces:**
- Consumes: `menu_box_fit`, `menu_visible_digit`, `MENU_DIGIT_COLS` from Tasks 1-2.
- Produces: no new signature. `menu_select`'s own signature is unchanged, so its five callers (title mode menu, category select, game select, device select, save/restore slot select) need no edit.

**No host test exists for this.** `main.cxx` needs SRL and cannot be compiled off
target. Verification is the unit suite still passing plus the user's build and
hardware check.

- [ ] **Step 1: Add the include**

In the `extern "C" {` block at `main.cxx:4`, alongside `#include "display.h"`:

```c
#include "menu_layout.h"
```

- [ ] **Step 2: Rewrite `menu_select`**

Replace the whole of `menu_select` (`main.cxx:1352-1391`) with:

```cpp
static int menu_select(const char *title, const char *const *items, int count) {
    const int VIS = 16;         // max list rows shown at once; longer lists scroll
    MenuBacking backing;        // opaque while the list is up; restored on exit
    int sel = 0;
    int top = 0;                // index of the first visible row
    int i;

    // Width: the longest item, plus the "> " cursor and the reserved digit
    // columns. Reserved unconditionally so the box does not resize when the
    // player switches between the pad and a keyboard mid-menu.
    int content_w = 0;
    for (i = 0; i < count; i++) {
        int len = 0;
        while (items[i][len]) len++;
        if (len > content_w) content_w = len;
    }
    content_w += 2 + MENU_DIGIT_COLS;

    // Rows: the visible slice, plus the two scroll markers and a blank line and
    // the hint. The markers keep their rows whether or not they are drawn, so
    // the box does not jump as the list scrolls.
    int rows = (count < VIS ? count : VIS) + 4;

    int x0, y0, w, h;
    menu_box_fit(title, content_w, rows, &x0, &y0, &w, &h);

    SRL::Core::Synchronize();   // consume any stale button/key edge
    for (;;) {
        check_soft_reset();   // A+B+C+Start -> back to the title screen
        if (g_pad->WasPressed(Button::Up))    sel = (sel - 1 + count) % count;
        if (g_pad->WasPressed(Button::Down))  sel = (sel + 1) % count;
        bool pick = g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START);
        bool cancel = g_pad->WasPressed(Button::B);
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (ke.kind == SATURN_KEY_ENTER) pick = true;
        else if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) cancel = true;
        else if (ke.kind == SATURN_KEY_CHAR) {
            // Digits name the visible rows, not absolute indices, so every entry
            // of a long game list stays reachable as the player scrolls.
            int idx = menu_visible_digit(ke.ch, top, VIS, count);
            if (idx >= 0) { sel = idx; pick = true; }
        }
        else if (ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + count) % count;
        else if (ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % count;
        if (cancel) return -1;
        if (pick)   return sel;

        // scroll the window so the selection stays visible
        if (sel < top)             top = sel;
        else if (sel >= top + VIS) top = sel - VIS + 1;
        int last = top + VIS; if (last > count) last = count;

        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand

        menu_clear();
        menu_frame(x0, y0, w, h, title);
        int cx = x0 + 2, cy = y0 + 3;
        SRL::Debug::Print(cx, cy, "%s", top > 0 ? "^ more" : "      ");
        for (i = top; i < last; i++) {
            char mark = (i == sel) ? '>' : ' ';
            int  vis  = i - top;      // 0-based row within the window
            if (nums && vis < 9)
                SRL::Debug::Print(cx, cy + 1 + vis, "%c %d) %s", mark, vis + 1, items[i]);
            else
                SRL::Debug::Print(cx, cy + 1 + vis, "%c    %s", mark, items[i]);
        }
        SRL::Debug::Print(cx, cy + 1 + (last - top), "%s", last < count ? "v more" : "      ");
        SRL::Debug::Print(cx, cy + 3 + (last - top), "%s",
            hint("pad picks   C=ok   B=back", "num picks   Enter=ok   Esc=back"));
        menu_sync();
    }
}
```

Two things this changes beyond boxing, both deliberate:

- `SRL::Core::Synchronize()` at the bottom of the loop becomes `menu_sync()`, matching every other menu loop, so looping PCM does not starve while a list is open.
- The `MenuBacking` guard is new. `menu_select` previously drew over the image background with no window suppression.

- [ ] **Step 3: Confirm the unit suite still passes**

Nothing here should touch it, so this is a regression check.

```bash
gcc -o /tmp/tml saturn/tests/test_menu_layout.c saturn/src/menu_layout.c -I saturn/src && /tmp/tml
gcc -o /tmp/td  saturn/tests/test_display.c     saturn/src/display.c     -I saturn/src && /tmp/td
```

Expected: `test_menu_layout: OK` then `test_display: OK`

- [ ] **Step 4: Hand off for a Saturn build**

Ask the user to run `saturn/compile.bat` and report any compile error. Do not run
it yourself. Fix anything it reports before moving on.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Box and number the list menu

Covers the title mode menu, category select, game select, device select,
and save/restore slot select in one change."
```

---

### Task 4: Boxed message screens and the online dialing sequence

**Files:**
- Modify: `saturn/src/main.cxx` — add `menu_message` near `menu_wait` (`main.cxx:1339`)
- Modify: `saturn/src/main.cxx:2266-2269` (save result), `2287-2290` (load failure), `~2847` (no games found)
- Modify: `saturn/src/main.cxx:2994-3022` (`online_mode` dialing, retry, failure)

**Interfaces:**
- Consumes: `menu_box_fit` from Task 1.
- Produces: `static void menu_message(const char *title, const char *line1, const char *line2);` — draws a fitted, centered box and returns immediately. It does **not** wait and does **not** call `Synchronize`; the caller decides whether to `menu_wait()` or keep redrawing per frame, which the dialing screens need.

- [ ] **Step 1: Add `menu_message`**

Insert immediately after `menu_wait` (which ends at `main.cxx:1347`):

```cpp
// Draw a centered box with one or two lines of text. Returns at once without
// waiting or synchronizing: the save/load result screens follow it with
// menu_wait(), while the dialing screens redraw it every frame.
//
// The caller owns any MenuBacking guard. Screens that are a single blocking
// message declare one; loops that already hold one do not need a second.
static void menu_message(const char *title, const char *line1, const char *line2) {
    int l1 = 0, l2 = 0;
    while (line1 && line1[l1]) l1++;
    while (line2 && line2[l2]) l2++;

    int content_w = (l1 > l2 ? l1 : l2);
    int rows      = (l2 > 0) ? 2 : 1;
    int x0, y0, w, h;
    menu_box_fit(title, content_w, rows, &x0, &y0, &w, &h);

    menu_clear();
    menu_frame(x0, y0, w, h, title);
    if (l1) SRL::Debug::Print(x0 + 2, y0 + 3, "%s", line1);
    if (l2) SRL::Debug::Print(x0 + 2, y0 + 4, "%s", line2);
}
```

- [ ] **Step 2: Box the save result screen**

Replace `main.cxx:2266-2269`:

```cpp
    menu_clear();
    SRL::Debug::Print(2, 4, "%s", ok ? "Saved." : "Save FAILED (no space?).");
    SRL::Debug::Print(2, 6, "(press any key/button)");
    menu_wait();
```

with:

```cpp
    {
        MenuBacking backing;   // opaque over an image background while this is up
        menu_message("SAVE", ok ? "Saved." : "Save FAILED (no space?).",
                     "(press any key/button)");
        menu_wait();
    }
```

- [ ] **Step 3: Box the load failure screen**

Replace `main.cxx:2287-2290`:

```cpp
        menu_clear();
        SRL::Debug::Print(2, 4, "No save in that slot.");
        SRL::Debug::Print(2, 6, "(press any key/button)");
        menu_wait();
```

with:

```cpp
        MenuBacking backing;
        menu_message("RESTORE", "No save in that slot.", "(press any key/button)");
        menu_wait();
```

- [ ] **Step 4: Box the empty-catalog screen**

At `main.cxx:~2847`, inside the `if (count <= 0)` branch, replace the
`menu_clear()` plus its `SRL::Debug::Print` calls with:

```cpp
        MenuBacking backing;
        menu_message("NO GAMES", "No games found on the disc.",
                     "(press any key/button)");
        menu_wait();
```

Read the existing lines before replacing — keep whatever the current wording is
if it differs from `"No games found on the disc."`, and keep the existing control
flow (the `return nullptr` at `main.cxx:2853`) exactly as it stands.

- [ ] **Step 5: Box the dialing sequence**

In `online_mode`, replace the three full-screen clear-and-print blocks. The
dial-attempt block at `main.cxx:2994-2998`:

```cpp
        for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
        SRL::Debug::Print(2, 4, "Dialing %s ... (attempt %d/%d)",
                          number, attempt, ONLINE_DIAL_ATTEMPTS);
        SRL::Debug::Print(2, 6, "%s", hint("L+R = cancel", "Esc = cancel"));
        SRL::Core::Synchronize();
```

becomes:

```cpp
        {
            char dial[40];
            snprintf(dial, sizeof(dial), "Dialing %s ... (attempt %d/%d)",
                     number, attempt, ONLINE_DIAL_ATTEMPTS);
            menu_message("ONLINE", dial,
                         hint("L+R = cancel", "Esc = cancel"));
            SRL::Core::Synchronize();
        }
```

The retry block at `main.cxx:3006-3008` becomes:

```cpp
            menu_message("ONLINE", "No carrier. Retrying...",
                         hint("L+R = cancel", "Esc = cancel"));
```

and it must be redrawn inside the existing 180-frame wait loop, so that loop
body becomes:

```cpp
            for (int f = 0; f < 180; f++) {   // ~3s at 60Hz
                if (online_cancel_requested()) { cancelled = true; break; }
                menu_message("ONLINE", "No carrier. Retrying...",
                             hint("L+R = cancel", "Esc = cancel"));
                SRL::Core::Synchronize();
            }
```

The failure block at `main.cxx:3019-3021` becomes:

```cpp
        menu_message("ONLINE",
            rc == NET_NO_MODEM ? "NetLink modem not found." : "Connection failed.",
            "");
```

Read the lines that follow it before editing — keep whatever wait or return
follows unchanged.

`online_mode` needs one `MenuBacking backing;` declared at the top of the
function, so the box stays opaque across the whole dialing sequence. Place it
immediately after `ensure_online_typeahead();` (`main.cxx:2982`). It must go out
of scope before the terminal session starts, so if the telnet session runs inside
the same function, wrap only the connect phase in a `{ ... }` block.

- [ ] **Step 6: Confirm the unit suite still passes**

```bash
gcc -o /tmp/tml saturn/tests/test_menu_layout.c saturn/src/menu_layout.c -I saturn/src && /tmp/tml
gcc -o /tmp/td  saturn/tests/test_display.c     saturn/src/display.c     -I saturn/src && /tmp/td
```

Expected: `test_menu_layout: OK` then `test_display: OK`

- [ ] **Step 7: Hand off for a Saturn build, then commit**

Ask the user to run `saturn/compile.bat`. Fix any error, then:

```bash
git add saturn/src/main.cxx
git commit -m "Box the message screens and the online dialing sequence"
```

---

### Task 5: Box the two confirmation prompts

**Files:**
- Modify: `saturn/src/main.cxx:2184-2206` (`menu_confirm`)
- Modify: `saturn/src/main.cxx:857-882` (`confirm_return_to_title`)
- Modify: `saturn/src/main.cxx:191` (console-height branch) and the `g_reboot_menu` / `REBOOT_MENU_ROWS` declarations

**Interfaces:**
- Consumes: `menu_box_fit` from Task 1, `menu_frame`, `MenuBacking`.
- Produces: no signature changes. Both functions keep their current signatures and return values, so no caller changes.

- [ ] **Step 1: Box and number `menu_confirm`**

Replace the drawing tail of `menu_confirm` (`main.cxx:2199-2204`) with a boxed
version, and add digit handling. The full function becomes:

```cpp
static bool menu_confirm(const char *line1, const char *line2) {
    MenuBacking backing;
    int l1 = 0, l2 = 0;
    while (line1 && line1[l1]) l1++;
    while (line2 && line2[l2]) l2++;

    int content_w = (l1 > l2 ? l1 : l2);
    if (content_w < 12) content_w = 12;   // room for the "1) Yes  2) No" row
    int x0, y0, w, h;
    menu_box_fit("CONFIRM", content_w, (l2 > 0 ? 5 : 4), &x0, &y0, &w, &h);

    SRL::Core::Synchronize();   // consume the edge that got us here
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        if (ke.kind == SATURN_KEY_ENTER) return true;
        if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) return false;
        if (ke.kind == SATURN_KEY_CHAR) {
            if (ke.ch == 'y' || ke.ch == 'Y' || ke.ch == '1') return true;
            if (ke.ch == 'n' || ke.ch == 'N' || ke.ch == '2') return false;
        } else {
            if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START))
                return true;
            if (g_pad->WasPressed(Button::B)) return false;
        }

        menu_clear();
        menu_frame(x0, y0, w, h, "CONFIRM");
        int cx = x0 + 2, cy = y0 + 3;
        if (l1) SRL::Debug::Print(cx, cy, "%s", line1);
        if (l2) SRL::Debug::Print(cx, cy + 1, "%s", line2);
        int hy = cy + (l2 > 0 ? 3 : 2);
        if (!g_kbd_visible) SRL::Debug::Print(cx, hy, "1) Yes    2) No");
        SRL::Debug::Print(cx, hy + 1, "%s",
            hint("A / C = Yes     B = No", "Enter = Yes     Esc = No"));
        menu_sync();
    }
}
```

- [ ] **Step 2: Box `confirm_return_to_title` and drop the console shrink**

Replace the whole of `confirm_return_to_title` (`main.cxx:857-882`) with:

```cpp
static bool confirm_return_to_title(const char *question) {
    MenuBacking backing;   // the box owns its area; no console shrink needed
    int qlen = 0;
    while (question[qlen]) qlen++;

    int content_w = qlen;
    if (content_w < 24) content_w = 24;   // room for the hint lines
    int x0, y0, w, h;
    menu_box_fit("RETURN TO TITLE", content_w, 5, &x0, &y0, &w, &h);

    SRL::Core::Synchronize();   // drop the submit edge that triggered this
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);   // so the prompt below matches the device in hand
        bool yes = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'y' || ke.ch == 'Y' || ke.ch == '1'))
                 || ke.kind == SATURN_KEY_ENTER
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::START)
                 || g_pad->WasPressed(Button::C);
        bool no  = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'n' || ke.ch == 'N' || ke.ch == '2'))
                 || ke.kind == SATURN_KEY_ESCAPE || g_pad->WasPressed(Button::B);
        if (yes) { soft_reset_to_title(); }         // in-process; never returns
        if (no) { return false; }

        // The console still renders behind at full height; the box sits over it
        // and the VDP2 window keeps any image background out from under it.
        render_console();
        menu_frame(x0, y0, w, h, "RETURN TO TITLE");
        int cx = x0 + 2, cy = y0 + 3;
        SRL::Debug::Print(cx, cy, "%s", question);
        if (!g_kbd_visible) SRL::Debug::Print(cx, cy + 2, "1) Yes    2) No");
        SRL::Debug::Print(cx, cy + 3, "%s", hint("(A) (C) (Start) = yes", "Y / Enter = yes"));
        SRL::Debug::Print(cx, cy + 4, "%s", hint("(B) = no", "N / Esc = no"));
        SRL::Core::Synchronize();
    }
}
```

Note there is deliberately no `menu_clear()` — the game console stays visible
behind the box, which is the point of this prompt.

- [ ] **Step 3: Remove `g_reboot_menu` and `REBOOT_MENU_ROWS`**

`confirm_return_to_title` was the only writer. Remove:

- the `if (g_reboot_menu) return avail - REBOOT_MENU_ROWS;` branch at `main.cxx:191`, so the function returns straight to the `g_kbd_visible ? ... : ...` line
- the `g_reboot_menu` declaration
- the `REBOOT_MENU_ROWS` `#define`

Then confirm nothing else referenced them:

```bash
grep -n "g_reboot_menu\|REBOOT_MENU_ROWS" saturn/src/main.cxx
```

Expected: no output. If anything remains, it is a live reference — stop and
resolve it rather than deleting it.

- [ ] **Step 4: Confirm the unit suite still passes**

```bash
gcc -o /tmp/tml saturn/tests/test_menu_layout.c saturn/src/menu_layout.c -I saturn/src && /tmp/tml
gcc -o /tmp/td  saturn/tests/test_display.c     saturn/src/display.c     -I saturn/src && /tmp/td
```

Expected: `test_menu_layout: OK` then `test_display: OK`

- [ ] **Step 5: Hand off for a Saturn build, then commit**

```bash
git add saturn/src/main.cxx
git commit -m "Box both confirmation prompts and drop the console shrink

confirm_return_to_title now draws a box that owns its own area, so
g_reboot_menu and REBOOT_MENU_ROWS are no longer needed."
```

---

### Task 6: Box the save-slot picker

**Files:**
- Modify: `saturn/src/main.cxx:~2060-2180` (`pick_slot_and_name`)

**Interfaces:**
- Consumes: `menu_box_fit`, `menu_visible_digit`, `MENU_DIGIT_COLS`.
- Produces: no signature change.

The box has two sizes: a short one while picking a slot, and a taller one while
editing a name, which has to fit the on-screen keyboard. Recompute on the frame
`editing` flips rather than sizing once.

- [ ] **Step 1: Add a `MenuBacking` guard and the sizing**

At the top of `pick_slot_and_name`, before the input loop, add:

```cpp
    MenuBacking backing;
```

- [ ] **Step 2: Suppress digit selection while editing, and add it while not**

In the not-editing branch of the input handling (the branch containing the
existing `sel = (int) (ke.ch - '1'); pick = true;` at `main.cxx:2100`), replace
that digit handling with:

```cpp
            int idx = menu_visible_digit(ke.ch, 0, SAVE_SLOTS, SAVE_SLOTS);
            if (idx >= 0) { sel = idx; pick = true; }
```

Leave the editing branch's `SATURN_KEY_CHAR` handling (`main.cxx:2128`)
completely alone — while editing, a digit is literal text for the save name and
must reach the field.

- [ ] **Step 3: Box the drawing**

Replace the drawing tail (`main.cxx:2150-2178`, from `menu_clear();` down to but
not including `SRL::Core::Synchronize();`) with:

```cpp
        // Two shapes: a short slot list, or a taller box with the on-screen
        // keyboard under it once a name is being typed.
        const char *btitle = editing ? "NAME THIS SAVE" : "SAVE - PICK A SLOT";
        int content_w = KB_COLS * 2;              // the keyboard is the widest thing
        int rows      = editing ? (SAVE_SLOTS + 2 + KB_ROWS + 1) : (SAVE_SLOTS + 2);
        int x0, y0, w, h;
        menu_box_fit(btitle, content_w, rows, &x0, &y0, &w, &h);

        bool nums = !g_kbd_visible && !editing;   // digits are literal text while editing

        menu_clear();
        menu_frame(x0, y0, w, h, btitle);
        int cx = x0 + 2, cy = y0 + 3;
        for (int i = 0; i < SAVE_SLOTS; i++) {
            char mark = (i == sel) ? '>' : ' ';
            if (editing && i == sel) {
                SRL::Debug::Print(cx, cy + i, "%c    %s_", mark, k.input);
            } else {
                const char *label = slotname[i][0] ? slotname[i] : "(empty)";
                if (nums) SRL::Debug::Print(cx, cy + i, "%c %d) %s", mark, i + 1, label);
                else      SRL::Debug::Print(cx, cy + i, "%c    %s", mark, label);
            }
        }
        if (!editing) {
            SRL::Debug::Print(cx, cy + SAVE_SLOTS + 1, "%s",
                hint("pad picks   C=edit   B=back", "num picks   Enter=edit   Esc=back"));
        } else {
            for (int r = 0; r < KB_ROWS; r++) {
                char rowbuf[KB_COLS * 2 + 1];
                int p = 0;
                for (int c = 0; c < KB_COLS; c++) {
                    rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
                    rowbuf[p++] = KB_LAYOUT[r][c];
                }
                rowbuf[p] = '\0';
                SRL::Debug::Print(cx, cy + SAVE_SLOTS + 1 + r, "%s", rowbuf);
            }
            SRL::Debug::Print(cx, cy + SAVE_SLOTS + 2 + KB_ROWS, "%s",
                hint("C=type X=space  B=back  A=OK", "type name  Esc=back  Enter=OK"));
        }
```

If the editing box exceeds 28 rows once `SAVE_SLOTS` and `KB_ROWS` are added up,
`menu_box_fit` clamps the height and the hint line will be cut off. Check the
actual values of `SAVE_SLOTS` and `KB_ROWS` before building: if
`SAVE_SLOTS + KB_ROWS + 7 > 28`, drop the blank separator row (change the `+ 2`
in `rows` to `+ 1` and shift the keyboard rows up by one) rather than letting it
clamp.

- [ ] **Step 4: Confirm the unit suite still passes**

```bash
gcc -o /tmp/tml saturn/tests/test_menu_layout.c saturn/src/menu_layout.c -I saturn/src && /tmp/tml
gcc -o /tmp/td  saturn/tests/test_display.c     saturn/src/display.c     -I saturn/src && /tmp/td
```

Expected: `test_menu_layout: OK` then `test_display: OK`

- [ ] **Step 5: Hand off for a Saturn build, then commit**

```bash
git add saturn/src/main.cxx
git commit -m "Box the save-slot picker

Digits number the slot rows only while not editing; once a name is being
typed they are literal text for the field."
```

---

### Task 7: Number the option page rows

**Files:**
- Modify: `saturn/src/main.cxx:1758` (`sound_options_page`), `1879` (`display_options_page`), `1564` (`controls_page`), `1487` (`configure_controls_page`), `1614` (`keyboard_controls_page`), `1967` (`options_menu`)
- Modify: `saturn/src/main.cxx:1404` (`config_page` — box only, no numbers)

**Interfaces:**
- Consumes: `menu_row_digit` from Task 2.
- Produces: no signature changes.

These pages are already boxed and hardcode their own dimensions. **No box is
resized in this task.** The 3-column prefix is absorbed by interior padding the
boxes already have — verified by measurement:

| Page | Box | Label x | Value col | Longest value | Right limit | Headroom |
|---|---|---|---|---|---|---|
| `sound_options_page` | `fx=1 fw=38` | 3 | x+14 = 17 | 12 (`"%d  (A=demo)"`) | 37 | 8 |
| `display_options_page` | `fx=0 fw=40` | 2 | x+16 = 18 | 15 (measured) | 38 | 5 |

Both absorb 3 columns. The other four pages have no value column at all — their
rows are plain labels — so they have more room still.

Every page here already declares `left` and `right` booleans. The activation
boolean differs: `sound_options_page` and `display_options_page` call it **`ok`**;
`controls_page`, `configure_controls_page`, `keyboard_controls_page`, and
`options_menu` call it **`act`**. Use the right one per page.

- [ ] **Step 1: Tighten the display value-width guard (failing test first)**

`saturn/tests/test_display.c` asserts preset names are `<= 22` (with the comment
"value drawn at x+16"). Once the 3-column prefix shifts the value column to
x+19, the real budget is `38 - (2 + 3 + 16) = 17`. The current longest name is
15, so this is a guard against future regressions, not a break.

Change that assertion in `test_display.c`:

```c
        /* Must fit the selector field: 40-column screen, value drawn at x+16,
           plus 3 columns reserved for the "N) " row-number prefix. */
        assert(strlen(display_preset_name(i)) <= 17);
```

- [ ] **Step 2: Run to confirm it still passes**

```bash
gcc -o /tmp/td saturn/tests/test_display.c saturn/src/display.c -I saturn/src && /tmp/td
```

Expected: `test_display: OK` — the longest name is 15, comfortably under 17. If
this FAILS, a name is longer than measured; stop and shorten the name rather than
loosening the assertion back.

- [ ] **Step 3: Add digit handling to `sound_options_page`**

In `sound_options_page` (`main.cxx:1758`), insert immediately after the `bool
cancel = ...` declaration and **before** `int row = rows[sel];`:

```cpp
        // A digit jumps to a row and acts on it in one press: value rows cycle
        // forward on a plain digit and backward on the shifted symbol, action
        // rows activate. `sel` indexes the visible row list, which is what
        // menu_row_digit's 0-based result already means.
        if (ke.kind == SATURN_KEY_CHAR) {
            int ddir = 0;
            int drow = menu_row_digit(ke.ch, nrows, &ddir);
            if (drow >= 0) {
                sel = drow;
                if (ddir > 0) right = true; else left = true;
                ok = true;
            }
        }
```

Setting `ok` unconditionally is safe on this page: `SR_MIX`, `SR_MUSIC`, and
`SR_PCM` ignore `ok` entirely; `SR_TRACK` already treats `left || right || ok`
identically; `SR_TOC` and `SR_OK` are action rows that should activate; and
`SR_CANCEL` with `ok` set is exactly the revert-and-exit the row means.

- [ ] **Step 4: Add digit handling to `display_options_page`**

Same insertion in `display_options_page` (`main.cxx:1879`), after its `bool
cancel = ...` and before its row dispatch. `nrows` there is a `const int`
computed from the `rows[]` array, so the call is identical:

```cpp
        if (ke.kind == SATURN_KEY_CHAR) {
            int ddir = 0;
            int drow = menu_row_digit(ke.ch, nrows, &ddir);
            if (drow >= 0) {
                sel = drow;
                if (ddir > 0) right = true; else left = true;
                ok = true;
            }
        }
```

- [ ] **Step 5: Add digit handling to the four `act` pages**

The same block, with `ok` replaced by `act` and the row count per page:

- `controls_page` (`main.cxx:1564`) — row count `3`
- `keyboard_controls_page` (`main.cxx:1614`) — row count `N` (already declared, `= 6`)
- `configure_controls_page` (`main.cxx:1487`) — row count `R_CANCEL + 1`
- `options_menu` (`main.cxx:1967`) — row count is its visible-row count; read the loop bound it already uses for Up/Down wraparound and pass that same expression

```cpp
        if (ke.kind == SATURN_KEY_CHAR) {
            int ddir = 0;
            int drow = menu_row_digit(ke.ch, <row count from the list above>, &ddir);
            if (drow >= 0) {
                sel = drow;
                if (ddir > 0) right = true; else left = true;
                act = true;
            }
        }
```

In `keyboard_controls_page`, place this **before** its `bool toggle = left ||
right || act;` line so the digit feeds into `toggle`.

In `options_menu` every row is a sub-page or an action, so `left`/`right` are
ignored there — setting them is harmless.

- [ ] **Step 6: Add the `N) ` prefix to each page's row rendering**

In each of the six pages' drawing blocks, declare once before the row loop:

```cpp
        bool nums = !g_kbd_visible;   // digits only while a keyboard is in hand
```

Then change every row's label print from the current `"%c Label"` form to the
gated pair. For a literal label:

```cpp
            if (nums) SRL::Debug::Print(x, y++, "%c %d) Reset to Defaults", mark, <rowindex> + 1);
            else      SRL::Debug::Print(x, y++, "%c    Reset to Defaults", mark);
```

and for the pages that print a label and a value on the same row, shift the value
column by `MENU_DIGIT_COLS` so the two modes align. In `sound_options_page`:

```cpp
                case SR_MIX:
                    if (nums) SRL::Debug::Print(x, y, "%c %d) Audio Mix", cur, i + 1);
                    else      SRL::Debug::Print(x, y, "%c    Audio Mix", cur);
                    SRL::Debug::Print(x + 14 + MENU_DIGIT_COLS, y++, "%s %s %s",
                        g_mix_mode > 0 ? "<" : " ", MIX[g_mix_mode],
                        g_mix_mode < MIX_RANDOM ? ">" : " ");
                    break;
```

Apply the same shape to `SR_TRACK`, `SR_MUSIC`, and `SR_PCM` (all at `x + 14`),
and to `display_options_page`'s rows (all at `x + 16`). The value offset gains
`MENU_DIGIT_COLS` unconditionally in both modes — that is the point of reserving
the columns, and it is why the box does not resize when the device changes.

Use `i + 1` where the loop variable is the visible row index (`sound_options_page`,
`display_options_page`), and the row constant `+ 1` where rows are printed
individually (`configure_controls_page`'s `R_RESET`, `R_DONE`, `R_CANCEL`).

- [ ] **Step 7: Confirm `config_page` needs no change**

`config_page` (`main.cxx:1404`) is dial-number text entry, where a digit is
literal input and must reach the field. It already calls `menu_frame`
(`main.cxx:1443`). Verify both facts by reading it, then leave it untouched. It
gets **no** digit handling and **no** `N) ` prefixes.

- [ ] **Step 8: Confirm the unit suite still passes**

```bash
gcc -o /tmp/tml saturn/tests/test_menu_layout.c saturn/src/menu_layout.c -I saturn/src && /tmp/tml
gcc -o /tmp/td  saturn/tests/test_display.c     saturn/src/display.c     -I saturn/src && /tmp/td
```

Expected: `test_menu_layout: OK` then `test_display: OK`

- [ ] **Step 9: Hand off for a Saturn build**

Ask the user to run `saturn/compile.bat`. This task touches six functions; expect
errors from `ok` vs `act` mismatches and fix them against each page's actual
declaration.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/main.cxx saturn/tests/test_display.c
git commit -m "Number the option page rows

A digit jumps to a row and acts on it: value rows cycle forward, the
shifted symbol cycles backward, action rows activate. The 3-column
prefix fits in existing interior padding, so no box is resized."
```

---

## Final verification

- [ ] Every menu draws as a centered box except `toc_dump_page`, which is deliberately excluded.
- [ ] `grep -n "g_reboot_menu\|REBOOT_MENU_ROWS" saturn/src/main.cxx` returns nothing.
- [ ] Both unit test binaries pass.
- [ ] The user has built with `saturn/compile.bat` and confirmed on hardware or emulator that:
  - digits appear after touching a keyboard and disappear after touching the pad
  - digits select on both the number row and the numpad
  - Shift+digit cycles a value row backward
  - digits in a long game list select the visible row, and stay correct after scrolling
  - typing a digit into the dial number or a save name inserts it rather than jumping a row
  - no menu box overflows the screen, in particular the save-name box with the keyboard open

**Nothing beyond "it builds and the unit tests pass" can be claimed from this
side.** The last bullet is the user's to confirm.
