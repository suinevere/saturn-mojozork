/*----------------------
 | title.h
 | Description: Title screen, background art, TGA loading, CD directory juggling,
 |   and the boot sequence random seed.
 | Author: suinevere
 | Dependencies: app_state.h, display.h, menu.h, SRL
 ----------------------*/
#ifndef TITLE_H
#define TITLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/*----------------------
 | title_draw_art
 | Description: Draws the title screen text art (Z-ATURN and copyright).
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_draw_art(void);

/*----------------------
 | title_bg_show
 | Description: Loads a TGA image from the CD into VDP2 NBG0 and displays it behind
 |   the title text (menus and gameplay stay on solid black). Accepts only uncompressed
 |   8bpp colour-mapped TGA. Returns false if the load fails or the format is
 |   unsupported, so the caller can fall back to a colour background.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: file -- filename of the TGA image to load
 | Returns: true if the requested display was applied; false if it fell back
 ----------------------*/
bool title_bg_show(const char *file);

/*----------------------
 | title_bg_hide
 | Description: Hides the title background image by disabling scroll on VDP2 NBG0.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void title_bg_hide(void);

/*----------------------
 | title_and_seed
 | Description: Displays the title screen with a "Press any button" prompt and waits
 |   for user input. Returns a random seed based on the number of elapsed frames.
 |   Also handles soft reset chords while waiting on this screen.
 | Author: suinevere
 | Dependencies: console_view.h, input.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: a random seed integer
 ----------------------*/
int title_and_seed(void);

/*----------------------
 | display_scan_images
 | Description: Scans the disc's TGA folder once at boot and registers any usable
 |   .TGA files found with the display system so the background selector can cycle
 |   into them.
 | Author: suinevere
 | Dependencies: display.c, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void display_scan_images(void);

/*----------------------
 | cd_capture_root
 | Description: Snapshots the root directory record straight after GFS_Reset()
 |   so that cd_enter_root() can return to it later.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_root_tbl, g_root_dirnames, g_root_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_capture_root(void);

/*----------------------
 | cd_enter_root
 | Description: Re-points the CD to the root directory captured by cd_capture_root.
 |   Used to make directory changes idempotent.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: g_root_tbl, g_root_dir_valid
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void cd_enter_root(void);

#ifdef __cplusplus
}
#endif

#endif
