# Dynamic Room Audio Atmospheres Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play looping CD-DA music that follows the mood of the current room on any v3 game, driven by a universal keyword/event classifier with optional per-game overrides.

**Architecture:** A pure-C engine (`music.c`) classifies the room from the turn's output text and the current room object (Z global 0), maps the resulting category many-to-one onto a CD-DA track, and calls a platform backend to play it. The Saturn backend (`music_cdda.cxx`) wraps `SRL::Sound::Cdda`; a host test stub records track calls instead. Lurking Horror's PCM sound slices are cached in RAM so CD reads don't interrupt CD-DA.

**Tech Stack:** C (engine, data tables, mojozork core), C++ (SRL `SRL::Sound::Cdda`), host `gcc` for pure-C unit tests, `saturn/compile.bat` for the Saturn image.

## Global Constraints

- Build the Saturn image ONLY with `saturn/compile.bat` (run from `saturn/`). Never invoke `make`/`gcc` for the Saturn image. Source files under `saturn/src/**/*.c` and `**/*.cxx` are auto-globbed by `saturn/makefile` — no makefile edits needed for new files.
- The mojozork core (`saturn/mojozork.c`) is shared with non-Saturn builds. Any new call from it MUST be guarded by `#if defined(MOJOZORK_SATURN)` so host/multizork builds have no link dependency on Saturn or music code (mirror the existing `GState->readline` guard at `opcode_read`).
- `SRL::Debug::Print` supports only `%d`, `%s`, `%c` — NO `%x`, and NO width/precision flags (`%02x`, `%-16s`). Align columns with separate fixed-x `Print` calls.
- On-device CD random reads use sector-addressed `SRL::Cd::File::LoadBytes` (Seek is broken); do not add `Seek`-based reads.
- Host unit tests are pure C only. Build/run pattern: `gcc -O2 -I saturn/src -o /tmp/<name> test/<name>.c saturn/src/<impl>.c ... && /tmp/<name> [args]`.
- 14 categories, enum order fixed: NEUTRAL, WILDERNESS, UNDERGROUND, WATER, NAUTICAL, TOWN, DUNGEON, DESERT, MAGIC, SCIFI, HORROR, MYSTERY, DANGER, TRIUMPH.
- Category→track `0` means "no dedicated track — keep current". Track 1 = data track; audio = tracks 2+.
- Audio sliders: Music (default 7) and PCM (default 4), range 0–7, 0 = silence.

---

### Task 1: Music data module (categories, tables, accessors)

Pure-C data layer: category enum + `MusicKeyword` type in `music.h`; keyword table, event table, category→track table, and (empty) per-game override table in `music_data.c`, each behind an accessor. Host-tested.

**Files:**
- Create: `saturn/src/music.h`
- Create: `saturn/src/music_data.c`
- Test: `test/music_data_test.c`

**Interfaces:**
- Produces:
  - `enum { MC_NEUTRAL=0, MC_WILDERNESS, MC_UNDERGROUND, MC_WATER, MC_NAUTICAL, MC_TOWN, MC_DUNGEON, MC_DESERT, MC_MAGIC, MC_SCIFI, MC_HORROR, MC_MYSTERY, MC_DANGER, MC_TRIUMPH };` and `#define MUSIC_NUM_CATEGORIES 14`
  - `typedef struct { const char* word; unsigned char cat; } MusicKeyword;`
  - `const MusicKeyword* music_keywords(int* n);` — room keywords (cats 1–11)
  - `const MusicKeyword* music_events(int* n);` — event words (cats 12–13)
  - `int music_category_track(int category);` — returns track, or 0 if out of range / no track
  - `int music_game_room_category(unsigned int release, const char* serial, unsigned int room);` — per-game override, or −1 if none

- [ ] **Step 1: Write the failing test**

Create `test/music_data_test.c`:

```c
#include <stdio.h>
#include <string.h>
#include "music.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

int main(void) {
    int fails = 0;

    /* category->track table */
    CHECK(music_category_track(MC_NEUTRAL) == 2);
    CHECK(music_category_track(MC_NAUTICAL) == music_category_track(MC_WATER)); /* shares */
    CHECK(music_category_track(MC_DANGER) == 10);
    CHECK(music_category_track(-1) == 0);
    CHECK(music_category_track(MUSIC_NUM_CATEGORIES) == 0);

    /* keyword/event accessors are non-empty and carry sane categories */
    int nk = 0; const MusicKeyword* kw = music_keywords(&nk);
    CHECK(kw != NULL && nk > 0);
    for (int i = 0; i < nk; i++) CHECK(kw[i].cat >= MC_WILDERNESS && kw[i].cat <= MC_MYSTERY);

    int ne = 0; const MusicKeyword* ev = music_events(&ne);
    CHECK(ev != NULL && ne > 0);
    for (int i = 0; i < ne; i++) CHECK(ev[i].cat == MC_DANGER || ev[i].cat == MC_TRIUMPH);

    /* no per-game overrides yet */
    CHECK(music_game_room_category(88, "840726", 5) == -1);

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/mdt test/music_data_test.c saturn/src/music_data.c && /tmp/mdt`
Expected: FAIL to compile — `music.h` / `music_data.c` don't exist yet.

- [ ] **Step 3: Write `music.h`**

Create `saturn/src/music.h`:

