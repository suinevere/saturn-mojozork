# Menu Chrome, Background Loading, and Text Color Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix TGA backgrounds that never render and text colors that never apply, then surface each background as a System Palette preset and give every menu page a centered bordered frame with consistent naming.

**Architecture:** Two bug fixes land first in `saturn/src/main.cxx` (CD directory discipline around bitmap opens; a local CRAM write replacing an unusable SRL API). Then `saturn/src/display.c` grows an image-preset index space behind accessor functions, and a single `menu_frame` helper replaces per-page hand-rolled chrome.

**Tech Stack:** C11 / C++ for Sega Saturn via SaturnRingLib (pinned submodule, SH-2 gcc 14.2.0). Host-side unit tests build with MinGW gcc.

**Spec:** `docs/superpowers/specs/2026-07-18-menu-chrome-and-display-fixes-design.md`

## Global Constraints

- **Never edit the `SaturnRingLib/` submodule.** All fixes live in `saturn/src/`. A fresh checkout of the pinned SDK must still build.
- **Do not run `compile.bat`.** The user builds. Make edits and stop; on-target verification is the user's step.
- Host tests build with: `cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display && /tmp/test_display`
- Expected host test output: `test_display: OK` with zero compiler warnings.
- Screen geometry: 40 columns (`CONSOLE_COLS`, columns 0–39), 28 usable rows (`SCREEN_ROWS` in `main.cxx:160`).
- `display.c` is plain C11 and must stay compilable by both SH-2 gcc and host gcc. No C++ constructs.
- Commit after each task. Do not squash tasks together.

## Already satisfied — do not implement

The spec's "Legibility guard" item under Phase 2a is **already done**. `display.c`'s `clashes()` returns 0 when `bg >= DISP_BG_COLOR_N`, and `test_guard_inactive_over_images` in `saturn/tests/test_display.c` covers it. Verify it still passes; add nothing.

---

### Task 1: CD directory discipline for bitmap loads

Fixes the reported "TGA files not loading Background". `scan_z3_folder` calls `GFS_SetDir` and leaves `Z3/` as the ambient CD directory; `title_bg_show` opens bitmaps by bare filename, so every load after the catalog scan resolves inside `Z3/` and fails.

**Files:**
- Modify: `saturn/src/main.cxx` (add helpers near `display_scan_images`; change `title_bg_show` at `:2182-2224`; change `scan_z3_folder` at `:2281-2299`; fix comment at `:2126-2127`)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `static void cd_enter_root(void)` and `static void cd_restore_z3(void)`, both file-scope in `main.cxx`. Task 4 does not use them; no later task depends on this one.

**No host test exists for this.** It depends on Saturn CD file-system state and cannot be unit tested. Verification is compile-correctness plus the user's on-target run.

- [ ] **Step 1: Read the current `scan_z3_folder` to find its dir table**

Run: `cd saturn/src && sed -n '2281,2310p' main.cxx`

Confirm it declares a `GfsDirTbl` (likely `static`) and calls `GFS_SetDir(&tbl)`. Note the exact variable names — the next step hoists them to file scope.

- [ ] **Step 2: Hoist the Z3 directory table to file scope**

In `main.cxx`, immediately **above** `static void display_scan_images(void)` (currently line 2129), insert:

```cpp
// ---- CD current-directory discipline ---------------------------------------
// scan_z3_folder() calls GFS_SetDir() to make Z3 the current CD directory, and
// that persists for the rest of the session. Anything opened by bare file name
// afterwards resolves inside Z3 -- which is why background bitmaps silently
// failed to load once the game catalog had been scanned. Bitmap loads bracket
// themselves with these two helpers instead of assuming a directory.
static GfsDirName g_z3_dirnames[SRL_MAX_CD_FILES];
static GfsDirTbl  g_z3_tbl;
static bool       g_z3_dir_valid = false;   // set once scan_z3_folder has run

static void cd_enter_root(void) {
    SRL::Cd::ChangeDir((char *) nullptr);
}

// Re-point the CD at Z3 so story-file opens keep working. No-op before the
// catalog scan has populated g_z3_tbl.
static void cd_restore_z3(void) {
    if (g_z3_dir_valid) GFS_SetDir(&g_z3_tbl);
}
```

- [ ] **Step 3: Point `scan_z3_folder` at the hoisted table**

In `scan_z3_folder` (around `main.cxx:2281`), replace its local `dirnames` / `tbl` declarations with the file-scope ones, and set the validity flag after `GFS_SetDir` succeeds.

Delete the local declarations (the `static GfsDirName dirnames[...]` and `static GfsDirTbl tbl;` lines inside the function), then replace every `dirnames` with `g_z3_dirnames` and every `tbl` with `g_z3_tbl` inside that function.

Immediately after the existing `GFS_SetDir(&g_z3_tbl);` line, add:

```cpp
    g_z3_dir_valid = true;   // cd_restore_z3() may now re-apply this directory
```

- [ ] **Step 4: Bracket the bitmap load in `title_bg_show`**

In `title_bg_show` (`main.cxx:2182`), the load block currently begins with `SRL::Bitmap::TGA* bmp = new SRL::Bitmap::TGA(file);` at line 2201. Replace the whole `if (!same) { ... }` body's load-and-check section so the CD is at root for the open and restored on **every** exit path.

Replace these lines:

