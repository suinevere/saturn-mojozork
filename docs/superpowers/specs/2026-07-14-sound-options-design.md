# Sound Options & Menu Music — Design

Date: 2026-07-14
Scope: Saturn client (`saturn/src/main.cxx`, `saturn/src/music.*`, `saturn/src/music_cdda.cxx`).

## Goal

Consolidate all audio settings into a dedicated **Sound Options** page with a real
OK/Cancel contract, add an **Audio Mix** mode selector with track demo/selection,
persist everything, and fix the audio that never starts (boot menu, first room).
Also give the Controls / Keyboard Controls pages the same OK/Cancel behavior and
fix the nested-page-left-on-screen bug.

## Decisions (settled during brainstorming)

- **Sequential / Random advance timing:** on track **loop-end** (each CD track
  plays fully, then advance/randomize). Not on room change.
- **Persistence:** the existing global `MOJOOPTS` console-backup blob. One global
  preference set across all games/slots. No per-game-slot storage.
- **Menu music:** the selected track plays (looping) for the *whole* menu flow —
  from the title screen through mode-select, game-select, and Options — regardless
  of mix mode. Hand off to the in-game engine only once a game begins.
- **Cancel behavior:** Cancel reverts everything (mix mode, selected track, both
  levels) *and* restores the audio that was playing when the page opened. OK
  commits and saves.
- **Room-change debounce:** a ~6-second buffer before committing a Dynamic
  room-driven track change; a desired target equal to the currently-playing track
  keeps the stream smooth (never restarts).

## State & persistence

New globals in `main.cxx` (alongside `g_music_level` / `g_pcm_level`):

- `g_mix_mode` — `0 = Dynamic`, `1 = Override-repeat`, `2 = Sequential`, `3 = Random`. Default `0`.
- `g_sel_track` — selected/override track, also the menu track. Default **10**.

Selectable track range: **2..32** (track 1 is data; see cd/music/tracklist).

### Blob format (`options_load` / `options_save`)

Current blob layout: `[difficulty][dialnum...][NUL][music][pcm][ctrl-sentinel=2][FA_N face][CA_N chord]`.

Extend **append-only** after the controller-mapping block, behind a new trailing
sentinel, so existing blobs keep loading (controller-mapping offset math is
untouched) and simply default the new fields:

```
... [FA_N face][CA_N chord] [sound-sentinel=1][mix_mode][sel_track]
```

- `options_load`: after reading the controller block, if the next byte is the
  sound-sentinel `1` and two more bytes follow, read `g_mix_mode` (clamp 0..3) and
  `g_sel_track` (clamp 2..32). Absent/old blob → keep defaults (Dynamic, 10).
- `options_save`: append the sentinel + two bytes. Confirm the buffer (`buf[64]`)
  still has room after the controller bytes; it does (worst case well under 64).

## Mix engine (`music.h` / `music.c` / `music_cdda.cxx`)

### New/changed API

- `music.c`: `void music_set_mix(int mode, int override_track)` — stores mode +
  override track for the engine.
- `music.c`: `void music_tick(void)` — called once per frame from the main game
  loop (and menu loops that keep the game running). Drives (a) the Dynamic-mode
  debounce commit, (b) Sequential/Random loop-end advance, and (c) Dynamic short-
  track re-pick when a play-once track ends.
- Backend query: the engine needs to know whether CD-DA is still playing (for
  loop-end detection). Add a query callback registered like the play callback:
  `music_set_isplaying(int (*fn)(void))`, with `music_cdda.cxx` providing
  `int music_cdda_is_playing(void)` via the SRL `Cdda` status API.
  - **Risk / to verify during implementation:** the exact SRL `Sound::Cdda` call
    that reports "finished / still playing." If no clean status query exists,
    fall back to tracking expected track duration; note this in the plan.
- Loop flag: a **long** Dynamic track and the Override-repeat/menu track play
  **looping**; Sequential, Random, and any **short** track play **one-shot** so
  loop-end can be detected. The engine tells the backend whether to loop (e.g.
  `music_cdda_play_mode(track, loop)`, with the existing `music_cdda_play(track)`
  kept as a looping wrapper for menu/preview).

