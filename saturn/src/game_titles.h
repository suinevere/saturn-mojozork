#ifndef GAME_TITLES_H
#define GAME_TITLES_H

#ifdef __cplusplus
extern "C" {
#endif

// Game categories, in selection-menu order (Other is last).
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

// Display title for a Z-machine story, keyed by its header release number (0x02)
// and serial (the 6 raw bytes at 0x12). Returns NULL if the game is unknown.
const char* game_title(unsigned short release, const char* serial);

// Category for (release, serial); GAME_CAT_OTHER if the game is unknown.
int game_category(unsigned short release, const char* serial);

#ifdef __cplusplus
}
#endif

#endif // GAME_TITLES_H
