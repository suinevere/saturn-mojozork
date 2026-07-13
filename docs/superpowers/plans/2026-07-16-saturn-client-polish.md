# Saturn Client Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the twelve requested Saturn-client refinements — continuous menu audio with a preloaded game catalog, no-restart volume, 1-based audio-track numbering, a non-blocking "more v" pager, a scroll-past-top affordance, always-accepted direction/command abbreviations, a crash-free Quit that returns to the title, and in-game function keys.

**Architecture:** Almost all work is in the Saturn-only TUs (`main.cxx`, `music_cdda.cxx`, `saturn_keyboard.*`, `typeahead*.c`). The pure-C typeahead change (Group C) is host-unit-tested; every Saturn-side change is verified by a clean `saturn/compile.bat debug`. Two tasks carry a runtime-discovery step (F-key scancodes; the Quit crash root cause) and use systematic-debugging-style instrumentation before their fix.

**Tech Stack:** C (mojozork core, typeahead, keyboard), C++ (`main.cxx`, SRL `Cdda`/`Cd`), host `gcc` for pure-C tests, `saturn/compile.bat` for the image.

## Global Constraints

- Build the Saturn image ONLY with `saturn/compile.bat` from `saturn/` (`./compile.bat debug`). Never `make`/`gcc` for the image. New `saturn/src/**` files auto-glob.
- Host unit tests are pure C: `gcc -O2 -I saturn/src -o /tmp/<n> test/<n>.c saturn/src/<impl>.c ... && /tmp/<n>`.
- `SRL::Debug::Print` supports only `%d/%s/%c` — no width/hex flags; align with separate fixed-x Print calls.
- The shared `saturn/mojozork.c` core: any new Saturn call from it MUST be guarded by `#if defined(MOJOZORK_SATURN)`.
- `g_sel_track` holds a REAL CD track number (persisted in the MOJOOPTS blob); do not change its meaning.
- Commit messages: two sentences max, no session number.
- Do not stage or touch the user's in-flight CD-asset changes (`saturn/cd/music/*.bin`, `saturn/cd/data/HOME.tga`); commit only the files each task names, via `git commit <paths> -m ...`.

## Known anchors (verified 2026-07-16 @ fce7512)

- `console_height()` = `SCREEN_ROWS - TOP_MARGIN` (main.cxx:167-168); `TOP_MARGIN = 1` (main.cxx:148).
- `render_console()` main.cxx:467-484: window over scrollback, `g_scroll` = lines up from bottom (0 = live bottom), draws `^`/`v` edge markers. `g_scroll` clamped to `[0, maxstart]`.
- `g_scroll` reset to 0 on submit at main.cxx:891 (`saturn_readline`) and main.cxx:2060 (`online_mode`).
- Keyboard events: `SaturnKeyKind` enum in `saturn_keyboard.h:8-28` (no F-keys yet); scancode→event map in `saturn_keyboard.cxx:161-183` (custom SGL codes: Enter 90/25, Esc 118, arrows 134/137/138/141, Insert 129, etc.). `code` is the raw `raw[KBD_OFF_CODE]` byte.
- `music_set_level(int)` music_cdda.cxx:10-21: sets volume AND re-issues `PlaySingle` when `g_track>0` (this is the restart to fix in A1). `music_cdda_play(int)` = looping wrapper.
- `music_cdda_is_short` (music_cdda.cxx) already reads `SRL::Cd::TableOfContents::GetTable()` — model A2's TOC scan on it.
- `game_select()` main.cxx:1798-1861: `scan_z3_folder` (1810) + `read_game_info` loop (1823-1829) are the CD reads; browse loop (1832-1860) has none. `fce7512` added a `music_cdda_play(g_sel_track)` re-arm after 1829 — A3 removes it.
- Boot/menu-music block main.cxx ~2101-2108: `music_reset(); music_set_level(g_music_level); music_cdda_play(g_sel_track);` before `title_and_seed()`.
- Menu-flow: `title_and_seed()` (1657) → mode `menu_select` loop (2119-2139) → `game_select()` / `online_mode()` (1961) → Z3 load (2150-2166) → game-start sound block (2170-2186) → `mojo_run()` → terminal `while(1) render_console()` (2197).
- `mojo_run()` (mojozork_saturn.c:23-27) returns when `GState->quit` set. `soft_reset_to_title()` (main.cxx:601) — in-process return to title, never returns; re-runs the boot-music block.
- Save/restore: `g_restore_device`/`g_restore_slot` (main.cxx:41-42) + `g_autocmd` (43); slot picker `choose_dest(...)` (1582) and the save-slot editor (1445-...); `g_autocmd="restore"` applied on the next readline (828-829).
- Typeahead: `typeahead_set_easy(bool easy, int have_solution)` and `build_typeahead_from_story`/`apply_solution_overlay` in `typeahead.c`/`typeahead_solution.c`; installed in `ensure_typeahead()` (main.cxx:60-72). Trie type `TrieNode` in `typeahead.h`.