```c
#ifndef MUSIC_H
#define MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

#define MUSIC_NUM_CATEGORIES 14
enum {
    MC_NEUTRAL = 0, MC_WILDERNESS, MC_UNDERGROUND, MC_WATER, MC_NAUTICAL,
    MC_TOWN, MC_DUNGEON, MC_DESERT, MC_MAGIC, MC_SCIFI, MC_HORROR,
    MC_MYSTERY, MC_DANGER, MC_TRIUMPH
};

typedef struct { const char* word; unsigned char cat; } MusicKeyword;

/* Data tables (music_data.c). */
const MusicKeyword* music_keywords(int* n);   /* room keywords, cats 1..11 */
const MusicKeyword* music_events(int* n);     /* event words, cats 12..13 */
int music_category_track(int category);       /* CD-DA track, 0 = keep current */
int music_game_room_category(unsigned int release, const char* serial,
                             unsigned int room);  /* override, -1 if none */

/* Backend callback: play a CD-DA track looping. track 0 = keep current. */
typedef void (*music_play_fn)(int track);

/* Engine (music.c). */
void music_reset(void);
void music_set_backend(music_play_fn play);
void music_set_game(unsigned int release, const char* serial);
void music_note_output(const char* str, unsigned int len);
void music_on_turn(unsigned int room);

/* Pure classifiers, exposed for tests. */
int music_classify_room(const char* text);    /* cat 1..11, or MC_NEUTRAL */
int music_scan_event(const char* text);       /* cat 12/13, or -1 */

#ifdef __cplusplus
}
#endif
#endif /* MUSIC_H */
```

- [ ] **Step 4: Write `music_data.c`**

Create `saturn/src/music_data.c`:

```c
#include "music.h"
#include <string.h>

/* Room keywords -> categories 1..11 (best-effort; edit freely). */
static const MusicKeyword KW[] = {
    {"forest",MC_WILDERNESS},{"tree",MC_WILDERNESS},{"trees",MC_WILDERNESS},
    {"woods",MC_WILDERNESS},{"grove",MC_WILDERNESS},{"meadow",MC_WILDERNESS},
    {"field",MC_WILDERNESS},{"clearing",MC_WILDERNESS},{"path",MC_WILDERNESS},
    {"hill",MC_WILDERNESS},{"mountain",MC_WILDERNESS},{"garden",MC_WILDERNESS},

    {"cave",MC_UNDERGROUND},{"cavern",MC_UNDERGROUND},{"tunnel",MC_UNDERGROUND},
    {"underground",MC_UNDERGROUND},{"cellar",MC_UNDERGROUND},{"mine",MC_UNDERGROUND},
    {"passage",MC_UNDERGROUND},{"grotto",MC_UNDERGROUND},{"crawlway",MC_UNDERGROUND},

    {"river",MC_WATER},{"stream",MC_WATER},{"lake",MC_WATER},{"pool",MC_WATER},
    {"water",MC_WATER},{"waterfall",MC_WATER},{"shore",MC_WATER},{"bank",MC_WATER},
    {"underwater",MC_WATER},{"flooded",MC_WATER},

    {"ship",MC_NAUTICAL},{"boat",MC_NAUTICAL},{"deck",MC_NAUTICAL},{"cabin",MC_NAUTICAL},
    {"hull",MC_NAUTICAL},{"sea",MC_NAUTICAL},{"ocean",MC_NAUTICAL},{"dock",MC_NAUTICAL},
    {"harbor",MC_NAUTICAL},{"sail",MC_NAUTICAL},{"mast",MC_NAUTICAL},{"submarine",MC_NAUTICAL},

    {"house",MC_TOWN},{"hall",MC_TOWN},{"kitchen",MC_TOWN},{"office",MC_TOWN},
    {"street",MC_TOWN},{"building",MC_TOWN},{"stairs",MC_TOWN},{"parlor",MC_TOWN},
    {"bedroom",MC_TOWN},

    {"temple",MC_DUNGEON},{"tomb",MC_DUNGEON},{"crypt",MC_DUNGEON},{"ruin",MC_DUNGEON},
    {"altar",MC_DUNGEON},{"ancient",MC_DUNGEON},{"chamber",MC_DUNGEON},
    {"dungeon",MC_DUNGEON},{"catacomb",MC_DUNGEON},{"vault",MC_DUNGEON},

    {"desert",MC_DESERT},{"sand",MC_DESERT},{"dune",MC_DESERT},{"oasis",MC_DESERT},
    {"wasteland",MC_DESERT},

    {"spell",MC_MAGIC},{"magic",MC_MAGIC},{"enchant",MC_MAGIC},{"wizard",MC_MAGIC},
    {"scroll",MC_MAGIC},{"rune",MC_MAGIC},{"mystic",MC_MAGIC},{"sorcerer",MC_MAGIC},

    {"console",MC_SCIFI},{"computer",MC_SCIFI},{"airlock",MC_SCIFI},{"panel",MC_SCIFI},
    {"robot",MC_SCIFI},{"laboratory",MC_SCIFI},{"reactor",MC_SCIFI},{"corridor",MC_SCIFI},
    {"module",MC_SCIFI},{"cockpit",MC_SCIFI},

    {"corpse",MC_HORROR},{"rotting",MC_HORROR},{"stench",MC_HORROR},{"shadow",MC_HORROR},
    {"eerie",MC_HORROR},{"decay",MC_HORROR},{"skeleton",MC_HORROR},

    {"body",MC_MYSTERY},{"clue",MC_MYSTERY},{"murder",MC_MYSTERY},{"evidence",MC_MYSTERY},
    {"study",MC_MYSTERY},{"library",MC_MYSTERY},{"detective",MC_MYSTERY},{"locked",MC_MYSTERY},
};

/* Event words -> categories 12..13 (fire on any turn's text). */
static const MusicKeyword EV[] = {
    {"monster",MC_DANGER},{"troll",MC_DANGER},{"grue",MC_DANGER},{"attack",MC_DANGER},
    {"fight",MC_DANGER},{"flames",MC_DANGER},{"fire",MC_DANGER},{"burning",MC_DANGER},
    {"scream",MC_DANGER},{"danger",MC_DANGER},
    {"treasure",MC_TRIUMPH},{"gold",MC_TRIUMPH},{"jewel",MC_TRIUMPH},{"chest",MC_TRIUMPH},
    {"reward",MC_TRIUMPH},{"gleaming",MC_TRIUMPH},{"victory",MC_TRIUMPH},
};

/* Category -> CD-DA track (many-to-one). 0 = no track / keep current.
   Edit to match the tracks you actually record. */
static const unsigned char CATEGORY_TRACK[MUSIC_NUM_CATEGORIES] = {
    /* NEUTRAL */ 2, /* WILDERNESS */ 3, /* UNDERGROUND */ 4, /* WATER */ 5,
    /* NAUTICAL */ 5, /* TOWN */ 6, /* DUNGEON */ 4, /* DESERT */ 3,
    /* MAGIC */ 7, /* SCIFI */ 8, /* HORROR */ 9, /* MYSTERY */ 6,
    /* DANGER */ 10, /* TRIUMPH */ 11
};

/* Per-game room->category overrides, keyed by release+serial. Empty for v1;
   add rows later as data only. */
typedef struct {
    unsigned short release; const char* serial;
    const unsigned char* room_cat;   /* index = room object id, value = cat+1, 0 = none */
    int nrooms;
} MusicGameMap;
static const MusicGameMap GAME_MAPS[] = { { 0, 0, 0, 0 } };  /* sentinel, unused */

const MusicKeyword* music_keywords(int* n) { *n = (int)(sizeof KW / sizeof KW[0]); return KW; }
const MusicKeyword* music_events(int* n)   { *n = (int)(sizeof EV / sizeof EV[0]); return EV; }

int music_category_track(int category) {
    if (category < 0 || category >= MUSIC_NUM_CATEGORIES) return 0;
    return CATEGORY_TRACK[category];
}

int music_game_room_category(unsigned int release, const char* serial, unsigned int room) {
    for (int i = 0; i < (int)(sizeof GAME_MAPS / sizeof GAME_MAPS[0]); i++) {
        const MusicGameMap* g = &GAME_MAPS[i];
        if (g->serial == 0) continue;
        if (g->release != release || memcmp(g->serial, serial, 6) != 0) continue;
        if ((int)room >= g->nrooms) return -1;
        int v = g->room_cat[room];
        return v ? v - 1 : -1;
    }
    return -1;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/mdt test/music_data_test.c saturn/src/music_data.c && /tmp/mdt`
