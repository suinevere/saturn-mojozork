/*----------------------
 | app_state.h
 | Description: The cross-cutting runtime globals shared across the interpreter's
 |   modules -- persisted game options (difficulty, audio levels/mix, display
 |   palette, online dial number), save/restore session state (pre-picked and
 |   last-used slots, the queued auto-command), the soft-reset jump target, the
 |   story file in play, and the console scroll position. Housing g_scroll here
 |   (rather than in the input or console_view module) avoids a mutual
 |   input<->console_view header cycle: input writes it, console_view reads it,
 |   and this header is neutral C-safe ground both can include. Declarations
 |   only -- definitions live in app_state.cxx.
 | Author: suinevere
 | Dependencies: display.h
 ----------------------*/

#ifndef APP_STATE_H
#define APP_STATE_H

#include <setjmp.h>
#include <stdbool.h>
#include "display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Difficulty (Options menu). Easy = full typeahead + winning-path hints; Medium =
   typeahead, grammar weights only; Hard = typeahead off. */
enum { DIFF_EASY = 0, DIFF_MEDIUM = 1, DIFF_HARD = 2 };

/* Online dial number (editable in Options -> Network; persisted). 11 digits is
   the longest we accept (NANP country code plus number). */
#define DIALNUM_MAX 11

// Difficulty selected in Options (DIFF_EASY/MEDIUM/HARD); gates typeahead hints
// and grammar weighting.
extern int g_difficulty;

// CD-DA music volume level, 0..7 (0 = off); persisted in MOJOOPTS.
extern int g_music_level;

// PCM sound-effect volume level, 0..7 (0 = off); persisted in MOJOOPTS.
extern int g_pcm_level;

// Audio mix mode selected in Options > Sound (MIX_DYNAMIC/OVERRIDE/SEQUENTIAL/
// RANDOM, from music.h); persisted in MOJOOPTS.
extern int g_mix_mode;

// Selected/override CD-DA track number; also the title/menu track.
extern int g_sel_track;

// Current display colors/background/image, applied to VDP2 by display_apply
// and persisted in MOJOOPTS.
extern DisplayState g_display;

// Online dial number text, editable in Options > Network; persisted in MOJOOPTS.
extern char g_dialnum[DIALNUM_MAX + 1];

// Save device pre-picked from "Load Save Game", applied by the first in-game
// "restore" (queued via g_autocmd) instead of the choose_dest prompt.
extern int g_restore_device;

// Save slot pre-picked from "Load Save Game", paired with g_restore_device.
extern int g_restore_slot;

// Command auto-submitted on the next readline (used to queue "restore" after a
// pre-picked load).
extern const char *g_autocmd;

// Device of the last save/restore that actually committed this session; -1
// until one commits.
extern int g_last_device;

// Slot of the last save/restore that actually committed this session; used by
// the F5/F6/F9 quick keys to skip the pickers.
extern int g_last_slot;

// Save destination pre-picked by quick-save, the save-side mirror of
// g_restore_device/g_restore_slot. One-shot.
extern int g_save_device;

// Save slot pre-picked by quick-save, paired with g_save_device. One-shot.
extern int g_save_slot;

// Soft-reset jump target armed by main() just before the title screen; the
// input loops longjmp here on the reset chord or the typed "reboot" command.
extern jmp_buf g_title_jmp;

// True once g_title_jmp has been armed by setjmp and is safe to longjmp to.
extern bool g_title_jmp_armed;

// Story file currently loaded from CD (set by main after game selection);
// re-read by saturn_read_story_file for save/restart.
extern const char *g_story_filename;

// Console scroll offset from the live bottom, in lines (0 = latest text).
// Written by the input module (scroll_handle_key, pad_scroll_update) and read
// by console_view's render_console.
extern int g_scroll;

#ifdef __cplusplus
}
#endif

#endif /* APP_STATE_H */
