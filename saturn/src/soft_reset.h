/*----------------------
 | soft_reset.h
 | Description: Declarations for the in-process soft-reset / reboot entry points
 |   that remain defined in main.cxx but are called across module boundaries
 |   (menu, menu_pages, online). The soft reset returns the player to the title
 |   screen via longjmp without an SMPC/hardware reset. main.cxx owns the
 |   definitions and the setjmp target (g_title_jmp, in app_state).
 | Author: suinevere
 | Dependencies: app_state.h (g_title_jmp), net_connect.c, sound.cxx (invoked by
 |   the definitions in main.cxx, not by this header)
 ----------------------*/

#ifndef SOFT_RESET_H
#define SOFT_RESET_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | is_reboot_command
 | Description: True when `line` is exactly "reboot" (case-insensitive). The
 |   reboot command is global -- honored at both the local game prompt and the
 |   online terminal.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: line -- the raw input line to test
 | Returns: non-zero if the line is the reboot command, 0 otherwise
 ----------------------*/
int is_reboot_command(const char *line);

/*----------------------
 | soft_reset_to_title
 | Description: Performs the Sega software reset -- drops any live connection,
 |   releases the story image, and longjmps back to the title screen (armed in
 |   main). In-process restart, not an SMPC reset; never returns.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_title_jmp, g_title_jmp_armed
 | Params: N/A
 | Returns: N/A (does not return)
 ----------------------*/
void soft_reset_to_title(void);

/*----------------------
 | soft_reset_chord_held
 | Description: True when the software-reset chord (A+B+C+Start) is physically
 |   held this frame.
 | Author: suinevere
 | Dependencies: input.h (g_pad)
 | Globals: g_pad
 | Params: N/A
 | Returns: true while the reset chord is held, false otherwise
 ----------------------*/
bool soft_reset_chord_held(void);

/*----------------------
 | confirm_return_to_title
 | Description: Modal Y/N prompt asking `question`. On Yes it soft-resets to the
 |   title screen in-process (the same return-to-title as the A+B+C+Start chord),
 |   retaining the options held in backup RAM; on No it returns false so the
 |   caller resumes. Shared by the reboot and quit commands (local prompt and the
 |   online terminal), both of which discard an unsaved game.
 | Author: suinevere
 | Dependencies: menu.h, N/A
 | Globals: N/A
 | Params: question -- the yes/no question to display
 | Returns: false when the player declines (on Yes it does not return)
 ----------------------*/
bool confirm_return_to_title(const char *question);

/*----------------------
 | check_soft_reset
 | Description: Polls the software-reset chord; call once per frame from any
 |   input loop. Never returns once the chord has been held long enough -- it
 |   soft-resets to the title.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void check_soft_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_RESET_H */