Expected: `ALL PASS`

- [ ] **Step 6: Commit**

```bash
git add saturn/src/music.h saturn/src/music_data.c test/music_data_test.c
git commit -m "Music data module: categories, keyword/event/track tables"
```

---

### Task 2: Pure classifiers (`music_classify_room`, `music_scan_event`)

Whole-word, case-insensitive scan of a text block. `music_classify_room` counts room-keyword hits per category and returns the max (ties → lower category number; no hits → `MC_NEUTRAL`). `music_scan_event` returns the first event category found, or −1.

**Files:**
- Create: `saturn/src/music.c`
- Test: `test/music_classify_test.c`

**Interfaces:**
- Consumes: `music_keywords`, `music_events` (Task 1).
- Produces: `int music_classify_room(const char* text);`, `int music_scan_event(const char* text);`

- [ ] **Step 1: Write the failing test**

Create `test/music_classify_test.c`:

```c
#include <stdio.h>
#include "music.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

int main(void) {
    int fails = 0;

    CHECK(music_classify_room("You are in a damp cave. A narrow tunnel leads north.") == MC_UNDERGROUND);
    CHECK(music_classify_room("A sunny forest clearing, tall trees all around.") == MC_WILDERNESS);
    CHECK(music_classify_room("The airlock hisses. A console blinks on the corridor wall.") == MC_SCIFI);
    CHECK(music_classify_room("The wizard's study is lined with scroll racks; a rune glows.") == MC_MAGIC);
    CHECK(music_classify_room("Nothing in particular here.") == MC_NEUTRAL);

    /* case-insensitive + whole-word (no 'scavenge' false hit for 'cave') */
    CHECK(music_classify_room("A CAVERN yawns below.") == MC_UNDERGROUND);
    CHECK(music_classify_room("You scavenge the bins.") == MC_NEUTRAL);

    CHECK(music_scan_event("A hideous monster lunges to attack!") == MC_DANGER);
    CHECK(music_scan_event("A pile of gold and a jewel gleam here.") == MC_TRIUMPH);
    CHECK(music_scan_event("You wait.") == -1);

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/mct test/music_classify_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/mct`
Expected: FAIL to compile — `music.c` doesn't exist.

- [ ] **Step 3: Write the classifier core of `music.c`**

Create `saturn/src/music.c` with the includes and the two pure classifiers (engine state added in Task 3):

