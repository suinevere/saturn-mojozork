/* Host unit tests for the pure-C music engine (data tables, classifiers, and the
   per-turn state machine). Build:
     gcc -O2 -I saturn/src -o /tmp/mt test/music_test.c \
         saturn/src/music.c saturn/src/music_data.c && /tmp/mt */
#include <stdio.h>
#include "music.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

static int g_last_track, g_calls;
static void rec(int track) { g_last_track = track; g_calls++; }

int main(void) {
    int fails = 0;

    /* --- data tables --- */
    CHECK(music_category_track(MC_NEUTRAL) == 2);
    CHECK(music_category_track(MC_TRIUMPH) == 15);
    CHECK(music_category_track(-1) == 0);
    CHECK(music_category_track(MUSIC_NUM_CATEGORIES) == 0);
    int nk = 0; const MusicKeyword* kw = music_keywords(&nk);
    CHECK(kw && nk > 0);
    for (int i = 0; i < nk; i++) CHECK(kw[i].cat >= MC_WILDERNESS && kw[i].cat <= MC_MYSTERY);
    int ne = 0; const MusicKeyword* ev = music_events(&ne);
    CHECK(ev && ne > 0);
    for (int i = 0; i < ne; i++) CHECK(ev[i].cat == MC_DANGER || ev[i].cat == MC_TRIUMPH);
    CHECK(music_game_room_category(88, "840726", 5) == -1);

    /* --- classifiers --- */
    CHECK(music_classify_room("You are in a damp cave. A narrow tunnel leads north.") == MC_UNDERGROUND);
    CHECK(music_classify_room("A sunny forest clearing, tall trees all around.") == MC_WILDERNESS);
    CHECK(music_classify_room("The airlock hisses. A console blinks on the corridor wall.") == MC_SCIFI);
    CHECK(music_classify_room("The wizard's study is lined with scroll racks; a rune glows.") == MC_MAGIC);
    CHECK(music_classify_room("Nothing in particular here.") == MC_NEUTRAL);
    CHECK(music_classify_room("A CAVERN yawns below.") == MC_UNDERGROUND);
    CHECK(music_classify_room("You scavenge the bins.") == MC_NEUTRAL);   /* no 'cave' false hit */
    CHECK(music_scan_event("A hideous monster lunges to attack!") == MC_DANGER);
    CHECK(music_scan_event("A pile of gold and a jewel gleam here.") == MC_TRIUMPH);
    CHECK(music_scan_event("You wait.") == -1);

    /* --- engine --- */
    music_set_backend(rec);
    music_set_game(0, "000000");
    music_reset();
    g_last_track = -99; g_calls = 0;

    music_note_output("You are in a dark cave with a tunnel.", 37);
    music_on_turn(10);
    CHECK(g_last_track == music_category_track(MC_UNDERGROUND));   /* 4 */

    int calls_before = g_calls;
    music_on_turn(10);                                            /* same room, no text */
    CHECK(g_calls == calls_before);                              /* kept current */

    music_note_output("A forest clearing among tall trees.", 35);
    music_on_turn(11);
    CHECK(g_last_track == music_category_track(MC_WILDERNESS));   /* 3 */

    music_note_output("A troll leaps out to attack!", 28);
    music_on_turn(11);
    CHECK(g_last_track == music_category_track(MC_DANGER));       /* 14, override */

    music_note_output("An empty stone landing.", 23);            /* NEUTRAL text */
    music_on_turn(10);
    CHECK(g_last_track == music_category_track(MC_UNDERGROUND));  /* cached, 4 */

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
