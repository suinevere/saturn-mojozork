#include <srl.hpp>
#include <setjmp.h>

extern "C" {
#include "console.h"
#include "keyboard.h"
#include "display.h"
#include "menu_layout.h"
#include "saturn_keyboard.h"
#include "saturn_backup.h"
#include "saturn_glue.h"
#include "sound.h"
#include "term.h"
#include "net/net_connect.h"
#include "typeahead.h"
#include "typeahead_extract.h"
#include "typeahead_solution.h"
#include "music.h"
#include "netbin_blobs.h"
}
#include "app_state.h"
#include "input.h"
#include "console_view.h"
#include "options.h"
#include "soft_reset.h"
#include "menu.h"
#include "menu_pages.h"
#include "save_ui.h"
#include "title.h"
#include "game_catalog.h"
#include "netbin_sound.h"

// Global typeahead trie (should be populated by the game backend eventually)
static TrieNode* g_typeahead_root = nullptr;

// The persisted game options and save-session state that used to live here
// (g_difficulty, g_music_level, g_pcm_level, g_mix_mode, g_sel_track, g_display,
// g_dialnum, g_restore_*, g_save_*, g_last_*, g_autocmd) now live in app_state.

extern "C" void* typeahead_malloc(unsigned int size) {
    return SRL::Memory::HighWorkRam::Malloc(size);
}

extern "C" void typeahead_free(void* ptr) {
    SRL::Memory::Free(ptr);
}

// Build the typeahead from whatever story is currently loaded, decoding its own
// dictionary and grammar on-device. Rebuilds (freeing the old trie) whenever the
// loaded story changes, so switching games picks up the new vocabulary.
static const uint8_t* g_ta_story = nullptr;
static int g_ta_diff = -1;
static void ensure_typeahead() {
    uint32_t len = 0;
    const uint8_t* story = saturn_story_data(&len);
    if (g_typeahead_root && story == g_ta_story && g_ta_diff == g_difficulty) return;
    if (g_typeahead_root) { destroy_typeahead(g_typeahead_root); g_typeahead_root = nullptr; }
    g_typeahead_root = create_trie_node();
    int have_solution = 0;
    if (story != nullptr && len > 0 && g_difficulty != DIFF_HARD) {
        build_typeahead_from_story(g_typeahead_root, story, len);           // grammar layer
        // Apply the winning-path overlay in BOTH Easy and Normal: Easy restricts
        // suggestions to it; Normal uses its links as the "unless in solution"
        // exception to the grammar filter.
        have_solution = apply_solution_overlay(g_typeahead_root, story, len);
        // Last, so the twelve stock abbreviations are in whatever trie the story
        // produced (the extractor drops the bare direction ones). Hard builds no
        // trie at all, so it stays helpless -- the rule is: they exist iff it does.
        typeahead_add_abbreviations(g_typeahead_root);
    }
    typeahead_set_easy(g_difficulty == DIFF_EASY, have_solution);
    g_ta_story = story;
    g_ta_diff = g_difficulty;
}


#define ZATURN_DIAL_NUMBER "199403"

// snprintf links from newlib; the SRL dummy <stdio.h> omits its declaration.
extern "C" int snprintf(char *str, size_t size, const char *fmt, ...);


using namespace SRL::Types;

// Soft reset (the Sega-mandated A+B+C+Start chord, and the typed "reboot" command)
// returns the player to the title screen *in-process* -- NOT a hardware/SMPC reset,
// which reboots the console all the way back to CD load. main() arms g_title_jmp
// (in app_state) just before the title screen; the input loops poll the chord and
// longjmp back to it. Configured options live in backup RAM, so they survive the
// jump. g_title_jmp/g_title_jmp_armed and g_story_filename now live in app_state.

// menu_sync now lives in menu.h/menu.cxx.

// ---- options / display ------------------------------------------------------
// options_load, options_save, display_apply, display_cycle_row, valid_dialnum,
// text_set_color, and the DisplayCycleRow enum now live in options.h/options.cxx.

// menu_confirm now declared in menu.h.
static bool confirm_return_to_title(const char *question);   // defined below; called above by the reboot/quit commands

// ---- global reboot / quit commands -----------------------------------------

// True if `line` is exactly "reboot" (case-insensitive). The reboot command is
// global -- available from both the local game prompt and the online terminal.
int is_reboot_command(const char *line) {
    static const char cmd[] = "reboot";
    int i;
    for (i = 0; cmd[i]; i++) {
        char c = line[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c != cmd[i]) return 0;
    }
    return line[i] == '\0';
}