```cpp
        SRL::Bitmap::TGA* bmp = new SRL::Bitmap::TGA(file);
        if (!bmp) return false;   // operator new returns null on OOM here (see srl_memory.hpp)
        if (bmp->GetInfo().Palette == nullptr) {
            // Truecolor (or any other non-paletted) image: not the 8bpp shape
            // this loader/VRAM layout requires. Reject before it ever reaches
            // VRAM instead of risking the bank-spanning static above.
            delete bmp;
            return false;
        }
```

with:

```cpp
        // Open at the root: scan_z3_folder() leaves Z3 as the current CD
        // directory, and a bare-name open would resolve there and fail. Every
        // exit path below must cd_restore_z3() or story-file opens break.
        cd_enter_root();
        SRL::Bitmap::TGA* bmp = new SRL::Bitmap::TGA(file);
        if (!bmp) {               // operator new returns null on OOM (srl_memory.hpp)
            cd_restore_z3();
            return false;
        }
        if (bmp->GetInfo().Palette == nullptr) {
            // Truecolor (or any other non-paletted) image: not the 8bpp shape
            // this loader/VRAM layout requires. Reject before it ever reaches
            // VRAM instead of risking the bank-spanning static above.
            delete bmp;
            cd_restore_z3();
            return false;
        }
        cd_restore_z3();
```

The `cd_restore_z3()` on the success path must come **after** the `new SRL::Bitmap::TGA(file)` completes (the constructor performs the CD read) and before `SRL::VDP2::NBG0::LoadBitmap(bmp)`, which touches only VRAM.

- [ ] **Step 5: Correct the false comment**

In `main.cxx`, replace the comment text at lines 2126-2128:

```cpp
// leave the shared directory pointed at TGA. Unlike scan_z3_folder, this never
// calls GFS_SetDir -- bitmaps are opened by bare file name (see title_bg_show),
// so there is no need to leave the shared directory pointed at TGA.
```

with:

```cpp
// Unlike scan_z3_folder, this never calls GFS_SetDir. Bitmaps are opened by
// bare file name, which resolves against whatever directory is current -- so
// title_bg_show brackets its own load with cd_enter_root()/cd_restore_z3()
// rather than relying on ambient state. An earlier comment here claimed bare
// names resolve regardless of current directory; that was wrong, and it is why
// backgrounds silently failed once the Z3 catalog scan had run.
```

Match the surrounding comment block exactly when editing — read lines 2120-2130 first to get the precise existing text.

- [ ] **Step 6: Verify the source is self-consistent**

Run: `cd saturn/src && grep -n "cd_enter_root\|cd_restore_z3\|g_z3_dir_valid\|g_z3_tbl\|g_z3_dirnames" main.cxx`

Expected: `cd_enter_root` appears twice (definition + one call in `title_bg_show`); `cd_restore_z3` appears four times (definition + three calls covering the OOM, non-paletted, and success paths); `g_z3_dir_valid` appears three times (declaration, set in `scan_z3_folder`, read in `cd_restore_z3`); no stray references to the removed local `tbl` / `dirnames` remain inside `scan_z3_folder`.

Run: `cd saturn/src && grep -n "static GfsDirTbl\|static GfsDirName" main.cxx`

Expected: the `display_scan_images` pair plus the hoisted `g_z3_*` pair. If `scan_z3_folder` still declares its own, Step 3 was incomplete.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Load background bitmaps from the CD root, not the ambient directory

scan_z3_folder's GFS_SetDir leaves Z3 as the current CD directory for the
rest of the session. title_bg_show opens bitmaps by bare file name, so
every background load after the catalog scan resolved inside Z3, found
nothing, and fell back to a color background. The title screen worked only
because it loads before that scan.

Bracket the load with cd_enter_root()/cd_restore_z3() on all three exit
paths, and correct the comment that claimed bare-name opens were
directory-independent."
```

---

### Task 2: Text color via direct CRAM write

Fixes "none of the text color changes land". `SRL::ASCII::SetColor` computes its CRAM address as `(colorBank >> 6)` — byte 64 for the default palette 1 — but CRAM is initialized `CRM16_2048` (2 bytes/entry), where palette 1 begins at byte 32. Its writes land in unused color RAM.

**Files:**
- Modify: `saturn/src/main.cxx` (add helper above `display_apply` at `:250`; change call sites at `:256` and `:2709`)

**Interfaces:**
- Consumes: nothing.
- Produces: `static void text_set_color(unsigned short rgb555)` at file scope in `main.cxx`.

**No host test exists for this.** It writes a hardware register address. Verification is compile-correctness plus the user's on-target run.

- [ ] **Step 1: Add the helper**

In `main.cxx`, immediately **above** the `// Push g_display to the hardware.` comment block at line 250, insert:

```cpp
// Set the color the SGL font glyphs render in.
//
// SRL::ASCII::SetColor is unusable: it computes its CRAM address as
// (colorBank >> 6), which is byte offset 64 for the default palette 1
// (colorBank = 1 << 12, srl_ascii.hpp:23). But SRL initializes color RAM as
// CRM16_2048 (srl_vdp2.hpp:1498) -- 16-bit entries, 2 bytes each -- so palette
// 1 of a 4bpp cell begins at byte offset 32. The shift is off by one bit, so
// every SetColor write lands in unused CRAM and never reaches a glyph. Text
// looked white only because the font's default palette-1 entry 15 already is.
//
// ASCII::colorBank is private, so the bank cannot be read back; the layout is
// pinned by SRL's own initialization above. Index 15 is the color the SGL font
// glyphs use, and the one install_block_glyph() fills.
//
// VDP2_COLRAM (sl_def.h:981) is a bare integer address, not a pointer, so the
// cast is required. It reaches this file via <srl.hpp>.
#define ASCII_PAL1_CRAM (VDP2_COLRAM + 32)   /* palette 1, CRM16_2048 */
static void text_set_color(unsigned short rgb555) {
    ((volatile unsigned short *) ASCII_PAL1_CRAM)[15] = rgb555;
}
```