```c
#include "music.h"
#include <string.h>

/* Lowercase a byte. */
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* True if `word` occurs in `text` as a whole word (case-insensitive). A word
   boundary is any non-alphabetic character or string end. */
static int has_word(const char* text, const char* word) {
    int wl = (int) strlen(word);
    for (const char* p = text; *p; p++) {
        int i = 0;
        while (i < wl && p[i] && lc(p[i]) == word[i]) i++;   /* word is stored lowercase */
        if (i == wl) {
            char before = (p == text) ? ' ' : p[-1];
            char after  = p[wl];
            int lb = !((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z'));
            int la = !((after  >= 'a' && after  <= 'z') || (after  >= 'A' && after  <= 'Z'));
            if (lb && la) return 1;
        }
    }
    return 0;
}

int music_classify_room(const char* text) {
    if (!text) return MC_NEUTRAL;
    int counts[MUSIC_NUM_CATEGORIES];
    for (int i = 0; i < MUSIC_NUM_CATEGORIES; i++) counts[i] = 0;
    int nk = 0; const MusicKeyword* kw = music_keywords(&nk);
    for (int i = 0; i < nk; i++) if (has_word(text, kw[i].word)) counts[kw[i].cat]++;
    int best = MC_NEUTRAL, bestn = 0;
    for (int c = MC_WILDERNESS; c <= MC_MYSTERY; c++)
        if (counts[c] > bestn) { bestn = counts[c]; best = c; }
    return best;
}

int music_scan_event(const char* text) {
    if (!text) return -1;
    int ne = 0; const MusicKeyword* ev = music_events(&ne);
    for (int i = 0; i < ne; i++) if (has_word(text, ev[i].word)) return ev[i].cat;
    return -1;
}
```

Note: keyword/event `word` literals in `music_data.c` are already lowercase, which `has_word` relies on.

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/mct test/music_classify_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/mct`
Expected: `ALL PASS`

- [ ] **Step 5: Commit**

```bash
git add saturn/src/music.c test/music_classify_test.c
git commit -m "Music classifiers: whole-word room keyword + event scan"
```

---

### Task 3: Engine state machine (`music_on_turn` and friends)

The per-turn decision logic: accumulate output text, classify on room change (with a 256-entry room cache and per-game override), apply event overrides until the next room change, keep-current on no-match, and call the backend only when the target track changes.

**Files:**
- Modify: `saturn/src/music.c` (append engine state + functions)
- Test: `test/music_engine_test.c`

**Interfaces:**
- Consumes: `music_classify_room`, `music_scan_event` (Task 2), `music_category_track`, `music_game_room_category` (Task 1).
- Produces: `music_reset`, `music_set_backend`, `music_set_game`, `music_note_output`, `music_on_turn` (signatures in `music.h`, Task 1).

- [ ] **Step 1: Write the failing test**

Create `test/music_engine_test.c`:

```c
#include <stdio.h>
#include "music.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

static int g_last_track;       /* records the last backend call */
static int g_calls;
static void rec(int track) { g_last_track = track; g_calls++; }

