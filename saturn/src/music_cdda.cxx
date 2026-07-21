#include <srl.hpp>
extern "C" {
#include "music.h"
}

// CD-DA output level (0..7; SRL's SetVolume max is 7). 0 = silence.
static int g_level = 7;
static int g_track = 0;   // currently requested track (0 = none)
static int g_loop  = 1;   // whether the current track loops

#define MUSIC_SHORT_SECONDS 15

// ---- raw CD table of contents ----------------------------------------------
//
// SRL::Cd::TableOfContents cannot be used to read the TOC. TrackLocation derives
// from ITrack, so `Control` lives in its own 4-byte base subobject and
// sizeof(TrackLocation) is 8, not 4 -- the whole struct measures 812 bytes while
// CDC_TgetToc only ever writes the BIOS's 408-byte (102-longword) TOC into it.
// Consequences: toc.Tracks[t] reads TOC longword 2t (a different track than asked
// for), and every entry past t=50 is uninitialized stack that decodes as whatever
// happened to be there. That is what produced a ~40-entry track selector on a
// 32-track disc with most entries playing nothing. So read the TOC ourselves.
//
// Layout of CDC_TgetToc's 102 longwords:
//   [0..98]  one entry per CD track 1..99: (ctrladr << 24) | fad
//            absent tracks read 0xFFFFFFFF
//   [99]     first-track info: (ctrladr << 24) | (track number << 16) | ...
//   [100]    last-track  info: same layout
//   [101]    lead-out:         (ctrladr << 24) | fad
// ctrladr's high nibble is the control field: bit 2 set = data track, clear =
// audio track; 0x0f marks the entry as absent. FAD is 1/75s frames.
#define TOC_WORDS       102
#define TOC_FIRST_WORD  99
#define TOC_LAST_WORD   100
#define TOC_LEADOUT     101

static uint32_t g_toc[TOC_WORDS];
static int      g_toc_ready = 0;

static const uint32_t* toc_raw(void) {
    if (!g_toc_ready) { CDC_TgetToc(g_toc); g_toc_ready = 1; }
    return g_toc;
}
static int      toc_ctrl(uint32_t w)  { return (int)((w >> 28) & 0xfu); }
static uint32_t toc_fad(uint32_t w)   { return w & 0x00ffffffu; }
static int      toc_is_audio(uint32_t w) {
    int c = toc_ctrl(w);
    return (c != 0xf) && ((c & 0x4) == 0);
}
// Track number of the disc's first/last track, or 0 when the TOC reads bogus
// (no disc, or a read that came back before the drive was ready).
static int toc_track_no(int word) {
    int n = (int)((toc_raw()[word] >> 16) & 0xffu);
    return (n >= 1 && n <= 99) ? n : 0;
}

// Play `track`. loop=1 uses the CD block's native repeat (seamless, best-effort
// gapless loop); loop=0 plays once so the engine's music_tick() can detect
// completion via music_cdda_is_playing() and advance.
extern "C" void music_cdda_play_mode(int track, int loop) {
    g_track = track; g_loop = loop;
    if (track <= 0 || g_level == 0) { SRL::Sound::Cdda::StopPause(); return; }
    SRL::Sound::Cdda::SetVolume((uint8_t) g_level);
    SRL::Sound::Cdda::PlaySingle((uint16_t) track, loop != 0);
}

extern "C" void music_cdda_play(int track) { music_cdda_play_mode(track, 1); }

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

// 1 while a CD-DA track is playing, via the CD block's status register. A looped
// track (native repeat) stays in CDC_ST_PLAY forever; a one-shot track leaves
// CDC_ST_PLAY when it ends, which is the loop-end signal music_tick() relies on.
extern "C" int music_cdda_is_playing(void) {
    CdcStat stat;
    CDC_GetCurStat(&stat);
    return (CDC_GET_STC(&stat) == CDC_ST_PLAY) ? 1 : 0;
}

// Short = track duration under MUSIC_SHORT_SECONDS, from the TOC frame delta
// (this track's start to the next track's start; the last track measures against
// the lead-out). Cached, since the TOC is static for the life of the disc.
extern "C" int music_cdda_is_short(int track) {
    static signed char cache[100];   /* 0 unknown, 1 short, 2 long; index by CD track number */
    static int inited = 0;
    if (!inited) { for (int i = 0; i < 100; i++) cache[i] = 0; inited = 1; }
    if (track < 1 || track > 99) return 0;
    if (cache[track]) return cache[track] == 1;
    const uint32_t* toc = toc_raw();
    int last = toc_track_no(TOC_LAST_WORD);
    if (last == 0) return 0;                     // unreadable TOC: treat everything as long
    uint32_t start = toc_fad(toc[track - 1]);
    uint32_t end   = (track >= last) ? toc_fad(toc[TOC_LEADOUT]) : toc_fad(toc[track]);
    int frames = (int)(end - start);
    int is_short = (frames > 0 && frames < MUSIC_SHORT_SECONDS * 75) ? 1 : 0;   // frames<=0 treated as long
    cache[track] = is_short ? 1 : 2;
    return is_short;
}

// Ordered list of the disc's CD-DA track numbers -- real CD track numbers, the same
// ones PlaySingle() takes and the same ones the .cue lists. Walks only the tracks the
// TOC says exist (first-track record .. last-track record) so absent slots and the
// longwords past the end of the BIOS TOC are never consulted. On our discs track 1 is
// data and the music is tracks 2..N, so this comes back as {2,3,...,N}. Returns 0 for a
// disc with no CD-DA at all (or an unreadable TOC). Cached; the TOC is static.
extern "C" int music_cdda_audio_tracks(const unsigned char** out) {
    static unsigned char list[99];
    static int n = -1;
    if (n < 0) {
        const uint32_t* toc = toc_raw();
        int first = toc_track_no(TOC_FIRST_WORD), last = toc_track_no(TOC_LAST_WORD);
        n = 0;
        if (first != 0 && last != 0 && first <= last)
            for (int t = first; t <= last && n < 99; t++)
                if (toc_is_audio(toc[t - 1])) list[n++] = (unsigned char) t;
    }
    if (out) *out = (n > 0) ? list : 0;
    return n;
}

// 1 if the disc carries CD-DA audio at all; gates the audio rows in Sound Options.
extern "C" int music_cdda_has_audio(void) {
    return music_cdda_audio_tracks(0) > 0 ? 1 : 0;
}

// The track the CD block was last asked to play (0 = none). Sound Options opens its
// Track row on this so it shows what is actually sounding, rather than the saved
// selection.
extern "C" int music_cdda_current_track(void) { return g_track; }
