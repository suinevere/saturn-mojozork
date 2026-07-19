# Menu chrome, background loading, and text color — design

**Date:** 2026-07-18
**Branch:** `sound-options`
**Status:** approved, ready for planning

## Problem

Four defects and inconsistencies in the Saturn client's menu layer, reported together:

1. TGA backgrounds selected in Display Options never appear — the selector cycles
   the names but the screen falls back to a solid color.
2. Background images are only reachable from the `Background` row; the reporter
   wants each one available as a `System Palette` preset paired with white text.
3. Text color changes made in Display Options have no visible effect.
4. Menu titles and entry names are inconsistent: some pages are borderless and
   left-aligned while `OPTIONS` is a centered bordered box, and several entries
   carry stale or over-long names (`Configure Z-ATURN`, `Gamepad Controls`).

Items 1 and 3 are bugs with identified root causes. Items 2 and 4 are UI work.

## Root cause analysis

### Background loading (item 1)

Not a missing `PNG` directory. The repository contains no stale `data/PNG`
reference, and the build already places `TGA/` at the ISO root: `shared.mk:145`
sets `ASSETS_DIR = ./cd/data`, making `cd/data` the ISO root, so `cd/data/TGA/`
lands as `/TGA`. `GFS_NameToId("TGA")` at root resolves correctly and
`display_scan_images` registers all eight files.

The failure is ambient CD-directory state. `scan_z3_folder` (`main.cxx:2299`)
calls:

```c
GFS_SetDir(&tbl);   // subsequent File() opens resolve inside Z3
```

This persists for the rest of the session. `title_bg_show` opens bitmaps by bare
filename (`new SRL::Bitmap::TGA("AMIGA.TGA")`), so every load after
`preload_game_catalog()` resolves inside `Z3/`, finds nothing, and returns false.
`display_apply` then falls back to a color background (`main.cxx:258-264`),
which is why the symptom is a silent fallback rather than a crash or static.

The title screen works only because `title_bg_show()` at `main.cxx:2710` runs
*before* the Z3 scan, while the CD is still at root after `GFS_Reset()`.

The comment at `main.cxx:2126-2127` asserts the opposite — that bare-name opens
are "proven to resolve regardless of the current CD directory". That claim is
false and is why the bug survived review.

> **Correction 2 (post-implementation).** The directory diagnosis above is
> right about *why* bare-name opens fail inside `Z3`, but the remedy it
> prescribes — `SRL::Cd::ChangeDir((char *) nullptr)` — does not work, and
> adding it to `title_bg_show` broke the one path that used to load (the title
> screen, which was already at root).
>
> `ChangeDir(nullptr)` walks up with `Cd::ChangeDir("..")` and bails on
> `!= ErrorCode::ErrorOk` (`srl_cd.hpp:679`). But `ChangeDir(name)` returns
> `GFS_SetDir()`'s value, which is the **file count** on success, not
> `GFS_ERR_OK` (`== 0`, `sega_gfs.h:59`). Any successful step out of a non-empty
> directory is therefore read as an error, so the function returns before its
> root-detection loop — after having already moved the current directory. It
> does not reach root.
>
> Root is now captured the same way `Z3` is: `cd_capture_root()` snapshots the
> root `GfsDirTbl` while root is current (after `GFS_Reset()`), and
> `cd_enter_root()` re-applies it with `GFS_SetDir`. No reliance on
> `ChangeDir(nullptr)` anywhere.

### Text color (item 3)

`SRL::ASCII::SetColor` is unusable as shipped. At `srl_ascii.hpp:138`:

```cpp
uint16_t* colorAdr = reinterpret_cast<uint16_t *>(VDP2_COLRAM + (ASCII::colorBank >> 6));
colorAdr[colorIndex] = color;
```

`ASCII::colorBank` defaults to `1 << 12` (`srl_ascii.hpp:23`), i.e. palette 1.
`(1 << 12) >> 6` is byte offset **64**. But SRL initializes color RAM as
`CRM16_2048` (`srl_vdp2.hpp:1498`) — 16-bit entries, 2 bytes each — so palette 1
of a 4bpp cell begins at byte offset **32**. The shift is off by one bit; it
should be `>> 7`.

`SetColor(c, 15)` therefore writes CRAM entry 47 while the hardware reads entry
31. Every write lands in unused color RAM. Text renders white because the SGL
font's default palette-1 entry 15 is already white — the existing call at
`main.cxx:2709` never had an effect either.

This is a defect in the pinned SaturnRingLib submodule, not in this port.