- [ ] **Step 2: Replace the `display_apply` call site**

In `display_apply` (`main.cxx:256`), replace:

```cpp
    SRL::ASCII::SetColor(display_text_rgb(g_display.text), 15);
```

with:

```cpp
    text_set_color(display_text_rgb(g_display.text));
```

- [ ] **Step 3: Replace the boot call site**

In `main()` (`main.cxx:2709`), replace:

```cpp
    SRL::ASCII::SetColor(DISP_RGB555(0xFF, 0xFF, 0xFF), 15);
```

with:

```cpp
    text_set_color(DISP_RGB555(0xFF, 0xFF, 0xFF));
```

- [ ] **Step 4: Verify no `SetColor` call sites remain**

Run: `cd saturn/src && grep -n "ASCII::SetColor\|text_set_color" main.cxx`

Expected: zero `ASCII::SetColor` matches; three `text_set_color` matches (definition + two call sites). Any remaining `ASCII::SetColor` is a missed site.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Write the font color to the CRAM slot the hardware actually reads

SRL::ASCII::SetColor computes its CRAM address as (colorBank >> 6), giving
byte 64 for the default palette 1. CRAM is initialized CRM16_2048 -- 2 bytes
per entry -- so palette 1 begins at byte 32. The shift is off by one bit, so
every write landed in unused color RAM and no text color change was ever
visible. Text read as white because the font's default entry 15 already is.

Write the correct slot from a local helper rather than patching the pinned
submodule."
```

---

### Task 3: Image presets in `display.c` (host-tested)

Adds an image-preset index space so each background TGA is selectable from the System Palette row with text pinned to White. This is the only fully host-testable task; write the tests first.

**Files:**
- Modify: `saturn/src/display.h` (add `display_palette_count` declaration)
- Modify: `saturn/src/display.c` (accessors, cycle, decode)
- Test: `saturn/tests/test_display.c`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `int display_palette_count(void);` — returns `DISP_PRESET_N + display_image_count()`.
  - `display_preset_name(int)` / `display_preset_bg(int)` / `display_preset_text(int)` — accept indices up to `display_palette_count() - 1`. Signatures unchanged.
  - Task 4 relies on all four.

- [ ] **Step 1: Write the failing tests**

Append these five functions to `saturn/tests/test_display.c`, immediately **before** the `int main(void)` definition. The file uses plain `assert()` from `<assert.h>`, and already includes `<string.h>` and `<stdio.h>` — no new includes are needed.

```c
static void test_palette_count_includes_images(void) {
    static const char *const names[] = { "FOREST.TGA", "CASTLE.TGA" };
    display_set_images(NULL, 0);
    assert(display_palette_count() == DISP_PRESET_N);
    display_set_images(names, 2);
    assert(display_palette_count() == DISP_PRESET_N + 2);
    display_set_images(NULL, 0);
}

static void test_image_presets_pin_white_text(void) {
    static const char *const names[] = { "FOREST.TGA", "CASTLE.TGA" };
    display_set_images(names, 2);
    for (int i = 0; i < 2; i++) {
        int p = DISP_PRESET_N + i;
        assert(display_preset_bg(p)   == DISP_BG_COLOR_N + i);
        assert(display_preset_text(p) == DISP_TEXT_WHITE);
        assert(strcmp(display_preset_name(p), names[i]) == 0);
    }
    display_set_images(NULL, 0);
}

static void test_cycle_palette_reaches_images(void) {
    static const char *const names[] = { "FOREST.TGA", "CASTLE.TGA" };
    DisplayState d;
    display_set_images(names, 2);
    display_defaults(&d);
    /* Walk forward from the last color preset into the image presets. */
    d.palette = DISP_PRESET_N - 1;
    d.bg      = display_preset_bg(d.palette);
    d.text    = display_preset_text(d.palette);
    display_cycle_palette(&d, 1);
    assert(d.palette == DISP_PRESET_N);
    assert(d.bg      == DISP_BG_COLOR_N);
    assert(d.text    == DISP_TEXT_WHITE);
    display_cycle_palette(&d, 1);
    assert(d.palette == DISP_PRESET_N + 1);
    /* Past the last image it wraps to preset 0. */
    display_cycle_palette(&d, 1);
    assert(d.palette == 0);
    /* Backward from preset 0 lands on the last image. */
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PRESET_N + 1);
    display_set_images(NULL, 0);
}

static void test_image_preset_name_shown_not_custom(void) {
    static const char *const names[] = { "FOREST.TGA" };
    DisplayState d;
    display_set_images(names, 1);
    d.palette = DISP_PRESET_N;
    d.bg      = DISP_BG_COLOR_N;
    d.text    = DISP_TEXT_WHITE;
    assert(strcmp(display_palette_name(&d), "FOREST.TGA") == 0);
    /* Diverging from the pinned pair reads as Custom, same as color presets. */
    d.text = DISP_TEXT_GREEN;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_set_images(NULL, 0);
}

