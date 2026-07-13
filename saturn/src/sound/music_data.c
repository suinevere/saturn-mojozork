/*----------------------
 | music_data.c
 | Description: The tunable data the music engine reads: room keyword -> category
 |   table, event keyword -> category table, per-category CD-DA track pools, and
 |   the (currently empty) per-game room->category override maps, plus the three
 |   accessors the engine calls to reach them. All of it is data meant to be
 |   edited freely; the engine logic lives in music.c.
 | Author: suinevere
 | Dependencies: music.h (MusicKeyword, MC_*, MUSIC_NUM_CATEGORIES), string.h
 ----------------------*/
#include "music.h"
#include <string.h>

/*----------------------
 | KW
 | Description: Room keywords mapped to mood categories (best-effort, lowercase).
 |   music_classify_room counts hits per category to guess a room's mood.
 | Author: suinevere
 ----------------------*/
static const MusicKeyword KW[] = {
    {"forest",MC_WILDERNESS},{"tree",MC_WILDERNESS},{"trees",MC_WILDERNESS},
    {"woods",MC_WILDERNESS},{"grove",MC_WILDERNESS},{"meadow",MC_WILDERNESS},
    {"field",MC_WILDERNESS},{"clearing",MC_WILDERNESS},{"path",MC_WILDERNESS},
    {"hill",MC_WILDERNESS},{"mountain",MC_WILDERNESS},{"garden",MC_WILDERNESS},

    {"cave",MC_UNDERGROUND},{"cavern",MC_UNDERGROUND},{"tunnel",MC_UNDERGROUND},
    {"underground",MC_UNDERGROUND},{"cellar",MC_UNDERGROUND},{"mine",MC_UNDERGROUND},
    {"passage",MC_UNDERGROUND},{"grotto",MC_UNDERGROUND},{"crawlway",MC_UNDERGROUND},

    {"river",MC_WATER},{"stream",MC_WATER},{"lake",MC_WATER},{"pool",MC_WATER},
    {"water",MC_WATER},{"waterfall",MC_WATER},{"shore",MC_WATER},{"bank",MC_WATER},
    {"underwater",MC_WATER},{"flooded",MC_WATER},

    {"ship",MC_NAUTICAL},{"boat",MC_NAUTICAL},{"deck",MC_NAUTICAL},{"cabin",MC_NAUTICAL},
    {"hull",MC_NAUTICAL},{"sea",MC_NAUTICAL},{"ocean",MC_NAUTICAL},{"dock",MC_NAUTICAL},
    {"harbor",MC_NAUTICAL},{"sail",MC_NAUTICAL},{"mast",MC_NAUTICAL},{"submarine",MC_NAUTICAL},

    {"house",MC_TOWN},{"hall",MC_TOWN},{"kitchen",MC_TOWN},{"office",MC_TOWN},
    {"street",MC_TOWN},{"building",MC_TOWN},{"stairs",MC_TOWN},{"parlor",MC_TOWN},
    {"bedroom",MC_TOWN},

    {"temple",MC_DUNGEON},{"tomb",MC_DUNGEON},{"crypt",MC_DUNGEON},{"ruin",MC_DUNGEON},
    {"altar",MC_DUNGEON},{"ancient",MC_DUNGEON},{"chamber",MC_DUNGEON},
    {"dungeon",MC_DUNGEON},{"catacomb",MC_DUNGEON},{"vault",MC_DUNGEON},

    {"desert",MC_DESERT},{"sand",MC_DESERT},{"dune",MC_DESERT},{"oasis",MC_DESERT},
    {"wasteland",MC_DESERT},

    {"spell",MC_MAGIC},{"magic",MC_MAGIC},{"enchant",MC_MAGIC},{"wizard",MC_MAGIC},
    {"scroll",MC_MAGIC},{"rune",MC_MAGIC},{"mystic",MC_MAGIC},{"sorcerer",MC_MAGIC},

    {"console",MC_SCIFI},{"computer",MC_SCIFI},{"airlock",MC_SCIFI},{"panel",MC_SCIFI},
    {"robot",MC_SCIFI},{"laboratory",MC_SCIFI},{"reactor",MC_SCIFI},{"corridor",MC_SCIFI},
    {"module",MC_SCIFI},{"cockpit",MC_SCIFI},

    {"corpse",MC_HORROR},{"rotting",MC_HORROR},{"stench",MC_HORROR},{"shadow",MC_HORROR},
    {"eerie",MC_HORROR},{"decay",MC_HORROR},{"skeleton",MC_HORROR},

    {"body",MC_MYSTERY},{"clue",MC_MYSTERY},{"murder",MC_MYSTERY},{"evidence",MC_MYSTERY},
    {"study",MC_MYSTERY},{"library",MC_MYSTERY},{"detective",MC_MYSTERY},{"locked",MC_MYSTERY},
};

/*----------------------
 | EV
 | Description: Event keywords (danger / triumph) mapped to categories, lowercase.
 |   music_scan_event fires on any turn's text to override the room's base mood.
 | Author: suinevere
 ----------------------*/
static const MusicKeyword EV[] = {
    {"monster",MC_DANGER},{"troll",MC_DANGER},{"grue",MC_DANGER},{"attack",MC_DANGER},
    {"fight",MC_DANGER},{"flames",MC_DANGER},{"fire",MC_DANGER},{"burning",MC_DANGER},
    {"scream",MC_DANGER},{"danger",MC_DANGER},
    {"treasure",MC_TRIUMPH},{"gold",MC_TRIUMPH},{"jewel",MC_TRIUMPH},{"chest",MC_TRIUMPH},
    {"reward",MC_TRIUMPH},{"gleaming",MC_TRIUMPH},{"victory",MC_TRIUMPH},
};

