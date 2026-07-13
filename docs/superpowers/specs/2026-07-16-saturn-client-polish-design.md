# Saturn Client Polish — Design Spec

**Date:** 2026-07-16
**Branch base:** `fce7512` (on `sound-options`)
**Status:** Approved design, pending implementation plan

## Overview

A batch of audio-behavior, console, input, and flow refinements to the Saturn
mojozork client, grouped into five areas. All work builds forward on `fce7512`
(no history rewrite). Only Saturn-side files change; the shared `mojozork.c`
core is touched only behind `#if defined(MOJOZORK_SATURN)` if at all.

## Global Constraints

- Build the Saturn image ONLY with `saturn/compile.bat` (run from `saturn/`,
  e.g. `./compile.bat debug`). Never `make`/`gcc` for the Saturn image. New
  files under `saturn/src/**` are auto-globbed.
- Pure-C engine/data (`music.c`, `music_data.c`, `typeahead*.c`) stay host-unit-
  testable with `gcc`; SRL-dependent C++/UI (`music_cdda.cxx`, `main.cxx`) is
  verified by a clean Saturn build.
- `SRL::Debug::Print` supports only `%d/%s/%c` — no width/hex flags; align with
  separate fixed-x Print calls.
- Valid CD-DA audio tracks are whatever the disc TOC reports as audio (see A5);
  the current disc uses 2..32 with track 1 = data.
- Commit messages: two sentences max, no session number.

---

## Group A — Menu / game audio

### A1. Preload the game catalog at the title screen (was item 2)

**Requirement:** Load the game-category/game-list data once, up front, before the
title screen becomes interactive. Cache it permanently; never re-read from CD.

**Approach:** Extract the CD-reading portion of `game_select()` — `scan_z3_folder()`
and the per-game `read_game_info()` classification loop (main.cxx ~1806-1829) —
into a one-time `preload_game_catalog()` that fills the existing `static`
`names[]/labels[]/cats[]` arrays and a `static int g_catalog_count` (with a
`static bool g_catalog_ready`). Call it during startup, before `title_and_seed()`
returns control to the player:
- Show a brief "Loading games…" notice, run the preload, then present the title.
- Gate title advance (the "press start/seed" interaction) until
  `g_catalog_ready` is true.
- `preload_game_catalog()` is idempotent: once `g_catalog_ready`, it returns
  immediately.

`game_select()` becomes pure UI over the cache (the category/game `menu_select`
loop, main.cxx ~1832-1860) and performs **zero** CD reads.

**Interfaces:**
- `static void preload_game_catalog(void);` — fills the catalog cache once.
- Catalog cache: `g_catalog_count`, `g_catalog_ready`, existing static arrays.

### A2. Continuous track through loading/dialer (was item 1, part 1)

**Requirement:** Do not start/stop CD-DA tracks between the loading screens and
the multizork dialer screen. The track that is playing keeps playing.

**Approach:** With A1, the menu flow (title → mode menu → category → game select →
dialer) issues no CD data reads, so CD-DA is never interrupted and needs no
re-arm. The `game_select` re-arm added in `fce7512` (`music_cdda_play(g_sel_track)`
after the header reads) becomes redundant and is **removed** — it would cause an
audible restart. The only CD read remaining in the flow is the Z3 story load at
local game boot (A3).

### A3. Start a fresh track on game load and on online-session start (was item 1, part 2)

**Requirement:** Start a new track when a (local) game loads, and also when the
online multizork session begins.

**Approach:**
- Local Z3 boot: the in-game engine already starts the game's music at game-start
  (`music_reset` → `music_set_mix` → `music_start`; Dynamic waits for the first
  room, non-Dynamic plays immediately). No change beyond confirming this runs.
- Online session: when `online_mode()` begins its session (after the dialer
  connects), start a fresh track. Online multizork is a telnet terminal with no
  local Z-machine, so the room-driven Dynamic engine does not run there; online
  therefore plays the selected track `g_sel_track` looping (a "fresh track,"
  distinct from the carried-over menu track) regardless of mix mode. If the online
  loop already ticks `music_tick()` for menu audio, Sequential/Random will advance
  naturally on loop-end; Dynamic/Override simply loop `g_sel_track`. Issue the
  fresh track once at session start; do not restart on dialer retries.

### A4. No track restart on volume change (was item 3)

**Requirement:** Changing Music volume in the Sound Options page must not restart
the current track.

**Approach:** Today `music_set_level()` (music_cdda.cxx) calls `SetVolume` and, if
a track is requested, re-issues `PlaySingle` — restarting the track. Split the
concern:
- `music_set_volume(int level)` — sets CD-DA level via `SetVolume` only; on level
  0 it may `StopPause` (mute). Never re-issues `PlaySingle`.
