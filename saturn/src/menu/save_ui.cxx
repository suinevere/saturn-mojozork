/*----------------------
 | save_ui.cxx
 | Description: Save/restore slot-picker UI. Builds per-game backup filenames,
 |   chooses the backup device, and runs the combined slot picker + in-place name
 |   editor over the SGL backup library. Called by main.cxx's save/restore flow.
 | Author: suinevere
 | Dependencies: save_ui.h, menu.h (menu_select/menu_frame/menu_clear/MenuBacking),
 |   menu_layout.h (menu_box_fit/menu_visible_digit/MENU_DIGIT_COLS), keyboard.h
 |   (on-screen keyboard state/layout), saturn_backup.h (SATURN_BUP_* ids and bup
 |   queries), saturn_keyboard.h (key events), input.h (g_pad), console_view.h
 |   (note_input_device/hint/g_kbd_visible), app_state.h (g_story_filename), SRL.
 ----------------------*/

#include <srl.hpp>

#include "save_ui.h"
#include "menu.h"
extern "C" {
#include "menu_layout.h"
}
#include "keyboard.h"
#include "saturn_backup.h"
#include "saturn_keyboard.h"
#include "input.h"
#include "console_view.h"
#include "app_state.h"

/*----------------------
 | snprintf (extern)
 | Description: Forward declaration for snprintf, which links from newlib but is
 |   omitted by the SRL dummy <stdio.h>.
 | Author: suinevere
 ----------------------*/
extern "C" int snprintf(char *str, size_t size, const char *fmt, ...);

/*----------------------
 | make_slot_name
 | Description: Copies the story filename's base (up to 9 chars, stopping at the
 |   extension dot, forcing lowercase to uppercase) into `out`, then appends the
 |   slot digit and a NUL. The per-game prefix is what keeps each game's slots
 |   separate on the same backup device.
 | Author: suinevere
 | Dependencies: app_state.h (g_story_filename)
 | Globals: g_story_filename
 | Params: out -- destination buffer (>= 11 bytes); slot -- appended as one digit
 | Returns: N/A
 ----------------------*/
void make_slot_name(char *out, int slot) {
    int i = 0;
    for (const char *p = g_story_filename; *p && *p != '.' && i < 9; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        out[i++] = c;
    }
    out[i++] = (char) ('0' + slot);
    out[i] = 0;
}

/*----------------------
 | choose_device
 | Description: Assembles the device list (console always; cartridge only when
 |   saturn_bup_present reports one), runs it through menu_select, and maps the
 |   chosen row back to its SATURN_BUP_* id.
 | Author: suinevere
 | Dependencies: menu.h (menu_select), saturn_backup.h (saturn_bup_present /
 |   SATURN_BUP_CONSOLE / SATURN_BUP_CARTRIDGE)
 | Globals: N/A
 | Params: title -- menu title
 | Returns: chosen SATURN_BUP_* id, or -1 if cancelled
 ----------------------*/
int choose_device(const char *title) {
    const char *dev_items[2];
    int dev_ids[2];
    int ndev = 0;
    dev_items[ndev] = "Console (internal)"; dev_ids[ndev] = SATURN_BUP_CONSOLE; ndev++;
    if (saturn_bup_present(SATURN_BUP_CARTRIDGE)) {
        dev_items[ndev] = "Cartridge"; dev_ids[ndev] = SATURN_BUP_CARTRIDGE; ndev++;
    }
    int d = menu_select(title, dev_items, ndev);
    return (d < 0) ? -1 : dev_ids[d];
}

