# Sound Options & Menu Music Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a dedicated Sound Options page (OK/Cancel) with an Audio Mix selector, per-category random track pools, short-track handling, gapless looping, persisted settings, and fix the boot-menu / first-room silence.

**Architecture:** The pure-C engine (`music.c`) gains mix modes, a seedable RNG, a per-frame `music_tick()` (debounce commit + loop-end advance + short re-pick), and category-keyed switching. `music_data.c` swaps its single-track table for per-category track pools (Neutral merged into each). The Saturn backend (`music_cdda.cxx`) adds one-shot vs looping playback, an "is-playing" query, TOC-based short detection, and a best-effort gapless loop. `main.cxx` moves engine wiring to game-start (fixing first-room silence), plays a menu track from the title screen, persists the new settings in the `MOJOOPTS` blob, and hosts the new Sound Options page plus OK/Cancel on the Controls pages.

**Tech Stack:** C (engine, data, mojozork core), C++ (SRL `SRL::Sound::Cdda`), host `gcc` for pure-C unit tests, `saturn/compile.bat` for the Saturn image.

## Global Constraints

- Build the Saturn image ONLY with `saturn/compile.bat` (run from `saturn/`, e.g. `./compile.bat debug`). Never invoke `make`/`gcc` for the Saturn image. Files under `saturn/src/**/*.c` and `**/*.cxx` are auto-globbed by `saturn/makefile` — no makefile edits for new files.
- The mojozork core (`saturn/mojozork.c`) is shared with non-Saturn builds. Any new call from it MUST be guarded by `#if defined(MOJOZORK_SATURN)`.
- Host unit tests are pure C only. Build/run pattern: `gcc -O2 -I saturn/src -o /tmp/<name> test/<name>.c saturn/src/<impl>.c ... && /tmp/<name>`.
- `SRL::Debug::Print` supports only `%d/%s/%c` — no width/hex flags. Align with separate fixed-x Print calls.
- Selectable / valid CD-DA track range is **2..32** (track 1 is the data track). Default selected/menu/override track is **10**.
- Commit messages: no session number; two sentences max.

---

## File Structure

- `saturn/src/music.h` — mix-mode enum, track-range macros, changed `music_play_fn` signature, new engine/backend prototypes.
- `saturn/src/music_data.c` — per-category track pools + `music_category_pool()`; `CATEGORY_TRACK[]` removed.
- `saturn/src/music.c` — RNG, `music_category_track()` (random pick), mix state, `music_set_mix`/`music_start`/`music_tick`/`music_seed`/`music_set_isplaying`/`music_set_isshort`/`music_set_debounce_frames`, reworked `music_on_turn`, updated `music_refresh`.
- `saturn/src/music_cdda.cxx` — `music_cdda_play_mode(track,loop)`, `music_cdda_play` wrapper, `music_cdda_is_playing()`, `music_cdda_is_short(track)`, gapless loop, `music_set_level`.
- `saturn/src/main.cxx` — persistence (blob), globals, engine wiring at game-start, `music_tick()` calls, menu music, Sound Options page, Options cleanup + screen-clear fix, Controls OK/Cancel.
- `test/music_test.c` — updated for pools/RNG.
- `test/music_mix_test.c` — new: mix modes, debounce, short re-pick.

---

## Task 1: Per-category track pools (data layer)

**Files:**
- Modify: `saturn/src/music.h` (add `music_category_pool` prototype)
- Modify: `saturn/src/music_data.c:57-93` (replace `CATEGORY_TRACK[]` + `music_category_track`)
- Test: `test/music_test.c` (update assertions)

**Interfaces:**
- Produces: `int music_category_pool(int cat, const unsigned char** out)` — returns pool size (0 if `cat` out of range), sets `*out` to the track array.
- Note: `music_category_track(int)` is REMOVED from `music_data.c` here; it is re-created in `music.c` (Task 2) as a random pick. Between Task 1 and Task 2 the tree does not build a host binary that references `music_category_track` — Task 1's test uses `music_category_pool` only.

- [ ] **Step 1: Update the test to use pools**

In `test/music_test.c`, replace the four `music_category_track(...)` data-table assertions (lines ~17-20) with pool checks:

```c
    /* --- data tables: per-category pools --- */
    {
        const unsigned char* p;
        int n = music_category_pool(MC_NEUTRAL, &p);
        CHECK(n == 11);
        int has30 = 0; for (int i = 0; i < n; i++) { CHECK(p[i] >= 2 && p[i] <= 32); if (p[i] == 30) has30 = 1; }
        CHECK(has30);
        /* Neutral is merged into every other category: track 4 (Neutral) appears everywhere. */
        for (int c = MC_WILDERNESS; c <= MC_TRIUMPH; c++) {
            int m = music_category_pool(c, &p);
            CHECK(m > 0);
            int has4 = 0; for (int i = 0; i < m; i++) if (p[i] == 4) has4 = 1;
            CHECK(has4);
        }
        CHECK(music_category_pool(-1, &p) == 0);
        CHECK(music_category_pool(MUSIC_NUM_CATEGORIES, &p) == 0);
    }
```

Also delete the now-invalid engine-section lines that compare against `music_category_track(...)` (lines ~49, 57, 61, 65) — Task 3 rewrites the engine section entirely, so for now delete the whole `/* --- engine --- */` block (lines ~41-65) and its `rec`/`g_last_track`/`g_calls` usage. Leave the classifier section intact.

- [ ] **Step 2: Add the prototype to `music.h`**

In `saturn/src/music.h`, under the "Data tables" comment, replace the `music_category_track` line with:

```c
int music_category_pool(int category, const unsigned char** out);  /* pool size; *out=tracks */
```

(Keep `music_keywords`, `music_events`, `music_game_room_category`.)

- [ ] **Step 3: Replace the table + accessor in `music_data.c`**

In `saturn/src/music_data.c`, replace the `CATEGORY_TRACK[]` block and the `music_category_track` function (lines ~57-93, keeping `music_game_room_category`) with:

