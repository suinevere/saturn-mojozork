#include "music.h"
#include <string.h>

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
    if (g_play) g_play(0, 0);   /* 0 = stop / keep-none */
}

void music_refresh(void) {
    if (g_active_track > 0 && g_play) g_play(g_active_track, 1);
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
        if (g_play) g_play(track, 1);
    }

    g_turn_len = 0; g_turn_text[0] = 0;
}
