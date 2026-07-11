# Z-machine v3 Sound for The Lurking Horror — Design

**Date:** 2026-07-11
**Status:** Approved (ready for implementation plan)

## Goal

When *The Lurking Horror* issues a Z-machine `sound_effect`, play the matching
sample through the Saturn's SCSP. Parse the game's `.BLB` (Blorb) resource file
on-device, stream each sound's PCM to a sound channel, mix a looping ambient bed
under one-shot effects, honor stop/volume, and expose a Sound On/Off toggle.

The Lurking Horror is the only v3 Infocom game with sound effects, so this is
per-game and opt-in (driven by the presence of a sibling `.BLB`).

## Background (verified facts)

- **Sound file:** `saturn/cd/data/Z3/LRKHOROR.BLB`, 583,200 bytes, standard
  **Blorb** (`FORM…IFRS`). Its `RIdx` indexes **14 sounds** (`Snd #3,4,6–13,
  15–18`), each an **AIFF**: mono, **8-bit signed PCM, uncompressed (NONE),
  ~9676 Hz**. Sound lengths range ~7 KB–60 KB (≈0.8 s–6 s).
- **Loop chunk:** marks **7 sounds** (`#4,10,13,15,16,17,18`) as looping with
  `repeats = 0` (loop forever until stopped).
- **mojozork:** `sound_effect` (VAR opcode **245**) is an unimplemented
  `OPCODE_WRITEME` stub. The v5 variant (`sound_effect_ver5`, also 245) stays a
  stub — not needed for a v3 game.
- **SRL audio:** `SRL::Sound::Pcm` plays 8/16-bit PCM from a **main-RAM buffer**
  via SGL's PCM driver (`slPCMOn`), across **4 channels**; it computes SCSP pitch
  from the sample rate. `SRL::Cd::File::LoadBytes(offset, size, buf)` reads an
  arbitrary slice of a CD file — so a single sound's `SSND` bytes can be pulled
  straight out of the `.BLB`.
- **Constraints:** SCSP sound RAM ≈ 512 KB and total PCM ≈ 570 KB, so sounds are
  **loaded per-effect**, not preloaded. HWRAM heap ≈ 572 KB comfortably holds the
  story + typeahead trie + a couple of 60 KB sound buffers.
- **Build:** `SRL_USE_SGL_SOUND_DRIVER = 0` today; PCM playback needs it **on**.

## Approach

Chosen resource path: **parse the `.BLB` on-device** (no build-time conversion —
the `.BLB` is the single drop-in source and the PCM is already SCSP-native).
Chosen playback: **faithful mixing** (looping bed + one-shots, up to 4 channels).
Chosen looping: **re-trigger** a looping sound when its channel goes idle (simple;
a faint gap on short loops is acceptable for v1). SCSP hardware-loop registers are
a noted refinement if gaps are audible.

## Components

Each unit has one purpose and a small interface.

### 1. Blorb sound index — `saturn/src/sound_blorb.{c,h}`
- `int sound_blorb_open(const char* filename)` — open the `.BLB` on CD, parse the
  `RIdx` and, for each `Snd #N`, its AIFF `COMM` (channels/bits/rate) + `SSND`
  (data offset/length within the file), and the `Loop` chunk. Builds an in-memory
  index. Reads only headers/index (~hundreds of bytes); PCM stays on CD. Returns 1
  on success, 0 if absent/unparsable.
- `int sound_blorb_get(int number, uint32_t* off, uint32_t* len, uint16_t* rate, int* loops)`
  — look up a sound by its Z-machine number; returns 0 if unknown.
- `void sound_blorb_close(void)` — reset the index (on game switch).

Format notes: Blorb = `FORM/IFRS`; `RIdx` entries are `{usage[4], number, start}`;
each sound resource `start` points to a `FORM…AIFF` with `COMM` + `SSND`; the
`SSND` sample data begins 8 bytes past the chunk's data start (offset/blocksize
fields). The `Loop` chunk is `{number, repeats}` pairs.