// True if the first word of `line` is "q" or "quit" (case-insensitive). The local
// game prompt intercepts this rather than letting the interpreter run its own quit,
// which ends the story and drops us out of mojo_run.
//
// This is a safety boundary, not a convenience, so it errs towards catching too
// much: it must match every spelling the story's parser would read as QUIT. A
// Z-machine dictionary declares its own word separators (Zork I: , . ") and the
// tokenizer breaks on them with or without a space, so "quit." is QUIT to the game
// -- splitting on spaces alone would let it through. Anything that isn't a letter
// or digit therefore ends the word, and whatever follows is ignored: "quit now" is
// intercepted too, since a story may well parse that as QUIT, and the worst case
// for over-matching is a confirm prompt the player declines.
static int is_alnum_ch(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
static int is_quit_command(const char *line) {
    while (*line != '\0' && !is_alnum_ch(*line)) line++;   // skip spaces/separators
    char w[6];
    int n = 0;
    for (; is_alnum_ch(line[n]) && n < 5; n++) {           // first word, lowercased
        char c = line[n];
        w[n] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
    }
    w[n] = '\0';
    // Short-circuits at the terminator, so no read runs past it.
    return w[0] == 'q' && (w[1] == '\0' ||
           (w[1] == 'u' && w[2] == 'i' && w[3] == 't' && w[4] == '\0'));
}

// Perform the Sega software reset: drop any live connection, release the story
// image, and longjmp back to the title screen (armed in main). This is an
// in-process restart, not an SMPC reset -- the console never re-reads the CD. If
// the jump target isn't armed yet (can't happen once main is running), fall back
// to the SMPC reset-button NMI.
void soft_reset_to_title(void) {
    net_connect_close();          // idempotent: drops the modem link if online
    sound_stop_all();             // stop any playing/looping sound before we bail out
    if (g_title_jmp_armed) longjmp(g_title_jmp, 1);
    slNMIRequest();               // fallback only
    while (1) {}
}

// Frames the A+B+C+Start reset chord must be held before it fires. A debounce, not
// a feature: it rejects the garbage peripheral read on the very first frame (before
// the first vsync has polled real input), which otherwise reads as "all held" and
// resets instantly. ~0.5s is still well below any real four-button hold.
static const int SOFT_RESET_HOLD = 30;

// True when the software-reset chord (A+B+C+Start) is physically held this frame.
bool soft_reset_chord_held(void) {
    return g_pad->IsHeld(Button::A) && g_pad->IsHeld(Button::B) &&
           g_pad->IsHeld(Button::C) && g_pad->IsHeld(Button::START);
}

// Poll the software-reset chord. Call once per frame from the input loops; it never
// returns once the chord has been held long enough (it soft-resets to the title).
void check_soft_reset(void) {
    static int hold = 0;
    hold = soft_reset_chord_held() ? (hold + 1) : 0;
    if (hold >= SOFT_RESET_HOLD) soft_reset_to_title();
}

// Put `cmd` on the input line and submit it, as if the player had typed it and
// pressed Enter -- so it echoes, enters history, and reaches the interpreter by
// the one path every other command uses. How the F-key shortcuts run the game's
// own save/restore.
static void submit_command(KeyboardState &k, const char *cmd) {
    int n = 0;
    while (cmd[n] != '\0' && n < KB_INPUT_MAX - 1) { k.input[n] = cmd[n]; n++; }
    k.input[n] = '\0';
    k.input_len = n;
    k.cursor = n;
    k.submitted = 1;
}

// ---- hooks (extern "C" so the C core can call them) ------------------------

extern "C" void saturn_writestr(const char *str, size_t slen) {
    console_write(str, (unsigned int) slen);
    music_note_output(str, (unsigned int) slen);   // feed the room-music classifier
}

// One frame of on-screen input editing with typeahead, shared by the local game
// prompt and the online (multizork) terminal so both behave identically. Handles
// the gamepad (with auto-repeat) and real keyboard: moves the picker, types/
// deletes, cycles suggestions (L/R), accepts (A/Tab) or accepts+space (Y), and
// recalls history (X + Up/Down). May set k.submitted. sug_index/sug_last persist
// the cycle position across frames. Outputs the selected suggestion + typed-word
// length for the caller to render. Caller polls `ke`/`pad` and ticks
// pad_repeat_update() before calling.
static void typeahead_edit(KeyboardState &k, TrieNode *root,
                           int &sug_index, char *sug_last,
                           SaturnKeyEvent &ke, bool pad,
                           DictionaryWord *&selected_out, int &cw_len_out) {
    // On-screen keyboard editing (gamepad): move the picker, type, delete.
    if (pad) {
        if (caps_combo_fired()) keyboard_set_caps(!keyboard_get_caps());  // L+R toggles Caps
        // Recall + text-caret move ride their configurable chords (defaults
        // Z+Up/Down and Z+L/R).
        if (chord_fired(CA_RECALL, -1)) history_recall(&k, 1);   // older
        if (chord_fired(CA_RECALL, +1)) history_recall(&k, 0);   // newer
        if (chord_fired(CA_CURSOR, -1)) keyboard_caret_left(&k);
        if (chord_fired(CA_CURSOR, +1)) keyboard_caret_right(&k);
        // Plain D-pad (no Z/Y shift held) moves the on-screen keyboard picker.
        if (!g_pad->IsHeld(Button::Z) && !g_pad->IsHeld(Button::Y)) {
            if (pad_fired(Button::Up))    keyboard_move(&k, 0, -1);
            if (pad_fired(Button::Down))  keyboard_move(&k, 0,  1);
            if (pad_fired(Button::Left))  keyboard_move(&k, -1, 0);
            if (pad_fired(Button::Right)) keyboard_move(&k,  1, 0);
        }
        if (pad_fired(face_button(FA_TYPE))) keyboard_type(&k);       // type letter
        if (pad_fired(face_button(FA_BACK))) keyboard_backspace(&k);  // backspace
    }

    char current_word[256]; int cw_len; DictionaryWord *prev_word;
    DictionaryWord *cands[24]; int ncand; DictionaryWord *selected;
    auto refresh = [&]() {
        int ws = 0;
        for (int i = k.input_len - 1; i >= 0; i--) if (k.input[i] == ' ') { ws = i + 1; break; }
        cw_len = k.input_len - ws;
        if (cw_len > 255) cw_len = 255;
        for (int i = 0; i < cw_len; i++) current_word[i] = k.input[ws + i];
        current_word[cw_len] = '\0';
        prev_word = nullptr;
        if (ws > 1) {
            int ps = 0;
            for (int i = ws - 2; i >= 0; i--) if (k.input[i] == ' ') { ps = i + 1; break; }
            char pw[256]; int pl = (ws - 1) - ps; if (pl > 255) pl = 255;
            for (int i = 0; i < pl; i++) pw[i] = k.input[ps + i];
            pw[pl] = '\0';
            prev_word = find_exact_word(root, pw);
        }
        ncand = predict_candidates(root, prev_word, current_word, cands, 24, ws == 0);
        bool same = true;
        for (int i = 0; i <= cw_len; i++) if (current_word[i] != sug_last[i]) { same = false; break; }
        if (!same) { sug_index = 0; for (int i = 0; i <= cw_len; i++) sug_last[i] = current_word[i]; }
        if (ncand == 0) sug_index = 0; else if (sug_index >= ncand) sug_index %= ncand;
        selected = ncand > 0 ? cands[sug_index] : nullptr;
    };
    refresh();

    auto ghost_len = [&]() -> int {
        if (!selected) return 0;
        int n = 0; while (selected->text[n]) n++;
        return n > cw_len ? n - cw_len : 0;
    };
    auto accept = [&](bool add_space) {
        // Match the case the player is typing (see draw_input_line): uppercase the
        // completed suffix when the current word's last typed char is uppercase.
        bool up = cw_len > 0 && k.input_len > 0 &&
                  k.input[k.input_len - 1] >= 'A' && k.input[k.input_len - 1] <= 'Z';
        if (ghost_len() > 0)
            for (int i = cw_len; selected->text[i] && k.input_len < KB_INPUT_MAX - 1; i++) {
                char c = selected->text[i];
                if (up && c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
                keyboard_type_char(&k, c);
            }
        if (add_space && k.input_len < KB_INPUT_MAX - 1) keyboard_type_char(&k, ' ');
        sug_index = 0;
    };

    bool at_end = (k.cursor == k.input_len);   // typeahead is active only at line end

    // Insert toggles which arrows move the caret vs cycle suggestions.
    if (ke.kind == SATURN_KEY_INSERT) { g_caret_arrows = !g_caret_arrows; ke.kind = SATURN_KEY_NONE; }

    // Text-caret movement: Ctrl+Left/Right by default, plain Left/Right when toggled.
    bool caret_l = g_caret_arrows ? (ke.kind == SATURN_KEY_LEFT)  : (ke.kind == SATURN_KEY_CTRL_LEFT);
    bool caret_r = g_caret_arrows ? (ke.kind == SATURN_KEY_RIGHT) : (ke.kind == SATURN_KEY_CTRL_RIGHT);
    if (caret_l) keyboard_caret_left(&k);
    if (caret_r) keyboard_caret_right(&k);

    // Suggestion cycling: the Autocomplete chord (default L/R), or whichever arrows
    // aren't moving the caret. Only when the caret is at the end of the line.
    bool kb_prev = g_caret_arrows ? (ke.kind == SATURN_KEY_CTRL_LEFT)  : (ke.kind == SATURN_KEY_LEFT);
    bool kb_next = g_caret_arrows ? (ke.kind == SATURN_KEY_CTRL_RIGHT) : (ke.kind == SATURN_KEY_RIGHT);
    bool cyc_prev = (pad && chord_fired(CA_AUTO, -1)) || kb_prev;
    bool cyc_next = (pad && chord_fired(CA_AUTO, +1)) || kb_next;
    if (at_end && ncand > 0 && cyc_prev) sug_index = (sug_index - 1 + ncand) % ncand;
    if (at_end && ncand > 0 && cyc_next) sug_index = (sug_index + 1) % ncand;
    selected = (at_end && ncand > 0) ? cands[sug_index] : nullptr;
    if (ke.kind == SATURN_KEY_LEFT || ke.kind == SATURN_KEY_RIGHT ||
        ke.kind == SATURN_KEY_CTRL_LEFT || ke.kind == SATURN_KEY_CTRL_RIGHT) ke.kind = SATURN_KEY_NONE;

    // Accept (A): commit the ghost with NO trailing space; with no ghost, submit the
    // line unless it already ends in a space (so a just-typed separator doesn't fire
    // the command). X commits the ghost + a space, or -- no ghost -- types a space to
    // begin the next word. Keyboard Tab keeps the classic accept + space.
    bool a_press   = pad && g_pad->WasPressed(face_button(FA_ACCEPT));
    bool x_press   = pad && pad_fired(Button::X);
    bool has_ghost = selected && ghost_len() > 0;
    if (a_press) {
        if (has_ghost) accept(false);
        else if (k.input_len == 0 || k.input[k.input_len - 1] != ' ') keyboard_submit(&k);
    }
    if (ke.kind == SATURN_KEY_TAB) {
        // Tab completes the suggestion with NO trailing space; if there's nothing
        // left to complete (already at the end of a word), it adds a space.
        if (has_ghost) accept(false);
        else if (at_end && k.input_len > 0 && k.input[k.input_len - 1] != ' ')
            keyboard_type_char(&k, ' ');
        ke.kind = SATURN_KEY_NONE;
    }
    if (x_press) {
        if (has_ghost) accept(true);
        else           keyboard_type_char(&k, ' ');
    }

    // Remaining keyboard events (typing a letter extends/overwrites at the caret).
    if      (ke.kind == SATURN_KEY_CHAR)      keyboard_type_char(&k, ke.ch);
    else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
    else if (ke.kind == SATURN_KEY_DELETE)    keyboard_delete_forward(&k);
    else if (ke.kind == SATURN_KEY_ENTER)     keyboard_submit(&k);
    else if (ke.kind == SATURN_KEY_CLEAR)     { k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; }  // Ctrl+C
    else if (ke.kind == SATURN_KEY_UP)        history_recall(&k, 1);
    else if (ke.kind == SATURN_KEY_DOWN)      history_recall(&k, 0);
    else                                      scroll_handle_key(ke);   // PgUp/Dn/Home/End

    refresh();             // reflect any edit before drawing
    if (k.cursor != k.input_len) selected = nullptr;   // no typeahead unless caret at end
    selected_out = selected;
    cw_len_out = cw_len;
}

// Mark the words on the currently visible console as on-screen, so objects the
// game just described lead their suggestions. Must be re-run after any trie
// rebuild (e.g. a mid-game difficulty change), since the marks live on the words.
static void typeahead_scan_screen(TrieNode *root) {
    char scr[1024]; int sp = 0;
    int total = console_line_count(), rows = console_height();
    int startln = (total > rows) ? (total - rows) : 0;
    for (int li = startln; li < total && sp < (int) sizeof(scr) - 1; li++) {
        const char* ln = console_get_line(li);
        for (int j = 0; ln[j] && sp < (int) sizeof(scr) - 1; j++) scr[sp++] = ln[j];
        if (sp < (int) sizeof(scr) - 1) scr[sp++] = ' ';
    }
    scr[sp] = '\0';
    typeahead_set_screen(root, scr);
}

extern "C" void saturn_readline(char *buf, int maxlen) {
    if (maxlen < 2) { if (maxlen > 0) buf[0] = '\0'; return; }
    // Disarm a quick-save destination that was never spent. F5 arms it and submits
    // in the same breath, so it is always consumed before we are back here -- unless
    // the story's save opcode bailed out early (a failed allocation or story re-read
    // never reaches saturn_save_blob), in which case a stale slot would silently
    // hijack the player's next save. Being at a prompt means it is spent or void.
    // The restore side can't be swept the same way: "Load Save Game" arms
    // g_restore_* before the very readline that submits its "restore".
    g_save_device = -1;
    g_save_slot   = -1;
    // "Load Save Game" queues a one-shot "restore" so the first turn applies the
    // pre-picked save (see g_restore_slot).
    if (g_autocmd != nullptr) {
        const char *c = g_autocmd; g_autocmd = nullptr;
        int n = 0;
        while (c[n] && n < maxlen - 2) { buf[n] = c[n]; n++; }
        buf[n] = '\n'; buf[n + 1] = '\0';
        return;
    }
    ensure_typeahead();                       // decode the game's dictionary/grammar
    typeahead_scan_screen(g_typeahead_root);  // on-screen objects lead suggestions

    // Keep the keyboard state (and cursor position) across prompts, so the picker
    // stays where the player left it instead of jumping back to 'a' every command.
    static KeyboardState k;
    static int kbd_inited = 0;
    if (!kbd_inited) { keyboard_reset(&k); kbd_inited = 1; }
    // Start a fresh input line but preserve cursor_row/cursor_col.
    k.input_len = 0;
    k.input[0] = '\0';
    k.cursor = 0;
    k.submitted = 0;
    // The interpreter runs between reads without refreshing input, so a button
    // still held from dismissing the title (or the previous command's submit) can
    // read as a fresh press here and instantly submit/type. Refresh once to turn
    // any held button into "held", not "just pressed", before we poll.
    SRL::Core::Synchronize();
    int sug_index = 0;           // which suggestion the player has cycled to
    char sug_last[256] = "";     // the typed word the cycle position belongs to
    // The turn's output has now been printed by the interpreter (or is the first
    // room). Position the view at the TOP of that output before the first draw, so a
    // long response shows from its start with "more v" and the player pages down --
    // rather than being dumped at the bottom.
    console_scroll_to_output();
    for (;;) {
      while (!k.submitted) {
        check_soft_reset();   // A+B+C+Start -> back to the title screen
        // Prefer the keyboard: only read the gamepad on frames with no keyboard
        // event, so a keyboard keypress (which also bleeds into pad button bits)
        // doesn't double-trigger. The gamepad still works whenever the keyboard is
        // idle, so on-screen navigation never gets stuck.
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;   // real keyboard in use: hide the on-screen one
        bool pad = (ke.kind == SATURN_KEY_NONE);
        if (pad && g_pad->AnyPressed()) g_kbd_visible = true;    // gamepad in use: show the on-screen keyboard
        pad_repeat_update();   // tick held-button auto-repeat (D-pad, A, C, B, Y, L, R)
        chord_tick();          // tick the configurable shift-chord repeat

        // Start (gamepad), Esc or F10 (keyboard) opens the Options menu mid-game.
        if ((pad && g_pad->WasPressed(Button::START)) || ke.kind == SATURN_KEY_ESCAPE
            || ke.kind == SATURN_KEY_F10) {
            options_menu();
            ensure_typeahead();                       // rebuild if the difficulty changed
            typeahead_scan_screen(g_typeahead_root);  // re-mark on-screen words on the new trie
            SRL::Core::Synchronize();    // consume the edge that closed the menu
            continue;
        }
        // F11: the controls page for the device in hand. That is always the
        // keyboard -- a gamepad has no F11, and this very keypress just marked the
        // keyboard as the active device above -- so unlike the Options menu's
        // equivalent row there is no pad branch to pick here.
        if (ke.kind == SATURN_KEY_F11) {
            keyboard_controls_page();
            menu_clear();
            SRL::Core::Synchronize();    // consume the edge that closed the page
            continue;
        }
        // F12: the Sound Options page directly, unless there's nothing to configure --
        // no CD-DA on the disc and no game .BLB -- the same availability the Options menu
        // uses to show/hide its Sound row. (F10 Options, F11 Controls, F12 Sound all open
        // from here.) The key is consumed either way so it never types.
        if (ke.kind == SATURN_KEY_F12) {
            if (music_cdda_has_audio() || sound_has_audio()) {
                sound_options_page();
                menu_clear();
                SRL::Core::Synchronize();    // consume the edge that closed the page
            }
            continue;
        }
        // Save/load function keys. These submit the game's own save/restore command
        // rather than doing the work here: the save blob lives in the interpreter,
        // so there is nothing main.cxx could write by itself. The quick variants
        // pre-pick the last committed slot (the mechanism the "Load Save Game" menu
        // already uses) so the blob hooks skip their pickers; with no slot used yet
        // they fall through to asking, which is all F2/F3 ever do.
        if (ke.kind == SATURN_KEY_F2 || ke.kind == SATURN_KEY_F5) {
            if (ke.kind == SATURN_KEY_F5 && g_last_slot >= 0) {
                g_save_device = g_last_device; g_save_slot = g_last_slot;
            }
            submit_command(k, "save");
            continue;   // k.submitted is set: the loop ends and the core takes it
        }
        if (ke.kind == SATURN_KEY_F3 || ke.kind == SATURN_KEY_F6 || ke.kind == SATURN_KEY_F9) {
            if (ke.kind != SATURN_KEY_F3 && g_last_slot >= 0) {
                g_restore_device = g_last_device; g_restore_slot = g_last_slot;
            }
            submit_command(k, "restore");
            continue;
        }

        DictionaryWord* selected; int cw_len;
        typeahead_edit(k, g_typeahead_root, sug_index, sug_last, ke, pad, selected, cw_len);

        pad_scroll_update();   // Z+L/R page, Z+D-pad line/Home/End -- with hold repeat
        render_console();
        render_keyboard(k, selected, cw_len);
        SRL::Core::Synchronize();
        sound_service();   // re-trigger looping sounds / reap finished one-shots
        music_tick();      // advance one-shot mixes / commits debounced switches (startup seek-window behavior tracked for branch review)
      }
      // Autocomplete-accept appends a trailing space; strip trailing spaces so the
      // parser, the reboot check, history, and the echo all see the clean command.
      while (k.input_len > 0 && k.input[k.input_len - 1] == ' ') k.input[--k.input_len] = '\0';
      g_scroll = 0;   // snap to the live bottom so the echoed command is visible
      history_push(k.input);   // remember the command for Up/Down recall
      // Echo the entered command onto the game's "> " prompt line so it stays in the
      // scrollback -- there's no OS echo here, and the input widget vanishes on submit.
      console_write(k.input, (unsigned int) k.input_len);
      console_write("\n", 1);
      g_output_start = console_total_lines();   // mark where the next turn's output begins
      render_console();
      if (is_reboot_command(k.input)) {
          confirm_return_to_title("reboot back to the title screen?");   // resets on Yes
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
          SRL::Core::Synchronize();
          continue;   // declined reboot is not passed to the game
      }
      // Quit is intercepted here and never handed to the interpreter, whose quit
      // opcode crashes on Saturn once accepted. Confirming keeps the one "are you
      // sure" step the game itself would have asked -- worth keeping now that "q"
      // is a first-word suggestion a stray accept could pick.
      if (is_quit_command(k.input)) {
          confirm_return_to_title("quit back to the title screen?");     // resets on Yes
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
          SRL::Core::Synchronize();
          continue;   // declined quit must not reach the game either
      }
      break;
    }
    int n = k.input_len;
    if (n > maxlen - 2) n = maxlen - 2;
    for (int i = 0; i < n; i++) buf[i] = k.input[i];
    buf[n]     = '\n';   // opcode_read strips this, matching fgets contract
    buf[n + 1] = '\0';
}

// Re-read the story image (used by opcode_restart). In the netbin build the
// story lives in .rodata, so this is a copy rather than a CD read. On CD the
// GFS size read can come back garbage, so retry the stat until it reports the
// expected size, then read the whole file. Returns 1 on success, 0 on failure.
extern "C" int saturn_read_story_file(uint8_t *buf, uint32_t len) {
#ifdef NETBIN
    const unsigned char *src = netbin_story_data();
    if (src == 0 || netbin_story_size() != len) return 0;
    for (uint32_t i = 0; i < len; i++) buf[i] = src[i];
    return 1;
#else
    for (int attempt = 0; attempt < 300; attempt++) {
        SRL::Cd::File f(g_story_filename);
        if (f.Size.SectorSize == 2048 && (uint32_t) f.Size.Bytes == len) {
            if (f.Open()) {
                int32_t got = f.Read((int32_t) len, buf);
                f.Close();
                if (got == (int32_t) len) { return 1; }
            }
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    return 0;
#endif
}

extern "C" void saturn_die(const char *fmt, ...) {
    (void) fmt;
    console_write("\n*** interpreter halted ***\n", 28);
    while (1) { render_console(); SRL::Core::Synchronize(); }
}

// ---- save / restore menu ---------------------------------------------------

// menu_clear, menu_window_rect, g_menu_backing_depth, MenuBacking, and
// menu_frame now live in menu.h/menu.cxx.


// Modal Y/N confirm, asking `question`. On Yes, soft-resets to the title screen
// in-process (the same return-to-title as the A+B+C+Start chord); configured
// options in backup RAM are retained. On No, returns false so the caller resumes.
// Shared by the reboot and quit commands -- both discard an unsaved game, so both
// ask first.
static bool confirm_return_to_title(const char *question) {
    MenuBacking backing;        // the box owns its area; no console shrink needed
    int qlen = 0;
    while (question[qlen]) qlen++;

    // Everything drawn inside the box counts toward its width, hints included.
    // The longest row that is not the question is the pad variant of the first
    // hint, "(A) (C) (Start) = yes" (21); the digit row is 15 and the second
    // hint is at most 12. Budgeted unconditionally so the box does not resize
    // when the player switches between the pad and a keyboard mid-prompt.
    int content_w = qlen;
    if (content_w < 21) content_w = 21;
    int x0, y0, w, h;
    menu_box_fit("RETURN TO TITLE", content_w, 5, &x0, &y0, &w, &h);

    SRL::Core::Synchronize();   // drop the submit edge that triggered this
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);   // so the prompt below matches the device in hand
        bool yes = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'y' || ke.ch == 'Y' || ke.ch == '1'))
                 || ke.kind == SATURN_KEY_ENTER
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::START)
                 || g_pad->WasPressed(Button::C);
        bool no  = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'n' || ke.ch == 'N' || ke.ch == '2'))
                 || ke.kind == SATURN_KEY_ESCAPE || g_pad->WasPressed(Button::B);
        if (yes) { soft_reset_to_title(); }         // in-process return to title; never returns
        if (no) { return false; }

        // The console still renders behind at full height; the box sits over it
        // and the VDP2 window keeps any image background out from under it. No
        // menu_clear() on purpose -- the game text staying visible behind the
        // box is the point of this prompt.
        render_console();
        menu_frame(x0, y0, w, h, "RETURN TO TITLE");
        int cx = x0 + 2, cy = y0 + 3;
        SRL::Debug::Print(cx, cy, "%s", question);
        if (!g_kbd_visible) SRL::Debug::Print(cx, cy + 2, "1) Yes    2) No");
        SRL::Debug::Print(cx, cy + 3, "%s", hint("(A) (C) (Start) = yes", "Y / Enter = yes"));
        SRL::Debug::Print(cx, cy + 4, "%s", hint("(B) = no", "N / Esc = no"));
        SRL::Core::Synchronize();
    }
}