```c
/* Category -> pool of CD-DA tracks (2..32). Dynamic mode picks one at random on a
   category change. The NEUTRAL pool is merged into every other category (deduped),
   so neutral ambience can surface anywhere. Numbers are CD-DA tracks. */
static const unsigned char P_NEUTRAL[]     = {4,5,6,10,11,12,16,22,24,28,30};
static const unsigned char P_WILDERNESS[]  = {4,5,6,9,10,11,12,16,17,22,24,28,30,31};
static const unsigned char P_UNDERGROUND[] = {2,3,4,5,6,7,10,11,12,16,18,19,20,22,23,24,28,29,30};
static const unsigned char P_WATER[]       = {2,4,5,6,7,8,10,11,12,16,20,21,22,24,26,28,30};
static const unsigned char P_NAUTICAL[]    = {2,3,4,5,6,7,10,11,12,16,19,20,21,22,24,26,28,30};
static const unsigned char P_TOWN[]        = {4,5,6,9,10,11,12,16,22,24,28,30};
static const unsigned char P_DUNGEON[]     = {4,5,6,9,10,11,12,16,17,18,19,20,22,23,24,28,29,30};
static const unsigned char P_DESERT[]      = {4,5,6,9,10,11,12,16,22,24,28,30};
static const unsigned char P_MAGIC[]       = {4,5,6,8,10,11,12,16,18,19,21,22,23,24,26,28,29,30};
static const unsigned char P_SCIFI[]       = {3,4,5,6,8,10,11,12,14,15,16,18,19,22,23,24,27,28,30};
static const unsigned char P_HORROR[]      = {2,4,5,6,7,8,10,11,12,13,14,15,16,19,22,24,27,28,30};
static const unsigned char P_MYSTERY[]     = {3,4,5,6,8,10,11,12,15,16,21,22,24,27,28,30};
static const unsigned char P_DANGER[]      = {4,5,6,10,11,12,13,14,15,16,17,22,24,27,28,30};
static const unsigned char P_TRIUMPH[]     = {4,5,6,9,10,11,12,16,22,24,25,28,29,30};

#define POOL(a) { a, (unsigned char)(sizeof(a)/sizeof((a)[0])) }
static const struct { const unsigned char* p; unsigned char n; } CATEGORY_POOL[MUSIC_NUM_CATEGORIES] = {
    POOL(P_NEUTRAL), POOL(P_WILDERNESS), POOL(P_UNDERGROUND), POOL(P_WATER),
    POOL(P_NAUTICAL), POOL(P_TOWN), POOL(P_DUNGEON), POOL(P_DESERT),
    POOL(P_MAGIC), POOL(P_SCIFI), POOL(P_HORROR), POOL(P_MYSTERY),
    POOL(P_DANGER), POOL(P_TRIUMPH),
};
#undef POOL

int music_category_pool(int category, const unsigned char** out) {
    if (category < 0 || category >= MUSIC_NUM_CATEGORIES) { if (out) *out = 0; return 0; }
    if (out) *out = CATEGORY_POOL[category].p;
    return CATEGORY_POOL[category].n;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/mt test/music_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/mt`

Expected: PASS (`ALL PASS`). Note `music.c` still defines the old `music_on_turn` referencing `music_category_track`; if the link fails on `music_category_track`, that is expected only if Step 1's deletion missed a caller — `music.c`'s `music_on_turn` calls it. To keep this task self-building, in `saturn/src/music.c` temporarily change the one call `int track = music_category_track(target);` (in `music_on_turn`) to `int track = 0; { const unsigned char* pp; int nn = music_category_pool(target, &pp); if (nn) track = pp[0]; }`. Task 3 rewrites this function, so this is a throwaway bridge.

Expected after the bridge: PASS.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/music.h saturn/src/music_data.c saturn/src/music.c test/music_test.c
git commit -m "Replace single-track category map with per-category pools. Neutral tracks are merged into every category and exposed via music_category_pool."
```

---

## Task 2: Seedable RNG + random category pick

**Files:**
- Modify: `saturn/src/music.h` (RNG prototypes; changed `music_play_fn`)
- Modify: `saturn/src/music.c` (RNG, `music_category_track`, play signature)
- Test: `test/music_test.c` (seeded determinism + membership)

**Interfaces:**
- Consumes: `music_category_pool` (Task 1).
- Produces:
  - `typedef void (*music_play_fn)(int track, int loop);` (CHANGED: added `loop`, 1=loop, 0=one-shot).
  - `void music_seed(unsigned int s);`
  - `int music_category_track(int category);` — random member of the category's pool via the RNG; 0 if `category` invalid.

- [ ] **Step 1: Write the failing test**

Append to `test/music_test.c` `main()` before the final `printf`:

```c
    /* --- RNG-backed category pick --- */
    music_seed(12345);
    for (int t = 0; t < 50; t++) {
        int tr = music_category_track(MC_MAGIC);
        const unsigned char* p; int n = music_category_pool(MC_MAGIC, &p);
        int member = 0; for (int i = 0; i < n; i++) if (p[i] == tr) member = 1;
        CHECK(member);
    }
    CHECK(music_category_track(-1) == 0);
    /* Same seed -> same sequence (deterministic for tests). */
    music_seed(777); int a1 = music_category_track(MC_HORROR);
    music_seed(777); int a2 = music_category_track(MC_HORROR);
    CHECK(a1 == a2);
```

- [ ] **Step 2: Update `music.h`**

In `saturn/src/music.h`, change the backend callback typedef and add RNG prototypes:

```c
/* Backend callback: play a CD-DA track. loop: 1 = loop, 0 = play once. track 0 = stop. */
typedef void (*music_play_fn)(int track, int loop);
```

Under "Engine (music.c)." add:

```c
void music_seed(unsigned int s);            /* seed the track-pool RNG */
int  music_category_track(int category);    /* random track from the category pool; 0 if none */
```

- [ ] **Step 3: Implement in `music.c`**

At the top of `saturn/src/music.c` (after the includes), add:

```c
/* Small LCG so track picks vary in play but are deterministic under a fixed seed
   (host tests seed it). */
static unsigned int g_rng = 0x1234567u;
void music_seed(unsigned int s) { g_rng = s ? s : 1u; }
static unsigned int rng_next(void) { g_rng = g_rng * 1103515245u + 12345u; return (g_rng >> 16) & 0x7fffu; }

int music_category_track(int category) {
    const unsigned char* p; int n = music_category_pool(category, &p);
    if (n <= 0) return 0;
    return p[rng_next() % (unsigned)n];
}
```

Remove the throwaway bridge added in Task 1 Step 4 (restore `int track = music_category_track(target);` in `music_on_turn`).

Update the play callback usage: the field is `static music_play_fn g_play` — its two call sites currently `g_play(0)` (line ~71 in `music_reset`) and `g_play(track)` (line ~106 in `music_on_turn`) and `g_play(g_active_track)` (line ~75 in `music_refresh`). Change them to pass a loop flag:
- `music_reset`: `if (g_play) g_play(0, 0);`
- `music_refresh`: `if (g_active_track > 0 && g_play) g_play(g_active_track, 1);`
- `music_on_turn`: `if (g_play) g_play(track, 1);`

(Task 3 rewrites `music_on_turn` and `music_refresh` again; these edits just keep the file compiling now.)

- [ ] **Step 4: Update the host test stub signature**

The test file has no `rec` stub anymore (deleted in Task 1). No change needed. Run:

`gcc -O2 -I saturn/src -o /tmp/mt test/music_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/mt`

Expected: PASS (`ALL PASS`).

- [ ] **Step 5: Commit**

```bash
git add saturn/src/music.h saturn/src/music.c test/music_test.c
git commit -m "Add a seeded RNG and random per-category track pick. The backend play callback now carries a loop flag for one-shot vs looping playback."
```

---

## Task 3: Mix modes, debounce, short re-pick (engine)

**Files:**
- Modify: `saturn/src/music.h` (mix enum, macros, new prototypes)
- Modify: `saturn/src/music.c` (engine state + `music_on_turn`, `music_tick`, `music_set_mix`, `music_start`, setters, `music_refresh`)
- Test: `test/music_mix_test.c` (new)

**Interfaces:**
- Consumes: `music_category_track`, `music_category_pool`, `music_seed` (Tasks 1-2).
- Produces:
  - `enum { MIX_DYNAMIC=0, MIX_OVERRIDE=1, MIX_SEQUENTIAL=2, MIX_RANDOM=3 };`
  - `#define MUSIC_TRACK_MIN 2` / `#define MUSIC_TRACK_MAX 32`
  - `void music_set_mix(int mode, int override_track);`
  - `void music_start(void);` — assert playback for the current mix mode (non-Dynamic plays now; Dynamic waits for `music_on_turn`).
  - `void music_tick(void);` — per frame: commit a pending Dynamic switch, advance Sequential/Random on loop-end, re-pick after a short Dynamic track.
  - `void music_set_isplaying(int (*fn)(void));` — 1 = CD-DA still playing.
  - `void music_set_isshort(int (*fn)(int track));` — 1 = track is play-once.
  - `void music_set_debounce_frames(int n);` — test/config hook; default `MUSIC_DEBOUNCE_FRAMES` (360).

