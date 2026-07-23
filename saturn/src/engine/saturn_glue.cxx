/*----------------------
 | saturn_glue.cxx
 | Description: The bridge between the C Z-Machine core (mojozork) and the Saturn
 |   client. Implements the hooks the interpreter calls through its ZMachineState
 |   function pointers -- text output, the interactive read loop, story re-read,
 |   fatal halt, and save/restore of the game blob -- plus the typeahead trie the
 |   local prompt drives and the typeahead allocator the core links against. The
 |   read loop is where a turn's input is gathered: it runs the mid-game menu
 |   shortcuts, the save/load function keys, and the shared typeahead editor, and
 |   intercepts the reboot/quit commands before they reach the interpreter.
 | Author: suinevere
 | Dependencies: saturn_glue.h, console.h + console_view.h (screen, on-screen
 |   keyboard, typeahead_edit), keyboard.h (KeyboardState), saturn_keyboard.h
 |   (key events), input.h (g_pad, pad repeat/scroll, history), typeahead.h +
 |   typeahead_extract.h + typeahead_solution.h (the trie), menu.h + menu_pages.h
 |   (mid-game menus and save dialogs), save_ui.h (device/slot pickers),
 |   saturn_backup.h (backup reads/writes), soft_reset.h (reboot/quit handling),
 |   sound.h + music.h (per-turn audio service), app_state.h (option/session
 |   globals), SRL.
 ----------------------*/

#include <srl.hpp>

#include "console_view.h"
#include "input.h"
#include "menu.h"
#include "menu_pages.h"
#include "save_ui.h"
#include "soft_reset.h"
extern "C" {
#include "saturn_glue.h"
#include "console.h"
#include "keyboard.h"
#include "saturn_keyboard.h"
#include "saturn_backup.h"
#include "sound.h"
#include "music.h"
#include "typeahead.h"
#include "typeahead_extract.h"
#include "typeahead_solution.h"
}
#include "app_state.h"

using namespace SRL::Types;

/* snprintf links from newlib; the SRL dummy <stdio.h> omits its declaration. */
extern "C" int snprintf(char *str, size_t size, const char *fmt, ...);

/*----------------------
 | typeahead_malloc / typeahead_free
 | Description: The allocator the C typeahead core links against, routed to the
 |   SRL heap. typeahead_malloc pulls from High Work RAM; typeahead_free returns
 |   to the generic SRL free (which handles either heap).
 | Author: suinevere
 ----------------------*/
extern "C" void* typeahead_malloc(unsigned int size) {
    return SRL::Memory::HighWorkRam::Malloc(size);
}

extern "C" void typeahead_free(void* ptr) {
    SRL::Memory::Free(ptr);
}

/*----------------------
 | g_typeahead_root / g_ta_story / g_ta_diff
 | Description: The local prompt's typeahead trie and the (story, difficulty) it
 |   was last built for, so ensure_typeahead can tell when a rebuild is due.
 | Author: suinevere
 ----------------------*/
static TrieNode* g_typeahead_root = nullptr;
static const uint8_t* g_ta_story = nullptr;
static int g_ta_diff = -1;