/*----------------------
 | pick_slot_and_name
 | Description: Two-state modal over one box. In the PICK state the player
 |   navigates the slot list (D-pad / number keys; the list never scrolls, so a
 |   digit maps directly to a slot) and picks one; in the EDIT state that slot's
 |   name is edited in place on its own line, the box growing to hold the
 |   on-screen keyboard. Picking pre-fills the editor with the slot's current
 |   name and drops the keyboard cursor on that name's last character.
 |   Ctrl+C (SATURN_KEY_CLEAR) blanks the field, Backspace/B leaves EDIT back to
 |   PICK, and A/Enter/Start confirms.
 |
 |   The box is sized every frame rather than once, because its shape changes
 |   when `editing` flips. A slot row budgets a cursor mark, the reserved "N) "
 |   digit columns (reserved whether or not drawn), and up to 10 label chars
 |   (saturn_bup_info caps a comment at 10; "(empty)" is 7); the edit row budgets
 |   maxchars plus the caret. In EDIT the width must also cover the keyboard
 |   (KB_COLS*2) and the hint; in either state the LONGER of the two hint
 |   variants (pad vs keyboard) is budgeted unconditionally so the box does not
 |   resize when the player switches input device mid-menu.
 | Author: suinevere
 | Dependencies: menu.h (MenuBacking/menu_clear/menu_frame), menu_layout.h
 |   (menu_box_fit/menu_visible_digit/MENU_DIGIT_COLS), keyboard.h (KeyboardState
 |   and helpers/KB_LAYOUT/KB_ROWS/KB_COLS), saturn_backup.h (SAVE_SLOTS/
 |   saturn_bup_info), saturn_keyboard.h (saturn_keyboard_poll/SATURN_KEY_*),
 |   input.h (g_pad/Button), console_view.h (note_input_device/hint/g_kbd_visible)
 | Globals: g_pad, g_kbd_visible
 | Params: device -- SATURN_BUP_* target; out_slot -- receives chosen slot;
 |   out_name -- receives edited name (empty if blank); maxchars -- name cap
 | Returns: 1 with *out_slot / out_name set, or 0 if cancelled
 ----------------------*/
