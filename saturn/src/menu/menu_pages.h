/*----------------------
 | menu_pages.h
 | Description: The Options menu and its sub-pages -- Network dial number,
 |   Controls (live remap editor + controller/keyboard controls views), Sound,
 |   and Display. Owns the option-menu UI only;
 |   persistence and runtime apply of the settings these pages edit live in
 |   options.h. Three entries are called from outside this module: the main
 |   loop's Options/F10 and Sound/F12 hotkeys, and the in-game F11 Controls
 |   key. Every other page is reachable only from options_menu and stays
 |   file-local to menu_pages.cxx.
 | Author: suinevere
 | Dependencies: menu.h, input.h, options.h, console_view.h, app_state.h,
 |   soft_reset.h, keyboard.h, menu_layout.h, display.h, sound.h, music.h, SRL
 ----------------------*/

#ifndef MENU_PAGES_H
#define MENU_PAGES_H

/*----------------------
 | options_menu
 | Description: Opens the Options menu: a difficulty slider plus Network,
 |   Controls, Display, Sound (shown only when there is audio to configure),
 |   Return to Title, and Done. Blocks until the player picks Done, backs out
 |   with B/Esc, or confirms Return to Title (which soft-resets and does not
 |   return). Persists the difficulty change, if any, on exit.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_menu(void);

/*----------------------
 | keyboard_controls_page
 | Description: Physical-keyboard settings page: which arrows move the caret
 |   vs cycle suggestions, insert-vs-overwrite typing, CapsLock, and NumLock.
 |   OK commits and saves; Cancel (B/Esc) restores the snapshot taken on
 |   entry. Reached from the Controls page when a real keyboard is the active
 |   device, and directly from the in-game F11 key.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_caret_arrows
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void keyboard_controls_page(void);

/*----------------------
 | sound_options_page
 | Description: Sound Options page. Which rows appear depends on what the
 |   disc/game actually provide: Audio Mix / Track / Music need CD-DA;
 |   PCM level needs the loaded game's .BLB; OK/Cancel always show.
 |   Previews audio live while open; OK
 |   commits and saves, Cancel restores the snapshot including live audio.
 |   Reached from the Options menu's Sound row and directly from the in-game
 |   F12 key.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_mix_mode, g_sel_track, g_music_level, g_pcm_level
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sound_options_page(void);

#endif /* MENU_PAGES_H */