// menu_wait, menu_message, and menu_select (with its MENU_SELECT_HINT_PAD/KBD
// hint strings) now live in menu.h/menu.cxx.

// valid_dialnum now lives in options.h/options.cxx.













// menu_confirm now lives in menu.h/menu.cxx.

// Pick a backup device (cartridge only if inserted) then a slot (showing each
// slot's existing save comment). Returns 1 with *out_device/*out_slot set, or 0.
static int choose_dest(const char *title_dev, const char *title_slot,
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

extern "C" int saturn_save_blob(const uint8_t *data, uint32_t len) {
    int device, slot;
    char comment[12];
    char name[12];
    if (g_save_slot >= 0) {
        // Quick-save (F5): straight to the last slot -- no device, slot, or
        // overwrite prompt, which is the entire point of it. Keep whatever name
        // the slot already carries so the picker still reads sensibly.
        device = g_save_device; slot = g_save_slot;
        g_save_device = -1; g_save_slot = -1;              // one-shot
        make_slot_name(name, slot);
        if (!saturn_bup_info(device, name, comment))
            snprintf(comment, sizeof(comment), "Save %d", slot + 1);
    } else {
        device = choose_device("SAVE - device?");
        if (device < 0) return 0;

        // Pick a slot and edit its name in place (cursor stays on the chosen line).
        if (!pick_slot_and_name(device, &slot, comment, 8)) return 0;
        if (comment[0] == 0) snprintf(comment, sizeof(comment), "Save %d", slot + 1);

        // Confirm before overwriting an existing save in that slot.
        make_slot_name(name, slot);
        char existing[12];
        if (saturn_bup_info(device, name, existing)) {
            char q[40];
            snprintf(q, sizeof(q), "Overwrite \"%s\"?", existing);
            if (!menu_confirm(q, "Are you sure?")) return 0;
        }
    }

    int ok = saturn_bup_write(device, name, comment, data, len);
    if (ok) { g_last_device = device; g_last_slot = slot; }   // what the quick keys reuse
    {
        MenuBacking backing;   // opaque over an image background while this is up
        menu_message("SAVE", ok ? "Saved." : "Save FAILED (no space?).",
                     "(press any key/button)");
        menu_wait();
    }
    return ok;
}

extern "C" int saturn_load_blob(uint8_t *buf, uint32_t maxlen) {
    (void) maxlen;
    int device, slot;
    if (g_restore_slot >= 0) {                 // slot pre-picked via "Load Save Game"
        device = g_restore_device; slot = g_restore_slot;
        g_restore_device = -1; g_restore_slot = -1;   // one-shot
    } else if (!choose_dest("RESTORE - device?", "RESTORE - slot?", &device, &slot)) {
        return 0;
    }
    char name[12];
    make_slot_name(name, slot);
    int ok = saturn_bup_read(device, name, buf);
    if (ok) { g_last_device = device; g_last_slot = slot; }   // what the quick keys reuse
    if (!ok) {
        MenuBacking backing;
        menu_message("RESTORE", "No save in that slot.", "(press any key/button)");
        menu_wait();
    }
    return ok;
}

// ---- online mode (multizork telnet terminal) -------------------------------


#define ONLINE_DIAL_ATTEMPTS 3   // auto-redial count (modem carrier training is flaky)

// True if the player wants to abort: Esc on the Saturn keyboard, or the L+R
// trigger chord on the gamepad (both triggers unused for typing).
static bool online_cancel_requested(void) {
    if (saturn_keyboard_poll().kind == SATURN_KEY_ESCAPE) return true;
    return g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R);
}

