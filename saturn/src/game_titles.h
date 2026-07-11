#ifndef GAME_TITLES_H
#define GAME_TITLES_H

#ifdef __cplusplus
extern "C" {
#endif

// Display title for a Z-machine story, keyed by its header release number (0x02)
// and serial (the 6 raw bytes at 0x12). Returns NULL if the game is unknown.
const char* game_title(unsigned short release, const char* serial);

#ifdef __cplusplus
}
#endif

#endif // GAME_TITLES_H
