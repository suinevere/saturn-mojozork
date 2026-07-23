/*----------------------
 | music.c
 | Description: The platform-independent music engine: room-text classification
 |   into mood categories, the mix-mode state machine (Dynamic / Override /
 |   Sequential / Random), and the loop-end / debounce logic that decides when to
 |   change track. It owns no hardware -- a backend play callback (set by the
 |   Saturn client, or by the host tests) does the actual playing, and is_playing
 |   / is_short callbacks report drive state -- so this file builds and unit-tests
 |   on the host.
 | Author: suinevere
 | Dependencies: music.h (categories, mix modes, track bounds, keyword/pool/
 |   room-category data accessors), string.h
 ----------------------*/
#include "music.h"
#include <string.h>

/*----------------------
 | g_rng
 | Description: LCG state, so track picks vary in play yet stay deterministic
 |   under a fixed seed (the host tests seed it).
 | Author: suinevere
 ----------------------*/
static unsigned int g_rng = 0x1234567u;

/*----------------------
 | music_seed
 | Description: Sets the LCG seed, forcing a nonzero value so the sequence never
 |   degenerates.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_rng
 | Params: s -- requested seed (0 is coerced to 1)
 | Returns: N/A
 ----------------------*/
void music_seed(unsigned int s) { g_rng = s ? s : 1u; }

/*----------------------
 | rng_next / rng_next_pub
 | Description: Advances the LCG and returns a 15-bit value. rng_next_pub is a
 |   thin wrapper used where a distinct call site reads clearer.
 | Author: suinevere
 ----------------------*/
static unsigned int rng_next(void) { g_rng = g_rng * 1103515245u + 12345u; return (g_rng >> 16) & 0x7fffu; }
static unsigned int rng_next_pub(void) { return rng_next(); }

/*----------------------
 | music_category_track
 | Description: Picks a uniformly random track from a category's pool.
 | Author: suinevere
 | Dependencies: music.h (music_category_pool)
 | Globals: g_rng (via rng_next)
 | Params: category -- MC_* category id
 | Returns: a track number from the pool, or 0 if the pool is empty
 ----------------------*/
int music_category_track(int category) {
    const unsigned char* p; int n = music_category_pool(category, &p);
    if (n <= 0) return 0;
    return p[rng_next() % (unsigned)n];
}

/*----------------------
 | lc
 | Description: Lowercases one ASCII byte.
 | Author: suinevere
 ----------------------*/
static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/*----------------------
 | has_word
 | Description: Case-insensitive whole-word search: true when `word` (stored
 |   lowercase) occurs in `text` bounded on both sides by a non-alphabetic char or
 |   a string end, so "cave" does not match "caverns".
 | Author: suinevere
 | Dependencies: string.h
 | Globals: N/A
 | Params: text -- haystack; word -- lowercase needle
 | Returns: 1 on a whole-word match, 0 otherwise
 ----------------------*/
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

/*----------------------
 | music_classify_room
 | Description: Scores room text against the keyword table and returns the
 |   category with the most keyword hits, defaulting to MC_NEUTRAL on a tie at
 |   zero. This is the fallback when a game has no per-room category map.
 | Author: suinevere
 | Dependencies: music.h (music_keywords, MC_* / MUSIC_NUM_CATEGORIES)
 | Globals: N/A
 | Params: text -- the room's description text (NULL -> MC_NEUTRAL)
 | Returns: the winning MC_* category
 ----------------------*/
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

/*----------------------
 | music_scan_event
 | Description: Looks for an event keyword (combat, death, etc.) in the turn text,
 |   returning the first match's category so an event track can override the
 |   room's base mood for that turn.
 | Author: suinevere
 | Dependencies: music.h (music_events)
 | Globals: N/A
 | Params: text -- the turn's output text (NULL -> no event)
 | Returns: the event category, or -1 when none is present
 ----------------------*/
int music_scan_event(const char* text) {
    if (!text) return -1;
    int ne = 0; const MusicKeyword* ev = music_events(&ne);
    for (int i = 0; i < ne; i++) if (has_word(text, ev[i].word)) return ev[i].cat;
    return -1;
}

/* ---- engine state ---- */
#define MUSIC_TEXT_MAX 512

/*----------------------
 | engine state (g_play .. g_turn_len)
 | Description: The core engine state. g_play is the backend play callback; the
 |   room block (g_release/g_serial identify the loaded game for its per-room
 |   category map; g_have_room/g_cur_room track the last classified room;
 |   g_base_cat is that room's mood and g_event_cat a per-room event override,
 |   -1 = none); g_active_track is what is sounding (0 = nothing yet); g_room_cache
 |   memoizes classify results (0 = unseen, else cat+1); g_turn_text/g_turn_len
 |   accumulate the current turn's output for classification.
 | Author: suinevere
 ----------------------*/
