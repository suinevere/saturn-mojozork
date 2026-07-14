#include "music.h"
#include <string.h>

/* Room keywords -> categories 1..11 (best-effort; edit freely). Lowercase. */
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

/* Event words -> categories 12..13 (fire on any turn's text). Lowercase. */
static const MusicKeyword EV[] = {
    {"monster",MC_DANGER},{"troll",MC_DANGER},{"grue",MC_DANGER},{"attack",MC_DANGER},
    {"fight",MC_DANGER},{"flames",MC_DANGER},{"fire",MC_DANGER},{"burning",MC_DANGER},
    {"scream",MC_DANGER},{"danger",MC_DANGER},
    {"treasure",MC_TRIUMPH},{"gold",MC_TRIUMPH},{"jewel",MC_TRIUMPH},{"chest",MC_TRIUMPH},
    {"reward",MC_TRIUMPH},{"gleaming",MC_TRIUMPH},{"victory",MC_TRIUMPH},
};

/* Category -> CD-DA track. PROVISIONAL: these are distinct tracks 2..15 so each
   mood is audibly different, but they are NOT yet matched to the actual mood of
   each track. Re-map by ear -- edit these numbers to the track (2..32, per
   cd/music/tracklist) that fits each category. 0 = no track / keep current. */
static const unsigned char CATEGORY_TRACK[MUSIC_NUM_CATEGORIES] = {
    /* NEUTRAL     */  2,
    /* WILDERNESS  */  3,
    /* UNDERGROUND */  4,
    /* WATER       */  5,
    /* NAUTICAL    */  6,
    /* TOWN        */  7,
    /* DUNGEON     */  8,
    /* DESERT      */  9,
    /* MAGIC       */ 10,
    /* SCIFI       */ 11,
    /* HORROR      */ 12,
    /* MYSTERY     */ 13,
    /* DANGER      */ 14,
    /* TRIUMPH     */ 15,
};

/* Per-game room->category overrides, keyed by release+serial. Empty for v1;
   add rows later as data only. */
typedef struct {
    unsigned short release; const char* serial;
    const unsigned char* room_cat;   /* index = room object id, value = cat+1, 0 = none */
    int nrooms;
} MusicGameMap;
static const MusicGameMap GAME_MAPS[] = { { 0, 0, 0, 0 } };  /* sentinel, unused */

const MusicKeyword* music_keywords(int* n) { *n = (int)(sizeof KW / sizeof KW[0]); return KW; }
const MusicKeyword* music_events(int* n)   { *n = (int)(sizeof EV / sizeof EV[0]); return EV; }

int music_category_track(int category) {
    if (category < 0 || category >= MUSIC_NUM_CATEGORIES) return 0;
    return CATEGORY_TRACK[category];
}

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