### 2. Playback glue — `saturn/src/main.cxx` (extern "C" hook)
- `extern "C" void saturn_sound_effect(int number, int effect, int volume)`:
  - **effect 2 (start):** look up the sound; `LoadBytes(off,len)` its slice into a
    HWRAM buffer; set an `SRL::Sound::Pcm` handle (8-bit mono, rate); play on a
    channel. If it loops, record `{number, channel, buffer}` as the active loop.
  - **effect 3 (stop):** stop the sound's channel(s); clear loop state if it was
    the looping sound.
  - **effect 1 (prepare):** pre-load the slice into a buffer to hide CD latency
    (used if the game prepares before starting; otherwise start loads on demand).
  - **effect 4 (finish):** stop + free buffers for that sound.
  - **Volume:** map Z-machine volume (1–8, 255 = default) → SRL 0–127.
  - **Channels:** one-shots use a free channel (`Pcm::FindChannel`); the looping
    bed keeps its own channel. Re-starting the already-playing loop is a no-op.
  - **Sound Off / no `.BLB` / load fail / no free channel:** silently skip.
- `void sound_service(void)` — called once per input frame from `saturn_readline`
  (and the reboot/menu loops as convenient); re-triggers a looping sound whose
  channel has gone idle (`slPCMStat`), and frees finished one-shot buffers.

### 3. mojozork opcode — `saturn/mojozork.c`
- Replace `OPCODE_WRITEME(245, sound_effect)` with `opcode_sound_effect`: read the
  operands (`number`, `effect`, `volume`), then, if set, call a **`GState`
  function pointer** `void (*sound_effect)(int, int, int)` — the same pattern as
  `GState->writestr` / `readline` / `die`. The Saturn port sets it in `mojo_boot`
  to `saturn_sound_effect`; the multizork server and host/test builds leave it
  NULL, so the opcode is a silent no-op there (no link dependency on Saturn code).

### 4. Lifecycle / detection
- On game boot, derive `<basename>.BLB` from the story filename; `sound_blorb_open`
  it. Present + Sound On → sound enabled; else disabled (opcode no-ops).
- On reboot / game switch, stop all channels and `sound_blorb_close`.

### 5. Options + persistence
- Add `g_sound_on` (default 1). Extend the `MOJOOPTS` backup blob to
  `difficulty + dial number + sound flag` (older blobs load with sound defaulting
  On). Add a **Sound: On / Off** row to `options_menu`; turning it Off stops any
  playing sound and makes the hook a no-op.

### 6. Build
- Set `SRL_USE_SGL_SOUND_DRIVER = 1` in `saturn/Makefile` (copies the SGL sound
  driver onto the CD; required for `slPCM`). The `.BLB` already ships on the CD.

## Data flow

Game turn → `sound_effect` opcode (245) → `saturn_sound_effect(n, effect, vol)` →
(start) `sound_blorb_get` → `LoadBytes` the `SSND` slice from the `.BLB` on CD →
`SRL::Sound::Pcm` buffer → `slPCMOn` on a channel. Looping tracked; the per-frame
`sound_service` re-triggers finished loops. (stop) → channel off.

## Error handling

- No `.BLB` / parse failure / unknown sound number / CD read failure / no free
  channel / Sound Off → skip silently; the game continues normally.
- Non-Lurking-Horror games never issue `sound_effect` and have no `.BLB`, so there
  is zero behavioral impact on them.

## Testing / verification

- **Host (no hardware):** extend the Python probe to extract a sound's `SSND`
  bytes at the computed offset/length and wrap them as a `.wav` (8-bit unsigned or
  signed as appropriate) — listen to confirm the parser, offsets, and rate are
  correct. Do this for a one-shot and a looping sound.
- **Build:** compile with the sound driver enabled; confirm the image builds and
  links.
- **On-device (only true audio test):** run The Lurking Horror, trigger an early
  sound, confirm playback, that a looping bed loops and continues under a one-shot,
  and that stop works. Confirm no regression to silent games.

## Scope / YAGNI

- v3 `sound_effect` effects 1–4 only; no v5 finish-routine callback.
- No volume/pitch envelopes beyond the single volume operand.
- PCM only (no CD-audio path).
- Per-game via sibling `.BLB`; only Lurking Horror has one.
- Re-trigger looping for v1; SCSP hardware-loop is a follow-up if gaps are audible.

## Open questions

None blocking. The one accepted risk is a possible faint gap on short looping
sounds (mitigation noted above).