### Behavior by mode

- **Dynamic (0):** room-mood classification → a *category*, then a **random track
  from that category's pool** (see "Category track pools"). The pick is made once
  per category change and held while you stay in that category; room changes go
  through the debounce (below).
- **Override-repeat (1):** always `g_sel_track`, looping. Ignores room mood.
- **Sequential (2):** start at `g_sel_track`, one-shot; on loop-end, advance to the
  next track wrapping within 2..32.
- **Random (3):** one-shot; on loop-end, pick a random track in 2..32 (across all
  tracks — distinct from the per-category pools, which apply only to Dynamic).

### Dynamic-mode debounce (~6s), keyed on category

The Dynamic engine tracks the **category** currently sounding (`g_active_cat`), not
just the track, so re-entering a room of the same mood keeps the exact track
playing (no re-randomize, no restart). Implemented across `music_on_turn` and
`music_tick`:

- On a room change, compute the target category (event override, else the room's
  base category).
  - **Target category == currently-sounding category** → cancel any pending switch,
    do nothing; the stream keeps playing (no restart, no re-pick). This is the
    "matches current, keep it smooth" case.
  - **Target category differs, and a track is already playing** → pick a random
    track from the target category's pool, set it *pending* (with its category),
    and (re)start a countdown of `MUSIC_DEBOUNCE_FRAMES` (~360 @ 60fps NTSC).
    While a target of the *same pending category* keeps recurring, the countdown
    keeps running without re-randomizing; a target of a *new* category re-picks and
    resets the countdown. `music_tick()` decrements each frame; at zero it commits
    the pending track+category and plays it.
  - **Nothing playing yet** (engine's first in-game switch, `g_active_track == 0`)
    → pick and commit immediately so the first room is never held silent.
- Debounce applies to Dynamic only. Override-repeat never changes; Sequential/
  Random change on loop-end (their own timing).

### Category track pools

Replace the single-track `CATEGORY_TRACK[]` in `music_data.c` with a per-category
**pool** of CD-DA tracks; Dynamic mode picks a random track from the pool on each
category change. Per the requirement, the **Neutral** pool is merged into every
other category's pool (deduped). Source lists were given as 1-based audio indices
("Track NN"); the disc's playable audio is CD-DA tracks 2..32, so the stored values
are `NN + 1`. Final merged, deduped pools (CD-DA track numbers):

```
NEUTRAL     :  4  5  6 10 11 12 16 22 24 28 30                                  (11)
WILDERNESS  :  4  5  6  9 10 11 12 16 17 22 24 28 30 31                         (14)
UNDERGROUND :  2  3  4  5  6  7 10 11 12 16 18 19 20 22 23 24 28 29 30          (19)
WATER       :  2  4  5  6  7  8 10 11 12 16 20 21 22 24 26 28 30                (17)
NAUTICAL    :  2  3  4  5  6  7 10 11 12 16 19 20 21 22 24 26 28 30             (18)
TOWN        :  4  5  6  9 10 11 12 16 22 24 28 30                               (12)
DUNGEON     :  4  5  6  9 10 11 12 16 17 18 19 20 22 23 24 28 29 30             (18)
DESERT      :  4  5  6  9 10 11 12 16 22 24 28 30                               (12)
MAGIC       :  4  5  6  8 10 11 12 16 18 19 21 22 23 24 26 28 29 30             (18)
SCIFI       :  3  4  5  6  8 10 11 12 14 15 16 18 19 22 23 24 27 28 30          (19)
HORROR      :  2  4  5  6  7  8 10 11 12 13 14 15 16 19 22 24 27 28 30          (19)
MYSTERY     :  3  4  5  6  8 10 11 12 15 16 21 22 24 27 28 30                   (16)
DANGER      :  4  5  6 10 11 12 13 14 15 16 17 22 24 27 28 30                   (16)
TRIUMPH     :  4  5  6  9 10 11 12 16 22 24 25 28 29 30                         (14)
```

API in `music_data.c`:

- `int music_category_pool(int cat, const unsigned char** out)` — returns the pool
  size and sets `*out` to the track array (used by the engine and tests).
- `int music_category_track(int cat)` — picks a random track from the pool (0 if
  `cat` invalid). The engine calls it only on a category change (never per-turn),
  so a room's track stays stable until its category changes.

RNG: a small seedable PRNG in `music.c` (e.g. LCG), with `music_seed(unsigned)` so
gameplay varies while host tests stay deterministic. Seed from the game seed at
`music_reset`/`music_set_game`.

### Short tracks (play once, never loop)

Some CD-DA tracks are short stingers that sound wrong looped (e.g. track 25 ≈ 7s).
Rather than a hand-maintained label, **auto-detect** short tracks by duration:

- Detection: read the CD **TOC** at runtime in `music_cdda.cxx` — track length =
  (next track's start LBA − this track's start LBA) sectors ÷ 75 sectors/sec. Any
  track shorter than `MUSIC_SHORT_SECONDS` (≈15s) is short. Expose
  `int music_cdda_is_short(int track)`.
  - **Risk / to verify:** the SRL API for reading the CD TOC / track start LBAs. If
    unavailable, fall back to a generated short-track table computed from `.raw`
    lengths at build time.
- Playback rule: a short track is played **one-shot** and never looped in Dynamic
  mode. When it ends (detected by `music_tick` via the is-playing query), Dynamic
  **re-picks** from the *current category's* pool, preferring a non-short track (if
  the whole pool is short, pick any). Menu playback and **Override-repeat** honor
  their explicit "keep playing / repeat" intent and loop even a short track;
  Sequential/Random are already one-shot, so a short track simply advances sooner.

