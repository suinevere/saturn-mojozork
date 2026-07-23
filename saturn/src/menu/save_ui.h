/*----------------------
 | save_ui.h
 | Description: Save/restore slot-picker UI -- the per-game backup filename
 |   helper, the backup-device chooser, and the combined slot picker + in-place
 |   name editor. Declarations only; definitions live in save_ui.cxx. These are
 |   driven by main.cxx's save/restore orchestration (choose_dest / do_save /
 |   do_restore).
 | Author: suinevere
 | Dependencies: none in the header (C-safe signatures); the .cxx consumes
 |   menu.h, menu_layout.h, keyboard.h, saturn_backup.h, saturn_keyboard.h,
 |   input.h, console_view.h, app_state.h.
 ----------------------*/

#ifndef SAVE_UI_H
#define SAVE_UI_H

/* Save slots offered per game on a backup device (each game keeps its own set,
   named by make_slot_name). Used by the pickers here and by main.cxx's
   save/restore flow. */
#define SAVE_SLOTS 5

/*----------------------
 | make_slot_name
 | Description: Builds the per-game backup filename for a save slot -- the
 |   story's base name (uppercased, extension dropped, up to 9 chars) followed by
 |   the slot digit -- so each game keeps its own independent set of slots.
 |   Writes a NUL-terminated string to `out`.
 | Author: suinevere
 | Dependencies: app_state.h (g_story_filename)
 | Globals: g_story_filename
 | Params: out -- buffer receiving the filename (needs >= 11 bytes); slot -- slot
 |   index appended as a single digit
 | Returns: N/A
 ----------------------*/
void make_slot_name(char *out, int slot);

/*----------------------
 | choose_device
 | Description: Modal picker for the backup device. Always offers internal
 |   console memory; offers the cartridge only when one is inserted.
 | Author: suinevere
 | Dependencies: menu.h (menu_select), saturn_backup.h (device ids / presence)
 | Globals: N/A
 | Params: title -- menu title shown to the player
 | Returns: the chosen SATURN_BUP_* device id, or -1 if cancelled
 ----------------------*/
int choose_device(const char *title);

/*----------------------
 | pick_slot_and_name
 | Description: Combined save-slot picker and in-place name editor for `device`.
 |   Lists every slot; the player picks one (C/Enter/number), then edits that
 |   slot's name right on its line using the on-screen or physical keyboard.
 |   Backspace/B returns from editing to slot selection; A/Enter/Start confirms.
 | Author: suinevere
 | Dependencies: menu.h, menu_layout.h, keyboard.h, saturn_backup.h,
 |   saturn_keyboard.h, input.h, console_view.h
 | Globals: g_pad, g_kbd_visible
 | Params: device -- SATURN_BUP_* target; out_slot -- receives the chosen slot;
 |   out_name -- receives the edited name (empty string if left blank); maxchars
 |   -- name-length cap
 | Returns: 1 with *out_slot / out_name set, or 0 if cancelled
 ----------------------*/
int pick_slot_and_name(int device, int *out_slot, char *out_name, int maxchars);

/*----------------------
 | choose_dest
 | Description: Picks a backup device (cartridge offered only when inserted),
 |   then a slot -- each slot row labelled with its existing save comment, or
 |   "(empty)". Read-only: it does not write, so both the save flow and the
 |   "Load Save Game" menu use it to resolve a device+slot before acting.
 | Author: suinevere
 | Dependencies: menu.h, saturn_backup.h
 | Globals: N/A
 | Params: title_dev -- title for the device menu; title_slot -- title for the
 |   slot menu; out_device -- receives the chosen device; out_slot -- receives
 |   the chosen slot
 | Returns: 1 with *out_device / *out_slot set, or 0 if cancelled
 ----------------------*/
int choose_dest(const char *title_dev, const char *title_slot,
                int *out_device, int *out_slot);

#endif /* SAVE_UI_H */