static music_play_fn g_play = 0;
static unsigned int  g_release = 0;
static char          g_serial[8] = {0};
static int           g_have_room = 0;
static unsigned int  g_cur_room = 0;
static int           g_base_cat = MC_NEUTRAL;
static int           g_event_cat = -1;
static int           g_active_track = 0;
static unsigned char g_room_cache[256];
static char          g_turn_text[MUSIC_TEXT_MAX];
static int           g_turn_len = 0;

/*----------------------
 | mix state (g_mix_mode .. g_isshort)
 | Description: Mix-mode selection and its bookkeeping. g_mix_mode is the active
 |   MIX_*; g_override_track/g_seq_track carry the Override and Sequential
 |   positions; g_active_cat is the category currently sounding (-1 = none), with
 |   g_pending_cat/g_pending_track/g_pending_frames the debounced switch waiting
 |   to commit. MUSIC_DEBOUNCE_FRAMES (~3s @ 60fps) is how long a new room must
 |   hold before its track takes over; g_debounce_frames is the runtime override
 |   (host tests shorten it). g_isplaying/g_isshort are the drive-state callbacks.
 | Author: suinevere
 ----------------------*/
static int g_mix_mode = MIX_DYNAMIC;
static int g_override_track = 10;
static int g_seq_track = MUSIC_TRACK_MIN;
static int g_active_cat = -1;
static int g_pending_cat = -1;
static int g_pending_track = 0;
static int g_pending_frames = 0;
#define MUSIC_DEBOUNCE_FRAMES 180
static int g_debounce_frames = MUSIC_DEBOUNCE_FRAMES;
static int (*g_isplaying)(void) = 0;
static int (*g_isshort)(int) = 0;

/*----------------------
 | g_await_play
 | Description: Seen-playing latch. Armed whenever the engine issues a play, it
 |   gates loop-end detection until is_playing() has first gone true. The CD block
 |   spends several frames in SEEK right after PlaySingle where is_playing() reads
 |   0 before the track has actually started, which would otherwise be misread as
 |   loop-end (runaway advance/re-roll/re-pick).
 | Author: suinevere
 ----------------------*/
static int g_await_play = 0;

/*----------------------
 | music_set_isplaying / music_set_isshort / music_set_debounce_frames
 | Description: Install the drive-state callbacks and override the debounce
 |   length (clamped to >= 0).
 | Author: suinevere
 ----------------------*/
void music_set_isplaying(int (*fn)(void)) { g_isplaying = fn; }
void music_set_isshort(int (*fn)(int)) { g_isshort = fn; }
void music_set_debounce_frames(int n) { g_debounce_frames = (n < 0) ? 0 : n; }

/*----------------------
 | trk_is_short
 | Description: True when the is_short callback marks `t` as a short/play-once
 |   track; false when no callback is installed.
 | Author: suinevere
 ----------------------*/
static int trk_is_short(int t) { return g_isshort ? g_isshort(t) : 0; }

/*----------------------
 | g_track_cat
 | Description: The category the current Dynamic track was chosen for, so loop-end
 |   can tell "the place still sounds the same" (repeat the track) from "the mood
 |   changed" (pick a fresh one). Room identity is deliberately NOT the test:
 |   walking from one cave to the next is the same music, and re-rolling there
 |   would shuffle the score under a player who has not left the category.
 | Author: suinevere
 ----------------------*/
static int g_track_cat = -1;

/*----------------------
 | play_dyn
 | Description: Plays a Dynamic-mode track: records it as active, arms the
 |   seen-playing latch, tags it with the current category, and plays it looped
 |   unless it is a short/play-once track.
 | Author: suinevere
 | Dependencies: N/A (calls the backend via g_play)
 | Globals: g_active_track, g_await_play, g_track_cat, g_active_cat, g_play
 | Params: track -- track number to play
 | Returns: N/A
 ----------------------*/
static void play_dyn(int track) {
    g_active_track = track;
    g_await_play = 1;
    g_track_cat = g_active_cat;
    if (g_play) g_play(track, trk_is_short(track) ? 0 : 1);
}

/*----------------------
 | pick_prefer_long
 | Description: Chooses a track from `cat`'s pool, preferring a non-short one and,
 |   where the pool allows, one other than what is sounding now -- a category
 |   change should be audible, so it cycles off the current track instead of
 |   possibly re-rolling it. Falls back to any long track if the only long option
 |   is the current one, then to any track at all.
 | Author: suinevere
 | Dependencies: music.h (music_category_pool)
 | Globals: g_active_track (read)
 | Params: cat -- MC_* category to pick from
 | Returns: a track number, or 0 if the pool is empty
 ----------------------*/