int pick_slot_and_name(int device, int *out_slot, char *out_name, int maxchars) {
    MenuBacking backing;

    static const char PICK_HINT_PAD[] = "pad picks   C=edit   B=back";
    static const char PICK_HINT_KBD[] = "num picks   Enter=edit   Esc=back";
    static const char EDIT_HINT_PAD[] = "C=type X=space  B=back  A=OK";
    static const char EDIT_HINT_KBD[] = "type name  Esc=back  Enter=OK";

    char slotname[SAVE_SLOTS][12];
    for (int i = 0; i < SAVE_SLOTS; i++) {
        char fn[12];
        make_slot_name(fn, i);
        if (!saturn_bup_info(device, fn, slotname[i])) slotname[i][0] = '\0';
    }

    int sel = 0;
    int editing = 0;
    KeyboardState k;
    keyboard_reset(&k);
    SRL::Core::Synchronize();

    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);

        if (!editing) {
            bool pick = false, cancel = false;
            if (ke.kind == SATURN_KEY_ENTER) pick = true;
            else if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) cancel = true;
            else if (ke.kind == SATURN_KEY_CHAR) {
                int idx = menu_visible_digit(ke.ch, 0, SAVE_SLOTS, SAVE_SLOTS);
                if (idx >= 0) { sel = idx; pick = true; }
            } else {
                if (g_pad->WasPressed(Button::Up))   sel = (sel - 1 + SAVE_SLOTS) % SAVE_SLOTS;
                if (g_pad->WasPressed(Button::Down)) sel = (sel + 1) % SAVE_SLOTS;
                if (g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) pick = true;
                if (g_pad->WasPressed(Button::B)) cancel = true;
            }
            if (cancel) return 0;
            if (pick) {
                keyboard_reset(&k);
                for (int i = 0; slotname[sel][i] && k.input_len < maxchars; i++)
                    keyboard_type_char(&k, slotname[sel][i]);
                if (k.input_len > 0) {
                    char last = k.input[k.input_len - 1];
                    for (int r = 0; r < KB_ROWS; r++)
                        for (int c = 0; c < KB_COLS; c++)
                            if (KB_LAYOUT[r][c] == last) { k.cursor_row = r; k.cursor_col = c; }
                }
                editing = 1;
                SRL::Core::Synchronize();
                continue;
            }
        } else {
            bool submit = false;
            if (ke.kind == SATURN_KEY_ENTER) submit = true;
            else if (ke.kind == SATURN_KEY_ESCAPE) { editing = 0; SRL::Core::Synchronize(); continue; }
            else if (ke.kind == SATURN_KEY_CLEAR) { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }
            else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
            else if (ke.kind == SATURN_KEY_CHAR) { if (k.input_len < maxchars) keyboard_type_char(&k, ke.ch); }
            else {
                if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
                if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
                if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
                if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
                if (g_pad->WasPressed(Button::C))     { if (k.input_len < maxchars) keyboard_type(&k); }
                if (g_pad->WasPressed(Button::X))     { if (k.input_len < maxchars) keyboard_type_char(&k, ' '); }
                if (g_pad->WasPressed(Button::B))     { editing = 0; SRL::Core::Synchronize(); continue; }
                if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::START)) submit = true;
            }
            if (submit) {
                int n = k.input_len;
                if (n > maxchars) n = maxchars;
                for (int i = 0; i < n; i++) out_name[i] = k.input[i];
                out_name[n] = '\0';
                *out_slot = sel;
                return 1;
            }
        }

        const char *btitle = editing ? "NAME THIS SAVE" : "SAVE - PICK A SLOT";

        int row_w = 2 + MENU_DIGIT_COLS + 10;
        int edit_w = 2 + MENU_DIGIT_COLS + maxchars + 1;
        if (edit_w > row_w) row_w = edit_w;
        int content_w;
        int rows;
        if (editing) {
            int kb_w = KB_COLS * 2;
            int hint_w = (int) sizeof(EDIT_HINT_KBD) - 1;
            if ((int) sizeof(EDIT_HINT_PAD) - 1 > hint_w) hint_w = (int) sizeof(EDIT_HINT_PAD) - 1;
            content_w = row_w;
            if (kb_w > content_w)   content_w = kb_w;
            if (hint_w > content_w) content_w = hint_w;
            rows = SAVE_SLOTS + 2 + KB_ROWS + 1;
        } else {
            int hint_w = (int) sizeof(PICK_HINT_KBD) - 1;
            if ((int) sizeof(PICK_HINT_PAD) - 1 > hint_w) hint_w = (int) sizeof(PICK_HINT_PAD) - 1;
            content_w = row_w;
            if (hint_w > content_w) content_w = hint_w;
            rows = SAVE_SLOTS + 2;
        }
        int x0, y0, w, h;
        menu_box_fit(btitle, content_w, rows, &x0, &y0, &w, &h);

        bool nums = !g_kbd_visible && !editing;

        menu_clear();
        menu_frame(x0, y0, w, h, btitle);
        int cx = x0 + 2, cy = y0 + 3;
        for (int i = 0; i < SAVE_SLOTS; i++) {
            char mark = (i == sel) ? '>' : ' ';
            if (editing && i == sel) {
                SRL::Debug::Print(cx, cy + i, "%c    %s_", mark, k.input);
            } else {
                const char *label = slotname[i][0] ? slotname[i] : "(empty)";
                if (nums) SRL::Debug::Print(cx, cy + i, "%c %d) %s", mark, i + 1, label);
                else      SRL::Debug::Print(cx, cy + i, "%c    %s", mark, label);
            }
        }
        if (!editing) {
            SRL::Debug::Print(cx, cy + SAVE_SLOTS + 1, "%s", hint(PICK_HINT_PAD, PICK_HINT_KBD));
        } else {
            for (int r = 0; r < KB_ROWS; r++) {
                char rowbuf[KB_COLS * 2 + 1];
                int p = 0;
                for (int c = 0; c < KB_COLS; c++) {
                    rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
                    rowbuf[p++] = KB_LAYOUT[r][c];
                }
                rowbuf[p] = '\0';
                SRL::Debug::Print(cx, cy + SAVE_SLOTS + 1 + r, "%s", rowbuf);
            }
            SRL::Debug::Print(cx, cy + SAVE_SLOTS + 2 + KB_ROWS, "%s",
                hint(EDIT_HINT_PAD, EDIT_HINT_KBD));
        }
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | choose_dest
 | Description: Runs choose_device, then a menu_select over the SAVE_SLOTS slots
 |   with each row labelled by saturn_bup_info's comment (or "(empty)"). The
 |   labels are held in a function-static buffer so the const char* array handed
 |   to menu_select stays valid for the length of the menu. Cancelling either
 |   menu returns 0 without touching the out-params.
 | Author: suinevere
 | Dependencies: menu.h (menu_select), saturn_backup.h (SAVE_SLOTS/saturn_bup_info)
 | Globals: N/A
 | Params: title_dev -- device-menu title; title_slot -- slot-menu title;
 |   out_device -- receives the chosen device; out_slot -- receives the chosen slot
 | Returns: 1 with *out_device / *out_slot set, or 0 if cancelled
 ----------------------*/
int choose_dest(const char *title_dev, const char *title_slot,
                int *out_device, int *out_slot) {
    int device = choose_device(title_dev);
    if (device < 0) return 0;

    static char labels[SAVE_SLOTS][40];
    const char *slot_items[SAVE_SLOTS];
    for (int i = 0; i < SAVE_SLOTS; i++) {
        char name[12];
        make_slot_name(name, i);
        char comment[12];
        if (saturn_bup_info(device, name, comment)) snprintf(labels[i], sizeof(labels[i]), "%s", comment);
        else                                        snprintf(labels[i], sizeof(labels[i]), "(empty)");
        slot_items[i] = labels[i];
    }
    int slot = menu_select(title_slot, slot_items, SAVE_SLOTS);
    if (slot < 0) return 0;

    *out_device = device;
    *out_slot = slot;
    return 1;
}
