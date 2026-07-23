#include "music.h"
#include <string.h>

/* Small LCG so track picks vary in play but are deterministic under a fixed seed
   (host tests seed it). */
static unsigned int g_rng = 0x1234567u;
void music_seed(unsigned int s) { g_rng = s ? s : 1u; }
static unsigned int rng_next(void) { g_rng = g_rng * 1103515245u + 12345u; return (g_rng >> 16) & 0x7fffu; }
static unsigned int rng_next_pub(void) { return rng_next(); }

int music_category_track(int category) {
    const unsigned char* p; int n = music_category_pool(category, &p);
    if (n <= 0) return 0;
    return p[rng_next() % (unsigned)n];
}

/* Lowercase a byte. */
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* True if `word` (stored lowercase) occurs in `text` as a whole word
   (case-insensitive). A word boundary is any non-alphabetic char or string end. */
static int has_word(const char* text, const char* word) {
    int wl = (int) strlen(word);
    for (const char* p = text; *p; p++) {
        int i = 0;
        while (i < wl && p[i] && lc(p[i]) == word[i]) i++;
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

static int g_mix_mode = MIX_DYNAMIC;
static int g_override_track = 10;
static int g_seq_track = MUSIC_TRACK_MIN;
static int g_active_cat = -1;           /* category currently sounding, -1 = none */
static int g_pending_cat = -1;          /* debounce: category waiting to commit */
static int g_pending_track = 0;
static int g_pending_frames = 0;
#define MUSIC_DEBOUNCE_FRAMES 180       /* ~3s @ 60fps: how long a new room must
                                           hold before its track takes over */
static int g_debounce_frames = MUSIC_DEBOUNCE_FRAMES;
static int (*g_isplaying)(void) = 0;
static int (*g_isshort)(int) = 0;

/* Seen-playing latch. Armed whenever the engine issues a play through the
   backend; gates loop-end detection until is_playing() has first gone true.
   The CD block spends several frames in SEEK right after PlaySingle where
   is_playing() reads 0 before the track has actually started, which would
   otherwise be misread as loop-end (runaway advance/re-roll/re-pick). */
static int g_await_play = 0;

void music_set_isplaying(int (*fn)(void)) { g_isplaying = fn; }
void music_set_isshort(int (*fn)(int)) { g_isshort = fn; }
void music_set_debounce_frames(int n) { g_debounce_frames = (n < 0) ? 0 : n; }
static int trk_is_short(int t) { return g_isshort ? g_isshort(t) : 0; }

/* Category the current Dynamic track was chosen for, so loop-end can tell "the
   place still sounds the same" (repeat the same track) from "the mood changed"
   (pick a fresh one). Room identity is deliberately NOT the test: walking from
   one cave to the next cave is the same music, and re-rolling there would shuffle
   the score under a player who has not left the category. */
static int g_track_cat = -1;

/* Play `track`: looped unless it is a short/play-once track. */
static void play_dyn(int track) {
    g_active_track = track;
    g_await_play = 1;
    g_track_cat = g_active_cat;
    if (g_play) g_play(track, trk_is_short(track) ? 0 : 1);
}
/* Pick a track from `cat`'s pool, preferring a non-short one and, where the pool
   allows it, one other than what is sounding now: a category change should be
   audible, so it cycles off the current track instead of possibly re-rolling it. */
static int pick_prefer_long(int cat) {
    const unsigned char* p; int n = music_category_pool(cat, &p);
    if (n <= 0) return 0;
    int longs[64], m = 0;
    for (int i = 0; i < n && m < 64; i++)
        if (!trk_is_short(p[i]) && p[i] != g_active_track) longs[m++] = p[i];
    if (m == 0)   /* every long track is the one playing (or the pool is all short) */
        for (int i = 0; i < n && m < 64; i++) if (!trk_is_short(p[i])) longs[m++] = p[i];
    if (m > 0) return longs[rng_next_pub() % (unsigned)m];
    return music_category_track(cat);
}

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
    g_active_cat = -1; g_pending_cat = -1; g_pending_track = 0; g_pending_frames = 0;
    g_seq_track = MUSIC_TRACK_MIN;
    g_await_play = 0;
    g_track_cat = -1;
    if (g_play) g_play(0, 0);   /* 0 = stop / keep-none */
}

void music_refresh(void) {
    if (g_active_track > 0 && g_play) {
        int loop = (g_mix_mode == MIX_OVERRIDE) ? 1 : (trk_is_short(g_active_track) ? 0 : 1);
        if (g_mix_mode == MIX_SEQUENTIAL || g_mix_mode == MIX_RANDOM) loop = 0;
        g_play(g_active_track, loop);
    }
}

void music_set_mix(int mode, int override_track) {
    g_mix_mode = mode;
    if (override_track >= MUSIC_TRACK_MIN && override_track <= MUSIC_TRACK_MAX)
        g_override_track = override_track;
}

static void play_seq_current(void) {
    g_active_track = g_seq_track;
    g_await_play = 1;
    if (g_play) g_play(g_seq_track, 0);   /* one-shot so tick advances on loop-end */
}
static void play_random_now(void) {
    int t = MUSIC_TRACK_MIN + (int)(rng_next_pub() % (unsigned)(MUSIC_TRACK_MAX - MUSIC_TRACK_MIN + 1));
    g_active_track = t;
    g_await_play = 1;
    if (g_play) g_play(t, 0);
}

void music_start(void) {
    g_active_cat = -1; g_pending_cat = -1; g_pending_track = 0;
    switch (g_mix_mode) {
        case MIX_OVERRIDE:
            g_active_track = g_override_track;
            g_await_play = 1;
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
    /* Gate loop-end detection through the seen-playing latch: ignore is_playing()
       until the just-issued track has actually started (clears the CD seek window). */
    if (g_await_play) {
        if (g_isplaying && g_isplaying()) g_await_play = 0;   /* track actually started */
        return;                                               /* ignore is_playing during the seek window */
    }
    /* Loop-end driven behavior (one-shot modes / short Dynamic track). */
    if (g_active_track > 0 && g_isplaying && !g_isplaying()) {
        if (g_mix_mode == MIX_SEQUENTIAL) {
            g_seq_track = (g_seq_track >= MUSIC_TRACK_MAX) ? MUSIC_TRACK_MIN : g_seq_track + 1;
            play_seq_current();
        } else if (g_mix_mode == MIX_RANDOM) {
            play_random_now();
        } else if (g_mix_mode == MIX_DYNAMIC && g_active_cat >= 0) {
            /* Still in the category this track was picked for: play it again,
               rather than shuffling under a player whose surroundings have not
               changed mood. A category change re-picks (see music_on_turn), so
               the fresh-pick arm here only covers a category that moved on
               without its own play -- keep it, and prefer a long track. */
            if (g_track_cat == g_active_cat)
                play_dyn(g_active_track);
            else
                play_dyn(pick_prefer_long(g_active_cat));
        }
        /* MIX_OVERRIDE loops; isplaying stays true; nothing to do. */
    }
}

void music_note_output(const char* str, unsigned int len) {
    for (unsigned int i = 0; i < len && str[i]; i++) {
        if (g_turn_len < MUSIC_TEXT_MAX - 1) g_turn_text[g_turn_len++] = str[i];
    }
    g_turn_text[g_turn_len] = 0;
}

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
        g_active_cat = target; play_dyn(pick_prefer_long(target));      /* first switch: immediate */
    } else if (target != g_pending_cat) {
        g_pending_cat = target;
        g_pending_track = pick_prefer_long(target);
        g_pending_frames = g_debounce_frames;             /* new target: (re)start countdown */
    }
    g_turn_len = 0; g_turn_text[0] = 0;
}
