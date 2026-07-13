/*----------------------
 | game_catalog.h
 | Description: The CD "Z3" folder scan, the cached catalogue of story files it
 |   yields (filename, display title, category), and the two-page menu that lets
 |   the player pick one.
 | Author: suinevere
 | Dependencies: menu.h, game_titles.h, title.h, SRL/GFS
 ----------------------*/
#ifndef GAME_CATALOG_H
#define GAME_CATALOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/*----------------------
 | g_z3_dir_valid
 | Description: True once scan_z3_folder has captured the disc's "Z3" directory
 |   record, meaning it can be re-applied as the current CD directory. Cleared by
 |   main.cxx after a soft reset, because GFS_Reset() invalidates the record it
 |   describes.
 ----------------------*/
extern bool g_z3_dir_valid;

/*----------------------
 | scan_z3_folder
 | Description: Collects the names of the *.Z3 story files in the disc's "Z3"
 |   folder and makes that folder the current CD directory, so later opens by
 |   bare filename resolve inside it. Safe to call more than once.
 | Author: suinevere
 | Dependencies: title.h (cd_enter_root), SRL/GFS
 | Globals: g_z3_dirnames, g_z3_tbl, g_z3_dir_valid
 | Params: out -- receives up to `max` NUL-terminated names of at most 15 chars;
 |   max -- capacity of `out`
 | Returns: the number of names written (0 if the folder holds none), or -1 if
 |   there is no Z3 folder on the disc
 ----------------------*/
int scan_z3_folder(char out[][16], int max);

/*----------------------
 | preload_game_catalog
 | Description: Builds the in-memory catalogue -- one filename, display title and
 |   category per story file on the disc -- so that game_select can run without
 |   touching the drive. Idempotent; call it once during the title screen's silent
 |   window, because the CD reads it performs stop CD-DA playback.
 | Author: suinevere
 | Dependencies: game_titles.h, SRL
 | Globals: names, labels, cats, g_catalog_count, g_catalog_ready
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void preload_game_catalog(void);

/*----------------------
 | game_select
 | Description: Runs the game picker: a category page, then the games in that
 |   category sorted by release year and title. Backing out of the game page
 |   returns to the categories; backing out of the categories cancels. Reports an
 |   empty or missing Z3 folder to the player instead of returning a name.
 | Author: suinevere
 | Dependencies: menu.h, SRL
 | Globals: names, labels, cats, g_catalog_count
 | Params: N/A
 | Returns: the chosen story's filename (owned by this module, stable for the
 |   life of the program), or NULL if the player cancelled or there is nothing
 |   to choose from
 ----------------------*/
const char* game_select(void);

#ifdef __cplusplus
}
#endif

#endif