- [ ] **Step 1: Write the failing test**

Create `test/music_mix_test.c`:

```c
/* Host tests for mix modes, category-keyed debounce, and short-track re-pick.
   gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c \
       saturn/src/music.c saturn/src/music_data.c && /tmp/mmt */
#include <stdio.h>
#include "music.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } }while(0)

static int g_track = 0, g_loop = 0, g_calls = 0;
static void rec(int track, int loop) { g_track = track; g_loop = loop; g_calls++; }
static int playing = 1;
static int isplaying(void) { return playing; }
static int short_set[64];
static int isshort(int t) { return (t >= 0 && t < 64) ? short_set[t] : 0; }
static int in_pool(int cat, int tr) {
    const unsigned char* p; int n = music_category_pool(cat, &p);
    for (int i = 0; i < n; i++) if (p[i] == tr) return 1;
    return 0;
}

int main(void) {
    int fails = 0;
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_set_backend(rec);
    music_set_isplaying(isplaying);
    music_set_isshort(isshort);
    music_seed(1);

    /* --- Dynamic: first room commits immediately (nothing playing yet) --- */
    music_set_mix(MIX_DYNAMIC, 10);
    music_reset();
    music_set_debounce_frames(6);
    g_calls = 0;
    music_note_output("You are in a dark cave with a tunnel.", 37);
    music_on_turn(10);
    CHECK(g_calls == 1);
    CHECK(in_pool(MC_UNDERGROUND, g_track));
    CHECK(g_loop == 1);
    int first_track = g_track;

    /* Same category room: smooth, no new play, no restart. */
    g_calls = 0;
    music_note_output("Another damp cavern passage.", 28);
    music_on_turn(11);
    CHECK(g_calls == 0);

    /* Different category: pending, does NOT play until the countdown elapses. */
    g_calls = 0;
    music_note_output("A sunny forest clearing, tall trees around.", 43);
    music_on_turn(12);
    CHECK(g_calls == 0);            /* still debouncing */
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_calls == 0);            /* not yet (6 frames) */
    music_tick();
    CHECK(g_calls == 1);            /* committed on the 6th tick */
    CHECK(in_pool(MC_WILDERNESS, g_track));

    /* A category flip before commit resets the countdown to the newest target. */
    music_set_mix(MIX_DYNAMIC, 10);
    music_reset(); music_set_debounce_frames(6);
    music_note_output("A cave tunnel.", 14); music_on_turn(20);   /* immediate underground */
    music_note_output("A forest.", 9); music_on_turn(21);         /* pending wilderness */
    music_tick(); music_tick();                                   /* 2 frames */
    music_note_output("An airlock console corridor.", 28); music_on_turn(22); /* flip -> scifi, reset */
    g_calls = 0;
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_calls == 0);
    music_tick();
    CHECK(g_calls == 1);
    CHECK(in_pool(MC_SCIFI, g_track));

    /* --- Override: plays the override track looped, ignores rooms --- */
    music_set_mix(MIX_OVERRIDE, 7);
    music_reset(); g_calls = 0;
    music_start();
    CHECK(g_track == 7 && g_loop == 1);
    music_note_output("A cave.", 6); music_on_turn(30);
    CHECK(g_track == 7);           /* unchanged by room */

    /* --- Sequential: one-shot; advances on loop-end (isplaying==0) --- */
    music_set_mix(MIX_SEQUENTIAL, 5);
    music_reset(); music_start();
    CHECK(g_track == 5 && g_loop == 0);
    playing = 0; music_tick();     /* track ended -> advance */
    CHECK(g_track == 6 && g_loop == 0);
    playing = 1; music_tick();     /* still playing -> no change */
    CHECK(g_track == 6);
    /* wrap at MAX */
    music_set_mix(MIX_SEQUENTIAL, 32); music_reset(); music_start();
    CHECK(g_track == 32);
    playing = 0; music_tick();
    CHECK(g_track == 2);
    playing = 1;

    /* --- Random: one-shot; picks in 2..32 on loop-end --- */
    music_set_mix(MIX_RANDOM, 10); music_reset(); music_start();
    CHECK(g_track >= 2 && g_track <= 32 && g_loop == 0);
    playing = 0; int r0 = g_track; music_tick();
    CHECK(g_track >= 2 && g_track <= 32);
    playing = 1; (void)r0;

    /* --- Short Dynamic track: one-shot, then re-pick from same pool (prefer long) --- */
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_set_mix(MIX_DYNAMIC, 10); music_reset(); music_set_debounce_frames(0);
    /* Make every UNDERGROUND track short except one, force first pick short via seed search. */
    { const unsigned char* p; int n = music_category_pool(MC_UNDERGROUND, &p);
      for (int i = 0; i < n; i++) short_set[p[i]] = 1;
      short_set[p[n-1]] = 0;   /* exactly one long track */
    }
    music_note_output("A cave tunnel passage.", 22); music_on_turn(40);
    /* first pick may be short -> played one-shot */
    if (isshort(g_track)) CHECK(g_loop == 0);
    playing = 0; music_tick();     /* short ended -> re-pick, must land on the one long track */
    CHECK(in_pool(MC_UNDERGROUND, g_track));
    CHECK(isshort(g_track) == 0);  /* prefers the non-short track */
    CHECK(g_loop == 1);
    playing = 1;

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/mmt`

Expected: FAIL to link — `music_set_mix`, `music_start`, `music_tick`, `music_set_isplaying`, `music_set_isshort`, `music_set_debounce_frames` undefined.

- [ ] **Step 3: Update `music.h`**

In `saturn/src/music.h`, after the `MUSIC_NUM_CATEGORIES` enum block add:

```c
/* Audio Mix modes (Options > Sound Options). */
enum { MIX_DYNAMIC = 0, MIX_OVERRIDE = 1, MIX_SEQUENTIAL = 2, MIX_RANDOM = 3 };
#define MUSIC_TRACK_MIN 2
#define MUSIC_TRACK_MAX 32
```

Under "Engine (music.c)." add:

```c
void music_set_mix(int mode, int override_track);      /* mix mode + selected/override track */
void music_start(void);                                /* assert playback for the current mode */
void music_tick(void);                                 /* per frame: commit/advance/re-pick */
void music_set_isplaying(int (*fn)(void));             /* backend: 1 = CD-DA still playing */
void music_set_isshort(int (*fn)(int track));          /* backend: 1 = track plays once */
void music_set_debounce_frames(int n);                 /* room-switch debounce length */
```

- [ ] **Step 4: Rewrite the engine in `music.c`**

Add mix/debounce state near the other statics (after `g_turn_len`):

```c
static int g_mix_mode = MIX_DYNAMIC;
static int g_override_track = 10;
static int g_seq_track = MUSIC_TRACK_MIN;
static int g_active_cat = -1;           /* category currently sounding, -1 = none */
static int g_pending_cat = -1;          /* debounce: category waiting to commit */
static int g_pending_track = 0;
static int g_pending_frames = 0;
#define MUSIC_DEBOUNCE_FRAMES 360       /* ~6s @ 60fps */
static int g_debounce_frames = MUSIC_DEBOUNCE_FRAMES;
static int (*g_isplaying)(void) = 0;
static int (*g_isshort)(int) = 0;

void music_set_isplaying(int (*fn)(void)) { g_isplaying = fn; }
void music_set_isshort(int (*fn)(int)) { g_isshort = fn; }
void music_set_debounce_frames(int n) { g_debounce_frames = (n < 0) ? 0 : n; }
static int trk_is_short(int t) { return g_isshort ? g_isshort(t) : 0; }

/* Play `track`: looped unless it is a short/play-once track. */
static void play_dyn(int track) {
    g_active_track = track;
    if (g_play) g_play(track, trk_is_short(track) ? 0 : 1);
}
/* Pick a track from `cat`'s pool, preferring a non-short one. */
static int pick_prefer_long(int cat) {
    const unsigned char* p; int n = music_category_pool(cat, &p);
    if (n <= 0) return 0;
    int longs[64], m = 0;
    for (int i = 0; i < n && m < 64; i++) if (!trk_is_short(p[i])) longs[m++] = p[i];
    if (m > 0) return longs[rng_next_pub() % (unsigned)m];
    return music_category_track(cat);
}
```

`rng_next` is `static` in Task 2; expose an internal wrapper so `pick_prefer_long` can use it. Add near the RNG in Task 2's block (adjust): rename usage by adding

```c
static unsigned int rng_next_pub(void) { return rng_next(); }
```

directly under `rng_next`.

Add the mode setters and starters:

```c
void music_set_mix(int mode, int override_track) {
    g_mix_mode = mode;
    if (override_track >= MUSIC_TRACK_MIN && override_track <= MUSIC_TRACK_MAX)
        g_override_track = override_track;
}

static void play_seq_current(void) {
    g_active_track = g_seq_track;
    if (g_play) g_play(g_seq_track, 0);   /* one-shot so tick advances on loop-end */
}
static void play_random_now(void) {
    int t = MUSIC_TRACK_MIN + (int)(rng_next_pub() % (unsigned)(MUSIC_TRACK_MAX - MUSIC_TRACK_MIN + 1));
    g_active_track = t;
    if (g_play) g_play(t, 0);
}

void music_start(void) {
    g_active_cat = -1; g_pending_cat = -1; g_pending_track = 0;
    switch (g_mix_mode) {
        case MIX_OVERRIDE:
            g_active_track = g_override_track;
            if (g_play) g_play(g_override_track, 1);   /* override honors repeat even if short */
            break;
        case MIX_SEQUENTIAL:
            g_seq_track = g_override_track; play_seq_current(); break;
        case MIX_RANDOM:
            play_random_now(); break;
        case MIX_DYNAMIC: default:
            /* first music_on_turn drives it */ break;
    }
}

void music_tick(void) {
    /* Dynamic: commit a pending category switch after the debounce. */
    if (g_pending_cat >= 0) {
        if (g_pending_frames > 0) g_pending_frames--;
        if (g_pending_frames <= 0) {
            g_active_cat = g_pending_cat;
            int t = g_pending_track;
            g_pending_cat = -1; g_pending_track = 0;
            play_dyn(t);
        }
        return;
    }
    /* Loop-end driven behavior (one-shot modes / short Dynamic track). */
    if (g_active_track > 0 && g_isplaying && !g_isplaying()) {
        if (g_mix_mode == MIX_SEQUENTIAL) {
            g_seq_track = (g_seq_track >= MUSIC_TRACK_MAX) ? MUSIC_TRACK_MIN : g_seq_track + 1;
            play_seq_current();
        } else if (g_mix_mode == MIX_RANDOM) {
            play_random_now();
        } else if (g_mix_mode == MIX_DYNAMIC && g_active_cat >= 0) {
            play_dyn(pick_prefer_long(g_active_cat));
        }
        /* MIX_OVERRIDE loops; isplaying stays true; nothing to do. */
    }
}
```

Rewrite `music_on_turn` (replace the whole existing function body) so it only drives Dynamic mode and uses category-keyed debounce:

```c
void music_on_turn(unsigned int room) {
    if (g_mix_mode != MIX_DYNAMIC) { g_turn_len = 0; g_turn_text[0] = 0; return; }

    int event_cat = music_scan_event(g_turn_text);
    if (!g_have_room || room != g_cur_room) {
        int base = music_game_room_category(g_release, g_serial, room);
        if (base < 0) {
            unsigned char cached = (room < 256) ? g_room_cache[room] : 0;
            if (cached) base = cached - 1;
            else { base = music_classify_room(g_turn_text); if (room < 256) g_room_cache[room] = (unsigned char)(base + 1); }
        }
        g_cur_room = room; g_have_room = 1; g_base_cat = base; g_event_cat = -1;
    }
    if (event_cat >= 0) g_event_cat = event_cat;

    int target = (g_event_cat >= 0) ? g_event_cat : g_base_cat;
    if (target == g_active_cat) {
        g_pending_cat = -1; g_pending_track = 0;          /* smooth: keep the current stream */
    } else if (g_active_track == 0) {
        g_active_cat = target; play_dyn(music_category_track(target));  /* first switch: immediate */
    } else if (target != g_pending_cat) {
        g_pending_cat = target;
        g_pending_track = music_category_track(target);
        g_pending_frames = g_debounce_frames;             /* new target: (re)start countdown */
    }
    g_turn_len = 0; g_turn_text[0] = 0;
}
```