static int pick_prefer_long(int cat) {
    const unsigned char* p; int n = music_category_pool(cat, &p);
    if (n <= 0) return 0;
    int longs[64], m = 0;
    for (int i = 0; i < n && m < 64; i++)
        if (!trk_is_short(p[i]) && p[i] != g_active_track) longs[m++] = p[i];
    if (m == 0)
        for (int i = 0; i < n && m < 64; i++) if (!trk_is_short(p[i])) longs[m++] = p[i];
    if (m > 0) return longs[rng_next_pub() % (unsigned)m];
    return music_category_track(cat);
}

/*----------------------
 | music_set_backend
 | Description: Installs the backend play callback (track, loop) the engine drives.
 | Author: suinevere
 ----------------------*/
void music_set_backend(music_play_fn play) { g_play = play; }

/*----------------------
 | music_set_game
 | Description: Records the loaded game's release number and (truncated) serial so
 |   music_on_turn can consult that game's per-room category map.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_release, g_serial
 | Params: release -- Z-machine release word; serial -- 6-char serial (may be short)
 | Returns: N/A
 ----------------------*/
void music_set_game(unsigned int release, const char* serial) {
    g_release = release;
    for (int i = 0; i < 6 && serial && serial[i]; i++) g_serial[i] = serial[i];
    g_serial[6] = 0;
}

/*----------------------
 | music_reset
 | Description: Clears all room / mix / latch state back to first-boot values and
 |   tells the backend to stop. Called for a new game and on soft-reset re-entry
 |   so a stale engine cannot leak a track into the menu.
 | Author: suinevere
 | Dependencies: N/A (stops via g_play)
 | Globals: nearly all engine/mix state
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_reset(void) {
    g_have_room = 0; g_cur_room = 0; g_base_cat = MC_NEUTRAL; g_event_cat = -1;
    g_active_track = 0; g_turn_len = 0; g_turn_text[0] = 0;
    for (int i = 0; i < 256; i++) g_room_cache[i] = 0;
    g_active_cat = -1; g_pending_cat = -1; g_pending_track = 0; g_pending_frames = 0;
    g_seq_track = MUSIC_TRACK_MIN;
    g_await_play = 0;
    g_track_cat = -1;
    if (g_play) g_play(0, 0);
}

/*----------------------
 | music_refresh
 | Description: Re-issues the active track to the backend with the correct loop
 |   flag for the current mix mode (Override always loops; Sequential/Random are
 |   one-shot; otherwise loop unless the track is short). Used to re-assert
 |   playback after something else touched the drive.
 | Author: suinevere
 | Dependencies: N/A (plays via g_play)
 | Globals: g_active_track, g_mix_mode, g_play
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_refresh(void) {
    if (g_active_track > 0 && g_play) {
        int loop = (g_mix_mode == MIX_OVERRIDE) ? 1 : (trk_is_short(g_active_track) ? 0 : 1);
        if (g_mix_mode == MIX_SEQUENTIAL || g_mix_mode == MIX_RANDOM) loop = 0;
        g_play(g_active_track, loop);
    }
}

/*----------------------
 | music_set_mix
 | Description: Selects the mix mode and, when a valid track is given, records it
 |   as the Override/Sequential start track.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mix_mode, g_override_track
 | Params: mode -- MIX_*; override_track -- start track (ignored if out of range)
 | Returns: N/A
 ----------------------*/
void music_set_mix(int mode, int override_track) {
    g_mix_mode = mode;
    if (override_track >= MUSIC_TRACK_MIN && override_track <= MUSIC_TRACK_MAX)
        g_override_track = override_track;
}

/*----------------------
 | play_seq_current / play_random_now
 | Description: Start the current Sequential track, or a fresh random track,
 |   one-shot (so music_tick advances on loop-end) and arm the seen-playing latch.
 | Author: suinevere
 ----------------------*/
static void play_seq_current(void) {
    g_active_track = g_seq_track;
    g_await_play = 1;
    if (g_play) g_play(g_seq_track, 0);
}
static void play_random_now(void) {
    int t = MUSIC_TRACK_MIN + (int)(rng_next_pub() % (unsigned)(MUSIC_TRACK_MAX - MUSIC_TRACK_MIN + 1));
    g_active_track = t;
    g_await_play = 1;
    if (g_play) g_play(t, 0);
}