/*----------------------
 | ensure_typeahead
 | Description: Rebuilds g_typeahead_root from the currently loaded story whenever
 |   the story or the difficulty changes (freeing the old trie first), so
 |   switching games picks up the new vocabulary. Layers the story's own grammar,
 |   then the winning-path solution overlay (applied in BOTH Easy and Normal --
 |   Easy restricts suggestions to it, Normal uses its links as the "unless in
 |   solution" exception to the grammar filter), then the twelve stock
 |   abbreviations last so they land in whatever trie the story produced. Hard
 |   builds no trie at all, so it stays helpless -- the abbreviations exist iff a
 |   trie does.
 | Author: suinevere
 | Dependencies: saturn_glue.h (saturn_story_data), typeahead.h,
 |   typeahead_extract.h, typeahead_solution.h
 | Globals: g_typeahead_root, g_ta_story, g_ta_diff, g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void ensure_typeahead() {
    uint32_t len = 0;
    const uint8_t* story = saturn_story_data(&len);
    if (g_typeahead_root && story == g_ta_story && g_ta_diff == g_difficulty) return;
    if (g_typeahead_root) { destroy_typeahead(g_typeahead_root); g_typeahead_root = nullptr; }
    g_typeahead_root = create_trie_node();
    int have_solution = 0;
    if (story != nullptr && len > 0 && g_difficulty != DIFF_HARD) {
        build_typeahead_from_story(g_typeahead_root, story, len);
        have_solution = apply_solution_overlay(g_typeahead_root, story, len);
        typeahead_add_abbreviations(g_typeahead_root);
    }
    typeahead_set_easy(g_difficulty == DIFF_EASY, have_solution);
    g_ta_story = story;
    g_ta_diff = g_difficulty;
}

/*----------------------
 | saturn_writestr
 | Description: The interpreter's text-output hook: writes to the console and
 |   feeds the same text to the room-music classifier so Dynamic mix can react to
 |   what the room describes.
 | Author: suinevere
 | Dependencies: console.h, music.h
 | Globals: N/A
 | Params: str -- text to emit; slen -- its length
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_writestr(const char *str, size_t slen) {
    console_write(str, (unsigned int) slen);
    music_note_output(str, (unsigned int) slen);
}

/*----------------------
 | typeahead_scan_screen
 | Description: Marks the words currently visible on the console as on-screen, so
 |   objects the game just described lead their suggestions. Re-run after any trie
 |   rebuild (e.g. a mid-game difficulty change), since the marks live on the
 |   words.
 | Author: suinevere
 | Dependencies: console.h, console_view.h, typeahead.h
 | Globals: N/A
 | Params: root -- the trie whose words to mark
 | Returns: N/A
 ----------------------*/
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

/*----------------------
 | submit_command
 | Description: Puts `cmd` on the input line and marks it submitted, as if the
 |   player had typed it and pressed Enter -- so it echoes, enters history, and
 |   reaches the interpreter by the one path every other command uses. This is
 |   how the F-key shortcuts run the game's own save/restore.
 | Author: suinevere
 | Dependencies: keyboard.h
 | Globals: N/A
 | Params: k -- keyboard/input-line state to fill; cmd -- command text
 | Returns: N/A
 ----------------------*/
static void submit_command(KeyboardState &k, const char *cmd) {
    int n = 0;
    while (cmd[n] != '\0' && n < KB_INPUT_MAX - 1) { k.input[n] = cmd[n]; n++; }
    k.input[n] = '\0';
    k.input_len = n;
    k.cursor = n;
    k.submitted = 1;
}

/*----------------------
 | saturn_readline
 | Description: The interpreter's line-input hook, and the local game loop. First
 |   disarms any unspent quick-save destination (F5 arms and submits in one
 |   breath, so at a prompt it is always spent or void; a story save opcode that
 |   bailed early would otherwise leave a stale slot to hijack the next save -- the
 |   restore side can't be swept the same way, since "Load Save Game" arms
 |   g_restore_* before the very readline that submits its "restore"). A queued
 |   one-shot autocommand (the "restore" that applies a pre-picked save) is
 |   returned immediately. Otherwise it rebuilds the typeahead, marks on-screen
 |   words, keeps the keyboard picker position across prompts, and positions the
 |   view at the TOP of the turn's output so a long response reads from its start.
 |   The frame loop runs the soft-reset chord, the F10/F11/F12 menu shortcuts
 |   (Sound only when there is audio to configure), the F2/F5 save and
 |   F3/F6/F9 restore keys (which submit the game's own command so the blob hooks
 |   do the work), and the shared typeahead editor, then services audio. On
 |   submit it strips the autocomplete-accept trailing space, echoes the command,
 |   and intercepts reboot/quit (a declined confirm is not passed to the game)
 |   before handing the line back with the fgets-style trailing '\n'.
 | Author: suinevere
 | Dependencies: console.h, console_view.h, keyboard.h, saturn_keyboard.h,
 |   input.h, menu.h, menu_pages.h, soft_reset.h, sound.h, music.h, typeahead.h,
 |   SRL
 | Globals: g_save_device, g_save_slot, g_last_device, g_last_slot,
 |   g_restore_device, g_restore_slot, g_autocmd, g_kbd_visible, g_scroll,
 |   g_output_start, g_pad, g_typeahead_root
 | Params: buf -- receives the entered line + '\n'; maxlen -- capacity of buf
 | Returns: N/A
 ----------------------*/