Frame count is a named constant; assumes ~60fps NTSC. Adjust if the build targets
PAL/50fps.

### Gapless loop (best-effort)

Looping a CD-DA track can leave an audible gap when the drive seeks back to the
track start each cycle. For the **looping** cases (a long Dynamic track, Override-
repeat, the menu track), attempt to stitch the loop so it repeats without a
noticeable gap:

- Prefer the Saturn CD block's **native seamless repeat** — play the track (or its
  LBA range) with a hardware repeat count so the block re-reads from the track's
  start without a full re-seek, rather than our per-frame "detect end, re-issue
  `PlaySingle`" path (which re-seeks and gaps). If SRL's loop flag already drives
  the hardware repeat, ensure the looping cases use it.
- If we must re-issue on end, pre-arm the next play as early as the is-playing
  query allows to shrink the gap.
- **Best-effort:** if the CD block can't loop a single track seamlessly, accept the
  small gap — this is a polish item, not a correctness requirement. The one-shot
  modes (Sequential/Random/short) intentionally change *tracks* on end; a brief gap
  between two different tracks there is expected and out of scope for stitching.
- **Risk / to verify:** the exact SRL `Sound::Cdda` repeat/loop semantics (hardware
  seamless vs. software re-issue).

## Sound Options page (new, full-screen, OK/Cancel)

A new nested page opened from the Options menu. Rows:

1. **Audio Mix** — Left/Right cycles Dynamic / Override / Sequential / Random.
2. **Track** — Left/Right selects (2..32); A demos/previews the current track live.
3. **Music level** — Left/Right, 0..7.
4. **PCM level** — Left/Right, 0..7.
5. **[OK]**
6. **[Cancel]**

Input:

- **Start / A = OK** — commit `g_mix_mode`, `g_sel_track`, `g_music_level`,
  `g_pcm_level`; `options_save()`; apply audio (see below); close.
- **Esc / B = Cancel** — restore all four values to the on-open snapshot, restore
  the track/level that was playing when the page opened, no save; close.
- On-screen `[OK]` / `[Cancel]` rows are also selectable with A/Enter.

Snapshot on open: mix mode, selected track, music level, PCM level, and the
currently-playing track + level (so Cancel can restore live audio).

