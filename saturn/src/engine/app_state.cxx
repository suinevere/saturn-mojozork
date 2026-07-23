/*----------------------
 | app_state.cxx
 | Description: Single definitions of the cross-cutting globals declared extern
 |   in app_state.h -- persisted game options, save/restore session state, the
 |   soft-reset jump target, the loaded story file, and the console scroll
 |   position. Initializers are carried over verbatim from main.cxx.
 | Author: suinevere
 | Dependencies: app_state.h, music.h (MIX_DYNAMIC)
 ----------------------*/

#include "app_state.h"
#include "music.h"

/*----------------------
 | g_difficulty
 | Description: Current difficulty (DIFF_EASY/MEDIUM/HARD); gates whether the
 |   typeahead is built and how aggressively it filters. Persisted in MOJOOPTS.
 | Author: suinevere
 ----------------------*/
int g_difficulty = DIFF_EASY;

/*----------------------
 | g_music_level / g_pcm_level / g_mix_mode / g_sel_track
 | Description: Persisted audio options: CD-DA music level and PCM effect level
 |   (0..7, 0 = off), the music mix mode (MIX_*), and the selected/override CD
 |   track. Defaults match the Options sliders.
 | Author: suinevere
 ----------------------*/
int g_music_level = 7;
int g_pcm_level   = 4;
int g_mix_mode  = MIX_DYNAMIC;
int g_sel_track = 10;

/*----------------------
 | g_display
 | Description: Current display colors/background, applied to VDP2 by
 |   display_apply and persisted in MOJOOPTS.
 | Author: suinevere
 ----------------------*/
DisplayState g_display;

/*----------------------
 | g_dialnum
 | Description: The online dial number, editable in Options -> Network; defaults
 |   to the multizork line. Persisted in MOJOOPTS.
 | Author: suinevere
 ----------------------*/
char g_dialnum[DIALNUM_MAX + 1] = "199403";

/*----------------------
 | g_restore_device / g_restore_slot / g_autocmd
 | Description: One-shot restore session state: a device/slot pre-picked by "Load
 |   Save Game" (-1 = none) and the queued autocommand ("restore") the first turn
 |   submits so the pick is applied.
 | Author: suinevere
 ----------------------*/
int g_restore_device = -1;
int g_restore_slot   = -1;
const char *g_autocmd = nullptr;

/*----------------------
 | g_last_device / g_last_slot
 | Description: The last committed save/restore device+slot, reused by the
 |   quick-save/quick-load function keys (-1 = none yet).
 | Author: suinevere
 ----------------------*/
int g_last_device = -1;
int g_last_slot   = -1;

/*----------------------
 | g_save_device / g_save_slot
 | Description: One-shot quick-save destination armed by F5 (-1 = none), so the
 |   save blob hook skips its device/slot picker.
 | Author: suinevere
 ----------------------*/
int g_save_device = -1;
int g_save_slot   = -1;

/*----------------------
 | g_title_jmp / g_title_jmp_armed
 | Description: The setjmp target for the in-process soft reset (return to title),
 |   and whether main has armed it yet.
 | Author: suinevere
 ----------------------*/
jmp_buf  g_title_jmp;
bool     g_title_jmp_armed = false;

/*----------------------
 | g_story_filename
 | Description: The loaded story's CD filename; drives per-game save-slot names and
 |   is re-read by saturn_read_story_file for save/restart.
 | Author: suinevere
 ----------------------*/
const char *g_story_filename = "ZORK1.Z3";

/*----------------------
 | g_scroll
 | Description: The console scrollback position (0 = live bottom). Written by the
 |   input module's scroll handlers, read by console_view's renderer.
 | Author: suinevere
 ----------------------*/
int g_scroll = 0;
