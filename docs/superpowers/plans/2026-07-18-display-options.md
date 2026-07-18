# Display Options Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Display Options page letting the player pick the console's background and text color from 15 vintage microcomputer palettes, with the background selector also cycling into bitmaps found on the disc.

**Architecture:** All decision logic — color tables, preset table, cycling, the legibility guard, `Custom` derivation, and save-blob encode/decode — lives in a new pure-C module `saturn/src/display.c`, unit-tested on the host exactly like `console.c` and `keyboard.c`. `saturn/src/main.cxx` keeps only the parts that must touch hardware: applying colors via `SRL::ASCII::SetColor` / `SRL::VDP2::SetBackColor`, enumerating the disc's `TGA` directory, and drawing the menu.

**Tech Stack:** SaturnRingLib (C++), C11 for portable modules, MSYS2 mingw64 `gcc` (host tests), `sh2eb-elf` cross toolchain (Saturn build, via `saturn/compile.bat`).

## Global Constraints

- **Spec:** `docs/superpowers/specs/2026-07-18-display-options-design.md`. Read it before starting.
- **Do not run `saturn/compile.bat`.** The user performs Saturn builds. Host tests are yours to run.
- **Host test toolchain:** `/c/msys64/mingw64/bin/gcc`, C11. Each test is a self-contained `main()` using `assert` + `printf`; there is no test runner. Run commands from `saturn/`.
- **Portable modules are C11, not C++.** `display.c` must compile clean under `gcc -std=c11 -Wall -Wextra` and must not include any SRL header.
- **RGB555 word format:** `0x8000 | (b>>3)<<10 | (g>>3)<<5 | (r>>3)` — blue occupies the high bits, red the low. This matches `SRL::Types::HighColor`'s 8-bit constructor (`SaturnRingLib/saturnringlib/srl_color.hpp:66`).
- **Text color index is 15.** `SRL::ASCII::SetColor(rgb555, 15)` — index 15 is what the SGL glyphs and the carved block cursor both use. Never use `SRL::Debug::PrintColorSet`; it calls `slCurColor()` while `Debug::Print` reads `ASCII::colorBank`, so it is inert.
- **Background images must be 8bpp paletted and fit one VRAM bank** or SRL renders them as static.
- **Persisted `palette` byte is always a real preset index `0..14`** — never a `Custom` sentinel.
- **Commit after every task.** Do not add a `Claude-Session` trailer or similar to commit messages.

## File Structure

| File | Action | Responsibility |
| --- | --- | --- |
| `saturn/src/display.h` | create | Public types, constants, and function declarations for the display module. |
| `saturn/src/display.c` | create | Color tables, preset table, cycling logic, `Custom` derivation, save-blob codec. No SRL dependency. |
| `saturn/tests/test_display.c` | create | Host unit tests for `display.c`. |
| `saturn/src/main.cxx` | modify | Apply colors to VDP2, enumerate `TGA` dir, draw the Display Options page, wire into `options_menu` / `options_load` / `options_save` / title screen. |
| `saturn/CMakeLists.txt` | modify | Add `display.c` to the source list. |

---

### Task 1: Display module tables and accessors

Creates the module with its static data and read-only accessors. No mutation logic yet.

**Files:**
- Create: `saturn/src/display.h`
- Create: `saturn/src/display.c`
- Test: `saturn/tests/test_display.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `DisplayState`, `DISP_*` constants, `display_bg_rgb(int)`, `display_text_rgb(int)`, `display_bg_color_name(int)`, `display_text_name(int)`, `display_preset_name(int)`, `display_preset_bg(int)`, `display_preset_text(int)`, `display_palette_name(const DisplayState*)`, `display_defaults(DisplayState*)`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_display.c`:

```c
#include "../src/display.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Recompute the RGB555 packing independently so a typo in the macro is caught. */
static unsigned short rgb(int r, int g, int b) {
    return (unsigned short)(0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
}

static void test_tables_well_formed(void) {
    int i;
    for (i = 0; i < DISP_PRESET_N; i++) {
        assert(display_preset_bg(i)   >= 0 && display_preset_bg(i)   < DISP_BG_COLOR_N);
        assert(display_preset_text(i) >= 0 && display_preset_text(i) < DISP_TEXT_N);
        assert(display_preset_name(i) != NULL);
        assert(display_preset_name(i)[0] != '\0');
        /* Must fit the selector field: 40-column screen, value drawn at x+16. */
        assert(strlen(display_preset_name(i)) <= 22);
    }
    for (i = 0; i < DISP_BG_COLOR_N; i++)  assert(display_bg_color_name(i) != NULL);
    for (i = 0; i < DISP_TEXT_N; i++)      assert(display_text_name(i)     != NULL);
}

static void test_known_colors(void) {
    assert(display_bg_rgb(DISP_BG_BLACK)        == rgb(0x00, 0x00, 0x00));
    assert(display_bg_rgb(DISP_BG_AMBER)        == rgb(0xFF, 0xB0, 0x00));
    assert(display_bg_rgb(DISP_BG_BLUE)         == rgb(0x00, 0x00, 0xAA));
    assert(display_bg_rgb(DISP_BG_BRIGHT_WHITE) == rgb(0xFF, 0xFF, 0xFF));
    assert(display_text_rgb(DISP_TEXT_BRIGHT_AMBER) == rgb(0xFF, 0xAF, 0x00));
    assert(display_text_rgb(DISP_TEXT_BRIGHT_GREEN) == rgb(0x55, 0xFF, 0x55));
    /* ANSI 37 is light gray, not true white -- keeps BBC Micro / MSX authentic. */
    assert(display_text_rgb(DISP_TEXT_WHITE) == rgb(0xAA, 0xAA, 0xAA));
}

static void test_preset_contents(void) {
    /* Spot-check the ends and the two collision pairs from the spec. */
    assert(display_preset_bg(0)  == DISP_BG_BLACK);
    assert(display_preset_text(0) == DISP_TEXT_BRIGHT_AMBER);          /* Toshiba T3100 */
    assert(display_preset_bg(1)  == DISP_BG_AMBER);
    assert(display_preset_text(1) == DISP_TEXT_BLACK);                 /* Monochrome P3 */
    assert(display_preset_bg(14) == DISP_BG_BRIGHT_WHITE);
    assert(display_preset_text(14) == DISP_TEXT_BLACK);                /* Mac Classic */
    /* C64 (3) and Atari 800 (11) share a combo but not a name. */
    assert(display_preset_bg(3) == display_preset_bg(11));
    assert(display_preset_text(3) == display_preset_text(11));
    assert(strcmp(display_preset_name(3), display_preset_name(11)) != 0);
    /* IBM PC MDA (12) and Commodore PET (13) likewise. */
    assert(display_preset_bg(12) == display_preset_bg(13));
    assert(display_preset_text(12) == display_preset_text(13));
    assert(strcmp(display_preset_name(12), display_preset_name(13)) != 0);
}

static void test_defaults_and_palette_name(void) {
    DisplayState d;
    display_defaults(&d);
    assert(d.palette == 12);                       /* IBM PC (MDA Monitor) */
    assert(d.bg == DISP_BG_BLACK);
    assert(d.text == DISP_TEXT_BRIGHT_GREEN);
    assert(strcmp(display_palette_name(&d), display_preset_name(12)) == 0);

    /* Diverge from the preset -> Custom; restore -> the name comes back. */
    d.text = DISP_TEXT_CYAN;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    d.text = DISP_TEXT_BRIGHT_GREEN;
    assert(strcmp(display_palette_name(&d), display_preset_name(12)) == 0);

    /* The stored index disambiguates the collision: same colors, different name. */
    d.palette = 13;
    assert(strcmp(display_palette_name(&d), display_preset_name(13)) == 0);
}

int main(void) {
    test_tables_well_formed();
    test_known_colors();
    test_preset_contents();
    test_defaults_and_palette_name();
    printf("test_display: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display`

