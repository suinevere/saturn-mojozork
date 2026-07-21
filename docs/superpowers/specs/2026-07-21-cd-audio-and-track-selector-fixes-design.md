# CD track selector, Sound Options entry, and the Play Online audio drop — design

**Date:** 2026-07-21
**Branch:** `main`
**Status:** implemented (`f066b18`); confirmed on target 2026-07-21

## Problem

Four faults reported from play on real hardware, all touching CD audio:

1. **Leaving Sound Options restarted the music** whenever the page was opened
   mid-game, even if nothing on it was touched.
2. **The Track row always read `10`**, the saved default, instead of whatever
   was actually sounding.
3. **The track list ran to 40 entries on a 32-track disc**, and everything past
   roughly entry 16 played nothing (the previously-selected track kept going).
4. **Audio stopped on the way into Play Online** — but only once per power-on. A
   soft reset and a second visit were silent-free.

Items 2 and 3 turned out to share one root cause, and item 1 was a downstream
consequence of item 3. Item 4 is unrelated.

## Root cause analysis

### The TOC struct is the wrong size (items 2, 3, and 1)

`SRL::Cd::TableOfContents` cannot be used to read the Saturn's table of
contents. The declaration in `SaturnRingLib/saturnringlib/srl_cd.hpp`:

```cpp
struct ITrack { unsigned int Control : 4; /* + member functions */ };

struct TrackLocation : public ITrack {
    unsigned int Number : 4;
    unsigned int FrameAddress : 24;
};
```

`Control` lives in a **base subobject**, so it gets its own 4-byte allocation
unit and the derived class's bit-fields cannot pack into it. `TrackLocation` is
therefore 8 bytes, not the 4 the on-disc format needs. Measured directly (the
host compiler applies the same Itanium ABI rules as SH-2 gcc):

```
ITrack=4  TrackLocation=8  TrackInformation=8  TOC=812   (want 408)
```

`CDC_TgetToc` writes exactly 102 longwords — 408 bytes — into whatever it is
handed. Against an 812-byte struct that means:

- `toc.Tracks[t]` reads TOC longword **2t**, i.e. a different track than asked
  for. Entry `t` reported the control nibble of CD track `2t+1`.
- Every entry past `t = 50` lies beyond the 408 bytes the BIOS wrote and is
  **uninitialized stack**. A control nibble of `0x0f` means "absent"; arbitrary
  stack bytes are `0x0f` only 1 time in 16, so most of that tail decoded as
  phantom *audio* tracks.

That is the 40-entry list: ~15 real entries (the misaligned half of the disc's
31 audio tracks) plus a random tail of stack garbage. Selecting one of the
phantom entries called `PlaySingle()` with a track number the disc does not
have, which the CD block ignores — so the previous track simply kept playing,
which reads as "17–40 all play 16".

Item 1 falls straight out of this. `sound_options_page` opened by searching the
track list for the saved `g_sel_track` and, on a miss, **forcing**
`g_sel_track = atracks[0]`. Because the saved default (10) was rarely in the
garbage list, the forced write happened almost every time; the OK handler then
compared against its entry snapshot, saw `g_sel_track != s_trk`, and restarted
playback. Merely opening and closing the page changed state.

The same misreading also made `music_cdda_is_short` measure the wrong track's
frame delta, and made the CD TOC diagnostic page — which existed specifically to
hunt this bug — display fiction.

### The online vocabulary load was never front-loaded (item 4)

The Saturn has one drive head: it cannot read data and play CD-DA at the same
time. The established response is to front-load every menu-flow CD read into the
title screen's silent window (`preload_game_catalog`, `display_preload_images`),
after which no menu screen touches the CD and the menu track plays unbroken.

`ensure_online_typeahead()` was missed. It reads `ZORK1.Z3` to build the
typeahead trie and ran lazily on the first "Play Online" — with the menu track
playing. Its retry loop (up to 40 attempts, each constructing an
`SRL::Cd::File` and waiting 8 frames, working around a first-access
`GFS_GetFileSize` that returns garbage) turned one interruption into a long
stutter.

The once-per-power-on behaviour is the giveaway: `g_online_ta` is a static and
survives the soft-reset `longjmp`, so every pass after the first is a no-op.
Nothing about soft reset was special — the work had simply already been done.

## Non-goals

- Editing `SaturnRingLib/`. The SDK is a pinned submodule; the fix is local
  decoding in `saturn/src/music_cdda.cxx`.
- Reworking the mix engine's track ceiling (see Deferred).
- Preloading story bytes to mask CD reads. Considered and rejected: the reads
  belong in the silent window, not in RAM.

