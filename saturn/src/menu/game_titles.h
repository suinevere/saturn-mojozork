/*----------------------
 | game_titles.h
 | Description: Maps a Z-machine story's header release+serial to a curated display
 |   title and a selection-menu category. The table is generated (game_titles.c);
 |   this header is its stable contract.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef GAME_TITLES_H
#define GAME_TITLES_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | GAME_CAT_* (enum)
 | Description: The game categories in selection-menu order (Other last);
 |   GAME_CAT_COUNT is the count.
 | Author: suinevere
 ----------------------*/
enum {
    GAME_CAT_ZORK = 0,      // The Zork Universe
    GAME_CAT_PLANETFALL,    // The Planetfall Series
    GAME_CAT_MYSTERY,       // The Mystery Series
    GAME_CAT_ADVENTURE,     // Tales of Adventure & Fantasy
    GAME_CAT_SCIFI,         // Sci-Fi & Horror
    GAME_CAT_COMEDY,         // anything else
    GAME_CAT_OTHER,         // anything else
    GAME_CAT_COUNT
};

/*----------------------
 | game_title / game_category
 | Description: title returns the curated display title for a story keyed by its
 |   header release (0x02) and serial (6 raw bytes at 0x12), or NULL if unknown;
 |   category returns its GAME_CAT_* (GAME_CAT_OTHER if unknown).
 | Author: suinevere
 ----------------------*/
const char* game_title(unsigned short release, const char* serial);
int game_category(unsigned short release, const char* serial);

#ifdef __cplusplus
}
#endif

#endif // GAME_TITLES_H
