# Design: `zaturn.netbin` — self-contained Saturn Zork for PlanetWeb 4.0

**Date:** 2026-07-21
**Status:** Approved, pending implementation plan

## Goal

Produce a second build target that emits a single self-contained Saturn
executable loadable by the PlanetWeb 4.0 browser's `.netbin` loader — a
"Full port, CD assets stripped" configuration that boots straight into the
existing title/menu flow with Zork I embedded, requiring no CD at runtime.

A separate **companion audio disc** is an optional add-on: when present, it
restores CD-DA music. The netbin never depends on it.

## Target contract (assume worst case)

The `.netbin` loader constraints we design against:

- **Entry point / load base `0x06010000`** (stock SRL builds link at
  `0x06004000`). The executable must be linked with its entry strictly at
  this address.
- **Single image < 400 KB.**
- **Startup must re-initialize video modes itself** — we cannot assume the
  browser left VDP1/VDP2 in any known state.
- **No CD access required at runtime** — at boot the drive holds PlanetWeb's
  own disc, not ours. Every *mandatory* CD read must be removed or replaced.

## Size budget

Measured from the current CD build's object sizes (`sh2eb-elf-size`):

| Item | KB |
| --- | --- |
| Current on-disc image (preloader + `.text` + `.data` + `.rodata`) | ~313 |
| − stripped modules (`game_catalog`, `typeahead_extract` text; `typeahead_solution` is **kept**) | −74 |
| + embedded `ZORK1.Z3` (`.rodata`) | +85 |
| + sound code retained (`sound` 7.7, `sound_blorb` 1.1, `music` 2.6, `music_cdda` 2.7, `music_data` 2.4) | +16.5 |
| + embedded `SDDRVS.TSK` (26,610 B) + `BOOTSND.MAP` (82 B) | +26 |
| **Total** | **~350** |
| **Headroom under 400 KB** | **~50** |

`.bss` is `NOLOAD` — it never lands in the file, so large runtime buffers
(`typeahead_extract` ~59 KB, `saturn_backup` ~26 KB) cost nothing against the
400 KB file ceiling; they consume HWRAM, of which there is ample spare.

`typeahead` autocomplete is **kept** — dropping the ~65 KB
`typeahead_solution` blob is unnecessary at this budget.

**`SDDRVS.DAT` is not needed.** SRL's `Hardware::Initialize()`
(`srl_sound.hpp:28`) loads only `SDDRVS.TSK` and `BOOTSND.MAP`. The 163 KB
`SDDRVS.DAT` is referenced nowhere in code — only in `shared.mk:519`'s clean
rule. It is copied to the disc and never read, in the CD build too.

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
2. **Sound driver from RAM, not CD.** SRL's sound stack must still compile,
   but `Hardware::Initialize()` reads the driver via `Cd::File`. The netbin
   supplies its **own initializer** that runs the same sequence — `SND_Init`,
   `SND_ChgMap(0)`, `slInitSound`, `CDC_CdInit`, `SND_SetCdDaLev(7,7)` —
   against the embedded `SDDRVS.TSK` / `BOOTSND.MAP` arrays instead of CD
   reads. The function is ~20 lines and fully visible in the header, so this
   is a small local reimplementation, not an SRL fork. (Exact mechanism —
   compile guard vs. call-site replacement — is settled in the plan.)
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
- Options → **Display** (colors), Options → **Controls**, Options →
  **Return to Title**.
- Options → **Sound** — retained, since CD-DA works with the companion disc.
  The page is already availability-gated: `main.cxx:437` only opens it when
  `music_cdda_has_audio() || sound_has_audio()`, and the PCM level row is
  gated on the loaded game's `.BLB`. Both gates resolve correctly on their
  own — the entry simply appears once an audio disc is present.
- **Save / Restore** and the **Load Save Game** menu entry — backup RAM via
  `saturn_backup`, which uses the BIOS backup library, not the CD. Already
  required for options persistence (MOJOOPTS blob).
- **CD-DA music** (`music`, `music_cdda`, `music_data`) — see Companion disc.
- **Play Online** (multizork NetLink telnet) — best-effort; see Risks.

### Dropped

- Background artwork / TGA loading; Options → **Background** selector.
- Multi-game catalog and game-select menu (`game_catalog`) — collapses to the
  single embedded title.
- PCM sound effects are retained in code but **inert for Zork I**: effects
  load from a per-game Blorb sibling and Zork I ships no `.BLB` (none exists
  anywhere on the disc). This is already true of the CD build.

## Embedded payload