static void test_decode_rejects_stale_image_preset(void) {
    static const char *const two[]  = { "FOREST.TGA", "CASTLE.TGA" };
    static const char *const one[]  = { "FOREST.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    /* Save while two images exist, selecting the second one. */
    display_set_images(two, 2);
    saved.palette = DISP_PRESET_N + 1;
    saved.bg      = DISP_BG_COLOR_N + 1;
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    /* Round-trips intact while both images are present. */
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(d.palette == DISP_PRESET_N + 1);

    /* On a disc with only one image, the stale preset index is rejected. */
    display_set_images(one, 1);
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 0);
    assert(d.palette < display_palette_count());
    display_set_images(NULL, 0);
}
```

Then register all five inside `int main(void)`, immediately after the existing `test_cycle_palette();` line:

```c
    test_palette_count_includes_images();
    test_image_presets_pin_white_text();
    test_cycle_palette_reaches_images();
    test_image_preset_name_shown_not_custom();
    test_decode_rejects_stale_image_preset();
```

If `string.h` is not already included at the top of the test file, add `#include <string.h>` for `strcmp`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display && /tmp/test_display`

Expected: FAIL at compile time with `implicit declaration of function 'display_palette_count'`. That is the correct first failure — the declaration does not exist yet.

- [ ] **Step 3: Declare `display_palette_count` in the header**

In `saturn/src/display.h`, immediately after the `int display_bg_count(void);` line (currently line 61), add:

```c
/* DISP_PRESET_N color presets followed by one preset per registered image.
   Image presets pair that image's background with white text. Call
   display_set_images() first -- the count moves with the disc's TGA list. */
int display_palette_count(void);
```

- [ ] **Step 4: Implement the accessors in `display.c`**

In `saturn/src/display.c`, the image registration block (`g_image_names` / `g_image_count` / `display_set_images` / `display_image_count`) currently sits **below** the preset accessors. `display_preset_*` must now read `g_image_count`, so move that block **above** `const char *display_preset_name(int index)`.

