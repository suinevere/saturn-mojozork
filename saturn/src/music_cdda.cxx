#include <srl.hpp>
extern "C" {
#include "music.h"
}

// CD-DA output level (0..7; SRL's SetVolume max is 7). 0 = silence.
static int g_level = 7;
static int g_track = 0;   // currently requested track (0 = none)

extern "C" void music_set_level(int level) {
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    g_level = level;
    if (level == 0) {
        SRL::Sound::Cdda::StopPause();
    } else {
        SRL::Sound::Cdda::SetVolume((uint8_t) level);
        // If a track was requested while muted, (re)start it now.
        if (g_track > 0) SRL::Sound::Cdda::PlaySingle((uint16_t) g_track, true);
    }
}

extern "C" void music_cdda_play(int track) {
    g_track = track;
    if (track <= 0 || g_level == 0) { SRL::Sound::Cdda::StopPause(); return; }
    SRL::Sound::Cdda::SetVolume((uint8_t) g_level);
    SRL::Sound::Cdda::PlaySingle((uint16_t) track, /*loop=*/true);
}