extern "C" void saturn_readline(char *buf, int maxlen) {
    if (maxlen < 2) { if (maxlen > 0) buf[0] = '\0'; return; }
    g_save_device = -1;
    g_save_slot   = -1;
    if (g_autocmd != nullptr) {
        const char *c = g_autocmd; g_autocmd = nullptr;
        int n = 0;
        while (c[n] && n < maxlen - 2) { buf[n] = c[n]; n++; }
        buf[n] = '\n'; buf[n + 1] = '\0';
        return;
    }
    ensure_typeahead();
    typeahead_scan_screen(g_typeahead_root);

    static KeyboardState k;
    static int kbd_inited = 0;
    if (!kbd_inited) { keyboard_reset(&k); kbd_inited = 1; }
    k.input_len = 0;
    k.input[0] = '\0';
    k.cursor = 0;
    k.submitted = 0;
    SRL::Core::Synchronize();
    int sug_index = 0;
    char sug_last[256] = "";
    console_scroll_to_output();
    for (;;) {
      while (!k.submitted) {
        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;
        bool pad = (ke.kind == SATURN_KEY_NONE);
        if (pad && g_pad->AnyPressed()) g_kbd_visible = true;
        pad_repeat_update();
        chord_tick();

        if ((pad && g_pad->WasPressed(Button::START)) || ke.kind == SATURN_KEY_ESCAPE
            || ke.kind == SATURN_KEY_F10) {
            options_menu();
            ensure_typeahead();
            typeahead_scan_screen(g_typeahead_root);
            SRL::Core::Synchronize();
            continue;
        }
        if (ke.kind == SATURN_KEY_F11) {
            keyboard_controls_page();
            menu_clear();
            SRL::Core::Synchronize();
            continue;
        }
        if (ke.kind == SATURN_KEY_F12) {
            if (music_cdda_has_audio() || sound_has_audio()) {
                sound_options_page();
                menu_clear();
                SRL::Core::Synchronize();
            }
            continue;
        }
        if (ke.kind == SATURN_KEY_F2 || ke.kind == SATURN_KEY_F5) {
            if (ke.kind == SATURN_KEY_F5 && g_last_slot >= 0) {
                g_save_device = g_last_device; g_save_slot = g_last_slot;
            }
            submit_command(k, "save");
            continue;
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

        pad_scroll_update();
        render_console();
        render_keyboard(k, selected, cw_len);
        SRL::Core::Synchronize();
        sound_service();
        music_tick();
      }
      while (k.input_len > 0 && k.input[k.input_len - 1] == ' ') k.input[--k.input_len] = '\0';
      g_scroll = 0;
      history_push(k.input);
      console_write(k.input, (unsigned int) k.input_len);
      console_write("\n", 1);
      g_output_start = console_total_lines();
      render_console();
      if (is_reboot_command(k.input)) {
          confirm_return_to_title("reboot back to the title screen?");
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
          SRL::Core::Synchronize();
          continue;
      }
      if (is_quit_command(k.input)) {
          confirm_return_to_title("quit back to the title screen?");
          k.input_len = 0; k.input[0] = '\0'; k.cursor = 0; k.submitted = 0;
          SRL::Core::Synchronize();
          continue;
      }
      break;
    }
    int n = k.input_len;
    if (n > maxlen - 2) n = maxlen - 2;
    for (int i = 0; i < n; i++) buf[i] = k.input[i];
    buf[n]     = '\n';
    buf[n + 1] = '\0';
}

/*----------------------
 | saturn_read_story_file
 | Description: Re-reads the loaded story image from CD for opcode_restart (there
 |   is no fopen on Saturn). The GFS size read can come back garbage on first
 |   access, so it retries the stat until the sector size and byte count match the
 |   expected length, then reads the whole file.
 | Author: suinevere
 | Dependencies: app_state.h (g_story_filename), SRL
 | Globals: g_story_filename
 | Params: buf -- destination; len -- expected story length in bytes
 | Returns: 1 on success, 0 on failure
 ----------------------*/
extern "C" int saturn_read_story_file(uint8_t *buf, uint32_t len) {
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
}

/*----------------------
 | saturn_die
 | Description: The interpreter's fatal-halt hook. Prints a halt notice and spins
 |   forever redrawing the console, so the last screen stays readable. Never
 |   returns.
 | Author: suinevere
 | Dependencies: console.h, console_view.h, SRL
 | Globals: N/A
 | Params: fmt -- unused format string from the interpreter
 | Returns: N/A (does not return)
 ----------------------*/
extern "C" void saturn_die(const char *fmt, ...) {
    (void) fmt;
    console_write("\n*** interpreter halted ***\n", 28);
    while (1) { render_console(); SRL::Core::Synchronize(); }
}

/*----------------------
 | saturn_save_blob
 | Description: The interpreter's save hook. A pre-armed quick-save (F5) goes
 |   straight to the last slot with no device/slot/overwrite prompt -- the whole
 |   point of it -- keeping whatever name the slot already carries. Otherwise it
 |   picks a device, runs the slot picker + in-place name editor, defaults an
 |   empty name to "Save N", and confirms before overwriting an existing save.
 |   Records the committed device/slot as the quick-key target, then reports the
 |   result over an opaque backing so it reads over an image background.
 | Author: suinevere
 | Dependencies: save_ui.h (choose_device/pick_slot_and_name/make_slot_name),
 |   saturn_backup.h, menu.h, SRL
 | Globals: g_save_device, g_save_slot, g_last_device, g_last_slot
 | Params: data -- blob to write; len -- its length
 | Returns: 1 on success, 0 on cancel or failure
 ----------------------*/
extern "C" int saturn_save_blob(const uint8_t *data, uint32_t len) {
    int device, slot;
    char comment[12];
    char name[12];
    if (g_save_slot >= 0) {
        device = g_save_device; slot = g_save_slot;
        g_save_device = -1; g_save_slot = -1;
        make_slot_name(name, slot);
        if (!saturn_bup_info(device, name, comment))
            snprintf(comment, sizeof(comment), "Save %d", slot + 1);
    } else {
        device = choose_device("SAVE - device?");
        if (device < 0) return 0;

        if (!pick_slot_and_name(device, &slot, comment, 8)) return 0;
        if (comment[0] == 0) snprintf(comment, sizeof(comment), "Save %d", slot + 1);

        make_slot_name(name, slot);
        char existing[12];
        if (saturn_bup_info(device, name, existing)) {
            char q[40];
            snprintf(q, sizeof(q), "Overwrite \"%s\"?", existing);
            if (!menu_confirm(q, "Are you sure?")) return 0;
        }
    }

    int ok = saturn_bup_write(device, name, comment, data, len);
    if (ok) { g_last_device = device; g_last_slot = slot; }
    {
        MenuBacking backing;
        menu_message("SAVE", ok ? "Saved." : "Save FAILED (no space?).",
                     "(press any key/button)");
        menu_wait();
    }
    return ok;
}

/*----------------------
 | saturn_load_blob
 | Description: The interpreter's restore hook. A pre-picked slot (armed by "Load
 |   Save Game") is consumed once; otherwise it runs choose_dest to resolve a
 |   device and slot. Reads the blob, records the device/slot as the quick-key
 |   target on success, and reports an empty slot to the player.
 | Author: suinevere
 | Dependencies: save_ui.h (choose_dest/make_slot_name), saturn_backup.h, menu.h
 | Globals: g_restore_device, g_restore_slot, g_last_device, g_last_slot
 | Params: buf -- destination for the blob; maxlen -- capacity (unused; the
 |   backup layer knows the record size)
 | Returns: 1 on success, 0 on cancel or failure
 ----------------------*/
extern "C" int saturn_load_blob(uint8_t *buf, uint32_t maxlen) {
    (void) maxlen;
    int device, slot;
    if (g_restore_slot >= 0) {
        device = g_restore_device; slot = g_restore_slot;
        g_restore_device = -1; g_restore_slot = -1;
    } else if (!choose_dest("RESTORE - device?", "RESTORE - slot?", &device, &slot)) {
        return 0;
    }
    char name[12];
    make_slot_name(name, slot);
    int ok = saturn_bup_read(device, name, buf);
    if (ok) { g_last_device = device; g_last_slot = slot; }
    if (!ok) {
        MenuBacking backing;
        menu_message("RESTORE", "No save in that slot.", "(press any key/button)");
        menu_wait();
    }
    return ok;
}
