// GENERATED FILE -- do not edit by hand.
// Produced by tools/typeahead/gen_solution.py.
//
// Per-game "solution" overlay: base-weight and transition boosts derived from a
// winning walkthrough, applied on top of the runtime grammar layer. Keyed by the
// story's release number + serial, so it only touches the game it was built for.

#include "typeahead_solution.h"
#include <string.h>

typedef struct { const char* w; short wt; } SolWord;
typedef struct { const char* a; const char* b; short wt; } SolLink;
typedef struct {
    unsigned short release; const char* serial;
    const SolWord* words; int nwords;
    const SolLink* links; int nlinks;
} Solution;

static const SolWord g0_words[] = { {"all",43}, {"bag",41}, {"bar",42}, {"basket",44}, {"bauble",42}, {"bell",42}, {"boat",42}, {"bolt",41}, {"book",43}, {"bracelet",41}, {"buoy",43}, {"button",41}, {"canary",43}, {"candles",43}, {"case",52}, {"chalice",42}, {"chimney",41}, {"climb",43}, {"close",41}, {"coal",43}, {"coffin",45}, {"cross",41}, {"diamond",42}, {"dig",44}, {"door",43}, {"down",68}, {"drop",54}, {"east",79}, {"echo",41}, {"egg",44}, {"emerald",41}, {"enter",45}, {"from",41}, {"garlic",44}, {"give",41}, {"go",100}, {"gold",42}, {"grate",42}, {"house",45}, {"in",55}, {"inflat",41}, {"inside",42}, {"invent",41}, {"jade",41}, {"kill",48}, {"knife",45}, {"lamp",48}, {"launch",41}, {"leaflet",42}, {"leave",41}, {"lid",43}, {"light",42}, {"machine",41}, {"mailbox",41}, {"match",42}, {"matche",41}, {"mirror",41}, {"move",41}, {"north",75}, {"northeast",45}, {"northwest",44}, {"off",42}, {"on",44}, {"open",52}, {"painting",42}, {"plastic",41}, {"pray",41}, {"pump",42}, {"push",41}, {"put",56}, {"railing",41}, {"rainbow",41}, {"read",42}, {"ring",41}, {"rope",42}, {"rub",41}, {"rug",41}, {"sand",44}, {"scarab",41}, {"sceptre",43}, {"screwdriver",46}, {"shovel",43}, {"skull",43}, {"south",73}, {"southeast",43}, {"southwest",47}, {"stiletto",41}, {"switch",41}, {"sword",47}, {"take",80}, {"thief",44}, {"tie",41}, {"to",42}, {"torch",43}, {"trap",43}, {"tree",42}, {"troll",45}, {"turn",48}, {"ulysse",41}, {"unlock",41}, {"up",63}, {"wait",43}, {"wave",41}, {"west",59}, {"wind",41}, {"window",41}, {"with",52}, {"wrench",43}, {"yellow",41} };
static const SolLink g0_links[] = { {"bar","in",58}, {"bauble","in",58}, {"bolt","with",58}, {"bracelet","in",58}, {"canary","from",58}, {"canary","in",58}, {"candles","with",58}, {"chalice","in",58}, {"climb","down",58}, {"climb","tree",66}, {"close","lid",58}, {"coal","in",58}, {"coffin","in",58}, {"cross","rainbow",58}, {"diamond","in",58}, {"dig","sand",82}, {"drop","all",58}, {"drop","book",58}, {"drop","buoy",58}, {"drop","coffin",58}, {"drop","garlic",58}, {"drop","knife",58}, {"drop","leaflet",58}, {"drop","pump",58}, {"drop","screwdriver",66}, {"drop","shovel",58}, {"drop","stiletto",58}, {"drop","sword",58}, {"drop","wrench",58}, {"egg","in",58}, {"egg","to",58}, {"enter","house",90}, {"from","egg",58}, {"give","egg",58}, {"go","down",96}, {"go","east",96}, {"go","inside",58}, {"go","north",96}, {"go","northeast",90}, {"go","northwest",82}, {"go","south",96}, {"go","southeast",74}, {"go","southwest",96}, {"go","up",96}, {"go","west",96}, {"gold","in",58}, {"in","basket",82}, {"in","case",96}, {"in","machine",58}, {"inflat","plastic",58}, {"inside","boat",58}, {"inside","case",58}, {"kill","thief",74}, {"kill","troll",90}, {"leave","boat",58}, {"light","candles",58}, {"light","match",58}, {"move","rug",58}, {"off","lamp",66}, {"on","lamp",82}, {"open","bag",58}, {"open","buoy",58}, {"open","case",58}, {"open","coffin",58}, {"open","grate",58}, {"open","lid",66}, {"open","mailbox",58}, {"open","trap",74}, {"open","window",58}, {"painting","inside",58}, {"plastic","with",58}, {"push","yellow",58}, {"put","bar",58}, {"put","bauble",58}, {"put","bracelet",58}, {"put","canary",58}, {"put","chalice",58}, {"put","coal",58}, {"put","coffin",58}, {"put","diamond",58}, {"put","egg",58}, {"put","gold",58}, {"put","painting",58}, {"put","sceptre",58}, {"put","screwdriver",58}, {"put","skull",58}, {"put","torch",66}, {"read","book",58}, {"read","leaflet",58}, {"ring","bell",58}, {"rope","to",58}, {"rub","mirror",58}, {"sceptre","in",58}, {"screwdriver","in",58}, {"skull","in",58}, {"switch","with",58}, {"take","all",66}, {"take","bar",58}, {"take","bauble",58}, {"take","bell",58}, {"take","book",58}, {"take","buoy",58}, {"take","canary",58}, {"take","candles",66}, {"take","chalice",58}, {"take","coal",66}, {"take","coffin",66}, {"take","diamond",58}, {"take","egg",58}, {"take","emerald",58}, {"take","garlic",74}, {"take","gold",58}, {"take","jade",58}, {"take","knife",58}, {"take","lamp",66}, {"take","matche",58}, {"take","painting",58}, {"take","rope",58}, {"take","scarab",58}, {"take","sceptre",58}, {"take","screwdriver",66}, {"take","shovel",66}, {"take","skull",66}, {"take","sword",58}, {"take","torch",58}, {"take","wrench",58}, {"thief","with",74}, {"tie","rope",58}, {"to","railing",58}, {"to","thief",58}, {"torch","in",66}, {"trap","door",74}, {"troll","with",90}, {"turn","bolt",58}, {"turn","off",66}, {"turn","on",82}, {"turn","switch",58}, {"unlock","grate",58}, {"up","canary",58}, {"up","chimney",58}, {"wave","sceptre",58}, {"wind","up",58}, {"with","knife",74}, {"with","match",58}, {"with","pump",58}, {"with","screwdriver",58}, {"with","sword",90}, {"with","wrench",58}, {"yellow","button",58} };

