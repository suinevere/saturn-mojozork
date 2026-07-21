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
 | Description: Shows a TGA image on VDP2 NBG0 behind the title text (menus and
 |   gameplay stay on solid black). Serves it from the Low Work RAM cache when
 |   display_preload_images took it, which keeps the CD idle and the music
 |   playing; otherwise reads it from the disc once. Accepts only uncompressed
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
 | display_preload_images
 | Description: Decodes every background registered by display_scan_images into
 |   a Low Work RAM cache, so that cycling pictures in the Options menu uploads
 |   from RAM instead of reading the disc. The Saturn cannot play CD-DA while
 |   reading data, so a read under the menu track is heard as a skip; this moves
 |   those reads into the title screen's already-silent window. Call after
 |   display_scan_images() and before the menu music starts. Idempotent, and the
 |   cache survives the soft-reset longjmp, so a return to title costs no reads.
 |   Pictures that do not fit the cache budget still work, read on demand.
 | Author: suinevere
 | Dependencies: display.c, SRL
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void display_preload_images(void);

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