// Wait for any button/key, used on terminal error screens.
static void online_wait_any(void) {
    for (;;) {
        if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return;
        if (saturn_keyboard_poll().kind != SATURN_KEY_NONE) return;
        SRL::Core::Synchronize();
    }
}

// Wait until input has released AND settled before the terminal starts reading it,
// so nothing spurious is submitted to the server on connect. Powering the NetLink
// modem over the SMPC reboots the controllers/keyboard (they re-init from EEPROM),
// so for a short window after connect their peripheral reports can be stale or
// garbage; reading input during that window decodes the garbage as phantom
// keypresses that get typed and submitted as a scrambled line. Guard against it by
// requiring a sustained fully-idle streak -- covering both the raw held-state and
// the *decoded* key event the loop actually consumes -- after a minimum settle,
// while draining the keyboard decoder each frame so no stale repeat leaks through.
static void online_settle_input(void) {
    const int MIN_FRAMES  = 45;    // ~0.75s: let the rebooted peripherals stabilize
    const int IDLE_NEEDED = 10;    // consecutive fully-idle frames required to accept
    const int MAX_FRAMES  = 300;   // ~5s cap so we never hang if input never quiets
    int frames = 0, idle = 0;
    while ((frames < MIN_FRAMES || idle < IDLE_NEEDED) && frames < MAX_FRAMES) {
        SRL::Core::Synchronize();
        frames++;
        bool busy =
            saturn_keyboard_poll().kind != SATURN_KEY_NONE   // also drains decoder state
            || saturn_keyboard_any_down() != 0
            || g_pad->IsHeld(Button::A) || g_pad->IsHeld(Button::B)
            || g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::X)
            || g_pad->IsHeld(Button::START) || g_pad->IsHeld(Button::Up)
            || g_pad->IsHeld(Button::Down) || g_pad->IsHeld(Button::Left)
            || g_pad->IsHeld(Button::Right);
        idle = busy ? 0 : (idle + 1);
    }
}