- Keep a re-issue path ONLY for the mute→unmute transition (level was 0 → now
  >0, where the track was actually stopped), so unmute resumes the track.
- The Sound Options Music row calls the volume-only path for in-range changes.

**Interfaces:**
- `void music_set_volume(int level);` — volume-only (no restart).
- `music_set_level` retains the unmute-resume behavior for the 0→>0 edge.

### A5. 1-based audio-track numbering + "no audio on disc" (was item 4)

**Requirement:** The Sound Options Track row numbers tracks starting at 1 over the
disc's actual audio tracks. If the disc has no audio tracks, show
`"no audio on disc"` and make the row non-adjustable.

**Approach:** Add a TOC scan that enumerates the disc's real audio-track numbers
(via `SRL::Cd::TableOfContents` + `TrackType::Audio`) into an ordered list, cached
after first read:
- `int music_cdda_audio_tracks(const unsigned char** out);` — returns N (count of
  audio tracks) and sets `*out` to an ordered array of real CD track numbers; 0 if
  none.
- Sound Options Track row displays the 1-based position (`index+1` of N) but the
  underlying selection is the real CD track number `out[index]`. `g_sel_track`
  continues to store the real CD track number, so the persistence blob (Task 5 of
  the prior feature) and old saves stay valid.
- If N == 0: the row renders `"no audio on disc"`, Left/Right do nothing, and
  preview/OK issue no play.
- Selection clamps to `[0, N-1]` over the audio list (not a raw 2..32 range).

**Note on defaults:** default `g_sel_track` stays 10 if track 10 is an audio
track; otherwise the page snaps the selection to the first audio track on open.

---

## Group B — Console pager & scroll

Current console (`render_console`, main.cxx ~467-484) shows a window of
`console_height()` rows over the scrollback, offset from the bottom by `g_scroll`
(0 = live bottom). It already draws `^`/`v` edge markers. On submit, `g_scroll`
is reset to 0 (bottom).

### B1. "more v" pager, non-blocking (was item 5)

**Requirement:** When a response is taller than the ~20-line display window —
including the initial room description at game load — the view stops at the TOP of
the new output (not auto-scrolled to the bottom) and shows a `"more v"` indicator.
Input remains available.

**Approach:**
- Track the scrollback line index at which the latest output block began
  (`g_output_start`), captured right before the game's output for a turn is
  emitted (and at game boot for the initial output).
- After output, if `total - g_output_start > rows` (new block taller than the
  window), set `g_scroll` so the window's top line == `g_output_start` (i.e.
  `g_scroll = maxstart - g_output_start`, clamped ≥ 0) instead of `g_scroll = 0`.
  Otherwise keep the normal bottom view.
- When the window's bottom is above the true bottom (`start + rows < total`),
  render `"more v"` at the bottom-right edge instead of the bare `v`. Keep the
  bare `^` for off-screen-above.
- "~20 lines" = the existing `console_height()` window; no fixed 20 hardcode
  unless the window is larger — cap the pager window at 20 rows if the console is
  taller. (Confirm `console_height()` value during planning; if it is already ~20
  this is a no-op.)

### B2. Scroll-past-top blank line (was item 6)

**Requirement:** At the very top of the scrollback, one more scroll-up reveals a
single blank line above the first line, then stops.

**Approach:** Allow `g_scroll` to reach `maxstart + 1` (one past the oldest line).
In `render_console`, when `g_scroll == maxstart + 1`, render row 0 as blank and
shift the visible lines down by one; clamp any further scroll-up at `maxstart + 1`.
The `^` marker is hidden once the blank-line top is shown (there is nothing above
it).

---

## Group C — Prompt abbreviations (was item 7)

**Requirement:** `N, NE, E, SE, S, SW, W, NW, L, I, Q, Z` are always accepted at
the prompt (never rejected by the Easy/Normal typeahead grammar filter) and are
offered as autocomplete suggestions.

**Approach:** The typeahead layer (`typeahead.c` + `typeahead_solution.c`, gated by
`typeahead_set_easy`) filters candidate words. Inject these abbreviations as an
always-valid token set:
- Add them to the typeahead trie as always-accepted words so they are never
  filtered out in Easy/Normal and appear as ghost/autocomplete suggestions.
