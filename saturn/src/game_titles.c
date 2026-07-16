// GENERATED FILE -- do not edit by hand.
// Produced by tools/gametitles/gen_titles.py.
//
// A display title for a Z-machine story, keyed by its header release number
// (0x02) + serial (0x12). The header carries no title, so this maps the known
// releases/serials of the Infocom games to a curated name.

#include "game_titles.h"
#include <string.h>

typedef struct { unsigned short release; const char* serial; const char* title; int cat; } GameTitle;

static const GameTitle TITLES[] = {
    { 16, "840518", "Enchanter (1983)", 0 },
    { 10, "830810", "Enchanter (1983)", 0 },
    { 15, "831107", "Enchanter (1983)", 0 },
    { 15, "999999", "Enchanter (1983)", 0 },
    { 16, "831118", "Enchanter (1983)", 0 },
    { 24, "851118", "Enchanter (1983)", 0 },
    { 29, "860820", "Enchanter (1983)", 0 },
    { 67, "000000", "Sorcerer (1984)", 0 },
    { 67, "831208", "Sorcerer (1984)", 0 },
    { 85, "840106", "Sorcerer (1984)", 0 },
    { 13, "851021", "Sorcerer (1984)", 0 },
    { 15, "851108", "Sorcerer (1984)", 0 },
    { 18, "860904", "Sorcerer (1984)", 0 },
    { 4, "840131", "Sorcerer (1984)", 0 },
    { 6, "840508", "Sorcerer (1984)", 0 },
    { 63, "850916", "Spellbreaker (1985)", 0 },
    { 86, "860829", "Spellbreaker (1985)", 0 },
    { 87, "860904", "Spellbreaker (1985)", 0 },
    { 119, "880429", "Zork I (1980)", 0 },
    { 23, "820428", "Zork I (1980)", 0 },
    { 25, "820515", "Zork I (1980)", 0 },
    { 26, "820803", "Zork I (1980)", 0 },
    { 28, "821013", "Zork I (1980)", 0 },
    { 30, "830330", "Zork I (1980)", 0 },
    { 75, "830929", "Zork I (1980)", 0 },
    { 76, "840509", "Zork I (1980)", 0 },
    { 88, "840726", "Zork I (1980)", 0 },
    { 22, "840518", "Zork II (1981)", 0 },
    { 15, "820308", "Zork II (1981)", 0 },
    { 17, "820427", "Zork II (1981)", 0 },
    { 18, "820512", "Zork II (1981)", 0 },
    { 18, "820517", "Zork II (1981)", 0 },
    { 19, "820721", "Zork II (1981)", 0 },
    { 22, "830331", "Zork II (1981)", 0 },
    { 23, "830411", "Zork II (1981)", 0 },
    { 48, "840904", "Zork II (1981)", 0 },
    { 63, "860811", "Zork II (1981)", 0 },
    { 15, "840518", "Zork III (1982)", 0 },
    { 10, "820818", "Zork III (1982)", 0 },
    { 12, "821025", "Zork III (1982)", 0 },
    { 15, "830331", "Zork III (1982)", 0 },
    { 16, "830410", "Zork III (1982)", 0 },
    { 17, "840727", "Zork III (1982)", 0 },
    { 25, "860811", "Zork III (1982)", 0 },
    { 1, "830517", "Planetfall (1983)", 1 },
    { 20, "830708", "Planetfall (1983)", 1 },
    { 26, "831014", "Planetfall (1983)", 1 },
    { 29, "840118", "Planetfall (1983)", 1 },
    { 37, "851003", "Planetfall (1983)", 1 },
    { 39, "880501", "Planetfall (1983)", 1 },
    { 63, "870218", "Stationfall (1987)", 1 },
    { 1, "861017", "Stationfall (1987)", 1 },
    { 87, "870326", "Stationfall (1987)", 1 },
    { 107, "870430", "Stationfall (1987)", 1 },
    { 18, "820311", "Deadline (1982)", 2 },
    { 19, "820427", "Deadline (1982)", 2 },
    { 21, "820512", "Deadline (1982)", 2 },
    { 22, "820809", "Deadline (1982)", 2 },
    { 26, "821108", "Deadline (1982)", 2 },
    { 27, "831005", "Deadline (1982)", 2 },
    { 28, "850129", "Deadline (1982)", 2 },
    { 13, "880501", "Moonmist (1986)", 2 },
    { 4, "860918", "Moonmist (1986)", 2 },
    { 9, "861022", "Moonmist (1986)", 2 },
    { 14, "841005", "Suspect (1984)", 2 },
    { 18, "850222", "Suspect (1984)", 2 },
    { 14, "000000", "Suspect (1984)", 2 },
    { 13, "830524", "The Witness (1983)", 2 },
    { 18, "830910", "The Witness (1983)", 2 },
    { 20, "831119", "The Witness (1983)", 2 },
    { 21, "831208", "The Witness (1983)", 2 },
    { 22, "840924", "The Witness (1983)", 2 },
    { 23, "840925", "The Witness (1983)", 2 },
    { 97, "851218", "Ballyhoo (1986)", 3 },
    { 99, "861014", "Ballyhoo (1986)", 3 },
    { 23, "840809", "Cutthroats (1984)", 3 },
    { 25, "840917", "Cutthroats (1984)", 3 },
    { 235, "861118", "Hollywood Hijinx (1986)", 3 },
    { 37, "861215", "Hollywood Hijinx (1986)", 3 },
    { 22, "840522", "Infidel (1983)", 3 },
    { 22, "830916", "Infidel (1983)", 3 },
    { 26, "870730", "Plundered Hearts (1987)", 3 },
    { 15, "840522", "Seastalker (1984)", 3 },
    { 17, "850208", "Seastalker (1984)", 3 },
    { 86, "840320", "Seastalker (1984)", 3 },
    { 15, "840612", "Seastalker (1984)", 3 },
    { 15, "840501", "Seastalker (1984)", 3 },
    { 16, "850515", "Seastalker (1984)", 3 },
    { 16, "850603", "Seastalker (1984)", 3 },
    { 18, "850919", "Seastalker (1984)", 3 },
    { 15, "840716", "Seastalker (1984)", 3 },
    { 68, "850501", "Wishbringer (1985)", 3 },
    { 69, "850920", "Wishbringer (1985)", 3 },
    { 32933, "880609", "Wishbringer (1985)", 3 },
    { 70, "880609", "Wishbringer (1985)", 3 },
    { 15, "820901", "Starcross (1982)", 4 },
    { 17, "821021", "Starcross (1982)", 4 },
    { 18, "830114", "Starcross (1982)", 4 },
    { 8, "840521", "Suspended (1983)", 4 },
    { 5, "830222", "Suspended (1983)", 4 },
    { 7, "830419", "Suspended (1983)", 4 },
    { 8, "830521", "Suspended (1983)", 4 },
    { 203, "870506", "The Lurking Horror (1987)", 4 },
    { 219, "870912", "The Lurking Horror (1987)", 4 },
    { 221, "870918", "The Lurking Horror (1987)", 4 },
    { 108, "840809", "Hitchhiker's Guide (1984)", 5 },
    { 119, "840822", "Hitchhiker's Guide (1984)", 5 },
    { 42, "850323", "Hitchhiker's Guide (1984)", 5 },
    { 47, "840914", "Hitchhiker's Guide (1984)", 5 },
    { 56, "841221", "Hitchhiker's Guide (1984)", 5 },
    { 58, "851002", "Hitchhiker's Guide (1984)", 5 },
    { 59, "851108", "Hitchhiker's Guide (1984)", 5 },
    { 60, "861002", "Hitchhiker's Guide (1984)", 5 },
    { 1, "840427", "Hypochondriac (1987)", 6 },
    { 10, "840826", " Hypochondriac (1987)", 6 },
    { 11, "870225", " Hypochondriac (1987)", 6 },
    { 2, "840505", " Hypochondriac (1987)", 6 },
    { 8, "870119", "Infocom Sampler (1987)", 6 },
    { 15, "840330", "Infocom Sampler (1984)", 6 },
    { 8, "870601", "Infocom Sampler (1987)", 6 },
    { 5, "840512", "Infocom Sampler (1984)", 6 },
    { 97, "870601", "Infocom Sampler (1987)", 6 },
    { 24, "840627", "Infocom Sampler (1984)", 6 },
    { 26, "840731", "Infocom Sampler (1984)", 6 },
    { 52, "850402", "Infocom Sampler (1985)", 6 },
    { 53, "850407", "Infocom Sampler (1985)", 6 },
    { 55, "850823", "Infocom Sampler (1985)", 6 },
    { 57, "860121", "Leather Goddesses of Phobos (1986)", 5 },
    { 118, "860325", "Leather Goddesses of Phobos (1986)", 5 },
    { 1, "851008", "Leather Goddesses of Phobos (1986)", 5 },
    { 160, "860521", "Leather Goddesses of Phobos (1986)", 5 },
    { 50, "860711", "Leather Goddesses of Phobos (1986)", 5 },
    { 59, "000001", "Leather Goddesses of Phobos (1986)", 5 },
    { 59, "860730", "Leather Goddesses of Phobos (1986)", 5 },
    { 2, "840207", "Mini-Zork I (1984)", 6 },
    { 34, "871124", "Mini-Zork I (1987)", 6 },
    { 2, "871123", "Mini-Zork II (1987)", 6 },
    { 1, "151001", "Colossal Cave Adventure (1977)", 0 },
};

static const GameTitle* find(unsigned short release, const char* serial) {
    for (int i = 0; i < (int)(sizeof(TITLES) / sizeof(TITLES[0])); i++)
        if (TITLES[i].release == release && memcmp(TITLES[i].serial, serial, 6) == 0)
            return &TITLES[i];
    return 0;
}

// Title for (release, serial), or NULL if unknown. `serial` is the 6 raw header
// bytes at 0x12 (not necessarily NUL-terminated).
const char* game_title(unsigned short release, const char* serial) {
    const GameTitle* g = find(release, serial);
    return g ? g->title : 0;
}

// Category for (release, serial); GAME_CAT_OTHER if unknown.
int game_category(unsigned short release, const char* serial) {
    const GameTitle* g = find(release, serial);
    return g ? g->cat : GAME_CAT_OTHER;
}
