/* Host tests for mix modes, category-keyed debounce, and short-track re-pick.
   gcc -O2 -I saturn/src -o /tmp/mmt test/music_mix_test.c \
       saturn/src/music.c saturn/src/music_data.c && /tmp/mmt */
#include <stdio.h>
#include "music.h"

#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } }while(0)

static int g_track = 0, g_loop = 0, g_calls = 0;
static void rec(int track, int loop) { g_track = track; g_loop = loop; g_calls++; }
static int playing = 1;
static int isplaying(void) { return playing; }
static int short_set[64];
static int isshort(int t) { return (t >= 0 && t < 64) ? short_set[t] : 0; }
static int in_pool(int cat, int tr) {
    const unsigned char* p; int n = music_category_pool(cat, &p);
    for (int i = 0; i < n; i++) if (p[i] == tr) return 1;
    return 0;
}

int main(void) {
    int fails = 0;
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_set_backend(rec);
    music_set_isplaying(isplaying);
    music_set_isshort(isshort);
    music_seed(1);

    /* --- Dynamic: first room commits immediately (nothing playing yet) --- */
    music_set_mix(MIX_DYNAMIC, 10);
    music_reset();
    music_set_debounce_frames(6);
    g_calls = 0;
    music_note_output("You are in a dark cave with a tunnel.", 37);
    music_on_turn(10);
    CHECK(g_calls == 1);
    CHECK(in_pool(MC_UNDERGROUND, g_track));
    CHECK(g_loop == 1);
    int first_track = g_track;

    /* Same category room: smooth, no new play, no restart. */
    g_calls = 0;
    music_note_output("Another damp cavern passage.", 28);
    music_on_turn(11);
    CHECK(g_calls == 0);

    /* Different category: pending, does NOT play until the countdown elapses. */
    g_calls = 0;
    music_note_output("A sunny forest clearing, tall trees around.", 43);
    music_on_turn(12);
    CHECK(g_calls == 0);            /* still debouncing */
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_calls == 0);            /* not yet (6 frames) */
    music_tick();
    CHECK(g_calls == 1);            /* committed on the 6th tick */
    CHECK(in_pool(MC_WILDERNESS, g_track));

    /* A category flip before commit resets the countdown to the newest target. */
    music_set_mix(MIX_DYNAMIC, 10);
    music_reset(); music_set_debounce_frames(6);
    music_note_output("A cave tunnel.", 14); music_on_turn(20);   /* immediate underground */
    music_note_output("A forest.", 9); music_on_turn(21);         /* pending wilderness */
    music_tick(); music_tick();                                   /* 2 frames */
    music_note_output("An airlock console corridor.", 28); music_on_turn(22); /* flip -> scifi, reset */
    g_calls = 0;
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_calls == 0);
    music_tick();
    CHECK(g_calls == 1);
    CHECK(in_pool(MC_SCIFI, g_track));

    /* --- Override: plays the override track looped, ignores rooms --- */
    music_set_mix(MIX_OVERRIDE, 7);
    music_reset(); g_calls = 0;
    music_start();
    CHECK(g_track == 7 && g_loop == 1);
    music_note_output("A cave.", 6); music_on_turn(30);
    CHECK(g_track == 7);           /* unchanged by room */

    /* --- Sequential: one-shot; advances on loop-end (isplaying==0) --- */
    music_set_mix(MIX_SEQUENTIAL, 5);
    music_reset(); music_start();
    CHECK(g_track == 5 && g_loop == 0);
    playing = 1; music_tick();     /* track registers as playing -> latch clears */
    playing = 0; music_tick();     /* track ended -> advance */
    CHECK(g_track == 6 && g_loop == 0);
    playing = 1; music_tick();     /* still playing -> no change */
    CHECK(g_track == 6);
    /* wrap at MAX (bounds come from music.h, so raising the track max can't
       silently leave this asserting the old wrap point) */
    music_set_mix(MIX_SEQUENTIAL, MUSIC_TRACK_MAX); music_reset(); music_start();
    CHECK(g_track == MUSIC_TRACK_MAX);
    playing = 1; music_tick();     /* settle */
    playing = 0; music_tick();
    CHECK(g_track == MUSIC_TRACK_MIN);
    playing = 1;

    /* --- Random: one-shot; picks within the track range on loop-end --- */
    music_set_mix(MIX_RANDOM, 10); music_reset(); music_start();
    CHECK(g_track >= MUSIC_TRACK_MIN && g_track <= MUSIC_TRACK_MAX && g_loop == 0);
    playing = 1; music_tick();      /* settle */
    playing = 0; int r0 = g_track; music_tick();
    CHECK(g_track >= MUSIC_TRACK_MIN && g_track <= MUSIC_TRACK_MAX);
    playing = 1; (void)r0;

    /* --- Short Dynamic track, same room: repeats itself --- */
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_set_mix(MIX_DYNAMIC, 10); music_reset(); music_set_debounce_frames(0);
    /* Make every UNDERGROUND track short except one, force first pick short via seed search. */
    { const unsigned char* p; int n = music_category_pool(MC_UNDERGROUND, &p);
      for (int i = 0; i < n; i++) short_set[p[i]] = 1;
      short_set[p[n-1]] = 0;   /* exactly one long track */
    }
    music_note_output("A cave tunnel passage.", 22); music_on_turn(40);
    /* first pick may be short -> played one-shot */
    if (isshort(g_track)) CHECK(g_loop == 0);
    int room40_track = g_track;
    playing = 1; music_tick();     /* track registers as playing -> latch clears */
    playing = 0; music_tick();     /* short ended, player has not moved -> same track again */
    CHECK(g_track == room40_track);
    playing = 1;

    /* --- Same category, new room: loop-end re-picks (prefer long) --- */
    music_note_output("Another cave tunnel passage.", 28); music_on_turn(41);
    CHECK(g_track == room40_track);  /* the move alone does not interrupt the stream */
    playing = 1; music_tick();       /* repeat registers as playing -> latch clears */
    playing = 0; music_tick();       /* the room moved on -> fresh pick for the new place */
    CHECK(in_pool(MC_UNDERGROUND, g_track));
    CHECK(isshort(g_track) == 0);    /* prefers the non-short track */
    CHECK(g_loop == 1);
    playing = 1;

    /* --- Anti-runaway: no advance during the CD seek window ---
       Right after PlaySingle the CD block sits in SEEK for several frames and
       is_playing() reads 0 before the track has ever registered as playing.
       The engine must NOT treat that as loop-end (which caused runaway skips /
       re-rolls / re-picks). Advance only after is_playing() has first gone true. */
    music_set_mix(MIX_SEQUENTIAL, 5);
    music_reset(); music_start();
    CHECK(g_track == 5);
    int seek_track = g_track;
    playing = 0;                        /* simulate the SEEK window after PlaySingle */
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_track == seek_track);       /* must NOT advance during the seek window */
    playing = 1; music_tick();          /* track settles -> latch clears */
    playing = 0; music_tick();          /* real loop-end -> advance now */
    CHECK(g_track == 6);
    playing = 1;

    /* Random: no re-roll during the seek window. */
    music_set_mix(MIX_RANDOM, 10); music_reset(); music_start();
    int rseek = g_track;
    playing = 0;
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_track == rseek);            /* no re-roll mid-seek */
    playing = 1; music_tick();          /* settle */
    playing = 0; g_calls = 0; music_tick();
    CHECK(g_calls == 1);               /* re-rolls on real loop-end */
    playing = 1;

    /* Dynamic: the active room track is not re-picked during the seek window. */
    for (int i = 0; i < 64; i++) short_set[i] = 0;
    music_set_mix(MIX_DYNAMIC, 10); music_reset(); music_set_debounce_frames(0);
    music_note_output("A cave tunnel passage.", 22); music_on_turn(50);
    int dseek = g_track;
    playing = 0; g_calls = 0;
    for (int i = 0; i < 5; i++) music_tick();
    CHECK(g_track == dseek);            /* not re-picked mid-seek */
    CHECK(g_calls == 0);               /* no backend play issued during the seek window */
    playing = 1;

    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