// Typeahead for the online terminal. multizork is Zork I, so build the trie from
// the local ZORK1.Z3 on the CD (same dictionary + grammar + solution overlay).
// The story bytes are freed afterward since the trie is self-contained.
//
// Called from the boot preloads, inside the title screen's silent window, so its CD
// reads land where no music is playing. Doing it lazily on the first "Play Online"
// instead is what used to stop the menu track dead on the way into the online menu:
// the Saturn has one drive head, so every read here silences CD-DA, and the
// flaky-first-stat retry loop below turns that into a long stutter rather than a
// blip. It was invisible after a soft reset only because g_online_ta survives the
// longjmp, making the second pass a no-op. Still idempotent, and still re-runs when
// the difficulty changes (that path does read the CD again, but it is a deliberate
// options change rather than the boot path).
static TrieNode* g_online_ta = nullptr;
static int g_online_diff = -1;
static void ensure_online_typeahead(void) {
    if (g_online_ta != nullptr && g_online_diff == g_difficulty) return;
    if (g_online_ta) { destroy_typeahead(g_online_ta); g_online_ta = nullptr; }
    g_online_ta = create_trie_node();
    g_online_diff = g_difficulty;
    if (g_difficulty == DIFF_HARD) return;      // typeahead off
    char names[1][16];
    if (scan_z3_folder(names, 1) < 0) return;   // no Z3 folder -> empty trie (no suggestions)
    uint8_t* story = nullptr; uint32_t len = 0;
    for (int attempt = 0; attempt < 40 && story == nullptr; attempt++) {
        SRL::Cd::File f("ZORK1.Z3");
        int32_t bytes = f.Size.Bytes, ssz = f.Size.SectorSize;
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t* buf = (uint8_t*) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf != nullptr && f.Open()) {
                int32_t got = f.Read(bytes, buf); f.Close();
                if (got == bytes) { story = buf; len = (uint32_t) bytes; break; }
            }
            if (buf != nullptr) SRL::Memory::HighWorkRam::Free(buf);
        }
        // No music_cdda_play() here: on the boot path the menu track has not started
        // yet, and kicking it off mid-preload would only have the next retry's CD
        // read silence it again. Callers re-assert playback once the reads are done.
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    if (story != nullptr) {
        build_typeahead_from_story(g_online_ta, story, len);
        int have_solution = (g_difficulty != DIFF_HARD)
                          ? apply_solution_overlay(g_online_ta, story, len) : 0;
        typeahead_add_abbreviations(g_online_ta);
        typeahead_set_easy(g_difficulty == DIFF_EASY, have_solution);
        SRL::Memory::HighWorkRam::Free(story);
    }
}