## File Structure

- `saturn/src/music_cdda.cxx` — add `music_set_volume` (no-restart), `music_cdda_audio_tracks` (TOC audio enumeration).
- `saturn/src/music.h` — prototypes for the two new backend functions.
- `saturn/src/main.cxx` — Sound Options Track/Music rows (A1/A2), catalog preload (A3), online fresh track (A4), pager (B1/B2), Quit→title (D), in-game F-keys (E2).
- `saturn/src/saturn_keyboard.h` / `saturn_keyboard.cxx` — F-key enum values + scancode mapping (E1).
- `saturn/src/typeahead.c` (+ `.h`) — always-accept + suggest the abbreviation set (C).
- `test/typeahead_abbrev_test.c` — new host test for Group C.

---

## Task 1: No-restart volume (`music_set_volume`) [Group A4]

**Files:**
- Modify: `saturn/src/music_cdda.cxx:10-21`
- Modify: `saturn/src/music.h` (Saturn CD-DA backend section)
- Modify: `saturn/src/main.cxx` (Sound Options Music/PCM rows in `sound_options_page`)

**Interfaces:**
- Produces: `void music_set_volume(int level);` — sets CD-DA output level (0..7) via `SetVolume` only; on 0 calls `StopPause`; NEVER re-issues `PlaySingle`. `music_set_level` keeps its unmute-resume (0→>0) behavior.

- [ ] **Step 1: Add the prototype to `music.h`**

Under the "Saturn CD-DA backend" comment add:
```c
void music_set_volume(int level);   /* 0..7 volume only; never restarts the track */
```

- [ ] **Step 2: Implement `music_set_volume` and narrow `music_set_level`**

In `saturn/src/music_cdda.cxx`, replace the `music_set_level` body (lines 10-21) with:
```cpp
extern "C" void music_set_volume(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    g_level = level;
    if (level == 0) SRL::Sound::Cdda::StopPause();     // mute: stop output, no restart
    else            SRL::Sound::Cdda::SetVolume((uint8_t) level);
}

extern "C" void music_set_level(int level) {
    int was_muted = (g_level == 0);
    music_set_volume(level);
    // Only the mute->unmute edge restarts: the track was actually stopped at 0.
    if (was_muted && level > 0 && g_track > 0)
        SRL::Sound::Cdda::PlaySingle((uint16_t) g_track, g_loop != 0);
}
```
(Keep the existing `g_level`, `g_track`, `g_loop` statics.)

- [ ] **Step 3: Use the volume-only path in the Sound Options Music row**

In `saturn/src/main.cxx` `sound_options_page`, the Music row currently calls `music_set_level(g_music_level)` on Left/Right (grep for `music_set_level(g_music_level)` inside that function). Replace that in-row live-apply call with `music_set_volume(g_music_level);`. Leave the OK-commit path calling `music_set_level` (so an OK after an unmute still resumes correctly). PCM row is unchanged (`sound_set_level`).

- [ ] **Step 4: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: zero errors; image produced. (Pre-existing warning ~main.cxx:1090 is not ours.)

- [ ] **Step 5: Commit**

```bash
git commit saturn/src/music.h saturn/src/music_cdda.cxx saturn/src/main.cxx -m "Split music volume from playback so changing the Music level in Sound Options only sets CD-DA volume. Only the mute-to-unmute edge restarts the track."
```

---

## Task 2: 1-based audio-track numbering + "no audio on disc" [Group A5]

**Files:**
- Modify: `saturn/src/music_cdda.cxx` (add `music_cdda_audio_tracks`)
- Modify: `saturn/src/music.h`
- Modify: `saturn/src/main.cxx` `sound_options_page` (Track row)

**Interfaces:**
- Consumes: `SRL::Cd::TableOfContents` (as in `music_cdda_is_short`).
- Produces: `int music_cdda_audio_tracks(const unsigned char** out);` — returns N (count of audio tracks on the disc) and sets `*out` to an ordered static array of real CD track numbers; 0 (and `*out=0`) if none. Cached after first read.