Expected: FAIL — `src/display.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/display.h`:

```c
#ifndef DISPLAY_H
#define DISPLAY_H

/* Pack 8-bit RGB into a Saturn RGB555 word (blue high, red low, opaque bit set).
   Matches SRL::Types::HighColor(uint8_t r, uint8_t g, uint8_t b). */
#define DISP_RGB555(r, g, b) \
    ((unsigned short)(0x8000 | (((b) >> 3) << 10) | (((g) >> 3) << 5) | ((r) >> 3)))

#define DISP_BG_COLOR_N 7    /* background colors; indices >= this are images */
#define DISP_TEXT_N     8
#define DISP_PRESET_N   15
#define DISP_IMAGE_MAX  8    /* cap on bitmaps read from the disc's TGA folder */

/* Background color indices. */
#define DISP_BG_BLACK        0
#define DISP_BG_AMBER        1
#define DISP_BG_BLUE         2
#define DISP_BG_LIGHT_GRAY   3
#define DISP_BG_BRIGHT_CYAN  4
#define DISP_BG_GREEN        5
#define DISP_BG_BRIGHT_WHITE 6

/* Text color indices. */
#define DISP_TEXT_BRIGHT_AMBER  0
#define DISP_TEXT_BLACK         1
#define DISP_TEXT_GREEN         2
#define DISP_TEXT_LIGHT_BLUE    3
#define DISP_TEXT_CYAN          4
#define DISP_TEXT_BRIGHT_YELLOW 5
#define DISP_TEXT_WHITE         6
#define DISP_TEXT_BRIGHT_GREEN  7

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int palette;   /* preset index 0..DISP_PRESET_N-1; "Custom" is derived, never stored */
    int bg;        /* 0..DISP_BG_COLOR_N-1 = color; >= DISP_BG_COLOR_N = image slot */
    int text;      /* 0..DISP_TEXT_N-1 */
} DisplayState;

unsigned short display_bg_rgb(int index);          /* color indices only */
unsigned short display_text_rgb(int index);
const char *display_bg_color_name(int index);
const char *display_text_name(int index);
const char *display_preset_name(int index);
int display_preset_bg(int index);
int display_preset_text(int index);

/* The machine name when bg/text still match PRESETS[d->palette], else "Custom". */
const char *display_palette_name(const DisplayState *d);

void display_defaults(DisplayState *d);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/display.c`:

```c
#include "display.h"

/* Background colors, in selector order. RGB values approximate the ANSI codes
   in the design spec's source table; Dark/Deep/Medium Blue all emit \033[44m
   and collapse to one entry, as do White and White/Light Gray (\033[47m). */
static const unsigned short BG_RGB[DISP_BG_COLOR_N] = {
    DISP_RGB555(0x00, 0x00, 0x00),   /* Black          ANSI 40  */
    DISP_RGB555(0xFF, 0xB0, 0x00),   /* Glowing Amber  ANSI 43  */
    DISP_RGB555(0x00, 0x00, 0xAA),   /* Blue           ANSI 44  */
    DISP_RGB555(0xAA, 0xAA, 0xAA),   /* Light Gray     ANSI 47  */
    DISP_RGB555(0x55, 0xFF, 0xFF),   /* Bright Cyan    ANSI 106 */
    DISP_RGB555(0x00, 0xAA, 0x00),   /* Green          ANSI 42  */
    DISP_RGB555(0xFF, 0xFF, 0xFF)    /* Bright White   ANSI 107 */
};

static const char *const BG_NAME[DISP_BG_COLOR_N] = {
    "Black", "Glowing Amber", "Blue", "Light Gray",
    "Bright Cyan", "Green", "Bright White"
};

/* Text colors, in selector order. White is ANSI 37 (light gray), not true
   white -- that is what makes the BBC Micro and MSX presets look right. */
static const unsigned short TEXT_RGB[DISP_TEXT_N] = {
    DISP_RGB555(0xFF, 0xAF, 0x00),   /* Bright Amber   ANSI 38;5;214 */
    DISP_RGB555(0x00, 0x00, 0x00),   /* Black          ANSI 30  */
    DISP_RGB555(0x00, 0xAA, 0x00),   /* Green          ANSI 32  */
    DISP_RGB555(0x55, 0x55, 0xFF),   /* Light Blue     ANSI 94  */
    DISP_RGB555(0x00, 0xAA, 0xAA),   /* Cyan           ANSI 36  */
    DISP_RGB555(0xFF, 0xFF, 0x55),   /* Bright Yellow  ANSI 93  */
    DISP_RGB555(0xAA, 0xAA, 0xAA),   /* White          ANSI 37  */
    DISP_RGB555(0x55, 0xFF, 0x55)    /* Bright Green   ANSI 92  */
};

static const char *const TEXT_NAME[DISP_TEXT_N] = {
    "Bright Amber", "Black", "Green", "Light Blue",
    "Cyan", "Bright Yellow", "White", "Bright Green"
};

/* Microcomputer presets. Names are shortened where the full hardware name
   would overflow the selector field. Note two deliberate duplicate combos:
   C64 == Atari 800, and IBM PC MDA == Commodore PET. They stay separate
   entries because the stored palette index -- not the color pair -- is what
   identifies the machine. */
typedef struct { const char *name; int bg; int text; } DisplayPreset;

static const DisplayPreset PRESETS[DISP_PRESET_N] = {
    { "Toshiba T3100",   DISP_BG_BLACK,        DISP_TEXT_BRIGHT_AMBER  },
    { "Monochrome P3",   DISP_BG_AMBER,        DISP_TEXT_BLACK         },
    { "Apple II Plus",   DISP_BG_BLACK,        DISP_TEXT_GREEN         },
    { "Commodore 64",    DISP_BG_BLUE,         DISP_TEXT_LIGHT_BLUE    },
    { "ZX Spectrum",     DISP_BG_LIGHT_GRAY,   DISP_TEXT_BLACK         },
    { "VIC-20",          DISP_BG_LIGHT_GRAY,   DISP_TEXT_CYAN          },
    { "TI-99/4A",        DISP_BG_BRIGHT_CYAN,  DISP_TEXT_BLACK         },
    { "Amstrad CPC 464", DISP_BG_BLUE,         DISP_TEXT_BRIGHT_YELLOW },
    { "BBC Micro",       DISP_BG_BLACK,        DISP_TEXT_WHITE         },
    { "MSX Standard",    DISP_BG_BLUE,         DISP_TEXT_WHITE         },
    { "TRS-80 CoCo",     DISP_BG_GREEN,        DISP_TEXT_BLACK         },
    { "Atari 800",       DISP_BG_BLUE,         DISP_TEXT_LIGHT_BLUE    },
    { "IBM PC (MDA)",    DISP_BG_BLACK,        DISP_TEXT_BRIGHT_GREEN  },
    { "Commodore PET",   DISP_BG_BLACK,        DISP_TEXT_BRIGHT_GREEN  },
    { "Mac Classic",     DISP_BG_BRIGHT_WHITE, DISP_TEXT_BLACK         }
};

unsigned short display_bg_rgb(int index) {
    if (index < 0 || index >= DISP_BG_COLOR_N) index = DISP_BG_BLACK;
    return BG_RGB[index];
}

unsigned short display_text_rgb(int index) {
    if (index < 0 || index >= DISP_TEXT_N) index = DISP_TEXT_BRIGHT_GREEN;
    return TEXT_RGB[index];
}

const char *display_bg_color_name(int index) {
    if (index < 0 || index >= DISP_BG_COLOR_N) return "?";
    return BG_NAME[index];
}

const char *display_text_name(int index) {
    if (index < 0 || index >= DISP_TEXT_N) return "?";
    return TEXT_NAME[index];
}

const char *display_preset_name(int index) {
    if (index < 0 || index >= DISP_PRESET_N) return "?";
    return PRESETS[index].name;
}

int display_preset_bg(int index) {
    if (index < 0 || index >= DISP_PRESET_N) return DISP_BG_BLACK;
    return PRESETS[index].bg;
}

int display_preset_text(int index) {
    if (index < 0 || index >= DISP_PRESET_N) return DISP_TEXT_BRIGHT_GREEN;
    return PRESETS[index].text;
}

const char *display_palette_name(const DisplayState *d) {
    if (d->palette >= 0 && d->palette < DISP_PRESET_N
        && d->bg   == PRESETS[d->palette].bg
        && d->text == PRESETS[d->palette].text) {
        return PRESETS[d->palette].name;
    }
    return "Custom";
}

void display_defaults(DisplayState *d) {
    d->palette = 12;                        /* IBM PC (MDA): closest to the */
    d->bg      = PRESETS[12].bg;            /* previous hardcoded appearance */
    d->text    = PRESETS[12].text;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display && /tmp/test_display`

Expected: `test_display: OK`, with no compiler warnings.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/display.h saturn/src/display.c saturn/tests/test_display.c
git commit -m "Add display module: color tables, microcomputer presets, palette naming"
```

---

### Task 2: Selector cycling and the legibility guard

Adds the mutation logic: cycling each of the three selectors, skipping combinations that would render text invisible.

**Files:**
- Modify: `saturn/src/display.h`
- Modify: `saturn/src/display.c`
- Test: `saturn/tests/test_display.c`

**Interfaces:**
- Consumes: everything from Task 1.
- Produces: `display_set_images(const char *const *names, int count)`, `display_image_count(void)`, `display_bg_count(void)`, `display_is_image(const DisplayState*)`, `display_bg_name(const DisplayState*)`, `display_cycle_bg(DisplayState*, int dir)`, `display_cycle_text(DisplayState*, int dir)`, `display_cycle_palette(DisplayState*, int dir)`.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_display.c`, above `main()`:

```c
static const char *const IMAGES[] = { "HOUSE.TGA", "CAVE.TGA" };

static void test_image_registration(void) {
    display_set_images(NULL, 0);
    assert(display_image_count() == 0);
    assert(display_bg_count() == DISP_BG_COLOR_N);

    display_set_images(IMAGES, 2);
    assert(display_image_count() == 2);
    assert(display_bg_count() == DISP_BG_COLOR_N + 2);

    /* Over-cap registration clamps rather than overflowing. */
    display_set_images(IMAGES, DISP_IMAGE_MAX + 5);
    assert(display_image_count() == DISP_IMAGE_MAX);
    display_set_images(IMAGES, 2);
}

static void test_bg_name_and_is_image(void) {
    DisplayState d;
    display_set_images(IMAGES, 2);
    display_defaults(&d);
    assert(!display_is_image(&d));
    assert(strcmp(display_bg_name(&d), "Black") == 0);

    d.bg = DISP_BG_COLOR_N;          /* first image slot */
    assert(display_is_image(&d));
    assert(strcmp(display_bg_name(&d), "HOUSE.TGA") == 0);
    /* An image is never a preset background, so the palette reads Custom. */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
}

static void test_cycle_bg_wraps_through_images(void) {
    DisplayState d;
    display_set_images(IMAGES, 2);
    display_defaults(&d);
    d.text = DISP_TEXT_BRIGHT_AMBER;   /* matches no bg color: guard inactive */
    d.bg = DISP_BG_BRIGHT_WHITE;       /* last color */

    display_cycle_bg(&d, 1);
    assert(d.bg == DISP_BG_COLOR_N);       /* into the images */
    display_cycle_bg(&d, 1);
    assert(d.bg == DISP_BG_COLOR_N + 1);
    display_cycle_bg(&d, 1);
    assert(d.bg == DISP_BG_BLACK);         /* wraps back to the first color */
    display_cycle_bg(&d, -1);
    assert(d.bg == DISP_BG_COLOR_N + 1);   /* and backwards past the end */
}

static void test_legibility_guard(void) {
    DisplayState d;
    int i;
    display_set_images(NULL, 0);          /* colors only: guard fully active */
    display_defaults(&d);

    /* Black text must never be able to sit on the Black background. */
    d.text = DISP_TEXT_BLACK;
    d.bg   = DISP_BG_BRIGHT_WHITE;
    for (i = 0; i < 40; i++) {
        display_cycle_bg(&d, 1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }
    for (i = 0; i < 40; i++) {
        display_cycle_bg(&d, -1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }

    /* Same guard from the text side: Green text vs the Green background. */
    d.bg   = DISP_BG_GREEN;
    d.text = DISP_TEXT_BLACK;
    for (i = 0; i < 40; i++) {
        display_cycle_text(&d, 1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }
    for (i = 0; i < 40; i++) {
        display_cycle_text(&d, -1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }
}

static void test_guard_inactive_over_images(void) {
    DisplayState d;
    int i, seen_black = 0;
    display_set_images(IMAGES, 2);
    display_defaults(&d);
    d.bg = DISP_BG_COLOR_N;      /* image background: every text color reachable */
    for (i = 0; i < DISP_TEXT_N; i++) {
        display_cycle_text(&d, 1);
        if (d.text == DISP_TEXT_BLACK) seen_black = 1;
    }
    assert(seen_black);
}

static void test_cycle_palette(void) {
    DisplayState d;
    display_set_images(NULL, 0);
    display_defaults(&d);                  /* palette 12 */

    display_cycle_palette(&d, 1);
    assert(d.palette == 13);
    assert(d.bg == display_preset_bg(13) && d.text == display_preset_text(13));
    assert(strcmp(display_palette_name(&d), "Commodore PET") == 0);

    display_cycle_palette(&d, -1);
    assert(d.palette == 12);
    assert(strcmp(display_palette_name(&d), "IBM PC (MDA)") == 0);

    /* Wraps at both ends. */
    d.palette = DISP_PRESET_N - 1;
    d.bg = display_preset_bg(d.palette); d.text = display_preset_text(d.palette);
    display_cycle_palette(&d, 1);
    assert(d.palette == 0);
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PRESET_N - 1);

    /* From a Custom state, forward lands on preset 0 and back on the last. */
    display_defaults(&d);
    d.text = DISP_TEXT_CYAN;               /* now Custom */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_cycle_palette(&d, 1);
    assert(d.palette == 0);
    assert(d.bg == display_preset_bg(0) && d.text == display_preset_text(0));

    display_defaults(&d);
    d.text = DISP_TEXT_CYAN;
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PRESET_N - 1);

    /* Selecting a preset always yields a legible pair. */
    {
        int i;
        for (i = 0; i < DISP_PRESET_N; i++) {
            assert(display_bg_rgb(display_preset_bg(i))
                != display_text_rgb(display_preset_text(i)));
        }
    }
}
```

And extend `main()`:

```c
int main(void) {
    test_tables_well_formed();
    test_known_colors();
    test_preset_contents();
    test_defaults_and_palette_name();
    test_image_registration();
    test_bg_name_and_is_image();
    test_cycle_bg_wraps_through_images();
    test_legibility_guard();
    test_guard_inactive_over_images();
    test_cycle_palette();
    printf("test_display: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display`

Expected: FAIL — implicit declaration of `display_set_images`, `display_cycle_bg`, and the other new functions.

- [ ] **Step 3: Extend the header**

Add to `saturn/src/display.h`, inside the `extern "C"` block, after `display_defaults`:

```c
/* Register the bitmaps discovered in the disc's TGA folder. Names are borrowed,
   not copied: the caller must keep the array alive for the program's lifetime.
   count is clamped to DISP_IMAGE_MAX. */
void display_set_images(const char *const *names, int count);
int display_image_count(void);
int display_bg_count(void);          /* DISP_BG_COLOR_N + display_image_count() */

int display_is_image(const DisplayState *d);
const char *display_bg_name(const DisplayState *d);   /* color name or file name */

/* dir is +1 or -1. Cycling bg or text skips any candidate that would make the
   text the same color as the background. */
void display_cycle_bg(DisplayState *d, int dir);
void display_cycle_text(DisplayState *d, int dir);
void display_cycle_palette(DisplayState *d, int dir);
```

- [ ] **Step 4: Write the implementation**

Add to `saturn/src/display.c`, at the end:

```c
static const char *const *g_image_names = 0;
static int g_image_count = 0;

void display_set_images(const char *const *names, int count) {
    if (!names || count <= 0) { g_image_names = 0; g_image_count = 0; return; }
    if (count > DISP_IMAGE_MAX) count = DISP_IMAGE_MAX;
    g_image_names = names;
    g_image_count = count;
}

int display_image_count(void) { return g_image_count; }
int display_bg_count(void)    { return DISP_BG_COLOR_N + g_image_count; }

int display_is_image(const DisplayState *d) {
    return d->bg >= DISP_BG_COLOR_N && d->bg < display_bg_count();
}

const char *display_bg_name(const DisplayState *d) {
    if (display_is_image(d)) return g_image_names[d->bg - DISP_BG_COLOR_N];
    return display_bg_color_name(d->bg);
}

/* True when this background/text pairing would render text invisible. An image
   background has no single color to clash with, so the guard is inactive. */
static int clashes(int bg, int text) {
    if (bg >= DISP_BG_COLOR_N) return 0;
    return display_bg_rgb(bg) == display_text_rgb(text);
}

static int step(int value, int dir, int count) {
    value += dir;
    if (value >= count) value = 0;
    if (value < 0)      value = count - 1;
    return value;
}

void display_cycle_bg(DisplayState *d, int dir) {
    int count = display_bg_count();
    int next  = step(d->bg, dir, count);
    int tries = count;
    while (clashes(next, d->text) && tries-- > 0) next = step(next, dir, count);
    d->bg = next;
}

void display_cycle_text(DisplayState *d, int dir) {
    int next  = step(d->text, dir, DISP_TEXT_N);
    int tries = DISP_TEXT_N;
    while (clashes(d->bg, next) && tries-- > 0) next = step(next, dir, DISP_TEXT_N);
    d->text = next;
}

void display_cycle_palette(DisplayState *d, int dir) {
    int next;
    if (display_palette_name(d) == g_custom_label) {
        /* Currently Custom: enter the list at whichever end we are heading for. */
        next = (dir > 0) ? 0 : DISP_PRESET_N - 1;
    } else {
        next = step(d->palette, dir, DISP_PRESET_N);
    }
    d->palette = next;
    d->bg      = PRESETS[next].bg;
    d->text    = PRESETS[next].text;
}
```