- Confirm during planning whether these are already dictionary words in the story
  (many Z-machine stories define `n/s/e/w/l/i/q` etc.) — if so, the fix is to stop
  the grammar filter from suppressing them; if not, add them as a static
  always-suggest set. Exact mechanism (trie insertion vs filter allowlist) is a
  planning decision based on how `build_typeahead_from_story` /
  `apply_solution_overlay` classify them.

**Testing:** Pure-C host test over the typeahead trie asserting each abbreviation
is accepted and suggested in Easy and Normal modes.

---

## Group D — Quit → title (was item 8)

**Requirement:** In-game Quit currently crashes after the player accepts it. It
must instead land on the title screen with menu music playing.

**Approach:**
- **Root-cause the crash first** (systematic-debugging). Trace the quit path: the
  Z-machine `quit` opcode in the shared core → whatever Saturn hook / longjmp it
  triggers. Identify why it crashes (likely an unguarded teardown, a missing
  `MOJOZORK_SATURN` guard, or returning into freed story state).
- Route quit through the existing **Return-to-Title soft reset**
  (`soft_reset_to_title()`, used by Options → Return to Title), which is known-good
  and re-arms menu music on re-entry (boot music block, incl. the `music_reset()`
  from the prior feature). The quit opcode's Saturn handling calls that path
  instead of crashing.
- A failing repro (documented steps or a harness check) is created before the
  fix, per systematic-debugging Phase 4.

---

## Group E — In-game function keys (was item 9)

**Requirement:** Function keys active ONLY during in-game input (ignored on the
title screen and in menus). Backing out of a save/load menu resumes the current
game — it never drops to the game-select menu.

**Mappings:**
- **F2** — Save menu: slot picker for the CURRENT game.
- **F3** — Load menu: slot picker for the CURRENT game (no cross-game select).
- **F5** — quick save to the last-used slot.
- **F6 / F9** — quick load from the last-used slot.
- **F10** — Options menu.
- **F11** — Controller menu (gamepad or keyboard controls page per current
  device).

**Approach:**
- Confirm the keyboard layer exposes F-key events (`SATURN_KEY_F2`…`F11` or
  equivalent). If not, add that plumbing in the keyboard input module first.
- Handle F-keys in the in-game input loop (`saturn_readline`), not in menu loops:
  a pressed F-key opens the corresponding menu/action, and on return control
  resumes the current input line (the game is still live in memory).
- F2/F3 reuse the existing save/load slot-picker UI (`choose_dest` /
  slot-selection helpers) scoped to `g_story_filename` (current game). On
  cancel/back, return to the in-game input loop (resume game), NOT `game_select`.
- F5 (quick save) and F6/F9 (quick load) act on the last-used slot for the current
  game; track `g_last_slot` (and device) per session. If no slot has been used
  yet, F5 falls back to the F2 slot picker and F6/F9 to the F3 picker (planning to
  confirm this fallback).
- F10 calls `options_menu()`; F11 calls the current-device controls page. On
  return, resume the game.
- Quick save/load reuse the same save/restore mechanism as the menu-driven
  save/load (queue the `save`/`restore` action or call the underlying routine),
  guarded so they only fire in-game.

**Interfaces:**
- Session state: `static int g_last_slot`, `static int g_last_device` (or reuse
  existing save/restore globals).
- In-game F-key dispatch inside `saturn_readline`.

---

## Testing Strategy

- **Host unit tests (pure C):** Group C typeahead abbreviations (accepted +
  suggested in Easy/Normal). Any pure-C helper added for A5 track enumeration that
  can be isolated from SRL. Existing `music_test.c` / `music_mix_test.c` remain
  green.
- **Saturn build:** every group's Saturn-side change verified by a clean
  `compile.bat debug`.
- **Emulator/hardware pass (user):** the behavioral items with no automated
  surface — A1 preload gating, A2 continuous audio, A3 fresh track on load/online,
  A4 no-restart-on-volume, A5 1-based numbering + "no audio on disc", B1 pager, B2
  scroll-past-top, D quit→title, E function keys.

## Out of Scope / Follow-ups (not this spec)

- Persisting the four keyboard-controls flags (`g_caret_arrows`/insert/caps/num)
  in the options blob — carried over from the prior feature's final review.
- Any change to the pure-C mix-engine debounce/latch behavior beyond what the
  above requires.

## Open Technical Unknowns (resolved in planning)

- **D:** exact root cause of the Quit crash (requires tracing the quit opcode's
  Saturn path).
- **E:** whether the keyboard layer already emits F-key events, or that plumbing
  must be added.
- **C:** whether the abbreviations are already story-dictionary words (filter
  allowlist) or need trie insertion (static suggest set).
- **B1:** the actual `console_height()` value vs the "~20 lines" target.