## Design

### Read the TOC directly

`music_cdda.cxx` now owns a raw `uint32_t g_toc[102]` filled once by
`CDC_TgetToc`, plus small accessors. The BIOS layout, documented at the top of
the file:

| Longword | Meaning |
|---|---|
| `[0..98]` | one entry per CD track 1..99: `(ctrladr << 24) \| fad`; absent = `0xFFFFFFFF` |
| `[99]` | first-track record: `(ctrladr << 24) \| (track number << 16) \| ...` |
| `[100]` | last-track record, same layout |
| `[101]` | lead-out: `(ctrladr << 24) \| fad` |

`ctrladr`'s high nibble is the control field: bit 2 set = data, clear = audio,
`0x0f` = entry absent. FAD is 1/75 s frames.

- `music_cdda_audio_tracks()` walks **only** first-track..last-track, so absent
  slots and everything past the end of the BIOS data are never consulted, and
  returns **real CD track numbers** — the same ones the `.cue` lists and
  `PlaySingle()` takes. On our discs that is `{2, 3, ..., 32}`.
- `music_cdda_has_audio()` is now just "is that list non-empty".
- `music_cdda_is_short()` measures the last track against the lead-out instead
  of running off the end of the track array.
- A bogus TOC (no disc, or a read that beat the drive) yields track number 0
  from the first/last records and degrades to "no audio" rather than garbage.

### Open Sound Options on what is playing

New backend accessor `music_cdda_current_track()` returns the track last handed
to the CD block (0 = none). It covers both cases: menu screens call
`music_cdda_play()` directly, and in-game the engine plays through
`music_cdda_play_mode()` — both set the same variable.

`sound_options_page` now resolves its initial row as: currently-playing track →
saved `g_sel_track` → first audio track. Critically it **does not write**
`g_sel_track` on entry, and the Track row commits only on an actual Left/Right/A
interaction. Opening and closing the page is now a genuine no-op, so the OK
handler's `previewed || mix changed || track changed` guard means what it says.

### Move the vocabulary load into the silent window

`ensure_online_typeahead()` joins the other two preloads at the title screen.
`online_mode()` still calls it (idempotent), which now only re-reads the CD if
the difficulty changed since boot — a deliberate options change, not the boot
path.

The `music_cdda_play()` call inside its retry loop was dropped. It existed to
recover music the retries had killed; on the boot path there is no music yet, so
it would only start a track for the next retry to silence again. Callers
re-assert playback once the reads are done.

### Remove the CD TOC diagnostic page

`toc_dump_page` and the Sound Options row reaching it are gone. The page was
built to hunt this exact bug; with the cause found and the decode local and
documented, it is dead weight in a shipping menu. Sound Options is back to five
rows (Audio Mix / Track / Music / PCM / OK / Cancel, minus whatever the disc and
game do not provide).

## Testing

Host tests (unchanged, all passing):

```
music_mix_test    ALL PASS
music_test        ALL PASS
test_display      OK
test_menu_layout  OK
```

`sh syntax-check.sh src/main.cxx src/menu_pages.cxx` exits clean against the
real SRL/SGL headers.

`music_cdda.cxx` cannot be syntax-checked in isolation — the harness does not
define `SRL::Sound` for it, and that failure **pre-dates** these changes
(verified by stashing the file and re-running). Its correctness rests on the
struct-size measurement above plus the on-target run.

None of the TOC decoding is reachable from host tests: it needs a real drive.
Verification was the user's on-target run — track list now 31 entries, every one
audible, Sound Options opens on the playing track and exits silently.

## Deferred

- **`MUSIC_TRACK_MAX` is still a hardcoded 33.** The Sequential and Random mix
  modes and the override clamp use it, so on a 32-track disc they can select a
  track that does not exist (a no-op, then recovery on the next loop-end). Now
  that `music_cdda_audio_tracks()` is trustworthy, feeding the real list into
  `music.c` — via a setter alongside the existing `music_set_isshort` /
  `music_set_isplaying` backend hooks — would remove the guesswork. Left alone
  here to keep the pure-C engine and its host tests untouched.
- **The flaky first-access `GFS_GetFileSize` retry still runs at story-load
  time**, after game selection and outside the silent window. Same class of
  problem as item 4 and audible in principle, but not what was reported. The
  catalog preload deliberately sidesteps the size stat (`read_game_info` uses a
  sector-addressed `LoadBytes`), so nothing warms it before play. Warming it per
  game during the preload, and caching the byte length, would move those retries
  into the silent window.