The `Custom` check compares against a single shared string so the test is a
pointer comparison, not a `strcmp`. Replace the literal in
`display_palette_name` with that shared constant — change the top of the file
to add, just below the `PRESETS` table:

```c
static const char *const g_custom_label = "Custom";
```

and change `display_palette_name`'s final line from `return "Custom";` to:

```c
    return g_custom_label;
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display && /tmp/test_display`

Expected: `test_display: OK`, no warnings.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/display.h saturn/src/display.c saturn/tests/test_display.c
git commit -m "Add display selector cycling with legibility guard and image slots"
```

---

### Task 3: Save-blob codec

Encodes and decodes the four-byte display block appended to the `MOJOOPTS` backup-RAM blob.

**Files:**
- Modify: `saturn/src/display.h`
- Modify: `saturn/src/display.c`
- Test: `saturn/tests/test_display.c`

**Interfaces:**
- Consumes: everything from Tasks 1 and 2.
- Produces: `display_encode(const DisplayState*, unsigned char *out)` returning the byte count written (always 4), and `display_decode(const unsigned char *buf, int len, DisplayState *d)` returning 1 when a valid block was read and 0 when it fell back to defaults.

- [ ] **Step 1: Write the failing test**

Append to `saturn/tests/test_display.c`, above `main()`:

```c
static void test_encode_decode_roundtrip(void) {
    DisplayState a, b;
    unsigned char buf[8];
    int n;

    display_set_images(NULL, 0);
    display_defaults(&a);
    a.palette = 7; a.bg = display_preset_bg(7); a.text = display_preset_text(7);

    n = display_encode(&a, buf);
    assert(n == 4);
    assert(buf[0] == 1);            /* sentinel */

    assert(display_decode(buf, n, &b) == 1);
    assert(b.palette == a.palette && b.bg == a.bg && b.text == a.text);
}

static void test_collisions_roundtrip(void) {
    /* The regression the stored palette index exists to prevent: identical
       colors must still reload as the machine the player picked. */
    DisplayState a, b;
    unsigned char buf[8];
    int pairs[2][2] = { { 3, 11 }, { 12, 13 } };
    int p, s;

    display_set_images(NULL, 0);
    for (p = 0; p < 2; p++) {
        for (s = 0; s < 2; s++) {
            int idx = pairs[p][s];
            a.palette = idx;
            a.bg = display_preset_bg(idx);
            a.text = display_preset_text(idx);
            display_encode(&a, buf);
            assert(display_decode(buf, 4, &b) == 1);
            assert(b.palette == idx);
            assert(strcmp(display_palette_name(&b), display_preset_name(idx)) == 0);
        }
    }
}

static void test_custom_state_roundtrips(void) {
    DisplayState a, b;
    unsigned char buf[8];

    display_set_images(NULL, 0);
    display_defaults(&a);
    a.text = DISP_TEXT_CYAN;                     /* diverged -> Custom */
    assert(strcmp(display_palette_name(&a), "Custom") == 0);

    display_encode(&a, buf);
    assert(buf[1] == 12);                        /* the machine index survives */
    assert(display_decode(buf, 4, &b) == 1);
    assert(b.text == DISP_TEXT_CYAN);
    assert(strcmp(display_palette_name(&b), "Custom") == 0);
}

static void test_decode_rejects_bad_input(void) {
    DisplayState d, def;
    unsigned char buf[8];

    display_set_images(NULL, 0);
    display_defaults(&def);

    /* Absent block. */
    assert(display_decode(NULL, 0, &d) == 0);
    assert(d.palette == def.palette && d.bg == def.bg && d.text == def.text);

    /* Truncated block. */
    buf[0] = 1; buf[1] = 3; buf[2] = 0;
    assert(display_decode(buf, 3, &d) == 0);
    assert(d.palette == def.palette);

    /* Wrong sentinel. */
    buf[0] = 9; buf[1] = 3; buf[2] = 2; buf[3] = 3;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.palette == def.palette);

    /* Out-of-range palette, background, and text each fall back. */
    buf[0] = 1; buf[1] = 99; buf[2] = 2; buf[3] = 3;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.palette == def.palette);

    buf[0] = 1; buf[1] = 3; buf[2] = 99; buf[3] = 3;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.bg == def.bg);

    buf[0] = 1; buf[1] = 3; buf[2] = 2; buf[3] = 99;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.text == def.text);
}

static void test_decode_missing_image_falls_back(void) {
    DisplayState a, b;
    unsigned char buf[8];

    /* Saved while two images were on the disc... */
    display_set_images(IMAGES, 2);
    display_defaults(&a);
    a.bg = DISP_BG_COLOR_N + 1;
    display_encode(&a, buf);

    /* ...reloaded on a disc with none: the background reverts to a color. */
    display_set_images(NULL, 0);
    assert(display_decode(buf, 4, &b) == 0);
    assert(!display_is_image(&b));
    assert(b.bg == display_preset_bg(b.palette) || b.bg < DISP_BG_COLOR_N);
    display_set_images(IMAGES, 2);
}
```

And extend `main()` with the five new calls, in that order, before the
`printf`:

```c
    test_encode_decode_roundtrip();
    test_collisions_roundtrip();
    test_custom_state_roundtrips();
    test_decode_rejects_bad_input();
    test_decode_missing_image_falls_back();
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display`

Expected: FAIL — implicit declaration of `display_encode` and `display_decode`.

- [ ] **Step 3: Extend the header**

Add to `saturn/src/display.h`, inside the `extern "C"` block:

```c
#define DISP_BLOB_BYTES 4    /* [sentinel=1][palette][bg][text] */

/* Write the display block. out must have room for DISP_BLOB_BYTES.
   Returns the number of bytes written. */
int display_encode(const DisplayState *d, unsigned char *out);

/* Read the display block. Any field that is absent, truncated, mis-sentinelled,
   out of range, or naming an image slot the current disc does not have falls
   back to its default. Returns 1 when the whole block was accepted, 0 when any
   part of it was defaulted. Call display_set_images() first, so image-slot
   validation has the real count. */
int display_decode(const unsigned char *buf, int len, DisplayState *d);
```

- [ ] **Step 4: Write the implementation**

Add to `saturn/src/display.c`, at the end:

```c
int display_encode(const DisplayState *d, unsigned char *out) {
    out[0] = 1;                                /* block sentinel */
    out[1] = (unsigned char) d->palette;       /* always a real preset index */
    out[2] = (unsigned char) d->bg;
    out[3] = (unsigned char) d->text;
    return DISP_BLOB_BYTES;
}