> **Correction (post-implementation).** The analysis above is wrong, and the
> first implementation of the fix inherited the error. `ASCII::colorBank` does
> **not** keep its declarator value of `1 << 12`: `Core::Initialize` →
> `VDP2::Initialize` calls `ASCII::SetPalette(0)` (`srl_vdp2.hpp:1517`) before
> any port code runs, so `colorBank == 0` and the font renders from **palette
> 0**, CRAM entries 0–15. With `colorBank == 0` the shift is harmless —
> `SetColor(c, i)` writes entry `i` — so SRL is not defective here; it was
> simply being called with an index that colors nothing.
>
> Two non-adjacent entries matter. The **glyph foreground is entry 1**, seeded
> by `SetPrintPaletteColor(0, White)`, which writes `1 + (index << 8)`
> (`srl_vdp2.hpp:1489`); its remaining calls (index 1–6) land on entries 257,
> 513, … which a 4bpp cell cannot reach. The **cursor is entry 15**, from
> `install_block_glyph()`'s `0xFF` fill. `text_set_color` writes both. See the
> comment above `text_set_color` in `main.cxx` for the derivation.
>
> The "fallback" recorded under Phase 1b below — `SetPalette(0)` — is therefore
> already the live configuration, not an alternative to try.

### CRAM occupancy (no conflict)

Verified that background images do not contend with the font palette.
`ScrollScreen::LoadBitmap` allocates one 256-color bank on first use and reuses
it thereafter (`srl_vdp2.hpp:856`, guarded on `TilePalette.GetData()==nullptr`).
`CRAM::Palette` computes `base + id * (16 << (mode - 2))` (`srl_cram.hpp:79`);
with `Paletted256` and `id == 1` this is entry 256, matching SRL's observed
`Pal: 512` debug output (byte offset 512).

Image palettes occupy CRAM entries 256–511. The ASCII font palette occupies
entries 16–31. **Images and colored text can coexist**; pairing images with
white text is a product decision, not a hardware constraint.

## Non-goals

- Patching the SaturnRingLib submodule. Fixes stay in `saturn/src/` so a fresh
  checkout of the pinned SDK still builds.
- Splitting `main.cxx` (2812 lines). The `menu_frame` helper removes real
  duplication; no broader restructuring.
- Merging the two Controls pages into one. Explicitly declined during design —
  the entry is renamed, routing is unchanged.
- Changing the `Z - A T U R N` title screen. "Centered banner" refers to menu
  page titles.

## Design

Ordered so the two bug fixes (Phase 1) are independent of and land before the UI
work (Phase 2). Phase 1 carries the only hardware-dependent risk and should be
verified on real output before Phase 2 builds on it.

### Phase 1a — CD directory discipline

Make the directory dependency explicit rather than ambient.

- Retain the Z3 `GfsDirTbl` (already `static` in `scan_z3_folder`) at file scope
  alongside a `static bool g_z3_dir_valid` set once the scan succeeds.
- Add two helpers:
  - `cd_enter_root()` — `SRL::Cd::ChangeDir((char *) nullptr)`
  - `cd_restore_z3()` — re-apply `GFS_SetDir(&tbl)` when `g_z3_dir_valid`, else
    no-op.
- `title_bg_show` wraps its load: `cd_enter_root()` → construct
  `SRL::Bitmap::TGA` → `cd_restore_z3()` before returning, on **both** the
  success and failure paths.
- Correct the false comment at `main.cxx:2126-2127`.

The existing color-background fallback in `display_apply` stays as a genuine
safety net for a missing or malformed file; it simply stops firing spuriously.

`cd_restore_z3()` must run before returning from `title_bg_show` even when the
bitmap fails to load, or a failed background selection would break subsequent
story-file opens.

### Phase 1b — text color

Add a local helper in `main.cxx` writing the correct CRAM slot directly.
`ASCII::colorBank` is private, so the palette index cannot be read back; the
layout is pinned by SRL's own initialization and documented at the call site.

```c
// SRL's ASCII::SetColor computes (colorBank >> 6) = byte 64 for palette 1, but
// CRAM is CRM16_2048 (srl_vdp2.hpp:1498) -- 2 bytes per entry -- so palette 1
// begins at byte 32. Its writes land in unused CRAM and never reach the glyphs.
// Write the real slot ourselves. Index 15 is the color the SGL font glyphs use
// and the one install_block_glyph() fills.
// VDP2_COLRAM (sl_def.h:981) is a bare integer address, not a pointer, so the
// cast below is required. It reaches main.cxx via <srl.hpp>.
#define ASCII_PAL1_CRAM (VDP2_COLRAM + 32)
static void text_set_color(unsigned short rgb555) {
    ((volatile unsigned short *) ASCII_PAL1_CRAM)[15] = rgb555;
}
```

Replaces both `SRL::ASCII::SetColor` call sites (`main.cxx:256`, `main.cxx:2709`).
Keeps palette 1, leaving CRAM entries 0–15 untouched for any other consumer.

**Verification risk.** This rests on the CRAM mode and 4bpp palette stride read
from source. It cannot be confirmed without running on hardware or Mednafen. If
text color still does not change, the fallback is `SRL::ASCII::SetPalette(0)`
before the first `Print`: at palette 0 both the correct offset and SRL's
computed offset collapse to 0, so the shipped `SetColor` works by accident. That
fallback moves glyphs to CRAM entries 0–15, so it should be a second choice.

### Phase 2a — images as System Palette presets

Extend the palette index space in `display.c` / `display.h`:

- Indices `0 .. DISP_PRESET_N-1` — existing 15 microcomputer presets, unchanged.
- Indices `DISP_PRESET_N .. DISP_PRESET_N + display_image_count() - 1` — one per
  discovered TGA, with `bg = DISP_BG_COLOR_N + (index - DISP_PRESET_N)` and
  `text = DISP_TEXT_WHITE`.

Affected functions:

- `display_palette_count()` — new; returns `DISP_PRESET_N + display_image_count()`.
- `display_preset_name`, `display_preset_bg`, `display_preset_text` — branch on
  the index to serve image presets.
- `display_cycle_palette` — walks the combined range.
- `display_palette_name` — returns the image name when the state matches an
  image preset, `"Custom"` otherwise, per existing semantics.

The `Background` row keeps both colors and images, so every image stays
reachable by two paths. This is deliberate — the answer to the design question
was to append presets, not to move images out of `Background`.

**Persistence.** `d->palette` is written to backup RAM and image presets depend
on disc contents. `display_decode` already validates image *slots*; extend that
to validate image *preset* indices against the live `display_image_count()`, so
a disc with fewer TGAs falls back to a default rather than indexing past the
preset table. This preserves the documented contract that `display_set_images`
must be called before `display_decode`.

**Legibility guard.** The existing guard skips candidates where text and
background would match. Image presets pin text to white; the guard must treat an
image background as never matching a text color, since it cannot know the
image's content.

### Phase 2b — chrome and renames

One shared helper replaces the hand-rolled border loop currently inline in
`options_menu` (`main.cxx:1802-1810`):

```c
// Draws a +--+ box of w x h at (x0, y0) and centers `title` on its second row.
static void menu_frame(int x0, int y0, int w, int h, const char *title);
```

Screen is 40 columns (`CONSOLE_COLS`, columns 0–39) and 28 usable rows
(`SCREEN_ROWS`). Boxes are centered horizontally: `x0 = (40 - w) / 2`.

| Page | w × h | Title |
|---|---|---|
| Options | 30 × 15 (unchanged) | `OPTIONS` |
| Display | 38 × 14 | `DISPLAY` |
| Sound | 38 × 16 | `SOUND` |
| Gamepad controls | 38 × 22 | `CONTROLS` |
| Keyboard controls | 38 × 18 | `CONTROLS` |
| Network | 38 × 12 | `NETWORK` |

Sizes are upper bounds derived from current content (gamepad controls is the
tallest: 3 face rows + 6 chord rows + 2 fixed rows + 3 action rows + separators
and hint). Implementation must confirm each page's content fits inside its
border and adjust the constant if not; content must not overwrite the frame.

Options menu row labels:

| Current | New |
|---|---|
| `Configure Z-ATURN` | `Network` |
| `Gamepad Controls` / `Keyboard Controls` | `Controls` |
| `Display` | `Display` (unchanged) |
| `Sound Options` | `Sound` |
| `Return to Title Screen` | `Return to Title` |
| `Done` | `Done` (unchanged) |

The `Controls` row drops its `g_kbd_visible` label switch but keeps the existing
routing to `controls_page()` or `keyboard_controls_page()` by device in hand.

Page titles become uppercase and centered inside the frame, replacing the
left-aligned `SRL::Debug::Print(x, y, "DISPLAY OPTIONS")` form at
`main.cxx:1715`, `main.cxx:1634`, `main.cxx:1250`, and the two Controls pages.

Full-screen pages currently drawing at `x = 2, y = 1` must have their content
origin shifted to sit inside the new frame.

## Testing

Host-side unit tests in `saturn/tests/` cover `display.c` without a Saturn:

- Image presets appear at the expected indices with `bg` set to the matching
  image slot and `text == DISP_TEXT_WHITE`.
- `display_cycle_palette` traverses colors then images and wraps in both
  directions.
- `display_decode` rejects an image-preset index that exceeds the live image
  count and falls back to a default.
- `display_decode` accepts a valid image-preset index round-tripped through
  `display_encode`.
- The legibility guard does not reject an image background.

`menu_frame` geometry is verifiable host-side if extracted behind a printf-style
seam; if not, it is covered by visual inspection.

Phase 1a and 1b are not unit-testable — both depend on Saturn hardware state
(CD file system, VDP2 color RAM). They require on-target verification.

## Verification (on target)

Run `cd saturn && ./compile.bat debug`, then:

1. Enter Display Options, cycle `Background` to each of the eight TGAs and
   confirm each renders (Phase 1a).
2. Return to the story menu, then re-enter Display Options and cycle backgrounds
   again — this is the path that previously failed, since the Z3 scan has run.
3. Load a story file after selecting an image background, confirming
   `cd_restore_z3()` left the CD where story opens expect it.
4. Change `Text` and confirm the on-screen color actually changes (Phase 1b).
5. Cycle `System Palette` past index 14 into the image presets; confirm each
   sets its image and forces white text (Phase 2a).
6. Confirm every menu page draws a centered bordered box with an uppercase
   centered title and no content overwriting the frame (Phase 2b).
7. Soft-reset (A+B+C+Start) and confirm backgrounds still load — this exercises
   `GFS_Reset()` plus the re-scan path.