- `ZORK1.Z3` (85 KB) linked into `.rodata` as a build-time-embedded blob
  (`.incbin` in a small `.S`/`.c` object, e.g. `story_zork1`).
- `SDDRVS.TSK` + `BOOTSND.MAP` (~26 KB) embedded the same way.
- The engine reads the story from a **RAM pointer** into the embedded blob
  instead of via `SRL::Cd::File`.
- `opcode_restart` — currently a CD re-read at `main.cxx:511` — re-copies from
  the embedded blob instead.

## Companion audio disc (optional)

An audio-only disc the player may swap in after the netbin has loaded. The
netbin is fully resident in HWRAM and the story is embedded, so **no data
reads occur during play** — the drive streams audio and nothing else.

This is a genuine improvement over the CD build: the audio-skip problems that
motivated the LWRAM TGA cache and the front-loaded title-screen preloads stem
from one drive head serving both data reads and CD-DA. That contention does
not exist here.

Requirements:

- **TOC cache invalidation.** `music_cdda.cxx`'s `toc_raw()` guards on
  `g_toc_ready` and reads the TOC **exactly once, ever**. After a disc change
  it would keep serving the browser disc's stale TOC. Add a
  `music_cdda_toc_reset()` that clears the flag, invoked on a swap / on entry
  to the Sound Options page.
- **No track-number surgery needed.** `music_cdda.cxx` already derives
  everything from the raw 102-longword TOC's control bits (`toc_is_audio`,
  `toc_track_no` on words 99/100) rather than hardcoding an audio-track
  offset, so a differently-laid-out disc is discovered automatically.
- **Minimal data track 01.** `promote_game_track` (`lib/music.sh:5`) only
  *warns* when no game track exists, and `process_audio` skips Track 1 from
  the music dir — so a disc built without `games.bat` yields a cue referencing
  a missing track 01. The companion disc needs a small data track so it is a
  proper Saturn mixed-mode disc that authenticates normally on swap.

### Second `CONFIG.ME`

Add a separate config for the **NetLink Custom Web Browser** disc, carrying
**only** the variables `music.bat` actually reads — both its shell and Windows
blocks parse exactly three keys:

```
AUDIO_URL=…
DISC_NAME=NetLink Custom Web Browser
OUTPUT_DIR=./NetLink Custom Web Browser
```

Deliberately **omitted** (they belong to `games.bat`): `GAMES_URL`,
`LURKING_URL`, `ADVENT_URL`, `DEST`, `AUDIO_DIR`, `BASE_ISO`.

> **Do not run `games.bat` against this config.** It is music-only. Running
> the game staging against it would inject game data tracks and defeat the
> purpose of the disc.

Implementation note: `music.bat` hardcodes the filename `CONFIG.ME` in both
execution blocks. It needs a config-path override (first argument or an env
var) defaulting to `CONFIG.ME`, so existing behavior is unchanged.

## Boot-path surgery (the bulk of the work)

All in `main.cxx`, guarded by `#ifdef NETBIN`. The stock boot sequence is
dense with CD calls, each of which would fail or hang with no readable disc:

- Skip `cd_capture_root`, `GFS_Reset`, `preload_game_catalog`,
  `display_preload_images`, and every mandatory `SRL::Cd::File` read (notably
  the story loads around `main.cxx:511`, `:772`, `:1063`, and `title_bg_show`).
- `display_scan_images()` returns an **empty list** → the existing
  `display_apply` fallback (documented to return false and paint a solid
  background when no image loads) does the right thing with no new code.
- The story "load" becomes a pointer-set / `memcpy` from the embedded blob.
- Sound init runs from the embedded driver arrays (see Build configuration).
- Add an **explicit video-mode re-initialization** at entry, forcing the
  SRL init path rather than assuming the browser's leftover state.

## Risks (flagged, not solved)

- **Disc swap behavior.** Swapping discs mid-run is ordinary multi-disc Saturn
  behavior, but the exact CD-block state after a swap under PlanetWeb cannot
  be predicted from here. Needs hardware testing.
- **Play Online / NetLink modem.** Whether PlanetWeb leaves the NetLink modem
  in a cold-startable state after the browser has used it is *unknown*. The
  code is kept; if the modem proves unusable, Play Online must degrade to an
  on-screen error rather than hang.
- **Video re-init correctness.** Depends on the actual VDP state the browser
  hands over; likely needs iteration on real hardware.

## Non-goals

- A second embedded story file (no budget at ~350 KB).
- Downloading story files over NetLink.
- A telnet-only build.
- Changing or refactoring the CD build's behavior.
