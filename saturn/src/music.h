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

/* Audio Mix modes (Options > Sound Options). */
enum { MIX_DYNAMIC = 0, MIX_OVERRIDE = 1, MIX_SEQUENTIAL = 2, MIX_RANDOM = 3 };
#define MUSIC_TRACK_MIN 2
#define MUSIC_TRACK_MAX 32

typedef struct { const char* word; unsigned char cat; } MusicKeyword;

/* Data tables (music_data.c). */
const MusicKeyword* music_keywords(int* n);   /* room keywords, cats 1..11 */
const MusicKeyword* music_events(int* n);     /* event words, cats 12..13 */
int music_category_pool(int category, const unsigned char** out);  /* pool size; *out=tracks */
int music_game_room_category(unsigned int release, const char* serial,
                             unsigned int room);  /* override, -1 if none */

/* Backend callback: play a CD-DA track. loop: 1 = loop, 0 = play once. track 0 = stop. */
typedef void (*music_play_fn)(int track, int loop);

/* Engine (music.c). */
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

/* Pure classifiers, exposed for tests. */
int music_classify_room(const char* text);    /* cat 1..11, or MC_NEUTRAL */
int music_scan_event(const char* text);       /* cat 12/13, or -1 */

/* Saturn CD-DA backend (music_cdda.cxx). */
void music_cdda_play(int track);                 /* track 0 = stop; else play looping */
void music_cdda_play_mode(int track, int loop);  /* loop: 1 loop, 0 play once */
void music_set_level(int level);                 /* 0..7, 0 = silence */
void music_set_volume(int level);   /* 0..7 volume only; never restarts the track */
int  music_cdda_is_playing(void);                /* 1 = a CD-DA track is still playing */
int  music_cdda_is_short(int track);             /* 1 = track shorter than the short threshold */

#ifdef __cplusplus
}
#endif
#endif /* MUSIC_H */