// Connect to the multizork server and run the telnet terminal until the link
// drops or the player quits, then return to the mode menu. Auto-redials a few
// times because the NetLink<->DreamPi carrier handshake is probabilistic.
static void online_mode(void) {
    ensure_online_typeahead();   // normally already built by the boot preloads: a no-op
    // Only a difficulty change since boot makes the call above touch the CD (and so
    // stop CD-DA); going straight here on boot can also arrive before the menu track
    // has begun. Either way, make sure something is playing. But when the menu track is
    // already looping (the common case: it never stopped), leave it be so it stays
    // seamless rather than restarting from the top.
    if (!music_cdda_is_playing()) music_cdda_play(g_sel_track);
    const char *number = g_dialnum;   // change it in Options -> Network

    // ---- connect, with auto-redial on carrier-training failure ----
    // Scoped so the image-suppressing window covers the whole dialing sequence
    // but is dropped again before the terminal session takes the screen over.
    {
    MenuBacking backing;
    net_connect_result_t rc = NET_DIAL_FAIL;
    for (int attempt = 1; attempt <= ONLINE_DIAL_ATTEMPTS; attempt++) {
        {
            // Widest this can get is 37 columns, which is exactly what a
            // full-width 40-column box can draw -- no margin left. That fit
            // depends on BOTH DIALNUM_MAX (11) and ONLINE_DIAL_ATTEMPTS
            // staying a single digit; raising either pushes this past 37.
            // menu_box_fit clamps width to 40 silently, so an overflow here
            // does not report an error, it just overwrites the right border.
            char dial[40];
            snprintf(dial, sizeof(dial), "Dialing %s ... (attempt %d/%d)",
                     number, attempt, ONLINE_DIAL_ATTEMPTS);
            menu_message("ONLINE", dial,
                         hint("L+R = cancel", "Esc = cancel"));
            SRL::Core::Synchronize();
        }

        rc = net_connect_open(number);        // blocking (~35s timeout on failure)
        if (rc == NET_OK) break;
        if (rc == NET_NO_MODEM) break;        // hardware missing; redial won't help

        // NET_DIAL_FAIL: brief pause (lets the DreamPi return to idle), then retry.
        if (attempt < ONLINE_DIAL_ATTEMPTS) {
            menu_message("ONLINE", "No carrier. Retrying...",
                         hint("L+R = cancel", "Esc = cancel"));
            bool cancelled = false;
            for (int f = 0; f < 180; f++) {   // ~3s at 60Hz
                if (online_cancel_requested()) { cancelled = true; break; }
                // Redrawn every frame so the hint follows the input device the
                // player is actually using during the wait.
                menu_message("ONLINE", "No carrier. Retrying...",
                             hint("L+R = cancel", "Esc = cancel"));
                SRL::Core::Synchronize();
            }
            if (cancelled) { net_connect_close(); return; }
        }
    }

    if (rc != NET_OK) {
        menu_message("ONLINE",
            rc == NET_NO_MODEM ? "NetLink modem not found." : "Connection failed.",
            "(press any button)");
        online_wait_any();
        return;
    }
    }   // dialing box down: the terminal owns the screen from here

    menu_clear();   // wipe the box chrome before the console draws over it

    const cui_transport_t *tr = net_connect_transport();
    TermState ts; term_init(&ts);
    KeyboardState k; keyboard_reset(&k);
    console_init();
    online_settle_input();      // wait for input to release + settle after the
    keyboard_reset(&k);         // modem's SMPC power-on, then clear any residue

    // ---- terminal loop ----
    // L and R now page the scrollback, so switching page direction briefly holds
    // both. Require the L+R disconnect chord to be held deliberately (~0.75s) so a
    // quick direction change doesn't drop the connection.
    const int LR_DISCONNECT_HOLD = 45;
    int lr_hold = 0;
    int sug_index = 0;           // suggestion cycle position
    char sug_last[256] = "";     // the typed word the cycle position belongs to
    int last_scan_lines = -1;    // rescan on-screen words only when output grows
    for (;;) {
        term_service(&ts, tr, ZATURN_RX_BUDGET);   // RX -> console

        // Mark on-screen objects when the server prints new lines (room text etc.).
        int lc = console_line_count();
        if (lc != last_scan_lines) {
            last_scan_lines = lc;
            char scr[1024]; int sp = 0;
            int rows = console_height();
            int startln = (lc > rows) ? (lc - rows) : 0;
            for (int li = startln; li < lc && sp < (int) sizeof(scr) - 1; li++) {
                const char* ln = console_get_line(li);
                for (int j = 0; ln[j] && sp < (int) sizeof(scr) - 1; j++) scr[sp++] = ln[j];
                if (sp < (int) sizeof(scr) - 1) scr[sp++] = ' ';
            }
            scr[sp] = '\0';
            typeahead_set_screen(g_online_ta, scr);
        }

        check_soft_reset();   // A+B+C+Start -> back to the title screen
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;   // real keyboard in use: hide the on-screen one
        bool pad = (ke.kind == SATURN_KEY_NONE);
        if (pad && g_pad->AnyPressed()) g_kbd_visible = true;    // gamepad in use: show the on-screen keyboard
        pad_repeat_update();   // held-button auto-repeat (D-pad, A, C, B, Y, L, R)
        chord_tick();          // tick the configurable shift-chord repeat

        // Esc (keyboard) or a sustained L+R hold (gamepad) disconnects.
        bool lr = g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R);
        lr_hold = lr ? (lr_hold + 1) : 0;
        if (ke.kind == SATURN_KEY_ESCAPE || lr_hold >= LR_DISCONNECT_HOLD) {
            console_write("\n*** disconnected ***\n", 22);
            render_console();
            SRL::Core::Synchronize();
            break;
        }

        DictionaryWord* selected; int cw_len;
        typeahead_edit(k, g_online_ta, sug_index, sug_last, ke, pad, selected, cw_len);
        pad_scroll_update();   // Z+L/R page, Z+D-pad line/Home/End -- with hold repeat

        bool did_submit = k.submitted;
        if (k.submitted) {
            g_scroll = 0;   // streaming terminal: sending a line returns to the live bottom
            history_push(k.input);   // remember the command for Up/Down recall
            if (is_reboot_command(k.input)) {
                confirm_return_to_title("reboot back to the title screen?");  // resets on Yes
                keyboard_reset(&k);                // declined: don't send to server
                online_settle_input();
            } else {
                term_submit_line(tr, &k);          // echo + send line; resets keyboard
            }
        }

        if (!cui_transport_is_connected(tr)) {
            console_write("\n*** connection lost ***\n", 25);
            render_console();
            SRL::Core::Synchronize();
            break;
        }

        render_console();
        render_keyboard(k, did_submit ? nullptr : selected, did_submit ? 0 : cw_len);
        SRL::Debug::Print(0, 28, "%s", hint("L/R=cycle  hold L+R=disconnect", "Esc=disconnect"));
        SRL::Core::Synchronize();
    }
    net_connect_close();
}

