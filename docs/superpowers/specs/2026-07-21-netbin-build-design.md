# Design: `zaturn.netbin` — self-contained Saturn Zork for PlanetWeb 4.0

**Date:** 2026-07-21
**Status:** Approved, pending implementation plan

## Goal

Produce a second build target that emits a single self-contained Saturn
executable loadable by the PlanetWeb 4.0 browser's `.netbin` loader — a
"Full port, CD assets stripped" configuration that boots straight into the
existing title/menu flow with Zork I embedded, requiring no CD at runtime.

## Target contract (assume worst case)

The `.netbin` loader constraints we design against:

- **Entry point / load base `0x06010000`** (stock SRL builds link at
  `0x06004000`). The executable must be linked with its entry strictly at
  this address.
- **Single image < 400 KB.**
- **Startup must re-initialize video modes itself** — we cannot assume the
  browser left VDP1/VDP2 in any known state.
- **No CD access at runtime** — the drive holds PlanetWeb's own disc, not
  ours. Every runtime CD read must be removed or replaced.

## Size budget

Measured from the current CD build's object sizes (`sh2eb-elf-size`):

| Item | KB |
| --- | --- |
| Current on-disc image (preloader + `.text` + `.data` + `.rodata`) | ~313 |
| − stripped modules (`typeahead_solution` is **kept**; drop `sound`, `sound_blorb`, `music`, `music_cdda`, `music_data`, `game_catalog`, `typeahead_extract` text) | −91 |
| Stripped port | ~222 |
| + embedded `ZORK1.Z3` (`.rodata`) | +85 |
| **Total** | **~307** |
| **Headroom under 400 KB** | **~93** |

`.bss` is `NOLOAD` — it never lands in the file, so large runtime buffers
(`typeahead_extract` ~59 KB, `saturn_backup` ~26 KB) cost nothing against the
400 KB file ceiling; they consume HWRAM, of which there is ample spare.

`typeahead` autocomplete is **kept** — dropping the ~65 KB
`typeahead_solution` blob is unnecessary at this budget.

## Build configuration

A new build path selected by a `NETBIN=1` make flag. It does **not** replace
the CD build; both must continue to work.

1. **Linker script.** A copy of `SaturnRingLib/modules/sgl/sgl.linker` named
   `sgl-netbin.linker`, identical except the base is `PRELOADER 0x06010000`.
   The stock script (line 4, `PRELOADER 0x06004000 :`) is a single literal;
   changing it shifts every section (`.text/.data/.rodata/.bss/heap`) up as a
   block. The only cost is 48 KB of heap headroom (heap ends at the fixed
   `work_area_start = ALIGN(0x060FC000 …)`), which is irrelevant here. The CD
   build keeps using the unmodified `sgl.linker`.
2. **Sound driver off.** `SRL_USE_SGL_SOUND_DRIVER = 0`. This removes the
   boot-time `SDDRVS.TSK`/`SDDRVS.DAT` CD load (~190 KB) — the single most
   important reason the netbin can boot with no disc present.
3. **Conditional compilation.** CD-dependent code is guarded by `#ifdef
   NETBIN` so the same source tree produces both builds.
4. **Output.** The target emits the artifact into `BuildDrop/` as
   `zaturn.netbin`, produced the same way `0.bin` is today: the linked ELF is
   converted to a flat raw image (`objcopy -O binary`). We assume the loader
   consumes a bare raw image whose first byte corresponds to `0x06010000`,
   with no additional container header. If hardware testing shows a header or
   wrapper is required, only this packaging step changes — nothing else in
   this design depends on it.

## Feature surface

### Kept

- Z-Machine engine (`mojozork` / `mojozork_saturn`).
- Console view, on-screen + hardware keyboard, `typeahead` autocomplete.
- Title screen and menu flow, rendered on a **solid background** (no TGA).
- **Custom text / background / palette colors** (`g_display` colors are
  independent of background *images*, so they survive intact).
- Options → **Display** (colors), Options → **Controls**.
- Options → **Return to Title**.
- **Save / Restore** and the **Load Save Game** menu entry — backup RAM via
  `saturn_backup`, which uses the BIOS backup library, not the CD. Already
  required for options persistence (MOJOOPTS blob), so game saves are nearly
  free on top.
- **Play Online** (multizork NetLink telnet) — kept as best-effort; see Risks.

### Dropped

- CD-DA music (`music`, `music_cdda`, `music_data`).
- PCM sound effects (`sound`, `sound_blorb`).
- Background artwork / TGA loading; Options → **Background** selector.
- Options → **Sound** page.
- Multi-game catalog and game-select menu (`game_catalog`) — collapses to the
  single embedded title.

## Embedded story

- `ZORK1.Z3` (85 KB) is linked into `.rodata` as a build-time-embedded blob
  (`.incbin` in a small `.S`/`.c` object, e.g. `story_zork1`).
- The engine reads the story from a **RAM pointer** into the embedded blob
  instead of via `SRL::Cd::File`.
- `opcode_restart` — currently a CD re-read at `main.cxx:511` — re-copies from
  the embedded blob instead.
- Game selection collapses to the single fixed title; the catalog UI is
  compiled out.

## Boot-path surgery (the bulk of the work)

All in `main.cxx`, guarded by `#ifdef NETBIN`. The stock boot sequence is
dense with CD calls, each of which would fail or hang with no disc:

- Skip `cd_capture_root`, `GFS_Reset`, `preload_game_catalog`,
  `display_preload_images`, and every `SRL::Cd::File` read (notably the story
  loads around `main.cxx:511`, `:772`, `:1063`, and `title_bg_show`).
- `display_scan_images()` returns an **empty list** → the existing
  `display_apply` fallback (documented to return false and paint a solid
  background when no image loads) does the right thing with no new code.
- The story "load" becomes a pointer-set / `memcpy` from the embedded blob.
- Add an **explicit video-mode re-initialization** at entry, forcing the
  SRL init path rather than assuming the browser's leftover state.

## Risks (flagged, not solved)

- **Play Online / NetLink modem.** Whether PlanetWeb leaves the NetLink modem
  in a cold-startable state after the browser has used it is *unknown*. The
  code is kept; if the modem proves unusable under the browser, Play Online
  must degrade to an on-screen error rather than hang. Cannot be de-risked
  without hardware testing.
- **Video re-init correctness.** Depends on the actual VDP state the browser
  hands over; likely needs iteration on real hardware.

## Non-goals

- A second embedded story file (would push to ~397 KB, no margin).
- Downloading story files over NetLink.
- A telnet-only build.
- Changing or refactoring the CD build's behavior.
