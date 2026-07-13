/*----------------------
 | music_cdda.cxx
 | Description: The Saturn CD-DA backend behind the platform-independent music
 |   engine: play/stop/volume against SRL's Cdda block, plus a hand-decoded CD
 |   table of contents used to enumerate the disc's audio tracks and measure each
 |   track's length. It reads the raw BIOS TOC directly because
 |   SRL::Cd::TableOfContents is unusable here (see the TOC section box).
 | Author: suinevere
 | Dependencies: SRL (Sound::Cdda, CDC_* BIOS TOC/status calls), music.h (the
 |   backend/query signatures the engine calls through)
 ----------------------*/
#include <srl.hpp>
extern "C" {
#include "music.h"
}

/*----------------------
 | g_level / g_track / g_loop
 | Description: Backend state. g_level is the CD-DA output level (0..7, SRL's
 |   SetVolume max; 0 = silence). g_track is the currently requested track (0 =
 |   none) and g_loop whether it loops.
 | Author: suinevere
 ----------------------*/
static int g_level = 7;
static int g_track = 0;
static int g_loop  = 1;

#define MUSIC_SHORT_SECONDS 15

/*----------------------
 | raw CD table of contents (g_toc / TOC_* / the toc_* helpers)
 | Description: SRL::Cd::TableOfContents cannot read the TOC: TrackLocation derives
 |   from ITrack, so Control sits in its own 4-byte base subobject and
 |   sizeof(TrackLocation) is 8 not 4 -- the struct measures 812 bytes while
 |   CDC_TgetToc only writes the BIOS's 408-byte (102-longword) TOC. So toc.Tracks[t]
 |   reads longword 2t (the wrong track) and entries past t=50 are uninitialized
 |   stack; that produced a ~40-entry selector on a 32-track disc with most entries
 |   silent. This code reads the 102-longword TOC itself. Layout: [0..98] one entry
 |   per CD track 1..99 as (ctrladr<<24)|fad (absent tracks read 0xFFFFFFFF); [99]
 |   first-track and [100] last-track info as (ctrladr<<24)|(track<<16)|...; [101]
 |   lead-out as (ctrladr<<24)|fad. ctrladr's high nibble is the control field:
 |   bit 2 set = data track, clear = audio; 0x0f marks the entry absent. FAD is
 |   1/75s frames. toc_raw lazily fetches and caches; toc_ctrl/toc_fad/toc_is_audio
 |   decode one longword; toc_track_no reads a first/last record's track number (0
 |   when the TOC reads bogus -- no disc, or a read before the drive was ready).
 | Author: suinevere
 ----------------------*/
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
static int toc_track_no(int word) {
    int n = (int)((toc_raw()[word] >> 16) & 0xffu);
    return (n >= 1 && n <= 99) ? n : 0;
}

/*----------------------
 | music_cdda_play_mode
 | Description: The engine's backend play callback. loop=1 uses the CD block's
 |   native repeat (seamless, best-effort gapless loop); loop=0 plays once so
 |   music_tick can detect completion via music_cdda_is_playing and advance. A
 |   track of 0 or a muted level stops output instead.
 | Author: suinevere
 | Dependencies: SRL (Sound::Cdda)
 | Globals: g_track, g_loop, g_level
 | Params: track -- CD track number (<=0 stops); loop -- nonzero to repeat
 | Returns: N/A
 ----------------------*/
extern "C" void music_cdda_play_mode(int track, int loop) {
    g_track = track; g_loop = loop;
    if (track <= 0 || g_level == 0) { SRL::Sound::Cdda::StopPause(); return; }
    SRL::Sound::Cdda::SetVolume((uint8_t) g_level);
    SRL::Sound::Cdda::PlaySingle((uint16_t) track, loop != 0);
}

/*----------------------
 | music_cdda_play
 | Description: Convenience wrapper that plays `track` looped.
 | Author: suinevere
 ----------------------*/
extern "C" void music_cdda_play(int track) { music_cdda_play_mode(track, 1); }

/*----------------------
 | music_set_volume
 | Description: Sets the CD-DA level (clamped 0..7). Level 0 stops output outright
 |   with no restart; any other level applies the volume live.
 | Author: suinevere
 | Dependencies: SRL (Sound::Cdda)
 | Globals: g_level
 | Params: level -- requested level, clamped to 0..7
 | Returns: N/A
 ----------------------*/
extern "C" void music_set_volume(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    g_level = level;
    if (level == 0) SRL::Sound::Cdda::StopPause();
    else            SRL::Sound::Cdda::SetVolume((uint8_t) level);
}