Cut these lines from their current position:

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
```

and paste them immediately **above** `const char *display_preset_name(int index) {`.

Then replace the three preset accessors:

```c
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
```

with:

```c
/* Palette indices run [0, DISP_PRESET_N) over the microcomputer presets, then
   one entry per registered image. An image preset pairs that image's
   background slot with white text -- the pairing the Display page offers so a
   picture never sits under a color that vanishes into it. */
int display_palette_count(void) { return DISP_PRESET_N + g_image_count; }

const char *display_preset_name(int index) {
    if (index < 0 || index >= display_palette_count()) return "?";
    if (index >= DISP_PRESET_N) return g_image_names[index - DISP_PRESET_N];
    return PRESETS[index].name;
}

int display_preset_bg(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_BG_BLACK;
    if (index >= DISP_PRESET_N) return DISP_BG_COLOR_N + (index - DISP_PRESET_N);
    return PRESETS[index].bg;
}

int display_preset_text(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_TEXT_BRIGHT_GREEN;
    if (index >= DISP_PRESET_N) return DISP_TEXT_WHITE;
    return PRESETS[index].text;
}
```

- [ ] **Step 5: Route the remaining `PRESETS[...]` reads through the accessors**

Three functions still index `PRESETS` directly and would ignore image presets. Replace each.

In `display_palette_name`:

```c
const char *display_palette_name(const DisplayState *d) {
    if (d->palette >= 0 && d->palette < DISP_PRESET_N
        && d->bg   == PRESETS[d->palette].bg
        && d->text == PRESETS[d->palette].text) {
        return PRESETS[d->palette].name;
    }
    return g_custom_label;
}
```

becomes:

```c
const char *display_palette_name(const DisplayState *d) {
    if (d->palette >= 0 && d->palette < display_palette_count()
        && d->bg   == display_preset_bg(d->palette)
        && d->text == display_preset_text(d->palette)) {
        return display_preset_name(d->palette);
    }
    return g_custom_label;
}
```

In `display_cycle_palette`:

```c
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

becomes:

```c
void display_cycle_palette(DisplayState *d, int dir) {
    int count = display_palette_count();
    int next;
    if (display_palette_name(d) == g_custom_label) {
        /* Currently Custom: enter the list at whichever end we are heading for. */
        next = (dir > 0) ? 0 : count - 1;
    } else {
        next = step(d->palette, dir, count);
    }
    d->palette = next;
    d->bg      = display_preset_bg(next);
    d->text    = display_preset_text(next);
}
```

In `display_decode`, the palette validation and the clash-repair tail:

```c
    if (buf[1] < DISP_PRESET_N)  d->palette = (int) buf[1];  else ok = 0;
```

becomes:

```c
    /* Image presets exist only while their image is on the disc, so validate
       against the live count, not the compile-time preset total. */
    if (buf[1] < display_palette_count()) d->palette = (int) buf[1];  else ok = 0;
```

and:

```c
    if (clashes(d->bg, d->text)) {
        d->bg   = PRESETS[d->palette].bg;
        d->text = PRESETS[d->palette].text;
        ok = 0;
    }
```

becomes:

```c
    if (clashes(d->bg, d->text)) {
        d->bg   = display_preset_bg(d->palette);
        d->text = display_preset_text(d->palette);
        ok = 0;
    }
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd saturn && /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/test_display && /tmp/test_display`

Expected: `test_display: OK` with zero warnings. All pre-existing tests must still pass — in particular `test_cycle_palette`, `test_guard_inactive_over_images`, and `test_decode_missing_image_falls_back`. If `test_cycle_palette` now fails, it asserted wrap-at-`DISP_PRESET_N` with no images registered; confirm it calls `display_set_images(NULL, 0)` first, and if it does not, that is a genuine regression to fix in `display.c`, not in the test.

- [ ] **Step 7: Verify no direct `PRESETS[` reads survive outside the table**

Run: `cd saturn/src && grep -n "PRESETS\[" display.c`

Expected: matches only inside `display_defaults` (which intentionally pins preset 12 at startup, before any image is registered). Any match inside `display_palette_name`, `display_cycle_palette`, or `display_decode` means Step 5 was incomplete.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/display.h saturn/src/display.c saturn/tests/test_display.c
git commit -m "Offer each background image as a System Palette preset

Palette indices now run over the 15 microcomputer presets followed by one
entry per registered image, each pairing that image's background with white
text. The Background row keeps both colors and images, so every image stays
reachable by two paths.

Image presets exist only while their image is on the disc, so display_decode
validates the saved palette index against the live count -- a disc with
fewer TGAs falls back rather than indexing past the preset table."
```

---

### Task 4: Wire image presets into the Display page

`display_apply`'s image-load fallback reads `display_preset_bg(g_display.palette)`. With an image preset selected, that returns the same failing image slot, so a load failure would loop back to the broken image instead of falling back to a color.

**Files:**
- Modify: `saturn/src/main.cxx` (`display_apply` at `:255-269`)

**Interfaces:**
- Consumes: `display_palette_count()`, `display_preset_bg()`, `display_preset_text()` from Task 3; `text_set_color()` from Task 2.
- Produces: nothing later tasks use.

- [ ] **Step 1: Read the current `display_apply`**

Run: `cd saturn/src && sed -n '250,275p' main.cxx`

Confirm Task 2 has already replaced the `SRL::ASCII::SetColor` line with `text_set_color(...)`. If it has not, stop — Task 2 must land first.

- [ ] **Step 2: Make the fallback escape image presets**

Replace the body of `display_apply`:

```cpp
static void display_apply(void) {
    text_set_color(display_text_rgb(g_display.text));
    if (display_is_image(&g_display)) {
        if (!title_bg_show(display_bg_name(&g_display))) {
            // Load failed (or the bitmap isn't the 8bpp shape we require): fall
            // back to a color background rather than leaving static on screen.
            g_display.bg = display_preset_bg(g_display.palette);
            title_bg_hide();
            SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
        }
    } else {
        title_bg_hide();
        SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
    }
}
```

with:

```cpp
static void display_apply(void) {
    text_set_color(display_text_rgb(g_display.text));
    if (display_is_image(&g_display)) {
        if (!title_bg_show(display_bg_name(&g_display))) {
            // Load failed (or the bitmap isn't the 8bpp shape we require): fall
            // back to a color background rather than leaving static on screen.
            //
            // The palette may itself be an image preset, whose bg is the slot
            // that just failed -- falling back to it would re-select the broken
            // image. Drop to a color preset in that case.
            int p = g_display.palette;
            if (p >= DISP_PRESET_N || p < 0) p = 12;   // IBM PC (MDA), the startup default
            g_display.palette = p;
            g_display.bg      = display_preset_bg(p);
            g_display.text    = display_preset_text(p);
            text_set_color(display_text_rgb(g_display.text));
            title_bg_hide();
            SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
        }
    } else {
        title_bg_hide();
        SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
    }
}
```

The second `text_set_color` call is required: the fallback changes `text` away from the white the image preset pinned, and the color was already pushed at the top of the function.

- [ ] **Step 3: Verify**

Run: `cd saturn/src && sed -n '255,285p' main.cxx`

Confirm: the fallback sets `palette`, `bg`, and `text` together; `text_set_color` appears twice in the function; the `else` branch is unchanged.

Run: `cd saturn/src && grep -n "DISP_PRESET_N" main.cxx`

Expected: at least one match, inside `display_apply`. `display.h` is already included at `main.cxx:7`, so the macro resolves.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Fall back to a color preset when an image preset fails to load

display_apply's fallback read display_preset_bg(palette), which for an image
preset returns the same slot that just failed to load -- re-selecting the
broken image instead of escaping it. Drop to a color preset and re-push the
text color, since the fallback moves text off the white the image preset
pinned."
```

---

### Task 5: `menu_frame` helper and Options menu adoption

Replaces the border loop currently inline in `options_menu` and applies the agreed renames.

**Files:**
- Modify: `saturn/src/main.cxx` (add helper near `menu_clear` at `:1142`; change `options_menu` at `:1802-1826`)

**Interfaces:**
- Consumes: nothing.
- Produces: `static void menu_frame(int x0, int y0, int w, int h, const char *title)` at file scope in `main.cxx`. Task 6 calls it for five more pages.

- [ ] **Step 1: Add the helper**

In `main.cxx`, immediately **after** the `menu_clear` definition (currently ending line 1146), insert:

```cpp
// Draw a w x h box of +--+ chrome at (x0, y0) and center `title` on its second
// row. Every menu page uses this so the chrome and title placement stay
// identical; pages differ only in the box they ask for.
//
// The caller owns the interior: content starts at (x0 + 2, y0 + 2) by
// convention, and must stay inside x0 + w - 2 so it never overwrites the right
// border.
static void menu_frame(int x0, int y0, int w, int h, const char *title) {
    for (int r = 0; r < h; r++) {
        char line[42]; int p = 0;
        for (int c = 0; c < w && p < (int) sizeof(line) - 1; c++)
            line[p++] = (r == 0 || r == h - 1) ? ((c == 0 || c == w - 1) ? '+' : '-')
                      : ((c == 0 || c == w - 1) ? '|' : ' ');
        line[p] = '\0';
        SRL::Debug::Print(x0, y0 + r, "%s", line);
    }
    int len = 0; while (title[len]) len++;
    int tx = x0 + (w - len) / 2;
    if (tx < x0 + 1) tx = x0 + 1;
    SRL::Debug::Print(tx, y0 + 1, "%s", title);
}
```

`line[42]` accommodates the widest box (40) plus the NUL, with the `p` bound guarding against a caller passing something wider.

- [ ] **Step 2: Replace the inline border loop in `options_menu`**

In `options_menu`, replace the loop and the title print (`main.cxx:1802-1810`):

```cpp
        for (int r = 0; r < h; r++) {
            char line[40]; int p = 0;
            for (int c = 0; c < w; c++)
                line[p++] = (r == 0 || r == h - 1) ? ((c == 0 || c == w - 1) ? '+' : '-')
                          : ((c == 0 || c == w - 1) ? '|' : ' ');
            line[p] = '\0';
            SRL::Debug::Print(x0, y0 + r, "%s", line);
        }
        SRL::Debug::Print(x0 + 11, y0 + 1, "OPTIONS");
```

with:

```cpp
        menu_frame(x0, y0, w, h, "OPTIONS");
```

The existing `const int x0 = 5, y0 = 8, w = 30, h = 15;` at `main.cxx:1759` stays as-is: `(40 - 30) / 2` is 5, so the box is already centered.

- [ ] **Step 3: Apply the renames**

In `options_menu`'s draw switch (`main.cxx:1819-1824`), replace:

```cpp
                case OI_CONFIG:   SRL::Debug::Print(x0 + 2, ay++, "%c Configure Z-ATURN", cur); break;
                case OI_CONTROLS: SRL::Debug::Print(x0 + 2, ay++, "%c %s", cur, g_kbd_visible ? "Gamepad Controls" : "Keyboard Controls"); break;
                case OI_DISPLAY:  SRL::Debug::Print(x0 + 2, ay++, "%c Display", cur); break;
                case OI_SOUND:    SRL::Debug::Print(x0 + 2, ay++, "%c Sound Options", cur); break;
                case OI_RETURN:   SRL::Debug::Print(x0 + 2, ay++, "%c Return to Title Screen", cur); break;
```

with:

```cpp
                case OI_CONFIG:   SRL::Debug::Print(x0 + 2, ay++, "%c Network", cur); break;
                case OI_CONTROLS: SRL::Debug::Print(x0 + 2, ay++, "%c Controls", cur); break;
                case OI_DISPLAY:  SRL::Debug::Print(x0 + 2, ay++, "%c Display", cur); break;
                case OI_SOUND:    SRL::Debug::Print(x0 + 2, ay++, "%c Sound", cur); break;
                case OI_RETURN:   SRL::Debug::Print(x0 + 2, ay++, "%c Return to Title", cur); break;
```

`OI_DONE` is unchanged. The `OI_CONTROLS` activate branch at `main.cxx:1790` keeps its `g_kbd_visible` routing — only the label loses the switch.

- [ ] **Step 4: Update the stale function comment**

The comment above `options_menu` (`main.cxx:1748-1750`) names the old labels. Replace:

```cpp
// Options menu (centered box): a difficulty slider plus actions (Configure,
// Controls, Sound Options, Return to Title, Done). Up/Down select a row; on
```

with:

```cpp
// Options menu (centered box): a difficulty slider plus actions (Network,
// Controls, Display, Sound, Return to Title, Done). Up/Down select a row; on
```

- [ ] **Step 5: Verify**

Run: `cd saturn/src && grep -n "Configure Z-ATURN\|Sound Options\|Gamepad Controls\|Keyboard Controls\|Return to Title Screen" main.cxx`

Expected: matches only in `controls_page` / `keyboard_controls_page` **page titles** (Task 6 replaces those) and in comments describing those pages. Zero matches inside `options_menu`'s draw switch.

Run: `cd saturn/src && grep -n "menu_frame" main.cxx`

Expected: two matches — the definition and the `options_menu` call.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Extract menu_frame helper and rename Options entries

One helper now draws the box and centers the title, replacing the loop
inlined in options_menu. Entries shorten to Network, Controls, Display,
Sound, and Return to Title; Controls drops its device-dependent label but
keeps routing to whichever page matches the device in hand."
```

---

### Task 6: Frame the six full-screen pages

Converts the remaining pages from borderless left-aligned layouts to centered framed boxes with uppercase centered titles.

**Files:**
- Modify: `saturn/src/main.cxx` (`config_page` at `:1249-1264`; `configure_controls_page` at `:1340-1360`; `controls_page` at `:1387-1407`; `keyboard_controls_page` at `:1448-1470`; `sound_options_page` at `:1632-1668`; `display_options_page` at `:1713-1742`)

**Interfaces:**
- Consumes: `menu_frame` from Task 5.
- Produces: nothing.

Box sizes, with `x0 = (40 - w) / 2` and `y0 = (28 - h) / 2`:

| Page | w | h | x0 | y0 | Title |
|---|---|---|---|---|---|
| `display_options_page` | 38 | 14 | 1 | 7 | `DISPLAY` |
| `sound_options_page` | 38 | 16 | 1 | 6 | `SOUND` |
| `controls_page` | 38 | 22 | 1 | 3 | `CONTROLS` |
| `keyboard_controls_page` | 38 | 18 | 1 | 5 | `CONTROLS` |
| `config_page` | 38 | 16 | 1 | 6 | `NETWORK` |
| `configure_controls_page` | 40 | 22 | 0 | 3 | `CONFIGURE CONTROLS` |

`configure_controls_page` gets the full 40 because it is the widest page: it prints values at `x + 20` and its hint string is already 38 characters, which will not fit inside a 38-wide frame. Step 6 shortens that hint — do not skip it, or the text will overwrite the border.

Five of the six pages set a content origin with `int x = 2, y = 1;` and flow downward. For those the change is mechanical: call `menu_frame` after `menu_clear()`, move the origin inside the frame, and delete the old title print. `config_page` uses absolute coordinates instead and needs each one re-anchored.

Locate each page by its `int x = 2, y = 1;` line rather than trusting the line numbers above — earlier tasks in this plan shift them. Run `cd saturn/src && grep -n "int x = 2, y = 1;" main.cxx` to get the current five (`config_page` is not among them; it has no such line).

- [ ] **Step 1: Frame `display_options_page`**

In `display_options_page`, replace (`main.cxx:1713-1715`):

```cpp
        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "DISPLAY OPTIONS"); y += 2;
