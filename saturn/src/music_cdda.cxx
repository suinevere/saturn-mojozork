#include <srl.hpp>
extern "C" {
#include "music.h"
}

// CD-DA output level (0..7; SRL's SetVolume max is 7). 0 = silence.
static int g_level = 7;
static int g_track = 0;   // currently requested track (0 = none)
static int g_loop  = 1;   // whether the current track loops

#define MUSIC_SHORT_SECONDS 15
#define MUSIC_SELECTABLE_MAX 32   /* Sound Options track selector shows at most this many (display 1..32 -> CD tracks 2..33) */

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

// Short = track duration under MUSIC_SHORT_SECONDS, computed from the CD table of
// contents (track length = next track's start - this track's start, in 1/75s
// frames). Path taken: TOC frame-delta via SRL::Cd::TableOfContents. Cached after
// first read since the TOC is static for the life of the disc.
extern "C" int music_cdda_is_short(int track) {
    static signed char cache[35];   /* 0 unknown, 1 short, 2 long; index by track (2..33) */
    static int inited = 0;
    if (!inited) { for (int i = 0; i < 35; i++) cache[i] = 0; inited = 1; }
    if (track < 2 || track > 33) return 0;
    if (cache[track]) return cache[track] == 1;
    SRL::Cd::TableOfContents toc = SRL::Cd::TableOfContents::GetTable();
    int frames = (int)toc.Tracks[track + 1].FrameAddress - (int)toc.Tracks[track].FrameAddress;
    int is_short = (frames > 0 && frames < MUSIC_SHORT_SECONDS * 75) ? 1 : 0;   // frames<=0 (last track / lead-out) treated as long
    cache[track] = is_short ? 1 : 2;
    return is_short;
}

// Ordered list of the disc's selectable CD-DA (audio) track numbers. Track 1 is the
// data track; the audio tracks follow it, so display index 1 maps to CD track 2.
// We do NOT trust GetType() here: on this target the TOC Control bits read 0 for
// every slot, so GetType() reports Audio for all 99 entries (data track + phantom
// unused slots), which is what made the old scan run to 99. Instead we enumerate
// real audio tracks starting at track 2, bounded by the disc's reported last track,
// and hard-cap the selectable list at MUSIC_SELECTABLE_MAX (display maxes at 30).
// Cached after the first read.
extern "C" int music_cdda_audio_tracks(const unsigned char** out) {
    static unsigned char list[MUSIC_SELECTABLE_MAX];
    static int n = -1;
    if (n < 0) {
        n = 0;
        SRL::Cd::TableOfContents toc = SRL::Cd::TableOfContents::GetTable();
        int last = toc.LastTrack.Number;         // real last track (a single reliable field)
        int hi = 1 + MUSIC_SELECTABLE_MAX;       // cap: CD tracks 2..31 -> display 1..30
        if (last >= 2 && last < hi) hi = last;   // shorter disc: don't list past its end
        for (int t = 2; t <= hi && n < MUSIC_SELECTABLE_MAX; t++)
            list[n++] = (unsigned char) t;
    }
    if (out) *out = (n > 0) ? list : 0;
    return n;
}
