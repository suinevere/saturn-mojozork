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
  debounce commit and (b) Sequential/Random loop-end advance.
- Backend query: the engine needs to know whether CD-DA is still playing (for
  loop-end detection). Add a query callback registered like the play callback:
  `music_set_isplaying(int (*fn)(void))`, with `music_cdda.cxx` providing
  `int music_cdda_is_playing(void)` via the SRL `Cdda` status API.
  - **Risk / to verify during implementation:** the exact SRL `Sound::Cdda` call
    that reports "finished / still playing." If no clean status query exists,
    fall back to tracking expected track duration; note this in the plan.
- Loop flag: Dynamic and Override-repeat play **looping**; Sequential and Random
  play **one-shot** so loop-end can be detected. The engine tells the backend
  whether to loop (e.g. `music_cdda_play_mode(track, loop)`, with the existing
  `music_cdda_play(track)` kept as a looping wrapper for menu/preview).

### Behavior by mode

- **Dynamic (0):** existing room-mood classification, looping. Room changes go
  through the debounce (below).
- **Override-repeat (1):** always `g_sel_track`, looping. Ignores room mood.
- **Sequential (2):** start at `g_sel_track`, one-shot; on loop-end, advance to the
  next track wrapping within 2..32.
- **Random (3):** one-shot; on loop-end, pick a random track in 2..32.

### Dynamic-mode debounce (~6s)

Implemented across `music_on_turn` and `music_tick`:

- On a room change, compute the desired target track.
  - **Desired == currently-playing track** → cancel any pending switch, do nothing;
    the stream keeps playing (no restart).
  - **Desired != playing, and a track is already playing** → set it as *pending*
    and (re)start a countdown of `MUSIC_DEBOUNCE_FRAMES` (~360 @ 60fps NTSC). A new
    differing target before the countdown elapses resets it to the newest target.
    `music_tick()` decrements each frame; at zero it commits the pending track and
    plays it.
  - **Nothing playing yet** (engine's first in-game switch, `g_active_track == 0`)
    → commit immediately so the first room is never held silent.
- Debounce applies to Dynamic only. Override-repeat never changes; Sequential/
  Random change on loop-end (their own timing).

Frame count is a named constant; assumes ~60fps NTSC. Adjust if the build targets
PAL/50fps.

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
- Remaining Options rows: Difficulty, Configure MojoZork, Gamepad/Keyboard
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
  Dynamic debounce (desired==active is a no-op; a differing target commits only
  after the countdown; a target flip before commit resets the countdown), and
  blob round-trip (`options_save` → `options_load` restores mix/track; a legacy
  blob without the sound sentinel defaults to Dynamic/10).
- On emulator: boot menu plays track 10; changing mix + track in Sound Options and
  choosing OK persists across a soft reset; Cancel reverts live and saved state;
  first room after load/new-game has audio; returning from nested pages leaves no
  leftover text.

## Out of scope

- Per-game-slot audio settings.
- New CD-DA tracks or remastering the disc.
- Changing the room-mood classification tables.