```

with:

```cpp
        menu_clear();
        const int fx = 1, fy = 7, fw = 38, fh = 14;
        menu_frame(fx, fy, fw, fh, "DISPLAY");
        int x = fx + 2, y = fy + 3;
```

The rest of the function is unchanged — every row already draws relative to `x` and `y`. The old `y += 2` is folded into the `fy + 3` origin (frame row 0 is the border, row 1 the title, row 2 blank).

- [ ] **Step 2: Frame `sound_options_page`**

In `sound_options_page`, replace (`main.cxx:1632-1634`):

```cpp
        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "SOUND OPTIONS"); y += 2;
```

with:

```cpp
        menu_clear();
        const int fx = 1, fy = 6, fw = 38, fh = 16;
        menu_frame(fx, fy, fw, fh, "SOUND");
        int x = fx + 2, y = fy + 3;
```

- [ ] **Step 3: Frame `controls_page`**

In `controls_page`, replace (`main.cxx:1388-1390`):

```cpp
        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "GAMEPAD CONTROLS"); y += 2;
```

with:

```cpp
        menu_clear();
        const int fx = 1, fy = 3, fw = 38, fh = 22;
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        int x = fx + 2, y = fy + 3;
```

This page prints values at `x + 18`, i.e. column 21, and the right border sits at column 38. Values have 17 columns before the border.

- [ ] **Step 4: Frame `keyboard_controls_page`**

In `keyboard_controls_page`, replace (`main.cxx:1450-1452`):

```cpp
        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "KEYBOARD CONTROLS"); y += 2;
