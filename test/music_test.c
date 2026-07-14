/* Host unit tests for the pure-C music engine (data tables, classifiers, and the
   per-turn state machine). Build:
     gcc -O2 -I saturn/src -o /tmp/mt test/music_test.c \
         saturn/src/music.c saturn/src/music_data.c && /tmp/mt */
#include <stdio.h>
#include "music.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

int main(void) {
    int fails = 0;

    /* --- data tables: per-category pools --- */
    {
        const unsigned char* p;
        int n = music_category_pool(MC_NEUTRAL, &p);
        CHECK(n == 11);
        int has30 = 0; for (int i = 0; i < n; i++) { CHECK(p[i] >= 2 && p[i] <= 32); if (p[i] == 30) has30 = 1; }
        CHECK(has30);
        /* Neutral is merged into every other category: track 4 (Neutral) appears everywhere. */
        for (int c = MC_WILDERNESS; c <= MC_TRIUMPH; c++) {
            int m = music_category_pool(c, &p);
            CHECK(m > 0);
            int has4 = 0; for (int i = 0; i < m; i++) if (p[i] == 4) has4 = 1;
            CHECK(has4);
        }
        CHECK(music_category_pool(-1, &p) == 0);
        CHECK(music_category_pool(MUSIC_NUM_CATEGORIES, &p) == 0);
    }
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

    /* --- RNG-backed category pick --- */
    music_seed(12345);
    for (int t = 0; t < 50; t++) {
        int tr = music_category_track(MC_MAGIC);
        const unsigned char* p; int n = music_category_pool(MC_MAGIC, &p);
        int member = 0; for (int i = 0; i < n; i++) if (p[i] == tr) member = 1;
        CHECK(member);
    }
    CHECK(music_category_track(-1) == 0);
    /* Same seed -> same sequence (deterministic for tests). */
    music_seed(777); int a1 = music_category_track(MC_HORROR);
    music_seed(777); int a2 = music_category_track(MC_HORROR);
    CHECK(a1 == a2);

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
