/*----------------------
 | music.h
 | Description: The music engine's interface: mood categories, mix modes, and the
 |   track bounds; the tunable data-table accessors (music_data.c); the
 |   platform-independent engine (music.c); the pure classifiers exposed for
 |   tests; and the Saturn CD-DA backend (music_cdda.cxx).
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef MUSIC_H
#define MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | MUSIC_NUM_CATEGORIES / MC_* / MIX_* / MUSIC_TRACK_MIN / MUSIC_TRACK_MAX
 | Description: The mood categories (MC_NEUTRAL..MC_TRIUMPH) and their count; the
 |   Audio Mix modes from Sound Options; and the track bounds. MUSIC_TRACK_MAX is
 |   the ceiling for Sequential/Random and the override clamp -- a fixed offer, not
 |   a detected count (playing a missing track is a harmless no-op); the Sound
 |   Options track selector instead lists the disc's real tracks from
 |   music_cdda_audio_tracks().
 | Author: suinevere
 ----------------------*/
#define MUSIC_NUM_CATEGORIES 14
enum {
    MC_NEUTRAL = 0, MC_WILDERNESS, MC_UNDERGROUND, MC_WATER, MC_NAUTICAL,
    MC_TOWN, MC_DUNGEON, MC_DESERT, MC_MAGIC, MC_SCIFI, MC_HORROR,
    MC_MYSTERY, MC_DANGER, MC_TRIUMPH
};
enum { MIX_DYNAMIC = 0, MIX_OVERRIDE = 1, MIX_SEQUENTIAL = 2, MIX_RANDOM = 3 };
#define MUSIC_TRACK_MIN 2
#define MUSIC_TRACK_MAX 33

/*----------------------
 | MusicKeyword
 | Description: One keyword -> category mapping row (used by both the room-keyword
 |   and event-keyword tables).
 | Author: suinevere
 ----------------------*/
typedef struct { const char* word; unsigned char cat; } MusicKeyword;

/*----------------------
 | data-table accessors (music_data.c)
 | Description: music_keywords / music_events return the room-keyword (cats 1..11)
 |   and event-word (cats 12..13) tables and their lengths; music_category_pool
 |   returns a category's track pool (*out) and size; music_game_room_category
 |   returns a game's authored room category, or -1 if none.
 | Author: suinevere
 ----------------------*/
const MusicKeyword* music_keywords(int* n);
const MusicKeyword* music_events(int* n);
int music_category_pool(int category, const unsigned char** out);
int music_game_room_category(unsigned int release, const char* serial,
                             unsigned int room);

/*----------------------
 | music_play_fn
 | Description: The backend play callback: play CD-DA `track` (0 = stop), looping
 |   when loop is 1 and once when 0.
 | Author: suinevere
 ----------------------*/
typedef void (*music_play_fn)(int track, int loop);

/*----------------------
 | engine (music.c)
 | Description: The platform-independent engine. reset clears state; set_backend/
 |   set_game/note_output/on_turn feed it the backend, loaded game, turn text, and
 |   room; refresh re-asserts the current track; seed seeds the pool RNG;
 |   category_track picks a random pool track; set_mix/start/tick drive the mix
 |   state machine per frame; set_isplaying/set_isshort install the drive-state
 |   callbacks; set_debounce_frames tunes the room-switch debounce.
 | Author: suinevere
 ----------------------*/
void music_reset(void);
void music_set_backend(music_play_fn play);
void music_set_game(unsigned int release, const char* serial);
void music_note_output(const char* str, unsigned int len);
void music_on_turn(unsigned int room);
void music_refresh(void);   /* re-assert the current room's track (after a preview) */
void music_seed(unsigned int s);            /* seed the track-pool RNG */
int  music_category_track(int category);    /* random track from the category pool; 0 if none */
void music_set_mix(int mode, int override_track);      /* mix mode + selected/override track */
void music_start(void);                                /* assert playback for the current mode */
void music_tick(void);                                 /* per frame: commit/advance/re-pick */
void music_set_isplaying(int (*fn)(void));             /* backend: 1 = CD-DA still playing */
void music_set_isshort(int (*fn)(int track));          /* backend: 1 = track plays once */
void music_set_debounce_frames(int n);                 /* room-switch debounce length */

/*----------------------
 | classifiers (music.c, exposed for tests)
 | Description: classify_room returns a room's mood from its text (cat 1..11, or
 |   MC_NEUTRAL); scan_event returns an event category from turn text (cat 12/13,
 |   or -1).
 | Author: suinevere
 ----------------------*/
int music_classify_room(const char* text);
int music_scan_event(const char* text);

/*----------------------
 | CD-DA backend (music_cdda.cxx)
 | Description: play/play_mode start a track (0 = stop; looping or once);
 |   set_level/set_volume set the 0..7 output level (set_volume never restarts the
 |   track); is_playing/is_short report drive state; audio_tracks lists the disc's
 |   real audio track numbers (*out) and count; has_audio is 1 if the disc carries
 |   any; current_track is the last track handed to the CD block (0 = none).
 | Author: suinevere
 ----------------------*/
void music_cdda_play(int track);
void music_cdda_play_mode(int track, int loop);
void music_set_level(int level);
void music_set_volume(int level);
int  music_cdda_is_playing(void);
int  music_cdda_is_short(int track);
int  music_cdda_audio_tracks(const unsigned char** out);
int  music_cdda_has_audio(void);
int  music_cdda_current_track(void);

#ifdef __cplusplus
}
#endif
#endif /* MUSIC_H */