/*----------------------
 | music_set_level
 | Description: Applies a new level and, only on the mute->unmute edge (the track
 |   was actually stopped at 0), restarts the last requested track so the music
 |   resumes.
 | Author: suinevere
 | Dependencies: SRL (Sound::Cdda)
 | Globals: g_level, g_track, g_loop
 | Params: level -- requested level
 | Returns: N/A
 ----------------------*/
extern "C" void music_set_level(int level) {
    int was_muted = (g_level == 0);
    music_set_volume(level);
    if (was_muted && level > 0 && g_track > 0)
        SRL::Sound::Cdda::PlaySingle((uint16_t) g_track, g_loop != 0);
}

/*----------------------
 | music_cdda_is_playing
 | Description: The engine's is_playing callback, read from the CD block status
 |   register. A looped (native repeat) track stays in CDC_ST_PLAY forever; a
 |   one-shot track leaves CDC_ST_PLAY when it ends -- the loop-end signal
 |   music_tick relies on.
 | Author: suinevere
 | Dependencies: SRL (CDC status)
 | Globals: N/A
 | Params: N/A
 | Returns: 1 while a CD-DA track is playing, else 0
 ----------------------*/
extern "C" int music_cdda_is_playing(void) {
    CdcStat stat;
    CDC_GetCurStat(&stat);
    return (CDC_GET_STC(&stat) == CDC_ST_PLAY) ? 1 : 0;
}

/*----------------------
 | music_cdda_is_short
 | Description: The engine's is_short callback: true when the track runs under
 |   MUSIC_SHORT_SECONDS, measured from the TOC frame delta (this track's start to
 |   the next track's start; the last track measures against the lead-out). A
 |   non-positive delta is treated as long, and an unreadable TOC treats every
 |   track as long. Cached per CD track, since the TOC is static for the disc.
 | Author: suinevere
 | Dependencies: SRL (via the TOC helpers)
 | Globals: g_toc (via toc_raw)
 | Params: track -- CD track number (1..99)
 | Returns: 1 if short, 0 if long or out of range
 ----------------------*/
extern "C" int music_cdda_is_short(int track) {
    static signed char cache[100];
    static int inited = 0;
    if (!inited) { for (int i = 0; i < 100; i++) cache[i] = 0; inited = 1; }
    if (track < 1 || track > 99) return 0;
    if (cache[track]) return cache[track] == 1;
    const uint32_t* toc = toc_raw();
    int last = toc_track_no(TOC_LAST_WORD);
    if (last == 0) return 0;
    uint32_t start = toc_fad(toc[track - 1]);
    uint32_t end   = (track >= last) ? toc_fad(toc[TOC_LEADOUT]) : toc_fad(toc[track]);
    int frames = (int)(end - start);
    int is_short = (frames > 0 && frames < MUSIC_SHORT_SECONDS * 75) ? 1 : 0;
    cache[track] = is_short ? 1 : 2;
    return is_short;
}

/*----------------------
 | music_cdda_audio_tracks
 | Description: Returns the disc's CD-DA track numbers in order -- real CD track
 |   numbers, the same ones PlaySingle and the .cue use. Walks only the tracks the
 |   TOC says exist (first-track record .. last-track record) so absent slots and
 |   the longwords past the end of the BIOS TOC are never consulted. On our discs
 |   track 1 is data and the music is 2..N, so this comes back as {2,3,...,N}.
 |   Cached; the TOC is static.
 | Author: suinevere
 | Dependencies: SRL (via the TOC helpers)
 | Globals: g_toc (via toc_raw)
 | Params: out -- receives a pointer to the internal list (or 0 when none)
 | Returns: the number of audio tracks (0 for a data-only or unreadable disc)
 ----------------------*/
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

/*----------------------
 | music_cdda_has_audio
 | Description: True when the disc carries any CD-DA audio; gates the audio rows
 |   in Sound Options.
 | Author: suinevere
 ----------------------*/
extern "C" int music_cdda_has_audio(void) {
    return music_cdda_audio_tracks(0) > 0 ? 1 : 0;
}

/*----------------------
 | music_cdda_current_track
 | Description: The track the CD block was last asked to play (0 = none). Sound
 |   Options opens its Track row on this so it shows what is actually sounding
 |   rather than the saved selection.
 | Author: suinevere
 ----------------------*/
extern "C" int music_cdda_current_track(void) { return g_track; }