/*----------------------
 | P_* pools and CATEGORY_POOL
 | Description: Per-category pools of CD-DA track numbers (2..32); Dynamic mode
 |   picks one at random on a category change. The neutral pool is folded into
 |   every category, so neutral ambience can surface anywhere. POOL() pairs each
 |   array with its length into the CATEGORY_POOL table, indexed by MC_* id.
 | Author: suinevere
 ----------------------*/
static const unsigned char P_NEUTRAL[]     = {4,5,6,10,11,12,16,22,24,28,30};
static const unsigned char P_WILDERNESS[]  = {4,5,6,9,10,11,12,16,17,22,24,28,30,31};
static const unsigned char P_UNDERGROUND[] = {2,3,4,5,6,7,10,11,12,16,18,19,20,22,23,24,28,29,30};
static const unsigned char P_WATER[]       = {2,4,5,6,7,8,10,11,12,16,20,21,22,24,26,28,30};
static const unsigned char P_NAUTICAL[]    = {2,3,4,5,6,7,10,11,12,16,19,20,21,22,24,26,28,30};
static const unsigned char P_TOWN[]        = {4,5,6,9,10,11,12,16,22,24,28,30};
static const unsigned char P_DUNGEON[]     = {4,5,6,9,10,11,12,16,17,18,19,20,22,23,24,28,29,30};
static const unsigned char P_DESERT[]      = {4,5,6,9,10,11,12,16,22,24,28,30};
static const unsigned char P_MAGIC[]       = {4,5,6,8,10,11,12,16,18,19,21,22,23,24,26,28,29,30};
static const unsigned char P_SCIFI[]       = {3,4,5,6,8,10,11,12,14,15,16,18,19,22,23,24,27,28,30};
static const unsigned char P_HORROR[]      = {2,4,5,6,7,8,10,11,12,13,14,15,16,19,22,24,27,28,30};
static const unsigned char P_MYSTERY[]     = {3,4,5,6,8,10,11,12,15,16,21,22,24,27,28,30};
static const unsigned char P_DANGER[]      = {4,5,6,10,11,12,13,14,15,16,17,22,24,27,28,30};
static const unsigned char P_TRIUMPH[]     = {4,5,6,9,10,11,12,16,22,24,25,28,29,30};

#define POOL(a) { a, (unsigned char)(sizeof(a)/sizeof((a)[0])) }
static const struct { const unsigned char* p; unsigned char n; } CATEGORY_POOL[MUSIC_NUM_CATEGORIES] = {
    POOL(P_NEUTRAL), POOL(P_WILDERNESS), POOL(P_UNDERGROUND), POOL(P_WATER),
    POOL(P_NAUTICAL), POOL(P_TOWN), POOL(P_DUNGEON), POOL(P_DESERT),
    POOL(P_MAGIC), POOL(P_SCIFI), POOL(P_HORROR), POOL(P_MYSTERY),
    POOL(P_DANGER), POOL(P_TRIUMPH),
};
#undef POOL

/*----------------------
 | MusicGameMap / GAME_MAPS
 | Description: Per-game room->category overrides keyed by release+serial
 |   (room_cat[room] = cat+1, 0 = none). Empty for v1 -- just a sentinel row;
 |   real maps are added later as data only.
 | Author: suinevere
 ----------------------*/
typedef struct {
    unsigned short release; const char* serial;
    const unsigned char* room_cat;
    int nrooms;
} MusicGameMap;
static const MusicGameMap GAME_MAPS[] = { { 0, 0, 0, 0 } };

/*----------------------
 | music_keywords / music_events
 | Description: Hand back the room-keyword / event-keyword tables and their
 |   lengths.
 | Author: suinevere
 ----------------------*/
const MusicKeyword* music_keywords(int* n) { *n = (int)(sizeof KW / sizeof KW[0]); return KW; }
const MusicKeyword* music_events(int* n)   { *n = (int)(sizeof EV / sizeof EV[0]); return EV; }

/*----------------------
 | music_category_pool
 | Description: Returns a category's track pool and its length, or an empty result
 |   for an out-of-range category.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: CATEGORY_POOL
 | Params: category -- MC_* id; out -- receives the pool pointer (may be NULL)
 | Returns: the pool length (0 when out of range)
 ----------------------*/
int music_category_pool(int category, const unsigned char** out) {
    if (category < 0 || category >= MUSIC_NUM_CATEGORIES) { if (out) *out = 0; return 0; }
    if (out) *out = CATEGORY_POOL[category].p;
    return CATEGORY_POOL[category].n;
}

/*----------------------
 | music_game_room_category
 | Description: Looks up a room's authored category for the loaded game, matching
 |   a GAME_MAPS row by release and 6-char serial. With no map (the v1 default) or
 |   a room past the map's end, returns -1 so the engine falls back to keyword
 |   classification.
 | Author: suinevere
 | Dependencies: string.h (memcmp)
 | Globals: GAME_MAPS
 | Params: release -- Z-machine release; serial -- game serial; room -- room id
 | Returns: the MC_* category, or -1 when unmapped
 ----------------------*/
int music_game_room_category(unsigned int release, const char* serial, unsigned int room) {
    for (int i = 0; i < (int)(sizeof GAME_MAPS / sizeof GAME_MAPS[0]); i++) {
        const MusicGameMap* g = &GAME_MAPS[i];
        if (g->serial == 0) continue;
        if (g->release != release || memcmp(g->serial, serial, 6) != 0) continue;
        if ((int)room >= g->nrooms) return -1;
        int v = g->room_cat[room];
        return v ? v - 1 : -1;
    }
    return -1;
}