Update `music_reset` to clear the new state (append inside it — do NOT touch
`g_mix_mode`/`g_override_track`, which are owned by `music_set_mix`):

```c
    g_active_cat = -1; g_pending_cat = -1; g_pending_track = 0; g_pending_frames = 0;
    g_seq_track = MUSIC_TRACK_MIN;
```

Update `music_refresh` to respect loop/short:

```c
void music_refresh(void) {
    if (g_active_track > 0 && g_play) {
        int loop = (g_mix_mode == MIX_OVERRIDE) ? 1 : (trk_is_short(g_active_track) ? 0 : 1);
        if (g_mix_mode == MIX_SEQUENTIAL || g_mix_mode == MIX_RANDOM) loop = 0;
        g_play(g_active_track, loop);
    }
}
```

- [ ] **Step 5: Run both host tests**

Run:
```
gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/mmt
gcc -O2 -I saturn/src -o /tmp/mt  test/music_test.c     saturn/src/music.c saturn/src/music_data.c && /tmp/mt
```
Expected: both `ALL PASS`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/music.h saturn/src/music.c test/music_mix_test.c
git commit -m "Add mix modes, category-keyed room-switch debounce, and short-track re-pick to the music engine. Sequential and Random advance on loop-end; a play-once Dynamic track re-picks a non-short track from its pool."
```

---

## Task 4: CD-DA backend — one-shot, is-playing, short detect, gapless

**Files:**
- Modify: `saturn/src/music.h` (backend prototypes)
- Modify: `saturn/src/music_cdda.cxx`
- Verify: Saturn build (`saturn/compile.bat`) — no host test (SRL-dependent).

**Interfaces:**
- Consumes: `music_set_level` (existing), mix engine callbacks (Task 3).
- Produces:
  - `void music_cdda_play_mode(int track, int loop);` (matches `music_play_fn`).
  - `void music_cdda_play(int track);` — looping wrapper (menu/preview).
  - `int music_cdda_is_playing(void);`
  - `int music_cdda_is_short(int track);`

- [ ] **Step 1: Add prototypes to `music.h`**

Under "Saturn CD-DA backend (music_cdda.cxx)." add/replace:

```c
void music_cdda_play(int track);                 /* track 0 = stop; else play looping */
void music_cdda_play_mode(int track, int loop);  /* loop: 1 loop, 0 play once */
void music_set_level(int level);                 /* 0..7, 0 = silence */
int  music_cdda_is_playing(void);                /* 1 = a CD-DA track is still playing */
int  music_cdda_is_short(int track);             /* 1 = track shorter than the short threshold */
```

- [ ] **Step 2: Rewrite `music_cdda.cxx`**

Replace the file body with (verify the SRL calls against the SDK during implementation — see Step 3 notes):

```cpp
#include <srl.hpp>
extern "C" {
#include "music.h"
}

static int g_level = 7;
static int g_track = 0;    /* currently requested track (0 = none) */
static int g_loop  = 1;    /* whether the current track loops */

#define MUSIC_SHORT_SECONDS 15

/* Play `track`. loop=1 uses the CD block's native repeat (seamless); loop=0 plays
   once so the engine's music_tick() can advance on completion. */
extern "C" void music_cdda_play_mode(int track, int loop) {
    g_track = track; g_loop = loop;
    if (track <= 0 || g_level == 0) { SRL::Sound::Cdda::StopPause(); return; }
    SRL::Sound::Cdda::SetVolume((uint8_t) g_level);
    SRL::Sound::Cdda::PlaySingle((uint16_t) track, loop != 0);
}

extern "C" void music_cdda_play(int track) { music_cdda_play_mode(track, 1); }

extern "C" void music_set_level(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    g_level = level;
    if (level == 0) { SRL::Sound::Cdda::StopPause(); }
    else {
        SRL::Sound::Cdda::SetVolume((uint8_t) level);
        if (g_track > 0) SRL::Sound::Cdda::PlaySingle((uint16_t) g_track, g_loop != 0);
    }
}

/* 1 while a CD-DA track is playing. Used by the engine to detect loop-end for the
   one-shot modes. */
extern "C" int music_cdda_is_playing(void) {
    return SRL::Sound::Cdda::GetStatus() == SRL::Sound::Cdda::Status::Playing ? 1 : 0;
}

/* Short = track duration under MUSIC_SHORT_SECONDS, computed from the CD TOC
   (track length = next start - this start, in 1/75s frames). Cached after first read. */