- [ ] **Step 1: Add the prototype to `music.h`**
```c
int music_cdda_audio_tracks(const unsigned char** out);  /* N audio tracks; *out=real track numbers */
```

- [ ] **Step 2: Implement the TOC audio-track scan in `music_cdda.cxx`**

Add (model the TOC access on the existing `music_cdda_is_short`):
```cpp
/* Ordered list of the disc's real CD-DA (audio) track numbers, from the TOC.
   Cached after the first read. Track 1 is typically the data track. */
extern "C" int music_cdda_audio_tracks(const unsigned char** out) {
    static unsigned char list[99];
    static int n = -1;
    if (n < 0) {
        n = 0;
        SRL::Cd::TableOfContents toc = SRL::Cd::TableOfContents::GetTable();
        for (int t = 1; t < SRL::Cd::MaxTrackCount && n < 99; t++) {
            if (toc.Tracks[t].GetType() == SRL::Cd::TableOfContents::TrackType::Audio)
                list[n++] = (unsigned char) t;
        }
    }
    if (out) *out = (n > 0) ? list : 0;
    return n;
}
```
(Verify `TrackType::Audio` and `MaxTrackCount` names against `srl_cd.hpp` during implementation — they are used verbatim by the SDK's own TOC code.)

- [ ] **Step 3: Rework the Sound Options Track row for 1-based display + "no audio on disc"**

In `sound_options_page` (main.cxx), the Track row currently steps `g_sel_track` over `MUSIC_TRACK_MIN..MUSIC_TRACK_MAX` and displays `%d`. Replace with an index over the audio list:
- At the top of `sound_options_page`, after the snapshot, add:
  ```cpp
  const unsigned char* atracks; int an = music_cdda_audio_tracks(&atracks);
  int aidx = 0;                     // current index into the audio-track list
  for (int i = 0; i < an; i++) if (atracks[i] == g_sel_track) { aidx = i; break; }
  ```
- Track-row Left/Right handling becomes:
  ```cpp
  else if (sel == 1) {
      if (an > 0) {
          if (left  && aidx > 0)      aidx--;
          if (right && aidx < an - 1) aidx++;
          g_sel_track = atracks[aidx];
          if (left || right || ok) music_cdda_play(g_sel_track);   // preview
      }
  }
  ```
- Track-row render:
  ```cpp
  if (an > 0) SRL::Debug::Print(x + 14, y++, "%d  (A=demo)", aidx + 1);   // 1-based
  else        SRL::Debug::Print(x + 14, y++, "no audio on disc");
  ```
- On OK/Cancel commit paths, `g_sel_track` already holds a real track number, so nothing else changes. If `an > 0` and no member matched (e.g. default 10 absent), `aidx` stays 0 and the first Left/Right snaps `g_sel_track` to `atracks[0]` — acceptable; optionally seed `g_sel_track = atracks[0]` when no match and `an>0`.

- [ ] **Step 4: Build**

Run: `cd saturn && ./compile.bat debug` → clean.

- [ ] **Step 5: Commit**

```bash
git commit saturn/src/music.h saturn/src/music_cdda.cxx saturn/src/main.cxx -m "Number the Sound Options track selector 1..N over the disc's actual audio tracks from the TOC. Show 'no audio on disc' and lock the row when the disc has none."
```

---

## Task 3: Preload the game catalog at the title; drop the game_select re-arm [Group A1/A2]

**Files:**
- Modify: `saturn/src/main.cxx` — `game_select` (1798-1861), a new `preload_game_catalog`, the startup/title flow (~2101-2110).

**Interfaces:**
- Produces: `static void preload_game_catalog(void);` fills static `names[]/labels[]/cats[]` + `static int g_catalog_count`, `static bool g_catalog_ready`; idempotent.

- [ ] **Step 1: Hoist the catalog cache to file scope**

The arrays `names`, `labels`, `cats` are currently `static` locals inside `game_select` (main.cxx:1800-1802). Move them to file scope near the other statics (with `MAX_GAMES`), and add:
```cpp
static int  g_catalog_count = 0;
static bool g_catalog_ready = false;
```

- [ ] **Step 2: Extract the CD-reading scan into `preload_game_catalog`**

Add above `game_select`:
```cpp
// Read the Z3 folder + each game's header ONCE (these CD reads stop CD-DA); cache
// the result so game_select does no CD I/O and the menu track plays uninterrupted.
static void preload_game_catalog(void) {
    if (g_catalog_ready) return;
    g_catalog_count = scan_z3_folder(names, MAX_GAMES);
    if (g_catalog_count > 0) {
        for (int i = 0; i < g_catalog_count; i++) {
            const char* title = read_game_info(names[i], &cats[i]);
            int j = 0; const char* src = title ? title : names[i];
            for (; src[j] && j < 39; j++) labels[i][j] = src[j];
            labels[i][j] = '\0';
        }
    }
    g_catalog_ready = true;
}
```

- [ ] **Step 3: Make `game_select` use the cache (no CD reads, no re-arm)**

Replace `game_select`'s body up to the browse loop: call `preload_game_catalog()` (idempotent), then use `g_catalog_count` as `count`. Delete the "Loading games..." notice + `scan_z3_folder` + the classification loop (they moved to preload) AND delete the `fce7512` `music_cdda_play(g_sel_track)` re-arm line. The browse loop (categories→games) is unchanged but reads `g_catalog_count`/`cats`/`labels`/`names`.

- [ ] **Step 4: Preload during the title, gate title advance until ready**

In the startup flow, call `preload_game_catalog()` before the title becomes interactive. Concretely: in `title_and_seed()` (main.cxx:1657), before its input loop, show a one-frame "Loading games..." then call `preload_game_catalog();` so the title only becomes seed-able once the catalog is cached. (If `title_and_seed` structure makes that awkward, call `preload_game_catalog();` immediately before `int seed = title_and_seed();` at main.cxx:2110 — the effect is the same: cached before any game menu.) The boot-music block (2101-2108) stays before this; the preload's CD reads happen once here and never again.

- [ ] **Step 5: Build**

Run: `cd saturn && ./compile.bat debug` → clean.

- [ ] **Step 6: Commit**

```bash
git commit saturn/src/main.cxx -m "Preload the game catalog once at the title screen and cache it, so game selection does no CD reads and the menu track keeps playing. Drop the now-redundant game_select re-arm."
```

---

## Task 4: Fresh track on online-session start [Group A3]

**Files:**
- Modify: `saturn/src/main.cxx` — `online_mode` (1961).

**Interfaces:**
- Consumes: `music_cdda_play`, `g_sel_track`, `music_tick` (if the online loop ticks it).

- [ ] **Step 1: Start a fresh track when the session begins**

In `online_mode()` (main.cxx:1961), after the dialer connects and just before entering the online input/session loop (grep for the first `SRL::Core::Synchronize()`/render in the connected path), add:
```cpp
    music_cdda_play(g_sel_track);   // fresh track for the online session (no local room engine)
```
Do NOT put this inside the redial retry loop — it must fire once per session, not per dial attempt. If `online_mode` already calls `music_tick()` in its loop (grep), Sequential/Random will advance naturally; otherwise the track simply loops, which is correct.

- [ ] **Step 2: Build**

Run: `cd saturn && ./compile.bat debug` → clean.

- [ ] **Step 3: Commit**

```bash
git commit saturn/src/main.cxx -m "Start a fresh menu track when the online multizork session connects, instead of carrying over the prior track. It fires once per session, not per dial retry."
```

---

## Task 5: "more v" pager + scroll-past-top blank line [Group B1/B2]

**Files:**
- Modify: `saturn/src/main.cxx` — `render_console` (467-484), scroll-clamp sites, submit sites (891, 2060), a new `g_output_start`.

**Interfaces:**
- Produces: `static int g_output_start;` — scrollback line index where the latest output block began. Behavior changes to `render_console` and the post-submit scroll positioning.

- [ ] **Step 1: Add output-start tracking**

Near `g_scroll` (main.cxx:288) add `static int g_output_start = 0;`. Set it to the current `console_line_count()` immediately BEFORE the game emits a turn's output. The cleanest hook: in `saturn_readline`, right after a line is submitted/echoed and before the interpreter runs the next turn — i.e., capture `g_output_start = console_line_count();` at the point the submitted command is handed back to the core (grep the submit path near main.cxx:888-897). Also set `g_output_start = 0` for the initial room (before `mojo_run`, at the game-start block ~2186) so "even on init" positions from the top.

- [ ] **Step 2: Position at top-of-output instead of bottom after a turn**

Where submit currently does `g_scroll = 0;` (main.cxx:891 and 2060), replace with a helper call:
```cpp
static void console_scroll_to_output(void) {
    int total = console_line_count(), rows = console_height();
    int maxstart = (total > rows) ? (total - rows) : 0;
    // If the new block is taller than the window, show it from its TOP; else bottom.
    if (total - g_output_start > rows) {
        int start = g_output_start;                 // desired top line
        g_scroll = maxstart - start;                // lines up from bottom
        if (g_scroll < 0) g_scroll = 0;
    } else {
        g_scroll = 0;                               // live bottom
    }
}
```
Call `console_scroll_to_output();` in place of both `g_scroll = 0;` submit resets.

- [ ] **Step 3: "more v" marker + scroll-past-top blank line in `render_console`**

Rewrite `render_console` (467-484) to (a) allow `g_scroll` up to `maxstart + 1` (one blank line above the top), and (b) print `"more v"` instead of `v` when there is off-screen text below:
```cpp
static void render_console(void) {
    int rows = console_height();
    int total = console_line_count();
    int maxstart = (total > rows) ? (total - rows) : 0;
    if (g_scroll < 0)            g_scroll = 0;
    if (g_scroll > maxstart + 1) g_scroll = maxstart + 1;   // one blank line past the top
    int top_blank = (g_scroll == maxstart + 1) ? 1 : 0;     // showing the blank-line affordance
    int start = maxstart - (g_scroll - top_blank);
    for (int r = 0; r < rows; r++) {
        SRL::Debug::PrintClearLine(TOP_MARGIN + r);
        int li = start + r - top_blank;                     // shift down by the blank row
        if (li >= 0 && li < total)
            SRL::Debug::Print(0, TOP_MARGIN + r, "%s", console_get_line(li));
    }
    if (start > 0 && !top_blank) SRL::Debug::Print(39, TOP_MARGIN, "^");
    if (start + rows < total)    SRL::Debug::Print(35, TOP_MARGIN + rows - 1, "more v");
}
```
(`more v` starts at x=35 so it fits the 40-col line; the bare `^` still marks off-screen-above and hides once the blank-line top is shown.)

- [ ] **Step 4: Build**

Run: `cd saturn && ./compile.bat debug` → clean.

- [ ] **Step 5: Commit**

```bash
git commit saturn/src/main.cxx -m "Stop a long response at the top of its output with a 'more v' marker instead of auto-scrolling to the bottom, including the first room. Allow one blank line past the top of the scrollback as an end-of-history affordance."
```

---

## Task 6: Always-accept + suggest direction/command abbreviations [Group C]

**Files:**
- Modify: `saturn/src/typeahead.c` (+ `typeahead.h` if a new entry point is needed)
- Test: `test/typeahead_abbrev_test.c` (new)

**Interfaces:**
- Consumes: existing trie API in `typeahead.h`.
- Produces: the abbreviations `{n,ne,e,se,s,sw,w,nw,l,i,q,z}` are always present as accepted + suggested trie words regardless of Easy/Normal filtering.

- [ ] **Step 1: Read the trie API and Easy/Normal filtering**

Read `typeahead.h` and the relevant parts of `typeahead.c`/`typeahead_solution.c` to learn: how words are inserted into the trie, how `typeahead_set_easy`/the solution overlay suppress non-grammar words, and the exact insert function signature. Record it in the task report — the exact insertion call is used in Step 3.

- [ ] **Step 2: Write the failing host test**

Create `test/typeahead_abbrev_test.c` that builds a trie, applies the always-accept abbreviation set, then asserts each of `n ne e se s sw w nw l i q z` is both (a) accepted (not filtered) and (b) returned as a suggestion, in BOTH Easy and Normal modes. Use the real trie/query API discovered in Step 1 (mirror the pattern in any existing `test/typeahead*` test; if none, drive `typeahead.c`'s public query function directly). Include a `CHECK` macro and an `ALL PASS`/`N FAILURES` tail like the music tests.

- [ ] **Step 3: Run RED**

Run: `gcc -O2 -I saturn/src -o /tmp/tat test/typeahead_abbrev_test.c saturn/src/typeahead.c <other required .c> && /tmp/tat`
Expected: FAIL — the abbreviations are filtered/absent before the change.

- [ ] **Step 4: Implement the always-accept + suggest set**

Add a function (e.g. `void typeahead_add_abbreviations(TrieNode* root);`) that inserts each abbreviation as an always-accepted, always-suggested word, and call it from `ensure_typeahead()` (main.cxx:60-72) after the story/solution overlay is built, so the abbreviations survive the Easy/Normal filter. The exact insertion mechanism (trie insert with an "always" flag vs. an allowlist consulted by the filter) is chosen from the Step-1 findings; whichever the codebase supports, these twelve tokens must end up accepted AND suggested in both modes.

- [ ] **Step 5: Run GREEN + regression**

Run the abbrev test (ALL PASS) and re-run any existing typeahead host test to confirm no regression. Then `cd saturn && ./compile.bat debug` → clean (the `ensure_typeahead` wiring compiles).

- [ ] **Step 6: Commit**

```bash
git commit saturn/src/typeahead.c saturn/src/typeahead.h test/typeahead_abbrev_test.c saturn/src/main.cxx -m "Always accept and suggest the compass/look/inventory/quit/wait abbreviations at the prompt, regardless of the Easy/Normal typeahead filter. Add a host test asserting they are accepted and suggested in both modes."
```

---

## Task 7: Quit → title screen with music (intercept, don't interpret) [Group D]

**Approach (per user directive):** Skip the crash repro. The Z-machine `quit` opcode
crashes on Saturn, so never let `q`/`quit` reach the interpreter — intercept it at
the readline submit and route to the reboot/title path, consuming the command.

**Files:**
- Modify: `saturn/src/main.cxx` — `saturn_readline` submit path (~883-897).

**Interfaces:**
- Consumes: `soft_reset_to_title()` (main.cxx:601).
- Produces: `static int is_quit_command(const char* s);` — 1 if `s` is `q`/`quit` (case-insensitive, surrounding spaces ignored).

- [ ] **Step 1: Add the quit matcher**

Near `saturn_readline` add:
```cpp
// True if the submitted line is a bare quit command. We intercept it (below)
// because the interpreter's quit path crashes on Saturn.
static int is_quit_command(const char* s) {
    while (*s == ' ') s++;                       // skip leading spaces
    char buf[8]; int n = 0;
    for (; s[n] && s[n] != ' ' && n < 7; n++)    // first word, lowercased
        buf[n] = (s[n] >= 'A' && s[n] <= 'Z') ? (char)(s[n] - 'A' + 'a') : s[n];
    buf[n] = '\0';
    const char* t = s + n; while (*t == ' ') t++;   // trailing must be empty
    if (*t) return 0;
    return (buf[0] == 'q' && (buf[1] == '\0' ||
            (buf[1]=='u' && buf[2]=='i' && buf[3]=='t' && buf[4]=='\0')));
}
```

- [ ] **Step 2: Intercept at submit, before returning the line to the core**

In `saturn_readline`, after the input line is finalized (trailing spaces stripped,
~main.cxx:883) and BEFORE the command is handed back to the interpreter /
history-pushed, add:
```cpp
    if (is_quit_command(k.input)) soft_reset_to_title();   // consume quit; never returns
```
Placed here, a `q`/`quit` line is consumed and routes to the title (which re-arms
menu music via the boot-music block) instead of being returned to `runInstruction`.
Non-quit lines are unaffected. Leave the post-`mojo_run` terminal `while(1)`
render loop as-is (it is only reached on a normal game end, not via quit).

- [ ] **Step 3: Build**

Run: `cd saturn && ./compile.bat debug` → clean.

- [ ] **Step 4: Commit**

```bash
git commit saturn/src/main.cxx -m "Intercept a bare q/quit at the prompt and return to the title screen with menu music, instead of passing it to the interpreter whose quit path crashes on Saturn. Non-quit input is unaffected."
```

---

## Task 8: Keyboard F-key events (enum + scancode mapping) [Group E, part 1]

**Files:**
- Modify: `saturn/src/saturn_keyboard.h` (enum), `saturn/src/saturn_keyboard.cxx` (scancode map).

**Interfaces:**
- Produces: `SATURN_KEY_F2, F3, F5, F6, F9, F10, F11` event kinds emitted by `saturn_keyboard_poll()` when the corresponding physical key is pressed.

- [ ] **Step 1: Add the enum values**

In `saturn_keyboard.h` `SaturnKeyKind`, append:
```c
    ,SATURN_KEY_F2, SATURN_KEY_F3, SATURN_KEY_F5,
    SATURN_KEY_F6, SATURN_KEY_F9, SATURN_KEY_F10, SATURN_KEY_F11
```
(Only the mapped keys the game uses; F1/F4/F7/F8/F12 are not required.)

- [ ] **Step 2: Map the F-key scancodes to the F-key events**

The F-key `code` values are confirmed against the Jo Engine keyboard table
(`joengine/jo_engine/keyboard.c:73-96`), whose nav/base-key codes (134 Left,
141 Right, 137 Up, 138 Down, 90/25 Enter, 102 Backspace, 133 Delete, 135 Home,
136 End) match this project's `saturn_keyboard.cxx` byte-for-byte — the same
Saturn keyboard code table. Add these alongside the existing special-key checks
(saturn_keyboard.cxx ~161-175), BEFORE the `kbd_map` char lookup at line 179:
```cpp
    if (code == 6)   { ev.kind = SATURN_KEY_F2;  return ev; }   // PS/2 set-2 F2 = 0x06
    if (code == 4)   { ev.kind = SATURN_KEY_F3;  return ev; }   // F3 = 0x04
    if (code == 3)   { ev.kind = SATURN_KEY_F5;  return ev; }   // F5 = 0x03
    if (code == 11)  { ev.kind = SATURN_KEY_F6;  return ev; }   // F6 = 0x0B
    if (code == 1)   { ev.kind = SATURN_KEY_F9;  return ev; }   // F9 = 0x01
    if (code == 9)   { ev.kind = SATURN_KEY_F10; return ev; }   // F10 = 0x09
    if (code == 120) { ev.kind = SATURN_KEY_F11; return ev; }   // F11 = 0x78
```
These low codes (1,3,4,6,9,11) and 120 are non-printable in the table, so they do
not collide with `kbd_map` entries; the checks precede the char lookup regardless.

- [ ] **Step 3: Build**

Run: `cd saturn && ./compile.bat debug` → clean.

- [ ] **Step 4: Commit**

```bash
git commit saturn/src/saturn_keyboard.h saturn/src/saturn_keyboard.cxx -m "Emit F2/F3/F5/F6/F9/F10/F11 key events from the Saturn keyboard poll using the PS/2 set-2 F-key scancodes. These drive the in-game function-key menus."
```

---

## Task 9: In-game function-key dispatch [Group E, part 2]

**Files:**
- Modify: `saturn/src/main.cxx` — `saturn_readline` input loop; session state for the last-used slot.

**Interfaces:**
- Consumes: `SATURN_KEY_F2/F3/F5/F6/F9/F10/F11` (Task 8), `options_menu`, the controls page(s), the save-slot picker + `choose_dest`/save routine, `g_restore_device`/`g_restore_slot`/`g_autocmd`, `g_story_filename`.
- Produces: `static int g_last_slot`, `static int g_last_device` (session memory of the last save/load slot).

- [ ] **Step 1: Add last-slot session state**

Near `g_restore_device`/`g_restore_slot` (main.cxx:41-42) add:
```cpp
static int g_last_slot = -1, g_last_device = -1;   // last save/load slot used this session
```
Set them whenever a save or restore commits a slot (in the save-slot editor path and where `g_restore_slot` is applied ~1637-1639, and in the F-key save/load below).

- [ ] **Step 2: Dispatch F-keys in `saturn_readline` (in-game only)**

In `saturn_readline`'s per-key handling (the block around main.cxx:793-799 where `ke.kind` is switched), add F-key handling. Because these are in-game only, they live in `saturn_readline` (not the menu loops). After handling, the loop continues editing the current line (game stays live):
```cpp
    else if (ke.kind == SATURN_KEY_F10) { options_menu(); menu_clear_full(); }
    else if (ke.kind == SATURN_KEY_F11) { if (g_kbd_visible) keyboard_controls_page(); else configure_controls_page(); menu_clear_full(); }
    else if (ke.kind == SATURN_KEY_F2)  { /* save current game */ if (run_save_menu()) { /* g_last_* set inside */ } menu_clear_full(); }
    else if (ke.kind == SATURN_KEY_F3)  { /* load current game */ if (run_load_menu()) { g_autocmd = "restore"; } menu_clear_full(); }
    else if (ke.kind == SATURN_KEY_F5)  { quick_save(); }
    else if (ke.kind == SATURN_KEY_F6 || ke.kind == SATURN_KEY_F9) { if (quick_load()) g_autocmd = "restore"; }
```
Then set `ke.kind = SATURN_KEY_NONE;` so the F-key doesn't fall through to the char/typeahead handling. After returning from any menu, re-render the console before resuming (the loop already calls `render_console()` each frame).

- [ ] **Step 3: Implement the save/load helpers scoped to the current game**

Add small helpers near the save/restore code (main.cxx ~1445-1650), reusing the existing slot-picker/save/restore mechanism for `g_story_filename` (current game). On cancel/back each RETURNS to the caller (the in-game loop), never to `game_select`:
- `run_save_menu()` — the existing save-slot editor/picker for the current game; on commit set `g_last_device`/`g_last_slot` and perform the save; return true on success.
- `run_load_menu()` — the existing slot picker (`choose_dest`-style) for the current game; on pick set `g_restore_device`/`g_restore_slot` (so the queued `restore` applies) and `g_last_*`; return true.
- `quick_save()` — if `g_last_slot >= 0`, save to `g_last_device`/`g_last_slot` directly (no picker); else fall back to `run_save_menu()`.
- `quick_load()` — if `g_last_slot >= 0`, set `g_restore_device`/`g_restore_slot` from `g_last_*` and return true; else fall back to `run_load_menu()`.
Implement these by factoring the current save/restore menu bodies (do not duplicate the slot-IO logic — call the same underlying save/restore routine the menu path uses). Exact factoring is determined by reading main.cxx:1445-1660 during implementation; keep the game-only guard (these are only reachable from `saturn_readline`).

- [ ] **Step 4: Build**

Run: `cd saturn && ./compile.bat debug` → clean.

- [ ] **Step 5: Commit**

```bash
git commit saturn/src/main.cxx -m "Add in-game function keys: F2 save and F3 load for the current game, F5 quick-save and F6/F9 quick-load to the last slot, F10 Options, F11 Controller. Backing out of a save/load menu resumes the game rather than dropping to game select."
```

---

## Task 10: Emulator / hardware verification pass

**Files:** none (manual, user hardware).

- [ ] **Step 1: Build the debug image** — `cd saturn && ./compile.bat debug` → clean.
- [ ] **Step 2: Verify each group on the emulator/hardware:**
  - A1/A2: menu track plays continuously across title → mode menu → category → game select → dialer (no restart/gaps); the only silence is the actual Z3 load.
  - A3: online session starts a fresh track; local game load starts the game's track.
  - A4: changing the Music level in Sound Options does not restart the track.
  - A5: Sound Options Track row numbers from 1; a disc with no audio shows "no audio on disc" and the row is locked.
  - B1: a long response (and the first room) stops at the top with "more v"; scrolling down reveals the rest.
  - B2: at the top of history, one more scroll-up shows a blank top line, then stops.
  - C: typing `n/ne/e/se/s/sw/w/nw/l/i/q/z` is accepted and suggested in Easy and Normal.
  - D: `quit` → accept → title screen with music, no crash.
  - E: F2 save / F3 load (current game; back resumes game), F5 quick-save, F6/F9 quick-load, F10 Options, F11 Controller — all only in-game.
- [ ] **Step 3: Re-run host tests (regression):**
  ```
  gcc -O2 -I saturn/src -o /tmp/mt  test/music_test.c      saturn/src/music.c saturn/src/music_data.c && /tmp/mt
  gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c  saturn/src/music.c saturn/src/music_data.c && /tmp/mmt
  gcc -O2 -I saturn/src -o /tmp/tat test/typeahead_abbrev_test.c saturn/src/typeahead.c <deps> && /tmp/tat
  ```
  Expected: all `ALL PASS`.
- [ ] **Step 4: Commit any fixes** discovered during verification (two-sentence messages).

---

## Self-Review Notes

- Spec coverage: A1 preload (Task 3), A2 continuous audio (Task 3), A3 fresh track on load/online (Task 4 + existing game-start), A4 no-restart volume (Task 1), A5 1-based numbering + "no audio on disc" (Task 2), B1 pager (Task 5), B2 scroll-past-top (Task 5), C abbreviations (Task 6), D quit→title (Task 7), E function keys (Tasks 8-9), verification (Task 10).
- Runtime-discovery tasks (not placeholders — the discovery IS the step): Task 7 Step 1-2 (crash root cause), Task 8 Step 2 (F-key scancodes). Both must record concrete findings before their fix step.
- Type consistency: `music_set_volume(int)` (Task 1) and `music_cdda_audio_tracks(const unsigned char**)` (Task 2) prototyped in `music.h`, consumed in `sound_options_page`. `g_output_start` (Task 5) shared by the submit sites and `render_console`. `g_last_slot`/`g_last_device` (Task 9) shared by the save/load helpers. F-key enum values (Task 8) consumed by the Task 9 dispatch.
- `g_sel_track` stays a real CD track number throughout (A5, persistence unchanged).