Live preview: moving Track/Music/PCM auditions immediately (music level via
`music_set_level`, track via a looping preview play, PCM via `sound_set_level`).

Apply on OK:

- **Non-Dynamic mix** → start playback immediately (the selected/override track),
  satisfying "if Dynamic Audio off, start playing track when menu closes."
- **Dynamic** → hand back to the room engine (`music_refresh` / next room change).

## Options page cleanup

- Remove the inline **Music**, **PCM**, and **Track** rows. Replace with a single
  **"Sound Options"** row that opens the new page.
- Remaining Options rows: Difficulty, Configure Z-ATURN, Gamepad/Keyboard
  Controls, Sound Options, Return to Title Screen, Done.
- **Nested-page-left-on-screen fix:** the full-screen nested pages (Sound Options,
  Controls, Keyboard Controls) paint the whole screen; the Options box only
  repaints its small region. Clear the whole screen when returning from any
  full-screen nested page before repainting the box.

## Controls & Keyboard Controls OK/Cancel

Give both pages (and the mapping editor) the same contract:

- Snapshot the editable state on open — `configure_controls_page`: the face/chord
  mapping arrays; `keyboard_controls_page`: caret-arrows / insert / caps / num
  flags.
- Add **[OK]** / **[Cancel]** rows; **Start = OK** (keep + `options_save` where the
  state is persisted), **Esc = Cancel** (restore the snapshot, no save).
- `config_page` (dial number) already has accept/cancel; align its labels/keys to
  the same Start=OK / Esc=Cancel wording.

## Boot & menu music

- Install the CD-DA backend before the title screen (currently installed lazily in
  the typeahead setup path; move/duplicate so it's set at startup).
- `options_load()` already runs at startup; it now also restores mix mode and
  selected track.
- At the title screen, start `g_sel_track` (default 10) looping, and keep it
  playing through mode-select, game-select, and Options for all mix modes.
- On game start, after `sound_init` + `sound_set_level` + `music_set_level`,
  call `music_set_mix(g_mix_mode, g_sel_track)` and assert playback:
  - Non-Dynamic → play the selected/override track.
  - Dynamic → let `music_on_turn` classify the first room; the menu track keeps
    streaming until that first commit, so the first room is never silent.

## Bug root-causes addressed

- **Boot menu silent:** no code path ever played music before a game ran — now the
  menu track plays from the title screen.
- **First room silent:** structural fix — a track is already streaming at game
  start and the engine only *changes* tracks on room change; the immediate-commit
  path for the first switch (`g_active_track == 0`) avoids the debounce holding it
  silent. Exact CD-DA start timing after the story-file read will be verified on
  emulator/hardware during implementation.

## Testing

- Host unit tests (`saturn/tests`, existing pattern): mix-mode selection
  (`music_set_mix` → correct target track per mode), Sequential wrap at 32→2,
  category pools (every `music_category_track` pick is a member of that category's
  pool; Neutral tracks appear in every pool; pools stay within 2..32; seeded RNG is
  deterministic), Dynamic debounce keyed on category (same-category re-entry is a
  no-op that keeps the exact track; a new category commits only after the countdown;
  a category flip before commit re-picks and resets the countdown), short-track
  handling (a track under the threshold is played one-shot and triggers a same-pool
  re-pick preferring non-short; TOC/length detection classifies the known stinger),
  and blob round-trip (`options_save` → `options_load` restores mix/track; a legacy blob
  without the sound sentinel defaults to Dynamic/10).
- On emulator: boot menu plays track 10; changing mix + track in Sound Options and
  choosing OK persists across a soft reset; Cancel reverts live and saved state;
  first room after load/new-game has audio; returning from nested pages leaves no
  leftover text; a looping ambient track repeats without an obvious gap (best-effort).

## Out of scope

- Per-game-slot audio settings.
- New CD-DA tracks or remastering the disc.
- Changing the room-mood **keyword** tables (`KW` / `EV`). The category → track
  mapping *is* in scope (replaced by the pools above); the word → category
  classification is unchanged.
