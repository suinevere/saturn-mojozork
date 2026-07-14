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

typedef struct { const char* word; unsigned char cat; } MusicKeyword;

/* Data tables (music_data.c). */
const MusicKeyword* music_keywords(int* n);   /* room keywords, cats 1..11 */
const MusicKeyword* music_events(int* n);     /* event words, cats 12..13 */
int music_category_track(int category);       /* CD-DA track, 0 = keep current */
int music_game_room_category(unsigned int release, const char* serial,
                             unsigned int room);  /* override, -1 if none */

/* Backend callback: play a CD-DA track looping. track 0 = keep current. */
typedef void (*music_play_fn)(int track);

/* Engine (music.c). */
void music_reset(void);
void music_set_backend(music_play_fn play);
void music_set_game(unsigned int release, const char* serial);
void music_note_output(const char* str, unsigned int len);
void music_on_turn(unsigned int room);
void music_refresh(void);   /* re-assert the current room's track (after a preview) */

/* Pure classifiers, exposed for tests. */
int music_classify_room(const char* text);    /* cat 1..11, or MC_NEUTRAL */
int music_scan_event(const char* text);       /* cat 12/13, or -1 */

/* Saturn CD-DA backend (music_cdda.cxx). */
void music_cdda_play(int track);   /* track 0 = stop; else play looping */
void music_set_level(int level);   /* 0..7, 0 = silence */

#ifdef __cplusplus
}
#endif
#endif /* MUSIC_H */