int main(void) {
    int fails = 0;
    music_set_backend(rec);
    music_set_game(0, "000000");     /* no override matches */
    music_reset();
    g_last_track = -99; g_calls = 0;

    /* Enter an underground room: classify -> UNDERGROUND -> track 4. */
    music_note_output("You are in a dark cave with a tunnel.", 37);
    music_on_turn(10);
    CHECK(g_last_track == music_category_track(MC_UNDERGROUND));   /* 4 */

    /* Same room again, no new text: no re-classify, no track change. */
    int calls_before = g_calls;
    music_on_turn(10);
    CHECK(g_calls == calls_before);                                /* kept current */

    /* Enter a forest room -> WILDERNESS -> track 3. */
    music_note_output("A forest clearing among tall trees.", 35);
    music_on_turn(11);
    CHECK(g_last_track == music_category_track(MC_WILDERNESS));    /* 3 */

    /* Event word in the SAME room overrides to DANGER track until room change. */
    music_note_output("A troll leaps out to attack!", 28);
    music_on_turn(11);
    CHECK(g_last_track == music_category_track(MC_DANGER));        /* 10 */

    /* New room clears the override; revisiting room 10 uses the CACHED category. */
    music_note_output("An empty stone landing.", 23);   /* NEUTRAL text */
    music_on_turn(10);
    CHECK(g_last_track == music_category_track(MC_UNDERGROUND));   /* from cache, 4 */

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `gcc -O2 -I saturn/src -o /tmp/met test/music_engine_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/met`
Expected: FAIL to link — `music_reset`, `music_on_turn`, etc. undefined.

- [ ] **Step 3: Append the engine to `music.c`**

Add to `saturn/src/music.c` (after the classifiers):

```c
/* ---- engine state ---- */
#define MUSIC_TEXT_MAX 512

static music_play_fn g_play = 0;
static unsigned int  g_release = 0;
static char          g_serial[8] = {0};
static int           g_have_room = 0;
static unsigned int  g_cur_room = 0;
static int           g_base_cat = MC_NEUTRAL;
static int           g_event_cat = -1;        /* override for current room, -1 = none */
static int           g_active_track = 0;      /* 0 = nothing requested yet */
static unsigned char g_room_cache[256];       /* 0 = unseen, else cat+1 */
static char          g_turn_text[MUSIC_TEXT_MAX];
static int           g_turn_len = 0;

void music_set_backend(music_play_fn play) { g_play = play; }

void music_set_game(unsigned int release, const char* serial) {
    g_release = release;
    for (int i = 0; i < 6 && serial && serial[i]; i++) g_serial[i] = serial[i];
    g_serial[6] = 0;
}

void music_reset(void) {
    g_have_room = 0; g_cur_room = 0; g_base_cat = MC_NEUTRAL; g_event_cat = -1;
    g_active_track = 0; g_turn_len = 0; g_turn_text[0] = 0;
    for (int i = 0; i < 256; i++) g_room_cache[i] = 0;
    if (g_play) g_play(0);   /* 0 = stop / keep-none */
}

void music_note_output(const char* str, unsigned int len) {
    for (unsigned int i = 0; i < len && str[i]; i++) {
        if (g_turn_len < MUSIC_TEXT_MAX - 1) g_turn_text[g_turn_len++] = str[i];
    }
    g_turn_text[g_turn_len] = 0;
}

void music_on_turn(unsigned int room) {
    int event_cat = music_scan_event(g_turn_text);

    if (!g_have_room || room != g_cur_room) {
        int base = music_game_room_category(g_release, g_serial, room);
        if (base < 0) {
            unsigned char cached = (room < 256) ? g_room_cache[room] : 0;
            if (cached) base = cached - 1;
            else {
                base = music_classify_room(g_turn_text);
                if (room < 256) g_room_cache[room] = (unsigned char)(base + 1);
            }
        }
        g_cur_room = room; g_have_room = 1; g_base_cat = base; g_event_cat = -1;
    }
    if (event_cat >= 0) g_event_cat = event_cat;

    int target = (g_event_cat >= 0) ? g_event_cat : g_base_cat;
    int track = music_category_track(target);
    if (track != 0 && track != g_active_track) {
        g_active_track = track;
        if (g_play) g_play(track);
    }

    g_turn_len = 0; g_turn_text[0] = 0;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -O2 -I saturn/src -o /tmp/met test/music_engine_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/met`
Expected: `ALL PASS`

- [ ] **Step 5: Re-run the earlier tests to confirm no regressions**

Run: `gcc -O2 -I saturn/src -o /tmp/mct test/music_classify_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/mct && gcc -O2 -I saturn/src -o /tmp/mdt test/music_data_test.c saturn/src/music_data.c && /tmp/mdt`
Expected: `ALL PASS` twice.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/music.c test/music_engine_test.c
git commit -m "Music engine: room-change classify, event override, room cache"
```

---

### Task 4: Saturn CD-DA backend (`music_cdda.cxx`)

Thin C++ shim implementing the play callback and a level setter over `SRL::Sound::Cdda`. Saturn-only; verified by the Saturn build (no host test).

**Files:**
- Create: `saturn/src/music_cdda.cxx`
- Modify: `saturn/src/music.h` (add `music_cdda_play` + `music_set_level` declarations)

**Interfaces:**
- Consumes: `music_play_fn` (Task 1).
- Produces: `void music_cdda_play(int track);` (the backend), `void music_set_level(int level);` (0–7 CD-DA output level).

- [ ] **Step 1: Add declarations to `music.h`**

In `saturn/src/music.h`, before the closing `#ifdef __cplusplus }`:

```c
/* Saturn CD-DA backend (music_cdda.cxx). */
void music_cdda_play(int track);   /* track 0 = stop; else play looping */
void music_set_level(int level);   /* 0..7, 0 = silence */
```

- [ ] **Step 2: Write `music_cdda.cxx`**

Create `saturn/src/music_cdda.cxx`:

```c
#include <srl.hpp>
extern "C" {
#include "music.h"
}

// Current CD-DA output level (0..7). 0 = silence.
static int g_level = 7;

extern "C" void music_set_level(int level) {
    if (level < 0) level = 0; if (level > 7) level = 7;
    g_level = level;
    if (level == 0) SRL::Sound::Cdda::StopPause();
    // NOTE: if SRL exposes a CD-DA volume call, scale it here from g_level.
    // Confirm the exact SRL/SGL symbol during build; otherwise level acts as
    // play (>0) vs mute (0).
}

extern "C" void music_cdda_play(int track) {
    if (track <= 0 || g_level == 0) { SRL::Sound::Cdda::StopPause(); return; }
    SRL::Sound::Cdda::PlaySingle((int16_t) track, /*loop=*/true);
}
```

- [ ] **Step 3: Build the Saturn image to verify it compiles/links**

Run (from `saturn/`): `./compile.bat debug 2>&1 | grep -iE "error|music|mojozork.bin|No post build" | head`
Expected: no `error`; `music_cdda.cxx` compiles; `mojozork.bin` produced.

Note: `SRL::Sound::Cdda::PlaySingle` signature/loop arg is confirmed from `SaturnRingLib/Samples/Sound - CDDA (simple)`. If the exact CD-DA volume symbol is needed for `music_set_level`, check `SaturnRingLib/Samples/Sound - CDDA/src/main.cxx` for a level/volume call and wire it; the mute-on-0 path above is the safe fallback.

- [ ] **Step 4: Commit**

```bash
git add saturn/src/music.h saturn/src/music_cdda.cxx
git commit -m "Saturn CD-DA music backend (SRL::Sound::Cdda shim)"
```

---

### Task 5: Wire the engine into the core and front-end

Call `music_on_turn` once per turn from `opcode_read` (guarded); mirror game output into the engine from `saturn_writestr`; install the CD-DA backend and set the game id at load; reset on game switch.

**Files:**
- Modify: `saturn/mojozork.c` (`opcode_read`, ~line 1307)
- Modify: `saturn/src/main.cxx` (`saturn_writestr`; game-load init; reset)

**Interfaces:**
- Consumes: `music_on_turn`, `music_note_output`, `music_set_backend`, `music_cdda_play`, `music_set_game`, `music_reset` (Tasks 1,3,4).
- Produces: per-turn music updates on-device.

- [ ] **Step 1: Add the per-turn hook in `opcode_read`**

In `saturn/mojozork.c`, in `opcode_read`, immediately AFTER `updateStatusBar();` (line ~1307), add:

```c
#if defined(MOJOZORK_SATURN)
    {
        extern void music_on_turn(unsigned int room);
        const uint8 *rmaddr = varAddress(0x10, 0, 0);   /* global 0 = current room */
        music_on_turn((unsigned int) READUI16(rmaddr));
    }
#endif
```

- [ ] **Step 2: Mirror output into the engine from `saturn_writestr`**

In `saturn/src/main.cxx`, find `extern "C" void saturn_writestr(const char *str, size_t slen)` and add a `music_note_output` call alongside `console_write`:

```c
extern "C" void saturn_writestr(const char *str, size_t slen) {
    console_write(str, (unsigned int) slen);
    music_note_output(str, (unsigned int) slen);
}
```

Add near the other C includes at the top of `main.cxx` (inside the existing `extern "C" { ... }` block that includes the core headers):

```c
#include "music.h"
```

- [ ] **Step 3: Install the backend + game id at game load, reset on switch**

In `saturn/src/main.cxx`, locate where a story is loaded and `sound_init(...)` is called (the game-start path). Immediately after the story header is available, add:

```c
    music_set_backend(music_cdda_play);
    music_reset();
    /* release = header 0x02..0x03 (big-endian); serial = header 0x12..0x17 */
    {
        extern unsigned char *g_story;   /* if not already in scope, use the loaded story pointer used by sound_init */
        /* Use the same story buffer the interpreter loaded. */
    }
```

Because the exact story-pointer symbol in `main.cxx` may differ, set the game id from the header bytes the front-end already has when it calls `sound_init`. Concretely, where the code reads the `.z3` header for `read_game_info`/load, add:

```c
    music_set_game((unsigned int)((hdr[2] << 8) | hdr[3]),
                   (const char*) &hdr[0x12]);
```

using the same 64-byte `hdr` buffer already loaded via `LoadBytes(0, 64, hdr)` in `read_game_info`. If that buffer isn't in scope at the load site, read 64 header bytes the same way there.

- [ ] **Step 4: Build to verify**

Run (from `saturn/`): `./compile.bat debug 2>&1 | grep -iE "error|warning: .*(main|mojozork)\.|mojozork.bin" | head`
Expected: no `error`; `mojozork.bin` produced.

- [ ] **Step 5: Commit**

```bash
git add saturn/mojozork.c saturn/src/main.cxx
git commit -m "Wire dynamic music: per-turn hook, output capture, load/reset"
```

---

### Task 6: Lurking Horror persistent PCM slice cache

Refactor `sound.cxx` so loaded PCM slices persist in a RAM cache keyed by sound number for the game's lifetime (freed on `sound_stop_all`/game switch), so each distinct sound costs at most one CD read — after that, loops and re-triggers never touch the CD and never interrupt CD-DA. Also add a PCM output-level setter.

**Files:**
- Modify: `saturn/src/sound.cxx`
- Modify: `saturn/src/sound.h` (add `sound_set_level`)

**Interfaces:**
- Consumes: existing `load_slice`, `Slot`, `saturn_sound_effect` (sound.cxx).
- Produces: `void sound_set_level(int level);` (0–7 PCM level), and slice reuse via a cache.

- [ ] **Step 1: Add `sound_set_level` to `sound.h`**

In `saturn/src/sound.h`, add:

```c
/* PCM output level 0..7 (0 = silence). Scales effect volume; 0 disables. */
void sound_set_level(int level);
```

- [ ] **Step 2: Add a persistent slice cache in `sound.cxx`**

In `saturn/src/sound.cxx`, add a small cache above `saturn_sound_effect`:

```c
// Persistent PCM slice cache: each sound number's decoded slice is loaded once
// and kept for the game's lifetime, so re-triggers/loops never re-read the CD
// (which would interrupt CD-DA music). Freed by sound_stop_all().
#define NCACHE 8
struct SliceCache { int number; int8_t* buf; uint32_t play; unsigned short rate; };
static SliceCache g_cache[NCACHE];

static int8_t* cached_slice(int number, unsigned int off, unsigned int len,
                            uint32_t* play_out, unsigned short rate) {
    for (int i = 0; i < NCACHE; i++)
        if (g_cache[i].number == number && g_cache[i].buf) {
            *play_out = g_cache[i].play; return g_cache[i].buf;
        }
    int8_t* buf = load_slice(off, len);
    if (!buf) return nullptr;
    uint32_t play = len < 0x900 ? 0x900 : len;
    for (int i = 0; i < NCACHE; i++)
        if (g_cache[i].number == 0) {
            g_cache[i].number = number; g_cache[i].buf = buf;
            g_cache[i].play = play; g_cache[i].rate = rate; break;
        }
    *play_out = play; return buf;
}
```

- [ ] **Step 3: Use the cache in `saturn_sound_effect` and stop owning the buffer in slots**

In `saturn_sound_effect`, replace the `int8_t* buf = load_slice(off, len);` block and the `s.buf = buf;` assignment so the slot references the cached buffer without owning it:

```c
    uint32_t play = 0;
    int8_t* buf = cached_slice(number, off, len, &play, rate);
    if (!buf) return;
    Slot& s = g_slot[free];
    s.number = number; s.loops = loops; s.buf = nullptr;   /* cache owns the buffer */
    s.channel2 = -1;
    s.pcm.set(buf, play, rate);
```

(The subsequent volume/level/`Play` lines stay. Because `s.buf` is now `nullptr`, `free_slot` will no longer free the slice — the cache owns it.)

- [ ] **Step 4: Free the cache in `sound_stop_all`**

In `sound_stop_all`, after the existing `free_slot` loop, add:

```c
    for (int i = 0; i < NCACHE; i++) {
        if (g_cache[i].buf) { SRL::Memory::Free(g_cache[i].buf); g_cache[i].buf = nullptr; }
        g_cache[i].number = 0;
    }
```

- [ ] **Step 5: Add `sound_set_level`**

In `saturn/src/sound.cxx`, add a level global and setter, and scale the effect volume by it. Add near `g_enabled`:

```c
static int g_level = 4;   // PCM output level 0..7 (default 4)
extern "C" void sound_set_level(int level) {
    if (level < 0) level = 0; if (level > 7) level = 7;
    g_level = level;
    g_enabled = (level > 0) ? 1 : 0;
    if (!g_enabled) sound_stop_all();
}
```

Then in `saturn_sound_effect`, scale the computed `s.vol` by the level (7 = full):

```c
    s.vol = (uint8_t)(((int) s.vol) * g_level / 7);
    if (s.vol == 0) s.vol = 1;
```

Add this right AFTER the existing `s.vol = ...;` line.

- [ ] **Step 6: Build to verify**

Run (from `saturn/`): `./compile.bat debug 2>&1 | grep -iE "error|warning: .*sound\.|mojozork.bin" | head`
Expected: no `error`; `mojozork.bin` produced.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/sound.cxx saturn/src/sound.h
git commit -m "LH PCM slice cache (avoid CD-DA contention) + PCM level"
```

---

### Task 7: Options — two 7-level audio sliders (Music / PCM)

Replace the single "Sound: On/Off" row with two slider rows: Music (default 7) and PCM (default 4), range 0–7. Persist both in `MOJOOPTS`; map legacy blobs. Drive `music_set_level` and `sound_set_level`.

**Files:**
- Modify: `saturn/src/main.cxx` (`options_load`, `options_save`, `options_menu`, and the globals near `g_sound_on`)

**Interfaces:**
- Consumes: `music_set_level` (Task 4), `sound_set_level` (Task 6).
- Produces: persisted `g_music_level`, `g_pcm_level`; both applied on load and on change.

- [ ] **Step 1: Replace `g_sound_on` with two level globals**

In `saturn/src/main.cxx`, find `static bool g_sound_on` (or `int g_sound_on`) and replace with:

```c
static int g_music_level = 7;   // 0..7, default full
static int g_pcm_level   = 4;   // 0..7, default mid
```

- [ ] **Step 2: Update `options_load`**

In `options_load`, replace the sound-flag read (the `g_sound_on = (buf[i + 1] == 1) ? 0 : 1;` line and its comment) with two level bytes plus legacy fallback:

```c
    // Two audio levels follow the dial NUL: [music][pcm], each 0..7. A legacy
    // blob had a single sound flag here (1 = off, else on); map it: off -> pcm 0.
    if (i + 1 < (int) sizeof(buf)) {
        uint8_t a = buf[i + 1], b = (i + 2 < (int) sizeof(buf)) ? buf[i + 2] : 0xFF;
        if (b <= 7 && a <= 7) { g_music_level = a; g_pcm_level = b; }
        else { g_pcm_level = (a == 1) ? 0 : 4; g_music_level = 7; }  /* legacy */
    }
```

Note: the controller-mapping block in `options_load` reads bytes AFTER the sound region. Update its offset base `int m = i + 2;` to `int m = i + 3;` (two level bytes now occupy `i+1` and `i+2`).

- [ ] **Step 3: Update `options_save`**

In `options_save`, replace the single sound byte write:

```c
    buf[n++] = (uint8_t) g_music_level;
    buf[n++] = (uint8_t) g_pcm_level;
```

(These replace the old `buf[n++] = (uint8_t)(g_sound_on ? 2 : 1);` line. The controller-mapping bytes still follow.)

- [ ] **Step 4: Apply levels on load**

Where `options_load()` is called at startup and after it returns, apply both levels (find the existing `sound_set_enabled(g_sound_on);` call and replace):

```c
    music_set_level(g_music_level);
    sound_set_level(g_pcm_level);
```

- [ ] **Step 5: Rework the `options_menu` Sound row into two slider rows**

In `options_menu`, the item indices grow by one. Update `NITEMS` from 6 to 7 and the box height, and renumber: `0 Difficulty, 1 Configure, 2 Controls, 3 Return, 4 Music, 5 PCM, 6 Done`. Replace the single Sound handling and render:

Handling (replace the `sel == 4` sound toggle branch):

```c
        if (sel == 4) { if (left && g_music_level > 0) g_music_level--; if (right && g_music_level < 7) g_music_level++; }
        else if (sel == 5) { if (left && g_pcm_level > 0) g_pcm_level--; if (right && g_pcm_level < 7) g_pcm_level++; }
        else if (act) {
            if (sel == 1) { config_page(); }
            else if (sel == 2) { if (g_kbd_visible) controls_page(); else keyboard_controls_page(); }
            else if (sel == 3) { /* Return to Title (unchanged menu_confirm block) */ }
            else if (sel == 6) break;   // Done
        }
```

Render (replace the single Sound line; use fixed-x prints, no width flags):

```c
        SRL::Debug::Print(x0 + 2, y0 + 9,  "%c Music: %d", sel == 4 ? '>' : ' ', g_music_level);
        SRL::Debug::Print(x0 + 2, y0 + 10, "%c PCM:   %d", sel == 5 ? '>' : ' ', g_pcm_level);
        SRL::Debug::Print(x0 + 2, y0 + 11, "%c Done",      sel == 6 ? '>' : ' ');
        SRL::Debug::Print(x0 + 2, y0 + 12, "%s", hint("Up/Dn A=pick  <>=level", "Up/Dn Enter  B=back"));
```

Increase the box height `h` by 1 (from 13 to 14) and adjust `y0` if needed so the box fits; keep the Difficulty/Configure/Controller/Return rows at their current `y0+3..y0+8` positions (Return was `y0+8`).

- [ ] **Step 6: Apply levels when the menu changes them**

At the end of `options_menu`, where it currently detects changes and calls `sound_set_enabled`, replace with applying both levels and saving if either changed:

```c
    music_set_level(g_music_level);
    sound_set_level(g_pcm_level);
    // save if difficulty or either level changed (compare against snapshots taken
    // at menu open, like the existing diff snapshot)
```

Take snapshots `int music0 = g_music_level, pcm0 = g_pcm_level;` at the top of `options_menu` (next to `diff`), and in the save condition use `(diff_changed || g_music_level != music0 || g_pcm_level != pcm0)`.

- [ ] **Step 7: Build to verify**

Run (from `saturn/`): `./compile.bat debug 2>&1 | grep -iE "error|warning: .*main\.|mojozork.bin" | head`
Expected: no `error`; `mojozork.bin` produced.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Options: two 7-level audio sliders (Music/PCM), persist + apply"
```

---

### Task 8: Final integration build + on-device verification checklist

**Files:** none (verification only).

- [ ] **Step 1: Full clean-ish build**

Run (from `saturn/`): `./compile.bat debug 2>&1 | tail -5`
Expected: `mojozork.bin` produced, `No post build steps`.

- [ ] **Step 2: Re-run all host unit tests**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/mdt test/music_data_test.c saturn/src/music_data.c && /tmp/mdt && \
gcc -O2 -I saturn/src -o /tmp/mct test/music_classify_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/mct && \
gcc -O2 -I saturn/src -o /tmp/met test/music_engine_test.c saturn/src/music.c saturn/src/music_data.c && /tmp/met
```
Expected: `ALL PASS` three times.

- [ ] **Step 3: On-device / emulator manual checklist (record results)**

- With no audio tracks on the disc: game runs normally, no crash, silent (feature no-ops).
- Options shows Music and PCM sliders; adjusting each persists across a reboot.
- (After audio tracks are added to the disc) walking between rooms of different moods changes the track; same-mood rooms do not restart it; an uncategorized room keeps the current track.
- Lurking Horror: PCM sound effects still play; music continues with at most a one-time brief dip the first time each distinct sound occurs.

- [ ] **Step 4: Commit any doc/notes updates**

```bash
git add -A && git commit -m "Dynamic room audio: final build + verification notes" || true
```

---

## Self-Review

**Spec coverage:** §2 room detection → Task 5 hook (global 0). §3 categories → Task 1 enum. §4.1 track table → Task 1. §4.2/4.3 keyword/event tables → Task 1 + Task 2 classifiers. §5 engine → Task 3. §6.1 CDDA shim → Task 4. §6.2 sliders/persistence → Task 7. §7 per-game maps → Task 1 (`music_game_room_category`, empty) + Task 3 (engine consults it). §8 LH cache → Task 6. §9 error handling → Task 3 (`music_reset`), Task 4 (track 0/level 0 stop), Task 5 (guarded hook). §10 testing → Tasks 1–3 host tests + Task 8 checklist. Open items §11 (CD-DA volume symbol, slider range) → flagged in Task 4/Task 7.

**Placeholder scan:** No "TBD/TODO/handle edge cases" left. The only deferred specifics are (a) the exact SRL CD-DA volume symbol (Task 4 documents the fallback and where to look) and (b) the story-pointer symbol in `main.cxx` (Task 5 gives the concrete `hdr`-buffer alternative used by `read_game_info`). Both are resolved with named, existing references, not vague placeholders.

**Type consistency:** `music_play_fn`/`music_cdda_play` (int track) consistent across Tasks 1/3/4/5. `music_set_level`/`sound_set_level` (int level) consistent Tasks 4/6/7. `MusicKeyword`/`music_keywords`/`music_events` consistent Tasks 1/2. Engine symbols (`music_reset/set_backend/set_game/note_output/on_turn`) declared in Task 1's `music.h` and defined in Task 3, used in Task 5.