int main(void) {
    SRL::Core::Initialize(HighColor::Colors::Black);
    netbin_sound_init();   // netbin only: SRL's CD-based driver load found no disc
    saturn_bup_init();
    cd_capture_root();              // must precede any GFS_SetDir: cd_enter_root() needs it
    display_scan_images();          // must precede options_load: display_decode()
    display_defaults(&g_display);   // validates image indices against this list
    options_load();   // restore saved difficulty (defaults to Easy)

    static MultiPad pads;
    g_pad = &pads;

    // Soft reset (A+B+C+Start, or the typed "reboot") longjmps back here to restart
    // at the title screen. The longjmp skips the normal unwinding, so the CD file
    // system can be left holding open handles / stale state from the previous run;
    // GFS_Reset() returns it to its clean post-init state (all handles closed,
    // current directory back to root) so every restart reloads from a known-good
    // state. The story image itself is owned by the Z-machine (initStory frees the
    // previous GState->story on the next boot), so we must NOT free it here -- doing
    // so double-frees it and corrupts the heap.
    int cd_reentry = setjmp(g_title_jmp);
    (void) cd_reentry;   // no step below branches on first-boot vs. reset anymore
    g_title_jmp_armed = true;
    GFS_Reset();
    cd_capture_root();   // GFS_Reset returns us to root; re-snapshot it there
    g_z3_dir_valid = false;   // the pre-reset Z3 table is stale until re-scanned
    // The soft reset longjmps here, which skips destructors -- so a MenuBacking
    // held by a page we jumped out of never ran. Clear it by hand, or NBG3
    // stays opaque and the title image is hidden for the rest of the session.
    g_menu_backing_depth = 0;
    slScrWindowModeNbg0(0);
    // Do NOT re-scan /TGA here. The first-boot scan (above) already registered
    // the image list and captured g_tga_tbl, and both are plain static RAM that
    // the longjmp leaves intact -- exactly like the cached game catalog, which
    // is likewise scanned once and reused across resets. A destructive re-scan
    // in the post-GFS_Reset state came up empty and WIPED the working list,
    // which is why every image vanished from the options menu after a return to
    // title. g_tga_tbl is proven durable across GFS_Reset: it is built before
    // the boot's own GFS_Reset (3456) and drives every load for the rest of that
    // session, so the same table stays valid across the reset that lands here.
    console_init();

    // Clear engine statics before the menu track. A soft reset longjmps here with
    // stale state (g_active_track > 0, backend still registered); zeroing it stops
    // a menu-frame music_tick() from firing the loop-end branch and leaking a stale
    // game track into the menu. The reset's internal stop is overridden on the next
    // line, and this is safe on first boot too (no backend registered yet).
    music_reset();                       // clear stale engine state (also on soft-reset re-entry)

    // Show the title art first (no prompt), then load the game catalog and the
    // background art while it is on screen, then start the menu music and let
    // title_and_seed add the prompt on the same screen. Those CD reads (Z3 folder scan
    // + each game's header, then every TGA, then ZORK1.Z3 for the online vocabulary)
    // stop CD-DA -- the Saturn's single drive head cannot play audio while reading data
    // -- so the load window is briefly silent by necessity. Doing all CD reads here
    // means no menu screen from here on (title, categories, games, Play Online, and the
    // Options background selector) ever touches the CD again, so the menu track then
    // plays uninterrupted. On soft-reset re-entry all three preloads are no-ops (already
    // cached), so no read happens and the music starts cleanly.
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
    text_set_color(DISP_RGB555(0xFF, 0xFF, 0xFF));
    title_bg_show("HOUSE.TGA");
    title_draw_art();
    SRL::Core::Synchronize();

    preload_game_catalog();              // CD reads happen once, here
    display_preload_images();            // and the background art, into Low Work RAM
    ensure_online_typeahead();           // and the online terminal's Zork I vocabulary

    music_set_level(g_music_level);      // honor the saved music level for menu audio
    music_cdda_play(g_sel_track);        // start the menu track; no CD reads remain in the menu flow

    int seed = title_and_seed();         // redraws the same art + "Press any button", waits
    display_apply();   // colors (or an image background) take over from the title

    // Top-level mode choice. "Play Online" runs the multizork telnet terminal
    // and returns here on disconnect; "Play Local" falls through to the offline
    // Z-machine flow below.
    static const char *modes[] = { "Play Local (single player)", "Play Online (multizork)",
                                   "Load Save Game", "Options" };
    const char* game_file = nullptr;

    for (;;) {
        int mode = menu_select("Z-ATURN", modes, 4);
        if (mode < 0) continue;   // Back at the root menu: nowhere to go up, so stay here
        if (mode == 3) { options_menu(); continue; }
        if (mode == 1) { online_mode(); continue; }
        if (mode == 2) {   // Load Save Game: pick a game, then one of its save slots.
            game_file = game_select();
            if (game_file == nullptr) continue;
            g_story_filename = game_file;   // so the slot names resolve to this game
            int device, slot;
            if (!choose_dest("LOAD - device?", "LOAD - slot?", &device, &slot)) continue;
            g_restore_device = device; g_restore_slot = slot;
            g_autocmd = "restore";          // the first turn applies the picked save
            break;
        }
        // Play Local (or B at this top menu): pick a game. Pressing B on the game
        // menu returns nullptr, so we loop back to this mode menu instead of trying
        // to load a bogus story file.
        game_file = game_select();
        if (game_file == nullptr) continue;
        break;
    }
    g_story_filename = game_file;   // save/restart must re-read the selected game

    uint8_t *story = nullptr;
    uint32_t len = 0;
#ifdef NETBIN
    // The story is embedded in .rodata. The Z-machine writes to its story
    // image and initStory takes ownership of this buffer, so copy into HWRAM
    // rather than pointing at the read-only original.
    {
        const unsigned char *src = netbin_story_data();
        uint32_t n = netbin_story_size();
        if (src != nullptr && n > 0) {
            uint8_t *buf = (uint8_t *) SRL::Memory::HighWorkRam::Malloc(n);
            if (buf != nullptr) {
                for (uint32_t i = 0; i < n; i++) buf[i] = src[i];
                story = buf; len = n;
            }
        }
    }
    if (story == nullptr) { saturn_die("Embedded story missing"); }
#else
    // Load *.Z3 from CD. GFS_GetFileSize (used by SRL::Cd::File's ctor) can
    // return an uninitialized size on first access, and File::Read relies on that
    // size for both its work-buffer and its read bound. So retry the stat until it
    // reports a sane size (2048-byte data sectors, plausible length), then allocate
    // exactly that and read the whole file. A wrong size corrupts the story buffer
    // and makes the interpreter crash / report bogus "Out of memory".
    for (int attempt = 0; attempt < 300 && story == nullptr; attempt++) {
        SRL::Cd::File f(game_file);
        int32_t bytes = f.Size.Bytes;
        int32_t ssz   = f.Size.SectorSize;
        SRL::Debug::Print(1, 26, "loading %s...           ", game_file);
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t *buf = (uint8_t *) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf != nullptr && f.Open()) {
                int32_t got = f.Read(bytes, buf);
                f.Close();
                if (got == bytes) { story = buf; len = (uint32_t) bytes; break; }
            }
            if (buf != nullptr) { SRL::Memory::HighWorkRam::Free(buf); }
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    if (story == nullptr) { saturn_die("Could not load %s from CD",game_file); }
#endif

    mojo_boot(story, len, seed);   // initStory takes ownership; it frees this on the next boot

    {   // enable sound if the game ships a sibling <base>.BLB
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
        blb[i] = '.'; blb[i+1] = 'B'; blb[i+2] = 'L'; blb[i+3] = 'B'; blb[i+4] = '\0';
        sound_init(blb);
        sound_set_level(g_pcm_level);     // honor the saved PCM level
        music_set_level(g_music_level);   // and the saved music level
        music_set_backend(music_cdda_play_mode);
        music_set_isplaying(music_cdda_is_playing);
        music_set_isshort(music_cdda_is_short);
        music_set_game((unsigned int)((story[2] << 8) | story[3]), (const char*) (story + 0x12));
        music_seed((unsigned int) seed);
        music_reset();                         // clear room cache for the new game
        music_set_mix(g_mix_mode, g_sel_track);
        music_start();                         // non-Dynamic starts now; Dynamic waits for first room
    }

    g_output_start = console_total_lines();   // mark before the first room; readline positions on it
    mojo_run();

    // The story has ended: mojo_run only returns once it sets quit, which the
    // prompt's own q/quit interception normally prevents -- but a death or victory
    // screen offers QUIT too, and that answer comes back here. Keeping the final
    // screen up forever (what this used to do) is the hang that made in-game Quit
    // look like a crash. Hold it until the player has read it, then return to the
    // title with menu music, the same place every other exit lands.
    render_console();
    SRL::Debug::Print(1, 27, "(press any key/button for the title screen)");
    menu_wait();
    soft_reset_to_title();   // never returns
    return 0;
}