extern "C" int music_cdda_is_short(int track) {
    static signed char cache[33];       /* 0 = unknown, 1 = short, 2 = long */
    static int inited = 0;
    if (!inited) { for (int i = 0; i < 33; i++) cache[i] = 0; inited = 1; }
    if (track < 2 || track > 32) return 0;
    if (cache[track]) return cache[track] == 1;
    int frames = SRL::Cd::GetTrackFrames(track);   /* SEE Step 3: replace with the real TOC API */
    int is_short = (frames > 0 && frames < MUSIC_SHORT_SECONDS * 75) ? 1 : 0;
    cache[track] = is_short ? 1 : 2;
    return is_short;
}
```

- [ ] **Step 3: Reconcile the SRL API names (RISK)**

`SRL::Sound::Cdda::GetStatus()`, `SRL::Sound::Cdda::Status::Playing`, and `SRL::Cd::GetTrackFrames()` are placeholders. Before building, grep the SDK for the real names and adjust:

Run:
```
grep -rIn "Cdda" ../SaturnRingLib/modules --include=*.hpp | grep -iE "status|isplay|getstatus|playing" | head
grep -rIn -iE "toc|track.*frame|gettrack|tno|CdGetToc" ../SaturnRingLib/modules --include=*.hpp | head
```
- If a status query exists (e.g. `IsPlaying()` / `GetCdStatus()`), use it in `music_cdda_is_playing`.
- If no clean is-playing query exists, FALLBACK: track expected end via a frame counter set from the TOC length at `PlaySingle` time, decremented in `music_cdda_is_playing`'s caller — but prefer the SDK query.
- For short detection, if the TOC/track-length API is unavailable, FALLBACK: generate a `static const unsigned char SHORT_TRACKS[]` table in `music_data.c` from the `.raw` byte lengths (CD-DA = 176400 bytes/s; short if `< 15 * 176400`). Given the current disc, that flags track 25 (≈7s); compute the full set from `saturn/cd/music/*.raw` sizes at implementation time and hard-code it. Then `music_cdda_is_short` returns membership.

Document whichever path you took in a one-line comment in the file.

- [ ] **Step 4: Fix the existing backend registration so the image links**

The `music_play_fn` signature changed to `(int,int)` in Task 2, so `main.cxx`'s
current `music_set_backend(music_cdda_play)` (≈ line 74, inside `ensure_typeahead`)
no longer type-matches. Change that one call to:

```cpp
    if (!music_backend_set) { music_set_backend(music_cdda_play_mode); music_backend_set = 1; }
```

(Task 6 relocates this block to game-start; this keeps the tree building until then.)

- [ ] **Step 5: Build the Saturn image**

Run: `cd saturn && ./compile.bat debug`
Expected: build completes with no errors; `saturn/BuildDrop/` image produced.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/music.h saturn/src/music_cdda.cxx saturn/src/main.cxx
git commit -m "Add one-shot playback, an is-playing query, and TOC-based short-track detection to the CD-DA backend. Looping uses the CD block's native repeat for a best-effort gapless loop."
```

---

## Task 5: Persist mix mode + selected track (options blob)

**Files:**
- Modify: `saturn/src/main.cxx` (globals near line 31; `options_load` 236-263; `options_save` 264-275)
- Verify: Saturn build.

**Interfaces:**
- Consumes: nothing new.
- Produces: globals `g_mix_mode` (default `MIX_DYNAMIC`), `g_sel_track` (default `10`), persisted in the `MOJOOPTS` blob after the controller block behind sound-sentinel `1`.

- [ ] **Step 1: Add globals**

Near `g_music_level`/`g_pcm_level` (line ~31), add:

```cpp
static int g_mix_mode  = MIX_DYNAMIC;   // Audio Mix: Dynamic/Override/Sequential/Random
static int g_sel_track = 10;            // selected/override track, also the menu track
```

(Ensure `#include "music.h"` is already in scope in main.cxx — it is, via the music calls.)

- [ ] **Step 2: Extend `options_save`**

In `options_save` (line ~264), after the controller-mapping loop and before `saturn_bup_write`, append the sound block:

```cpp
    buf[n++] = 1;                                 // sound-block sentinel
    buf[n++] = (uint8_t) g_mix_mode;              // 0..3
    buf[n++] = (uint8_t) g_sel_track;             // 2..32
```

(The controller loops cap at `n < 62`; the three appended bytes stay within `buf[64]`.)

- [ ] **Step 3: Extend `options_load`**

In `options_load` (line ~255-262), after the controller-mapping block, add:

```cpp
    // Sound block follows the controller bytes: sentinel 1, then [mix][track].
    int s = m + 1 + FA_N + CA_N;
    if (s + 2 < (int) sizeof(buf) && buf[s] == 1) {
        if (buf[s + 1] <= MIX_RANDOM) g_mix_mode = buf[s + 1];
        if (buf[s + 2] >= MUSIC_TRACK_MIN && buf[s + 2] <= MUSIC_TRACK_MAX) g_sel_track = buf[s + 2];
    }
```

(`m` is the controller sentinel offset already computed at line ~258. If the controller block was absent, `buf[m] != 2` and `s` still points past it; the `buf[s] == 1` guard keeps old blobs on defaults.)

- [ ] **Step 4: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Persist the Audio Mix mode and selected track in the options blob. Old blobs without the sound sentinel default to Dynamic and track 10."
```

---

## Task 6: Wire the engine at game-start + per-frame tick (fixes first-room silence)

**Files:**
- Modify: `saturn/src/main.cxx` — `ensure_typeahead` (remove lazy music block, lines ~71-80), game-start block (lines ~2068-2075), `saturn_readline` loop (line ~882), `menu_sync` (line ~200).
- Verify: Saturn build.

**Interfaces:**
- Consumes: `music_set_backend`, `music_set_isplaying`, `music_set_isshort`, `music_set_game`, `music_reset`, `music_seed`, `music_set_mix`, `music_start`, `music_tick`, `music_cdda_play_mode`, `music_cdda_is_playing`, `music_cdda_is_short`.

- [ ] **Step 1: Remove the lazy music wiring from `ensure_typeahead`**

Delete the block at lines ~71-80 (the `static int music_backend_set` / `music_set_backend` / `if (story != g_ta_story) { music_set_game...; music_reset(); }`). Keep `g_ta_story = story;` and `g_ta_diff = g_difficulty;`. This wiring moves to game-start so `music_on_turn` (which fires in `opcode_read` before `ensure_typeahead` runs) has a live backend.

- [ ] **Step 2: Wire the engine at game-start**

In the game-load sound block (lines ~2068-2075), after `music_set_level(g_music_level);` add:

```cpp
        music_set_backend(music_cdda_play_mode);
        music_set_isplaying(music_cdda_is_playing);
        music_set_isshort(music_cdda_is_short);
        music_set_game((unsigned int)((story[2] << 8) | story[3]), (const char*) (story + 0x12));
        music_seed((unsigned int) seed);
        music_reset();                         // clear room cache for the new game
        music_set_mix(g_mix_mode, g_sel_track);
        music_start();                         // non-Dynamic starts now; Dynamic waits for first room
```

- [ ] **Step 3: Tick the engine each frame during input**

In `saturn_readline`'s inner loop, right after `sound_service();` (line ~882) add:

```cpp
        music_tick();      // advance one-shot mixes / commit debounced Dynamic switches
```

And in `menu_sync` (line ~200), after `sound_service();` add the same line:

```cpp
    music_tick();
```

- [ ] **Step 4: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Wire the music engine at game-start instead of lazily in ensure_typeahead, so the first room's classification has a live backend. Tick the engine each input frame to drive mix advances and debounced switches."
```

---

## Task 7: Boot & menu music

**Files:**
- Modify: `saturn/src/main.cxx` — startup (after `options_load`, line ~1989), title/menu flow.
- Verify: Saturn build.

**Interfaces:**
- Consumes: `music_cdda_play`, `music_set_level`.

- [ ] **Step 1: Start the menu track at boot**

After `options_load();` in `main` (line ~1989) and after the pad is set up, once `SRL::Core::Initialize` has run, start the menu track so the title screen has audio. Add right after `console_init();` (line ~2006, inside the setjmp region so a soft reset re-arms it):

```cpp
    music_set_level(g_music_level);      // honor the saved music level for menu audio
    music_cdda_play(g_sel_track);        // menu track (default 10), looping across the menu flow
```

This plays through `title_and_seed`, the mode menu, game-select, and Options (all use `SRL::Core::Synchronize`/`menu_sync`, which don't stop CD-DA). The game-start block (Task 6) replaces it once a game begins.

- [ ] **Step 2: Build and sanity-check the flow**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean. (Emulator check happens in Task 11.)

- [ ] **Step 3: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Play the selected track from the title screen through the whole menu flow. The in-game engine takes over once a game starts."
```

---

## Task 8: Sound Options page (OK/Cancel, preview, apply)

**Files:**
- Modify: `saturn/src/main.cxx` — add `sound_options_page()` near the other pages (before `options_menu`, ~line 1236); forward-declare it.
- Verify: Saturn build.

**Interfaces:**
- Consumes: `g_mix_mode`, `g_sel_track`, `g_music_level`, `g_pcm_level`, `music_set_mix`, `music_start`, `music_refresh`, `music_set_level`, `sound_set_level`, `music_cdda_play`, `options_save`.
- Produces: `static void sound_options_page(void);`

- [ ] **Step 1: Add the forward declaration**

Near the other forward decls (line ~276), add:

```cpp
static void sound_options_page(void);
```

- [ ] **Step 2: Implement the page**

Insert before `options_menu` (line ~1237):

```cpp
// Sound Options (full-screen, OK/Cancel). Rows: Audio Mix, Track,
// Music level, PCM level, OK, Cancel. Start/A = OK (commit+save+apply), Esc/B =
// Cancel (restore snapshot incl. live audio). Previews play live while open.
static void sound_options_page(void) {
    static const char *const MIX[] = { "Dynamic", "Repeat", "Sequential", "Random" };
    const int N = 6;   // 0 Mix, 1 Track, 2 Music, 3 PCM, 4 OK, 5 Cancel
    int sel = 0;
    // Snapshot for Cancel.
    int s_mix = g_mix_mode, s_trk = g_sel_track, s_mus = g_music_level, s_pcm = g_pcm_level;
    SRL::Core::Synchronize();   // consume the edge that opened this
    for (;;) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        pad_repeat_update();
        if (g_pad->WasPressed(Button::Up)   || ke.kind == SATURN_KEY_UP)   sel = (sel - 1 + N) % N;
        if (g_pad->WasPressed(Button::Down) || ke.kind == SATURN_KEY_DOWN) sel = (sel + 1) % N;
        bool left  = g_pad->WasPressed(Button::Left)  || ke.kind == SATURN_KEY_LEFT;
        bool right = g_pad->WasPressed(Button::Right) || ke.kind == SATURN_KEY_RIGHT;
        bool ok   = g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::C)
                  || g_pad->WasPressed(Button::START) || ke.kind == SATURN_KEY_ENTER;
        bool cancel = g_pad->WasPressed(Button::B) || ke.kind == SATURN_KEY_ESCAPE
                    || ke.kind == SATURN_KEY_BACKSPACE;

        if (cancel) {   // revert everything, incl. live audio
            g_mix_mode = s_mix; g_sel_track = s_trk; g_music_level = s_mus; g_pcm_level = s_pcm;
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track);
            music_refresh();
            break;
        }
        if (sel == 0) { if (left && g_mix_mode > 0) g_mix_mode--; if (right && g_mix_mode < MIX_RANDOM) g_mix_mode++; }
        else if (sel == 1) {
            if (left  && g_sel_track > MUSIC_TRACK_MIN) g_sel_track--;
            if (right && g_sel_track < MUSIC_TRACK_MAX) g_sel_track++;
            if (left || right || ok) music_cdda_play(g_sel_track);   // demo/preview
        }
        else if (sel == 2) { if (left && g_music_level > 0) g_music_level--; if (right && g_music_level < 7) g_music_level++;
                             if (left || right) music_set_level(g_music_level); }
        else if (sel == 3) { if (left && g_pcm_level > 0) g_pcm_level--; if (right && g_pcm_level < 7) g_pcm_level++;
                             if (left || right) sound_set_level(g_pcm_level); }
        else if (ok && sel == 4) {   // OK
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track);
            if (g_mix_mode == MIX_DYNAMIC) music_refresh();   // hand back to the room engine
            else music_start();                               // start the chosen track now
            options_save();
            break;
        }
        else if (ok && sel == 5) {   // Cancel row == B
            g_mix_mode = s_mix; g_sel_track = s_trk; g_music_level = s_mus; g_pcm_level = s_pcm;
            music_set_level(g_music_level); sound_set_level(g_pcm_level);
            music_set_mix(g_mix_mode, g_sel_track); music_refresh();
            break;
        }

        menu_clear();
        int x = 2, y = 1;
        SRL::Debug::Print(x, y, "SOUND OPTIONS"); y += 2;
        SRL::Debug::Print(x, y, "%c Audio Mix", sel == 0 ? '>' : ' ');
        SRL::Debug::Print(x + 14, y++, "%s %s %s", g_mix_mode > 0 ? "<" : " ", MIX[g_mix_mode], g_mix_mode < MIX_RANDOM ? ">" : " ");
        SRL::Debug::Print(x, y, "%c Track", sel == 1 ? '>' : ' ');
        SRL::Debug::Print(x + 14, y++, "%d ", g_sel_track);
        SRL::Debug::Print(x, y, "%c Music", sel == 2 ? '>' : ' ');
        SRL::Debug::Print(x + 14, y++, "%d", g_music_level);
        SRL::Debug::Print(x, y, "%c PCM", sel == 3 ? '>' : ' ');
        SRL::Debug::Print(x + 14, y++, "%d", g_pcm_level);
        y++;
        SRL::Debug::Print(x, y++, "%c OK", sel == 4 ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c Cancel", sel == 5 ? '>' : ' ');
        y++;
        SRL::Debug::Print(x, y++, "%s", hint("<> change  A/Start=OK  B=Cancel", "<> change  Enter=OK  Esc=Cancel"));
        menu_sync();
    }
    SRL::Core::Synchronize();
}
```

- [ ] **Step 3: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Add a Sound Options page with Audio Mix, track demo/select, and volume rows behind an OK/Cancel contract. Cancel restores both the settings and the audio that was playing on open."
```

---

## Task 9: Options menu cleanup + screen-clear fix

**Files:**
- Modify: `saturn/src/main.cxx` — `options_menu` (lines ~1241-1320).
- Verify: Saturn build.

- [ ] **Step 1: Replace the inline audio rows with a Sound Options row**

Rework `options_menu` so the rows are: 0 Difficulty, 1 Configure MojoZork, 2 Controls, 3 Sound Options, 4 Return to Title, 5 Done. Set `NITEMS = 6`. Remove `preview_track`, `music0`, `pcm0`, `previewed`, the Music/PCM/Track handling (old `sel == 4/5/6` branches and their preview lines), and the Track/Music/PCM Print lines. Concretely:

- Change `const int NITEMS = 8;` to `const int NITEMS = 6;` and delete the `MAX_TRACK`, `preview_track`, `music0`, `pcm0`, `previewed` locals.
- In the activation block, renumber: `sel == 1` Configure, `sel == 2` Controls, `sel == 3` opens Sound Options, `sel == 4` Return to Title, `sel == 5` Done. Add:

```cpp
            else if (sel == 3) { sound_options_page(); menu_clear_full(); }
```

  (`menu_clear_full` clears the whole screen — see Step 2. Sound Options paints full-screen, so the box must repaint clean afterward.)
- Delete the two "Live preview" lines (`if (sel == 4 ...)` and `if (sel == 6 ...)`).
- Replace the row Print lines for Return/Music/PCM/Track/Done with:

```cpp
        SRL::Debug::Print(x0 + 2, y0 + 8,  "%c Sound Options", sel == 3 ? '>' : ' ');
        SRL::Debug::Print(x0 + 2, y0 + 9,  "%c Return to Title Screen", sel == 4 ? '>' : ' ');
        SRL::Debug::Print(x0 + 2, y0 + 10, "%c Done", sel == 5 ? '>' : ' ');
```

  and delete the old `y0 + 9/10/11/12` Music/PCM/Track/Done lines.
- In the Return-to-Title branch (was `sel == 3`, now `sel == 4`) and the Done branch (now `sel == 5`) update the indices.
- After the loop, delete the `g_music_level != music0 || g_pcm_level != pcm0` clause and the `music_set_level/sound_set_level/music_refresh(previewed)` tail — audio is now owned by Sound Options. Keep the difficulty save: `if (diff_changed) options_save();` and keep the button-release drain.

Also update the Controls activation to the new index (`sel == 2`).

- [ ] **Step 2: Add a full-screen clear helper and use it on nested-page return**

Near `menu_clear` (find its definition) add, if not present, a whole-screen clear used when returning from a full-screen nested page into the box:

```cpp
static void menu_clear_full(void) { for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r); }
```

Call `menu_clear_full();` immediately after each full-screen nested page returns inside `options_menu`: after `controls_page()` / `keyboard_controls_page()` (the Controls branch) and after `sound_options_page()` (Sound Options branch). This fixes leftover nested-page text framing the box.

- [ ] **Step 3: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Move audio rows out of the Options box into the new Sound Options page. Clear the whole screen when returning from a full-screen nested page so its text no longer frames the box."
```

---

## Task 10: OK/Cancel on Controls & Keyboard Controls

**Files:**
- Modify: `saturn/src/main.cxx` — `configure_controls_page` (1084-1140), `keyboard_controls_page` (1192-1235), `config_page` labels (1054-1055).
- Verify: Saturn build.

- [ ] **Step 1: Snapshot + OK/Cancel in `configure_controls_page`**

At the top of `configure_controls_page` (after `SRL::Core::Synchronize();`), snapshot the mapping:

```cpp
    int s_face[FA_N], s_chord[CA_N];
    for (int a = 0; a < FA_N; a++) s_face[a]  = g_face_btn[a];
    for (int a = 0; a < CA_N; a++) s_chord[a] = g_chord_slot[a];
```

Add a Cancel row after Done: change `R_DONE` semantics to OK and add `R_CANCEL = NASSIGN + 2`, `sel` modulo `(R_CANCEL + 1)`. Handle:

```cpp
        if (sel == R_DONE)   { if (act) { options_save(); break; } }        // OK
        else if (sel == R_CANCEL) { if (act) {                              // Cancel
            for (int a = 0; a < FA_N; a++) g_face_btn[a]   = s_face[a];
            for (int a = 0; a < CA_N; a++) g_chord_slot[a] = s_chord[a];
            break; } }
```

Make `back` (B/Esc) act as Cancel (restore snapshot, break) rather than the current save-on-exit. Remove the unconditional `options_save();` after the loop (OK now saves). Render:

```cpp
        SRL::Debug::Print(x, y++, "%c Reset to Defaults", sel == R_RESET ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c OK", sel == R_DONE ? '>' : ' ');
        SRL::Debug::Print(x, y++, "%c Cancel", sel == R_CANCEL ? '>' : ' ');
```

Update the hint line to `A/Start=OK  B/Esc=Cancel`.

- [ ] **Step 2: Snapshot + OK/Cancel in `keyboard_controls_page`**

At the top, snapshot the four flags:

```cpp
    int s_arrows = g_caret_arrows, s_ins = keyboard_get_insert(),
        s_caps = keyboard_get_caps(), s_num = keyboard_get_num();
```

Change `N` from 5 to 6: rows 0 Arrows, 1 Insert, 2 Caps, 3 Num, 4 OK, 5 Cancel. Replace the Done handling:

```cpp
        else if (sel == 4 && act) { options_save(); break; }   // OK
        else if (sel == 5 && act) {                            // Cancel
            g_caret_arrows = s_arrows; keyboard_set_insert(s_ins);
            keyboard_set_caps(s_caps); keyboard_set_num(s_num); break; }
```

Make `back` restore the snapshot then break (Cancel). Render OK then Cancel rows in place of the old Done row, and update the hint to `A/Start=OK  B/Esc=Cancel`.

- [ ] **Step 3: Align `config_page` labels**

In `config_page`'s hint (lines ~1054-1055), reword to the shared contract without changing behavior (A/Enter already saves, Start/Esc cancels):

```cpp
        SRL::Debug::Print(2, 13, "%s",
            hint("C=type B=del  A=OK  Start=Cancel", "type number  Enter=OK  Esc=Cancel"));
```

- [ ] **Step 4: Build**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Give the Controls and Keyboard Controls pages an explicit OK/Cancel contract with snapshot restore. Align the Configure page labels to the same Start/Esc wording."
```

---

## Task 11: Emulator verification pass

**Files:** none (manual/verification).

- [ ] **Step 1: Build the debug image**

Run: `cd saturn && ./compile.bat debug`
Expected: builds clean.

- [ ] **Step 2: Run the built image and verify** (use the project's emulator; see `SaturnRingLib/emulators`)

Check each:
- Title screen plays track 10; audio continues through mode-select, game-select, and Options.
- Start a game (new + via Load Save): the first room has audio (no silence).
- Options → Sound Options: changing Mix + Track previews live; OK persists; reboot (soft reset chord) and re-open Options → Sound Options shows the saved values.
- Sound Options → change values → Cancel: live audio and settings revert to the pre-open state; reboot shows the pre-Cancel saved values.
- Mix = Override: the selected track repeats; Sequential: tracks advance when one ends; Random: a different track follows on end; Dynamic: room changes switch mood after ~6s, and a short track (e.g. 25) plays once then a longer track from the pool follows.
- Return from Sound Options / Controls to the Options box: no leftover full-screen text framing the box.
- Controls / Keyboard Controls: OK saves, Cancel reverts.

- [ ] **Step 3: Re-run host unit tests (regression)**

Run:
```
gcc -O2 -I saturn/src -o /tmp/mt  test/music_test.c     saturn/src/music.c saturn/src/music_data.c && /tmp/mt
gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/mmt
```
Expected: both `ALL PASS`.

- [ ] **Step 4: Commit any fixes** discovered during verification with a two-sentence message.

---

## Self-Review Notes

- Spec coverage: Sound Options page + OK/Cancel (Task 8); Audio Mix + demo/select (Tasks 3,8); persistence (Task 5); menu/boot music (Task 7); first-room fix (Task 6); category pools + Neutral merge (Task 1); random pick (Task 2); category-keyed debounce (Task 3); short tracks (Tasks 3,4); gapless loop (Task 4); Options cleanup + screen-clear (Task 9); Controls OK/Cancel (Task 10); testing (Tasks 1-3,11).
- Type consistency: `music_play_fn` is `(int track, int loop)` everywhere (Tasks 2-4,6); backend registered with `music_cdda_play_mode`; `music_cdda_play` is the looping wrapper for menu/preview only.
- Risks flagged inline: SRL `Cdda` status query and CD TOC API (Task 4 Step 3), each with a stated fallback; verify on the SDK before building.