/*----------------------
 | music_start
 | Description: Begins playback for the selected mix mode: Override plays its
 |   track looped (honoring repeat even if short), Sequential and Random start
 |   their first one-shot track, and Dynamic waits -- the first music_on_turn
 |   drives it off the room.
 | Author: suinevere
 | Dependencies: N/A (plays via g_play)
 | Globals: g_mix_mode, g_active_cat, g_pending_*, g_active_track, g_seq_track
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_start(void) {
    g_active_cat = -1; g_pending_cat = -1; g_pending_track = 0;
    switch (g_mix_mode) {
        case MIX_OVERRIDE:
            g_active_track = g_override_track;
            g_await_play = 1;
            if (g_play) g_play(g_override_track, 1);
            break;
        case MIX_SEQUENTIAL:
            g_seq_track = g_override_track; play_seq_current(); break;
        case MIX_RANDOM:
            play_random_now(); break;
        case MIX_DYNAMIC: default:
            break;
    }
}

/*----------------------
 | music_tick
 | Description: One engine frame. First commits a pending Dynamic category switch
 |   once its debounce elapses. Then, while the seen-playing latch is armed, it
 |   ignores is_playing() until the just-issued track has actually started (this
 |   clears the CD seek window that would otherwise read as loop-end). On a real
 |   loop-end it advances Sequential (wrapping at MUSIC_TRACK_MAX), re-rolls
 |   Random, and for Dynamic replays the same track while still in its category
 |   (rather than shuffling under a player who has not changed mood) or, if the
 |   category moved on without its own play, picks a fresh long track. Override
 |   loops on its own, so there is nothing to do.
 | Author: suinevere
 | Dependencies: N/A (plays via g_play, reads g_isplaying)
 | Globals: g_pending_*, g_await_play, g_active_track, g_active_cat, g_track_cat,
 |   g_seq_track, g_mix_mode
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void music_tick(void) {
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
    if (g_await_play) {
        if (g_isplaying && g_isplaying()) g_await_play = 0;
        return;
    }
    if (g_active_track > 0 && g_isplaying && !g_isplaying()) {
        if (g_mix_mode == MIX_SEQUENTIAL) {
            g_seq_track = (g_seq_track >= MUSIC_TRACK_MAX) ? MUSIC_TRACK_MIN : g_seq_track + 1;
            play_seq_current();
        } else if (g_mix_mode == MIX_RANDOM) {
            play_random_now();
        } else if (g_mix_mode == MIX_DYNAMIC && g_active_cat >= 0) {
            if (g_track_cat == g_active_cat)
                play_dyn(g_active_track);
            else
                play_dyn(pick_prefer_long(g_active_cat));
        }
    }
}

/*----------------------
 | music_note_output
 | Description: Appends up to MUSIC_TEXT_MAX-1 bytes of the turn's output text to
 |   the classification buffer, stopping at a NUL, so music_on_turn can read the
 |   full turn.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_turn_text, g_turn_len
 | Params: str -- output text; len -- its length
 | Returns: N/A
 ----------------------*/
void music_note_output(const char* str, unsigned int len) {
    for (unsigned int i = 0; i < len && str[i]; i++) {
        if (g_turn_len < MUSIC_TEXT_MAX - 1) g_turn_text[g_turn_len++] = str[i];
    }
    g_turn_text[g_turn_len] = 0;
}

/*----------------------
 | music_on_turn
 | Description: The Dynamic-mode decision made once per turn (a no-op that just
 |   clears the buffer in other modes). Determines the target category -- an event
 |   keyword overrides the room's base mood, which comes from the game's per-room
 |   map, else a memoized keyword classification. If the target already sounds it
 |   keeps the stream; on the very first switch it plays immediately; otherwise it
 |   arms a debounced pending switch (restarting the countdown when the target
 |   changes), so brief passes through a room do not thrash the music.
 | Author: suinevere
 | Dependencies: music.h (music_game_room_category)
 | Globals: g_mix_mode, g_turn_text, g_cur_room, g_have_room, g_base_cat,
 |   g_event_cat, g_room_cache, g_active_cat, g_active_track, g_pending_*
 | Params: room -- the current room number
 | Returns: N/A
 ----------------------*/
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
        g_pending_cat = -1; g_pending_track = 0;
    } else if (g_active_track == 0) {
        g_active_cat = target; play_dyn(pick_prefer_long(target));
    } else if (target != g_pending_cat) {
        g_pending_cat = target;
        g_pending_track = pick_prefer_long(target);
        g_pending_frames = g_debounce_frames;
    }
    g_turn_len = 0; g_turn_text[0] = 0;
}