```

with:

```cpp
        menu_clear();
        const int fx = 1, fy = 5, fw = 38, fh = 18;
        menu_frame(fx, fy, fw, fh, "CONTROLS");
        int x = fx + 2, y = fy + 3;
```

- [ ] **Step 5: Frame `config_page`**

This page uses absolute coordinates, so each print moves. Replace (`main.cxx:1249-1264`):

```cpp
        menu_clear();
        SRL::Debug::Print(2, 1, "Configure Z-ATURN");
        SRL::Debug::Print(2, 3, "Server dial number:");
        SRL::Debug::Print(2, 4, "> %s_", k.input);
        for (int r = 0; r < KB_ROWS; r++) {
            char rowbuf[KB_COLS * 2 + 1]; int p = 0;
            for (int c = 0; c < KB_COLS; c++) {
                rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
                rowbuf[p++] = KB_LAYOUT[r][c];
            }
            rowbuf[p] = '\0';
            SRL::Debug::Print(4, 6 + r, "%s", rowbuf);
        }
        if (err[0]) SRL::Debug::Print(2, 11, "%s", err);
        SRL::Debug::Print(2, 13, "%s",
            hint("C=type B=del  A=OK  Start=Cancel", "type number  Enter=OK  Esc=Cancel"));
```

with:

```cpp
        menu_clear();
        const int fx = 1, fy = 6, fw = 38, fh = 16;
        menu_frame(fx, fy, fw, fh, "NETWORK");
        SRL::Debug::Print(fx + 2, fy + 3, "Server dial number:");
        SRL::Debug::Print(fx + 2, fy + 4, "> %s_", k.input);
        for (int r = 0; r < KB_ROWS; r++) {
            char rowbuf[KB_COLS * 2 + 1]; int p = 0;
            for (int c = 0; c < KB_COLS; c++) {
                rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
                rowbuf[p++] = KB_LAYOUT[r][c];
            }
            rowbuf[p] = '\0';
            SRL::Debug::Print(fx + 4, fy + 6 + r, "%s", rowbuf);
        }
        if (err[0]) SRL::Debug::Print(fx + 2, fy + 11, "%s", err);
        SRL::Debug::Print(fx + 2, fy + 13, "%s",
            hint("C=type B=del  A=OK  Start=Cancel", "type number  Enter=OK  Esc=Cancel"));
