#include <srl.hpp>
extern "C" {
#include "music.h"
}

// CD-DA output level (0..7; SRL's SetVolume max is 7). 0 = silence.
static int g_level = 7;
static int g_track = 0;   // currently requested track (0 = none)
static int g_loop  = 1;   // whether the current track loops

#define MUSIC_SHORT_SECONDS 15
#define MUSIC_SELECTABLE_MAX 32   /* Sound Options track selector shows at most this many audio tracks */

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

// 1 if the disc carries CD-DA audio. Track 1 (Tracks[0]) is the data track; the music
// begins at track 2 (Tracks[1]). The per-track Control nibble decodes reliably here, so
// we just ask whether track 2 is an audio track: yes -> the disc has music, no -> it's
// data-only and the audio UI stays hidden. Cached, since the TOC is static for the disc.
//
// Small SRL/BIOS bug worth knowing (this is why earlier detections were flaky): the TOC
// from CDC_TgetToc does not populate a track's entry unless the disc has a *pair* of
// tracks -- a lone track comes back as Unknown (Control 0x0f). Put another way, you need
// >= 2 tracks before track 1's entry fills in instead of reading Unknown. Our discs
// always carry track 1 (data) plus the audio tracks, so track 2's entry is always
// populated and this GetType() check is sound; only a hypothetical single-track disc
// would read Unknown, which we'd correctly treat as "no audio" anyway.
extern "C" int music_cdda_has_audio(void) {
    static int cached = -1;
    if (cached < 0) {
        SRL::Cd::TableOfContents toc = SRL::Cd::TableOfContents::GetTable();
        // Check if track 2 is audio
        cached = (toc.Tracks[1].GetType() == SRL::Cd::TableOfContents::TrackType::Audio) ? 1 : 0;
    }
    return cached;
}

// Ordered list of the selectable CD-DA (audio) track numbers. Track 1 is the data
// track; the audio tracks are CD tracks 2..MUSIC_TRACK_MAX, so display index 1 maps to
// CD track 2. The TOC can't count them (every field under-reports here), so once we know
// the disc HAS audio (music_cdda_has_audio) we offer the whole selectable range and let
// playback no-op for any track the disc doesn't actually carry -- PlaySingle() plays by
// number. Returns 0 (empty) for a disc with no CD-DA at all. Cached after first build.
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
