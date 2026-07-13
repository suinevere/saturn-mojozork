# Title-Screen Background Image Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display `HOUSE.TGA` as a full-screen background image behind the title screen, replacing the black backdrop, and hide it when the player enters the menu.

**Architecture:** Load `HOUSE.TGA` onto the otherwise-unused VDP2 **NBG0** scroll layer at a priority below the debug text layer (NBG3, `Layer7`), so the existing title text renders on top automatically. Two static helpers (`title_bg_show` / `title_bg_hide`) manage a one-time load-and-enable and a hide; they are wired into the existing title flow in `main()`.

**Tech Stack:** C++ (SaturnRingLib / SGL), Sega Saturn VDP2. Build via `saturn/compile.bat` (run by the user).

## Global Constraints

- **Build command:** `saturn/compile.bat` — the assistant does NOT run it; the user builds. Make edits only.
- **VDP2 bitmap containers:** 512×256 / 512×512 / 1024×256 / 1024×512. The 320×224 RGB555 image uses the **512×256** container (2 VRAM banks, auto-allocated).
- **Text priority:** debug text is on NBG3 at `Layer7` (top); the image layer must be a lower priority (`Layer1`).
- **CD-read timing:** the one-time image load must run **before** `preload_game_catalog()` (which cd's into `Z3`) and **before** `music_cdda_play()` (single CD head cannot read data while playing CD-DA).
- **Asset:** `saturn/cd/data/HOUSE.TGA`, 320×224, 32-bit uncompressed truecolor, at the CD root. Referenced as the bare filename `"HOUSE.TGA"`.

---

## File Structure

- Modify: `saturn/src/main.cxx`
  - Add two static helpers immediately after `title_draw_art()` (currently ends at line 1982).
  - Add two call sites in `main()`: `title_bg_show()` before the boot title draw (~line 2459), `title_bg_hide()` after `title_and_seed()` returns (~line 2467).

No new files. No new headers (`srl.hpp`, already included at `main.cxx:1`, provides `SRL::Bitmap::TGA` and `SRL::VDP2::NBG0`).

---

### Task 1: Title-screen background image on NBG0

**Files:**
- Modify: `saturn/src/main.cxx` (helpers after line 1982; call sites at ~2459 and ~2467)

**Interfaces:**
- Consumes: `SRL::Bitmap::TGA(const char*)`, `SRL::VDP2::NBG0::LoadBitmap(IBitmap*)`, `SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority)`, `SRL::VDP2::NBG0::ScrollEnable()`, `SRL::VDP2::NBG0::ScrollDisable()` (all from SRL, verified present in `srl_tga.hpp` / `srl_vdp2.hpp`).
- Produces: file-local `static void title_bg_show(void)` and `static void title_bg_hide(void)` — used only within `main.cxx`.

**Note on testing:** This is Saturn VDP2 rendering code with no host-runnable unit-test surface, so the standard write-failing-test-first cycle does not apply. The verification cycle is: build (user) → observe on emulator/hardware → commit. Steps below reflect that.

- [ ] **Step 1: Add the two helper functions**

Insert immediately after the closing brace of `title_draw_art()` (currently `main.cxx:1982`):

```cpp
// ---- title-screen background image (HOUSE.TGA on VDP2 NBG0) -----------------
// Shown behind the title text only; menus and gameplay stay on solid black.
// The debug text console lives on NBG3 at priority Layer7 (top), and NBG0 is
// disabled by default, so loading the image on NBG0 at a lower priority puts it
// safely behind all text with no other VDP2 changes.
static void title_bg_show(void) {
    static bool loaded = false;
    if (!loaded) {
        // One-time CD read + VRAM upload. Runs before the CD directory changes
        // to Z3 (so "HOUSE.TGA" resolves at the root) and before menu CD-DA
        // starts (the single CD head can't read data while playing audio).
        SRL::Bitmap::TGA* bmp = new SRL::Bitmap::TGA("HOUSE.TGA");
        SRL::VDP2::NBG0::LoadBitmap(bmp);
        delete bmp;   // pixels now live in VDP2 VRAM; free the work-RAM copy
        SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer1);  // below text (Layer7)
        loaded = true;
    }
    SRL::VDP2::NBG0::ScrollEnable();
}

static void title_bg_hide(void) {
    SRL::VDP2::NBG0::ScrollDisable();
}
```

- [ ] **Step 2: Wire `title_bg_show()` into the boot title draw**

Find the boot title draw in `main()` (currently `main.cxx:2459`):

```cpp
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
    title_draw_art();
    SRL::Core::Synchronize();
```

Add `title_bg_show();` on its own line immediately before `title_draw_art();`:

```cpp
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
    title_bg_show();
    title_draw_art();
    SRL::Core::Synchronize();
```

This placement is before `preload_game_catalog()` (next lines) and before `music_cdda_play()`, satisfying the CD-read-timing constraint.

- [ ] **Step 3: Wire `title_bg_hide()` after the title wait**

Find where `title_and_seed()` returns in `main()` (currently `main.cxx:2467`):

```cpp
    int seed = title_and_seed();         // redraws the same art + "Press any button", waits
```

Add `title_bg_hide();` on its own line immediately after it:

```cpp
    int seed = title_and_seed();         // redraws the same art + "Press any button", waits
    title_bg_hide();                     // menus and gameplay run on solid black
```

- [ ] **Step 4: Build**

The user builds via `saturn/compile.bat`. Expected: compiles cleanly (no new headers; all SRL symbols already available through `srl.hpp`). Resolve any compile errors before proceeding.

- [ ] **Step 5: Verify on emulator/hardware**

Run the produced disc image and confirm:
1. On boot, `HOUSE.TGA` appears behind the `MOJOZORK` title, credit, and "Press any button to begin".
2. Pressing a button enters the menu on a solid black background (image gone).
3. Soft-reset on the title (A+B+C+Start held) — after re-entry the image shows again with no visible reload delay.
4. Title text stays legible over the image (contrast caveat from the spec; mitigation is out of scope unless it looks bad).

- [ ] **Step 6: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Add HOUSE.TGA background image behind the title screen"
```

---

## Self-Review

**1. Spec coverage:**
- "Display HOUSE.TGA behind title, black elsewhere" → Steps 1–3 (show before title, hide after). ✓
- "VDP2 NBG0 bitmap, priority below text" → Step 1 (`LoadBitmap` + `SetPriority(Layer1)`). ✓
- "One-time load, guarded" → Step 1 (`static bool loaded`). ✓
- "Load before Z3 cd and before music" → Step 2 placement. ✓
- "Soft-reset re-shows with no reload" → `loaded` guard + VRAM persistence; verified in Step 5.3. ✓
- "Hide on menu entry" → Step 3. ✓
- Out-of-scope contrast/other-screen backgrounds → not implemented, noted in Step 5.4. ✓

**2. Placeholder scan:** No TBD/TODO/vague steps; all code shown in full. ✓

**3. Type consistency:** Helper names `title_bg_show` / `title_bg_hide` used identically in Steps 1–3. SRL calls match verified signatures (`LoadBitmap(IBitmap*)` accepts `TGA*`; `SetPriority(SRL::VDP2::Priority)`). ✓