int display_decode(const unsigned char *buf, int len, DisplayState *d) {
    int ok = 1;
    display_defaults(d);

    if (!buf || len < DISP_BLOB_BYTES || buf[0] != 1) return 0;

    if (buf[1] < DISP_PRESET_N)  d->palette = (int) buf[1];  else ok = 0;
    /* An image index is valid only if that image is on the disc right now. */
    if (buf[2] < display_bg_count()) d->bg   = (int) buf[2];  else ok = 0;
    if (buf[3] < DISP_TEXT_N)    d->text    = (int) buf[3];  else ok = 0;

    return ok;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display && /tmp/test_display`

Expected: `test_display: OK`, no warnings.

- [ ] **Step 6: Verify no other host test regressed**

Run:

```bash
cd saturn
/c/msys64/mingw64/bin/gcc -std=c11 -I src tests/test_console.c src/console.c -o /tmp/test_console && /tmp/test_console
/c/msys64/mingw64/bin/gcc -std=c11 -I src tests/test_keyboard.c src/keyboard.c -o /tmp/test_keyboard && /tmp/test_keyboard
```

Expected: `test_console: OK` and `test_keyboard: OK`.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/display.h saturn/src/display.c saturn/tests/test_display.c
git commit -m "Add display save-blob codec with range and image-slot validation"
```

---

### Task 4: Apply colors on hardware and persist them

Wires the module into `main.cxx`: the state is loaded, saved, and applied to VDP2. No menu yet — after this task the game boots in the default palette (black on bright green) and the title screen stays white, which is the visible proof the plumbing works.

**Files:**
- Modify: `saturn/src/main.cxx` (near line 34 for state, 241/275 for persistence, 1989/2494/2504 for the title screen, 2457 for init)
- Modify: `saturn/CMakeLists.txt`

**Interfaces:**
- Consumes: `DisplayState`, `display_defaults`, `display_encode`, `display_decode`, `display_is_image`, `display_bg_rgb`, `display_text_rgb` from Tasks 1–3.
- Produces: `g_display` (file-scope `DisplayState`) and `display_apply()`, both used by Tasks 5 and 6.

- [ ] **Step 1: Add the module to the Saturn build**

In `saturn/CMakeLists.txt`, find the source list that already names `src/console.c` and `src/keyboard.c`, and add `src/display.c` alongside them, keeping the existing ordering convention.

- [ ] **Step 2: Include the header and declare the state**

In `saturn/src/main.cxx`, add to the include block:

```cpp
#include "display.h"
```

Then, next to the other option globals near line 34 (`g_sel_track` and friends), add:

```cpp
// Display colors (Options > Display). Applied to VDP2 by display_apply();
// persisted in MOJOOPTS alongside the other options.
static DisplayState g_display;
```

- [ ] **Step 3: Write the apply function**

Add above `options_load` (near `main.cxx:241`), after the `title_bg_show` /
`title_bg_hide` forward declarations if present — otherwise add forward
declarations for them here:

```cpp
static void title_bg_show(void);
static void title_bg_hide(void);

// Push g_display to the hardware. Index 15 is the color index the SGL font
// glyphs use -- and the one install_block_glyph() fills -- so this recolors
// body text, menus, the on-screen keyboard, and the cursor in one call.
// (SRL::Debug::PrintColorSet is not usable here: it sets slCurColor while
// Debug::Print reads ASCII::colorBank.)
static void display_apply(void) {
    SRL::ASCII::SetColor(display_text_rgb(g_display.text), 15);
    if (display_is_image(&g_display)) {
        title_bg_show();       // image backgrounds arrive in a later task
    } else {
        title_bg_hide();
        SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
    }
}
```

- [ ] **Step 4: Load and save the display block**

In `options_load` (`main.cxx:241`), after the sound block that reads at offset
`s`, add:

```c
    // Display block follows the sound block: sentinel 1, then [palette][bg][text].
    // display_decode() range-checks every field and falls back to defaults.
    int dsp = s + 3;
    if (dsp + DISP_BLOB_BYTES <= (int) sizeof(buf)) {
        display_decode(buf + dsp, DISP_BLOB_BYTES, &g_display);
    }
```

In `options_save` (`main.cxx:275`), after the three sound bytes and before the
`saturn_bup_write` call, add:

```c
    if (n + DISP_BLOB_BYTES <= 62) n += display_encode(&g_display, buf + n);
```

- [ ] **Step 5: Initialize before loading, and apply after**

`display_decode` validates image indices against `display_bg_count()`, so the
image list must be registered before `options_load()` runs. Task 6 adds the
discovery call; for now, seed the defaults. At the top of `main()`, before the
existing `options_load()` call, add:

```cpp
    display_defaults(&g_display);
```

- [ ] **Step 6: Keep the title screen white, then apply**

At `main.cxx:2494` where `title_bg_show()` is called, force white text for the
title regardless of the saved palette:

```cpp
    SRL::ASCII::SetColor(DISP_RGB555(0xFF, 0xFF, 0xFF), 15);
    title_bg_show();
```

At `main.cxx:2504`, replace the unconditional `title_bg_hide();` with a call
that hands control to the player's setting:

```cpp
    display_apply();   // colors (or an image background) take over from the title
```

`display_apply()` calls `title_bg_hide()` itself on the color path, so the old
behavior is preserved when no image is selected.

- [ ] **Step 7: Verify the module still builds standalone**

The Saturn build is the user's to run, but `display.c` must stay free of SRL
dependencies. Run:

```bash
cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display && /tmp/test_display
```

Expected: `test_display: OK`.

- [ ] **Step 8: Ask the user to build and confirm**

Report: the changes are in place; `saturn/compile.bat` needs to be run.
Expected on hardware: the title screen renders with white text as before, and
after leaving the title the game text is bright green on black. Wait for the
user's confirmation before committing.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/main.cxx saturn/CMakeLists.txt
git commit -m "Apply saved display colors to VDP2 and persist them in MOJOOPTS"
```

---

### Task 5: The Display Options page

Adds the menu itself, modeled on `sound_options_page()`.

**Files:**
- Modify: `saturn/src/main.cxx` (forward declarations near line 294, new page near line 1519, menu entry at 1647)

**Interfaces:**
- Consumes: `g_display`, `display_apply()` from Task 4; the cycling and naming functions from Tasks 1–2.
- Produces: `display_options_page()`.

- [ ] **Step 1: Forward-declare the page**

Next to the other forward declarations at `main.cxx:294`:

```cpp
static void display_options_page(void);
```

- [ ] **Step 2: Write the page**

Add after `sound_options_page()` ends (`main.cxx:1633`):

```cpp
// Display Options (full-screen, OK/Cancel). Unlike Sound Options every row is
// always present -- there is no hardware dependency. Left/Right applies live so
// the result is visible behind the menu; Cancel restores the snapshot.
static void display_options_page(void) {
    enum { DR_PALETTE, DR_BG, DR_TEXT, DR_OK, DR_CANCEL };
    static const int rows[] = { DR_PALETTE, DR_BG, DR_TEXT, DR_OK, DR_CANCEL };
    const int nrows = (int)(sizeof(rows) / sizeof(rows[0]));

    int sel = 0;
    DisplayState snapshot = g_display;   // for Cancel
    SRL::Core::Synchronize();            // consume the edge that opened this
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + nrows) % nrows;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % nrows;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool ok   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                  || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool cancel = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                    || ke.kind == SATURN_KEY_BACKSPACE;
        int row = rows[sel];

        if (cancel || (ok && row == DR_CANCEL)) {
            g_display = snapshot;
            display_apply();
            break;
        }
        int dir = right ? 1 : (left ? -1 : 0);
        if (dir != 0) {
            if      (row == DR_PALETTE) display_cycle_palette(&g_display, dir);
            else if (row == DR_BG)      display_cycle_bg(&g_display, dir);
            else if (row == DR_TEXT)    display_cycle_text(&g_display, dir);
            if (row == DR_PALETTE || row == DR_BG || row == DR_TEXT) display_apply();
        }
        if (ok && row == DR_OK) { options_save(); break; }

        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "DISPLAY OPTIONS"); y += 2;
        for (int i = 0; i < nrows; i++) {
            char cur = (i == sel) ? '>' : ' ';
            switch (rows[i]) {
                case DR_PALETTE:
                    SRL::Debug::Print(x, y, "%c System Palette", cur);
                    SRL::Debug::Print(x + 17, y++, "< %s >", display_palette_name(&g_display));
                    break;
                case DR_BG:
                    SRL::Debug::Print(x, y, "%c Background", cur);
                    SRL::Debug::Print(x + 17, y++, "< %s >", display_bg_name(&g_display));
                    break;
                case DR_TEXT:
                    SRL::Debug::Print(x, y, "%c Text", cur);
                    SRL::Debug::Print(x + 17, y++, "< %s >", display_text_name(g_display.text));
                    break;
                case DR_OK:
                    y++;   // blank separator before the actions
                    SRL::Debug::Print(x, y++, "%c OK", cur);
                    break;
                case DR_CANCEL:
                    SRL::Debug::Print(x, y++, "%c Cancel", cur);
                    break;
            }
        }
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("<> change  A/Start=OK  B=Cancel",
                                             "<> change  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}
```

- [ ] **Step 3: Add the Options menu entry**

In `options_menu()` (`main.cxx:1647`), extend the enum and the item list.
Change:

```cpp
    enum { OI_DIFF, OI_CONFIG, OI_CONTROLS, OI_SOUND, OI_RETURN, OI_DONE };
```

to:

```cpp
    enum { OI_DIFF, OI_CONFIG, OI_CONTROLS, OI_DISPLAY, OI_SOUND, OI_RETURN, OI_DONE };
```

Change `int items[6], nitems = 0;` to `int items[7], nitems = 0;` and insert
after the `OI_CONTROLS` line:

```cpp
    items[nitems++] = OI_DISPLAY;   // always available: no hardware dependency
```

In the activation block, after the `OI_CONTROLS` branch, add:

```cpp
            else if (item == OI_DISPLAY) { display_options_page(); menu_clear_full(); }
```

Find the drawing loop below that renders each item's label and add a case for
`OI_DISPLAY` with the label `"Display"`, matching how `OI_SOUND` renders
`"Sound"`.

- [ ] **Step 4: Verify the host tests still pass**

Run:

```bash
cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display && /tmp/test_display
```

Expected: `test_display: OK`.

- [ ] **Step 5: Ask the user to build and confirm**

Report that `saturn/compile.bat` needs to be run. Expected on hardware:

- Options now lists Display between Controls and Sound.
- Cycling System Palette snaps both colors and the screen changes live.
- Changing Background or Text flips the name to `Custom`; restoring the value
  brings the machine name back.
- Body text, menus, the on-screen keyboard, and the block cursor all recolor
  together.
- Cancel reverts the live preview; OK persists across a power cycle.
- The guard makes Black-on-Black unreachable.

Wait for confirmation before committing.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Add Display Options page with live color preview"
```

---

### Task 6: Background images from the disc

Discovers the bitmaps in the disc's `TGA` directory and lets the background
selector cycle into them.

**Files:**
- Modify: `saturn/src/main.cxx` (near `title_bg_show` at 1989, and `main()` before `options_load`)

**Interfaces:**
- Consumes: `display_set_images`, `display_is_image`, `display_bg_name` from Task 2; `display_apply()` from Task 4.
- Produces: `display_scan_images()`, and a `title_bg_show(const char *file)` overload.

- [ ] **Step 1: Generalize the background loader**

`title_bg_show()` (`main.cxx:1989`) hardcodes `"HOUSE.TGA"` and caches with a
plain `static bool loaded`. It needs to take a file name, reload when the name
changes, and report failure. Replace the whole function with:

```cpp
// Returns false when the bitmap could not be loaded, so the caller can fall
// back to a color background rather than leaving static on screen.
static bool title_bg_show(const char *file) {
    static char loaded[16] = "";       // file currently resident in VDP2 VRAM
    if (strncmp(loaded, file, sizeof(loaded) - 1) != 0) {
        // CD read + VRAM upload. Bitmaps are opened by bare file name, exactly
        // as HOUSE.TGA already is -- proven to resolve regardless of the
        // current CD directory. Must run before menu CD-DA starts: the single
        // CD head can't read data while playing audio.
        // Every image must be 256-color paletted, not truecolor: SRL's VDP2
        // bitmap allocator doubles the container size for RGB555, pushing a
        // 512x256 bitmap to 256KB and across the A0/A1 VRAM bank boundary.
        // Bank-spanning bitmaps render as static (slBitMapNbg0 never reserves
        // the second bank in VDP2_RAMCTL -- see the note at the top of
        // srl_vdp2.hpp). At 8bpp the container is exactly 128KB and fits one
        // bank.
        SRL::Bitmap::TGA* bmp = new SRL::Bitmap::TGA(file);
        if (!bmp) return false;
        SRL::VDP2::NBG0::LoadBitmap(bmp);
        delete bmp;   // pixels now live in VDP2 VRAM; free the work-RAM copy
        SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);  // below text (Layer7)
        // LoadBitmap leaves stray debug prints on rows 20-21 ("4bpp" / "Pal: N")
        // from SRL itself (srl_vdp2.hpp:869,888). Patching the library would not
        // survive a fresh submodule checkout, so wipe the rows here instead.
        SRL::Debug::PrintClearLine(20);
        SRL::Debug::PrintClearLine(21);
        strncpy(loaded, file, sizeof(loaded) - 1);
        loaded[sizeof(loaded) - 1] = '\0';
    }
    SRL::VDP2::NBG0::ScrollEnable();
    return true;
}

static void title_bg_show(void) { (void) title_bg_show("HOUSE.TGA"); }
```

Note the original comment's claim that this "runs before the CD directory
changes to Z3" no longer holds once the page can load images mid-game. Opening
by bare file name is what makes that safe, and it is the behavior HOUSE.TGA
already relies on.

- [ ] **Step 2: Write the discovery function**

Model this on the existing `scan_z3_folder()` (`main.cxx:2068`), which already
solves this exact problem: it uses a private `GfsDirTbl` rather than SRL's
shared one, and returns to the root directory first so the scan is idempotent
across soft resets. Copy that structure rather than using
`SRL::Cd::ChangeDir("TGA")`, which would leave the shared table pointing at TGA
and disturb later story-file access.

Add just above `title_bg_show`:

```cpp
// Names of the bitmaps found in the disc's TGA folder, scanned once at boot.
// display.c borrows these pointers, so the storage must outlive the scan.
static char        g_image_name[DISP_IMAGE_MAX][16];
static const char *g_image_ptr[DISP_IMAGE_MAX];

// ISO9660 entries can arrive as "NAME.TGA;1". Accept only .TGA files; the
// deeper 8bpp/one-bank check cannot be done from the name and happens at load
// time, where a failure falls back to a color background.
static bool tga_name_is_usable(const char *name) {
    if (!name || !name[0] || name[0] == '.') return false;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.' && name[i+1] == 'T' && name[i+2] == 'G'
            && name[i+3] == 'A') return true;
    }
    return false;
}

static void display_scan_images(void) {
    static GfsDirName dirnames[SRL_MAX_CD_FILES];
    static GfsDirTbl  tbl;
    int found = 0;

    // Same idempotence dance as scan_z3_folder: get back to the root before
    // resolving "TGA", or after a soft reset it would resolve relative to
    // whatever directory we were left in.
    SRL::Cd::ChangeDir((char *) nullptr);

    int32_t fid = GFS_NameToId((int8_t *) "TGA");
    if (fid >= 0) {
        GFS_DIRTBL_TYPE(&tbl)    = GFS_DIR_NAME;
        GFS_DIRTBL_DIRNAME(&tbl) = dirnames;
        GFS_DIRTBL_NDIR(&tbl)    = SRL_MAX_CD_FILES;
        int32_t n = GFS_LoadDir(fid, &tbl);
        for (int32_t i = 0; i < n && found < DISP_IMAGE_MAX; i++) {
            const char *name = (const char *) dirnames[i];
            if (!tga_name_is_usable(name)) continue;
            int c = 0;
            while (name[c] && name[c] != ';' && c < (int) sizeof(g_image_name[0]) - 1) {
                g_image_name[found][c] = name[c]; c++;
            }
            g_image_name[found][c] = '\0';
            g_image_ptr[found] = g_image_name[found];
            found++;
        }
    }

    // Leave the CD where we found it: at the root, which is where the existing
    // boot sequence expects to be before scan_z3_folder runs.
    SRL::Cd::ChangeDir((char *) nullptr);
    display_set_images(found > 0 ? g_image_ptr : NULL, found);
}
```

If `GFS_LoadDir`'s entry layout differs from `scan_z3_folder`'s handling of
`dirnames[i]`, match whatever that function does — it is the working reference
in this codebase. Read it before writing this step.

- [ ] **Step 3: Fail soft when an image will not render**

In `display_apply()` (Task 4), if the TGA fails to load, fall back to a color
background rather than leaving static on screen. Change the image branch to:

```cpp
    if (display_is_image(&g_display)) {
        if (!title_bg_show(display_bg_name(&g_display))) {
            g_display.bg = display_preset_bg(g_display.palette);
            title_bg_hide();
            SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
        }
    } else {
```

Step 1 already gives `title_bg_show(const char *)` its `bool` return.

- [ ] **Step 4: Call discovery before loading options**

In `main()`, replace the `display_defaults(&g_display);` line added in Task 4
with:

```cpp
    display_scan_images();          // must precede options_load: display_decode()
    display_defaults(&g_display);   // validates image indices against this list
```

Two ordering constraints, both of which must hold where this is placed:

- `display_scan_images()` needs the CD subsystem up, so it must sit after
  `SRL::Core::Initialize()` (`main.cxx:2457`).
- It must run before `options_load()`, so `display_decode()` can reject a saved
  image index that the current disc does not have.

It leaves the CD at the root, which is the state `scan_z3_folder()` expects, so
placing it immediately before the existing boot-time scan is safest.

- [ ] **Step 5: Verify the host tests still pass**

Run:

```bash
cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display && /tmp/test_display
```

Expected: `test_display: OK`.

- [ ] **Step 6: Ask the user to build and confirm**

Report that `saturn/compile.bat` needs to be run. Expected on hardware:

- Cycling Background past Bright White reaches `HOUSE.TGA`.
- Selecting it shows the image behind the game text, and the palette name reads
  `Custom`.
- The image survives leaving the title screen and a power cycle.
- Cycling one more step wraps back to Black.

Note for the user: text over `HOUSE.TGA` is expected to be hard to read. That
is the readability question the spec flagged as a judgment to make after seeing
it on hardware, and dimming is explicitly out of scope for this pass.

To test the fallback path, copy any truecolor TGA into
`saturn/cd/data/TGA/` and confirm it is absent from the selector rather than
rendering as static. `tools/make_house_tga.py` is the conversion path for
adding a real one.

Wait for confirmation before committing.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Add disc-scanned TGA background images to the Display Options selector"
```

---

## Verification summary

After Task 6 the following should all hold:

- `test_display`, `test_console`, and `test_keyboard` all pass on the host.
- Options > Display cycles 15 machine palettes, 7 background colors, 8 text
  colors, and any usable bitmap in the disc's `TGA` folder.
- The C64/Atari 800 and IBM PC/PET collisions round-trip to the right machine
  name through save and reload.
- Background and text can never be set to the same color.
- The title screen is unchanged: HOUSE.TGA with white text.
- A blob saved before this feature loads at the IBM PC (MDA) defaults.