static const Solution SOLUTIONS[] = {
    { 88, "840726", g0_words, 109, g0_links, 149 },
};

void apply_solution_overlay(TrieNode* root, const unsigned char* story, unsigned int len) {
    if (len < 0x1a) return;
    unsigned short release = (unsigned short)((story[2] << 8) | story[3]);
    const char* serial = (const char*) (story + 0x12);
    for (int i = 0; i < (int)(sizeof(SOLUTIONS) / sizeof(SOLUTIONS[0])); i++) {
        if (SOLUTIONS[i].release != release) continue;
        if (memcmp(SOLUTIONS[i].serial, serial, 6) != 0) continue;
        for (int j = 0; j < SOLUTIONS[i].nwords; j++) {
            DictionaryWord* w = find_exact_word(root, SOLUTIONS[i].words[j].w);
            if (w && SOLUTIONS[i].words[j].wt > w->base_weight)
                w->base_weight = SOLUTIONS[i].words[j].wt;
        }
        for (int j = 0; j < SOLUTIONS[i].nlinks; j++) {
            DictionaryWord* a = find_exact_word(root, SOLUTIONS[i].links[j].a);
            DictionaryWord* b = find_exact_word(root, SOLUTIONS[i].links[j].b);
            if (a && b) add_next_word(a, b, SOLUTIONS[i].links[j].wt);
        }
        return;
    }
}
