# Title-Screen Background Image (HOUSE.TGA)

**Date:** 2026-07-17
**Status:** Approved design
**Scope:** Saturn client (`saturn/src/main.cxx`)

## Goal

Display `HOUSE.TGA` as a full-screen background image behind the title screen,
replacing the current black backdrop. The image shows behind the `MOJOZORK`
title, the credit line, and the "Press any button to begin" prompt, then
disappears when the player enters the menu. Menus and gameplay stay on solid
black for text readability.

## Context

- The Saturn client runs entirely on the SaturnRingLib (SRL) debug **text
  console**, which renders on **VDP2 NBG3 at priority `Layer7`** (the highest),
  over a black VDP2 back-color. See `srl_vdp2.hpp::VDP2::Initialize` (NBG3 gets
  `SetPriority(Layer7)` and `ScrollEnable()`; **NBG0 is `ScrollDisable()`d by
  default**).
- The current "title art" is two `SRL::Debug::Print` lines in
  `title_draw_art()` (`saturn/src/main.cxx`); "Press any button to begin" is
  added by `title_and_seed()`.
- `saturn/cd/data/HOUSE.TGA`: **320×224, 8-bit uncompressed paletted (256-color),
  bottom-left origin**, generated from `saturn/cd/data/house.png`. The file sits
  at the CD root (alongside the `Z3/` game folder).
- SRL's TGA loader (`srl_tga.hpp`) decodes paletted TGA into an indexed bitmap
  plus a CRAM palette; `BitmapInfo` maps a >128-entry palette to `Paletted256`.
- SRL's VDP2 bitmap container sizes are 512×256 / 512×512 / 1024×256 / 1024×512.
  A 320×224 image fits the **512×256** container.
- **The image must not be truecolor/RGB555.** `VRAM::AutoAllocateBmp` doubles the
  container size for RGB555, so 512×256 becomes 256KB and spans the A0/A1 VRAM
  bank boundary. Bank-spanning VDP2 bitmaps render as **static**: `slBitMapNbg0`
  never reserves the second bank in `VDP2_RAMCTL`, and SRL's allocator tracks
  banks only in software (see the author's note at `srl_vdp2.hpp:11-15`). At 8bpp
  the container is exactly 128KB and fits a single bank, which works.
- Palette index 0 is deliberately left unused: VDP2 treats index 0 on a scroll
  screen as transparent, which would punch back-color holes through the image.

## Approach

**VDP2 NBG0 bitmap** (chosen over a per-frame VDP1 sprite or a Bmp2Tile
tilemap, neither of which offers any benefit for a static full-screen image):

Load the image onto the otherwise-unused NBG0 layer at a priority **below**
the text layer, so the existing NBG3 text renders on top automatically.

## Design

### Two helpers (added next to `title_draw_art()` in `main.cxx`)

```
static void title_bg_show(void)   // lazy-load once, then enable NBG0
static void title_bg_hide(void)   // disable NBG0
```

- `title_bg_show()`
  - Guarded by a function-local `static bool loaded = false`.
  - On first call: construct `SRL::Bitmap::TGA` from `"HOUSE.TGA"`, call
    `SRL::VDP2::NBG0::LoadBitmap(&bmp)`, then `SRL::VDP2::NBG0::SetPriority(
    SRL::VDP2::Priority::Layer1)` (any value `< Layer7`). Free the temporary
    bitmap after `LoadBitmap` copies it to VRAM. Set `loaded = true`.
  - Every call (including the first): `SRL::VDP2::NBG0::ScrollEnable()`.
  - Rationale: the CD read + VRAM upload happen exactly once; subsequent shows
    (the redraw inside `title_and_seed`, and soft-reset re-entry) just
    re-enable the still-resident layer, since VDP2 VRAM persists across the
    soft-reset `longjmp`.
- `title_bg_hide()`
  - `SRL::VDP2::NBG0::ScrollDisable()`.

### Wiring in `main()`

- Call `title_bg_show()` **immediately before** `title_draw_art()` at the boot
  title draw (currently `main.cxx:2459`).
  - This is **before** `preload_game_catalog()` (which cd's into the `Z3`
    folder), so the bare filename `"HOUSE.TGA"` resolves at the CD root.
  - This is **before** `music_cdda_play()` starts the menu track, honoring the
    existing constraint that all CD reads finish before CD-DA audio begins (the
    single CD head cannot read data and play audio at once).
- Call `title_bg_hide()` **immediately after** `title_and_seed()` returns
  (currently `main.cxx:2467`), before the top-level mode menu.

### Control flow / lifecycle

- Boot: `title_bg_show()` (loads + enables) → title art → catalog preload →
  music → `title_and_seed()` (loops, layer stays enabled) → returns →
  `title_bg_hide()` → menu on black.
- Soft reset: `longjmp` back to the title re-runs `title_bg_show()` (load guard
  skips the reload; VRAM still holds the image) → ... → `title_bg_hide()`.
- Returning from a game/online session to the top menu: background already
  hidden; nothing to do.

## Constraints honored

- **1 VRAM bank** for the 512×256 8bpp bitmap, auto-allocated to VRAM-A by SRL's
  allocator; the text font (bank B1, `VDP2_VRAM_B1 + 0x1D000`) and map
  (`+ 0x1E000`) are untouched. Staying within one bank is a correctness
  requirement, not just a budget — see the bank-spanning note above.
- **Text always on top:** NBG3 (`Layer7`) > NBG0 (`Layer1`).
- **CD-read timing:** the one-time load runs before menu music and before the
  CD directory changes to `Z3`.

## Out of scope (noted, not implemented)

- Text-contrast mitigation. The title text is white and sits mid-screen
  (rows 12–18). If the house image is bright there, contrast may suffer.
  Follow-up options if needed: a darker text palette on the title screen, or a
  semi-transparent dim layer. Deferred unless the rendered result warrants it.
- Backgrounds behind menus or gameplay (explicitly excluded: title screen only).

## Testing / verification

This is Saturn hardware/VDP2 rendering code with no host-runnable unit surface.
Verification is by building the client (`saturn/compile.bat`, run by the user)
and observing on emulator/hardware:

1. On boot, `HOUSE.TGA` appears behind the `MOJOZORK` title, credit, and
   "Press any button to begin".
2. Pressing a button enters the menu on a solid black background (image gone).
3. Soft-reset (A+B+C+Start held on the title, or the in-game reset path)
   returns to the title with the image shown again and no visible reload delay.
4. Title text remains legible over the image (subject to the contrast caveat).