```

Every old absolute row `n` becomes `fy + n + 2`, matching the two rows the frame's border and title consume. The old title print is dropped — `menu_frame` draws it.

- [ ] **Step 6: Frame `configure_controls_page`**

This is the widest page. Replace (`main.cxx:1340-1344`):

```cpp
        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "CONFIGURE CONTROLS"); y += 2;
        SRL::Debug::Print(x, y++, "%s", hint("Left/Right change  A/Start=OK B=Cancel",
                                             "Left/Right change  Enter=OK Esc=Cancel"));
```

with:

```cpp
        menu_clear();
        const int fx = 0, fy = 3, fw = 40, fh = 22;
        menu_frame(fx, fy, fw, fh, "CONFIGURE CONTROLS");
        int x = fx + 2, y = fy + 3;
        // Shortened from "Left/Right change ...": the old strings were 38 chars
        // and ran into the frame's right border at column 39.
        SRL::Debug::Print(x, y++, "%s", hint("L/R change  A/Start=OK B=Cancel",
                                             "L/R change  Enter=OK Esc=Cancel"));
```

Both new hint strings are 31 characters. Drawn at column 2 they end at column 32, clear of the border at column 39.

The rest of the function is unchanged; it draws relative to `x` and `y`, with values at `x + 20` (column 22), leaving 17 columns before the border.

- [ ] **Step 7: Update the `config_page` comment**

Replace the comment above `config_page` (`main.cxx:1210-1212`):

```cpp
// Configure Z-ATURN: edit the server dial number with the on-screen / real
// keyboard. A/Enter accept (after validation); Start/Esc cancel. Both return to
// the Options menu.
```

with:

```cpp
// Network: edit the server dial number with the on-screen / real keyboard.
// A/Enter accept (after validation); Start/Esc cancel. Both return to the
// Options menu.
```

- [ ] **Step 8: Verify the on-screen strings are gone**

Run: `cd saturn/src && grep -n "DISPLAY OPTIONS\|SOUND OPTIONS\|GAMEPAD CONTROLS\|KEYBOARD CONTROLS\|Configure Z-ATURN\|Left/Right change" main.cxx`

Expected: zero matches. Any remaining match is a page whose title print or hint was not replaced.

Run: `cd saturn/src && grep -c "menu_frame" main.cxx`

Expected: `8` — one definition plus seven call sites (Options from Task 5, plus these six).

- [ ] **Step 9: Verify every framed page fits its box**

For each of the six pages, count the rows the body draws and confirm the last one lands above the bottom border, and that no drawn string reaches the right border.

Run: `cd saturn/src && sed -n '1388,1412p' main.cxx`

`controls_page` is the tightest: content origin `fy + 3` = row 6, then 3 face rows + 6 chord rows + 2 fixed rows + 1 blank + 3 action rows = 15 rows, ending at row 20. Its bottom border is at `fy + fh - 1` = row 24. Fits with room to spare.

Repeat the count for the other five. If any page's last row reaches its bottom border, increase that page's `fh` and recompute `fy = (28 - fh) / 2`. If any string reaches the right border, shorten it as Step 6 did for the hint. Do not let content overwrite the frame.

The two to check most carefully are `configure_controls_page` (widest strings, values at `x + 20`) and `controls_page` (most rows).

- [ ] **Step 10: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Frame the six full-screen menu pages

Display, Sound, both Controls pages, Configure Controls, and Network move
from borderless left-aligned layouts into centered boxes with uppercase
centered titles, matching Options. Titles shorten to DISPLAY, SOUND,
CONTROLS, and NETWORK.

config_page used absolute coordinates rather than a content origin, so each
print is re-anchored to the frame. configure_controls_page takes the full
40 columns and a shortened hint: its old 38-character hint string would have
run into the border."
```

---

## On-target verification (user's step)

After all six tasks land, the user runs `cd saturn && ./compile.bat debug` and checks:

1. Display Options: cycle `Background` through all eight TGAs; each renders (Task 1).
2. Return to the story menu, re-enter Display Options, cycle backgrounds again — this is the path that previously failed, since the Z3 scan has run (Task 1).
3. Load a story file after selecting an image background, confirming `cd_restore_z3()` left the CD where story opens expect it (Task 1).
4. Change `Text` and confirm the on-screen color actually changes (Task 2). **If it does not**, the fallback is `SRL::ASCII::SetPalette(0)` before the first `Print`, which moves glyphs to CRAM entries 0–15 where SRL's own arithmetic is accidentally correct.
5. Cycle `System Palette` past index 14 into the image presets; each sets its image and forces white text (Tasks 3, 4).
6. Every page draws a centered bordered box with an uppercase centered title, no content overwriting the frame (Tasks 5, 6).
7. Soft-reset (A+B+C+Start) and confirm backgrounds still load — exercises `GFS_Reset()` plus the re-scan path (Task 1).
