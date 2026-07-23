/*----------------------
 | online.cxx
 | Description: Network play. Dials the NetLink modem to the multizork server,
 |   then runs a telnet terminal that reuses the local game's console, on-screen
 |   keyboard and typeahead. The typeahead trie is built from the disc's ZORK1.Z3
 |   at boot (in the title screen's silent window) so its CD reads never interrupt
 |   the menu music.
 | Author: suinevere
 | Dependencies: online.h, net/net_connect.h (dialing + transport), term.h
 |   (telnet terminal), console.h/console_view.h (screen + on-screen keyboard),
 |   keyboard.h (KeyboardState), saturn_keyboard.h (key events), input.h (g_pad,
 |   pad repeat/scroll, history), typeahead.h + typeahead_extract.h +
 |   typeahead_solution.h (the trie), menu.h (dialing boxes), soft_reset.h
 |   (reboot command + confirm), music.h (menu-track playback), app_state.h
 |   (g_difficulty/g_dialnum/g_sel_track/g_scroll), game_catalog.h (Z3 scan), SRL.
 ----------------------*/

#include <srl.hpp>

#include "online.h"
#include "menu.h"
#include "console_view.h"
#include "input.h"
#include "soft_reset.h"
#include "game_catalog.h"
extern "C" {
#include "console.h"
#include "keyboard.h"
#include "saturn_keyboard.h"
#include "term.h"
#include "net/net_connect.h"
#include "typeahead.h"
#include "typeahead_extract.h"
#include "typeahead_solution.h"
#include "music.h"
}
#include "app_state.h"

using namespace SRL::Types;

/* snprintf links from newlib; the SRL dummy <stdio.h> omits its declaration. */
extern "C" int snprintf(char *str, size_t size, const char *fmt, ...);

#define ONLINE_DIAL_ATTEMPTS 3   /* auto-redial count (modem carrier training is flaky) */

/*----------------------
 | online_cancel_requested
 | Description: Reports the abort gesture -- Esc on the Saturn keyboard, or the
 |   L+R trigger chord on the gamepad (both triggers are unused for typing).
 | Author: suinevere
 | Dependencies: saturn_keyboard.h, input.h (g_pad)
 | Globals: g_pad
 | Params: N/A
 | Returns: true if the player asked to abort this frame
 ----------------------*/
static bool online_cancel_requested(void) {
    if (saturn_keyboard_poll().kind == SATURN_KEY_ESCAPE) return true;
    return g_pad->IsHeld(Button::L) && g_pad->IsHeld(Button::R);
}

/*----------------------
 | online_wait_any
 | Description: Blocks until any face button, Start, or a keyboard key is seen.
 |   Used to hold a terminal error screen until the player acknowledges it.
 | Author: suinevere
 | Dependencies: input.h (g_pad), saturn_keyboard.h, SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void online_wait_any(void) {
    for (;;) {
        if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return;
        if (saturn_keyboard_poll().kind != SATURN_KEY_NONE) return;
        SRL::Core::Synchronize();
    }
}

/*----------------------
 | online_settle_input
 | Description: Waits for input to release AND stay quiet before the terminal
 |   starts reading it, so nothing spurious is submitted on connect. Powering the
 |   NetLink modem over the SMPC reboots the controllers/keyboard (they re-init
 |   from EEPROM), and for a short window their peripheral reports can be stale or
 |   garbage that decodes into phantom keypresses. Requires a sustained
 |   fully-idle streak -- covering the raw held state AND the decoded key event
 |   the loop consumes -- after a minimum settle, draining the keyboard decoder
 |   every frame so no stale repeat leaks through. Caps at ~5s so it never hangs.
 | Author: suinevere
 | Dependencies: saturn_keyboard.h, input.h (g_pad), SRL
 | Globals: g_pad
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void online_settle_input(void) {
    const int MIN_FRAMES  = 45;
    const int IDLE_NEEDED = 10;
    const int MAX_FRAMES  = 300;
    int frames = 0, idle = 0;
    while ((frames < MIN_FRAMES || idle < IDLE_NEEDED) && frames < MAX_FRAMES) {
        SRL::Core::Synchronize();
        frames++;
        bool busy =
            saturn_keyboard_poll().kind != SATURN_KEY_NONE
            || saturn_keyboard_any_down() != 0
            || g_pad->IsHeld(Button::A) || g_pad->IsHeld(Button::B)
            || g_pad->IsHeld(Button::C) || g_pad->IsHeld(Button::X)
            || g_pad->IsHeld(Button::START) || g_pad->IsHeld(Button::Up)
            || g_pad->IsHeld(Button::Down) || g_pad->IsHeld(Button::Left)
            || g_pad->IsHeld(Button::Right);
        idle = busy ? 0 : (idle + 1);
    }
}

/*----------------------
 | g_online_ta / g_online_diff
 | Description: The online terminal's typeahead trie and the difficulty it was
 |   built for. Both survive the soft-reset longjmp, which is why a return to
 |   title makes the boot-time rebuild a no-op.
 | Author: suinevere
 ----------------------*/
static TrieNode* g_online_ta = nullptr;
static int g_online_diff = -1;

/*----------------------
 | ensure_online_typeahead
 | Description: Rebuilds g_online_ta from ZORK1.Z3 whenever it is missing or the
 |   difficulty changed; frees the story bytes afterward since the trie is
 |   self-contained. Hard difficulty leaves an empty trie (typeahead off). The
 |   retry loop works around GFS_GetFileSize returning an uninitialized size on
 |   first access. It issues no music_cdda_play: on the boot path the menu track
 |   has not started, and kicking it off mid-preload would only have the next
 |   retry's read silence it again -- callers re-assert playback once the reads
 |   are done.
 | Author: suinevere
 | Dependencies: game_catalog.h (scan_z3_folder), typeahead.h,
 |   typeahead_extract.h, typeahead_solution.h, SRL
 | Globals: g_online_ta, g_online_diff, g_difficulty
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void ensure_online_typeahead(void) {
    if (g_online_ta != nullptr && g_online_diff == g_difficulty) return;
    if (g_online_ta) { destroy_typeahead(g_online_ta); g_online_ta = nullptr; }
    g_online_ta = create_trie_node();
    g_online_diff = g_difficulty;
    if (g_difficulty == DIFF_HARD) return;
    char names[1][16];
    if (scan_z3_folder(names, 1) < 0) return;
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

/*----------------------
 | online_mode
 | Description: Ensures the typeahead is built (normally a no-op after the boot
 |   preloads) and makes sure the menu track is sounding -- but leaves an
 |   already-looping track alone so it stays seamless rather than restarting.
 |   Dials inside a MenuBacking-scoped image-suppressing window that covers the
 |   whole redial sequence and is dropped before the terminal takes the screen.
 |   The terminal loop services RX into the console, rescans on-screen words for
 |   the typeahead only when output grows, honors the global reboot command and
 |   the soft-reset chord, and disconnects on Esc or a deliberate ~0.75s L+R hold
 |   (L and R alone page the scrollback, so a brief chord must not drop the link).
 | Author: suinevere
 | Dependencies: net/net_connect.h, term.h, console.h, console_view.h, input.h,
 |   keyboard.h, saturn_keyboard.h, typeahead.h, menu.h, soft_reset.h, music.h,
 |   SRL
 | Globals: g_online_ta, g_dialnum, g_sel_track, g_scroll, g_pad, g_kbd_visible
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void online_mode(void) {
    ensure_online_typeahead();
    if (!music_cdda_is_playing()) music_cdda_play(g_sel_track);
    const char *number = g_dialnum;

    {
    MenuBacking backing;
    net_connect_result_t rc = NET_DIAL_FAIL;
    for (int attempt = 1; attempt <= ONLINE_DIAL_ATTEMPTS; attempt++) {
        {
            char dial[40];
            snprintf(dial, sizeof(dial), "Dialing %s ... (attempt %d/%d)",
                     number, attempt, ONLINE_DIAL_ATTEMPTS);
            menu_message("ONLINE", dial,
                         hint("L+R = cancel", "Esc = cancel"));
            SRL::Core::Synchronize();
        }

        rc = net_connect_open(number);
        if (rc == NET_OK) break;
        if (rc == NET_NO_MODEM) break;

        if (attempt < ONLINE_DIAL_ATTEMPTS) {
            menu_message("ONLINE", "No carrier. Retrying...",
                         hint("L+R = cancel", "Esc = cancel"));
            bool cancelled = false;
            for (int f = 0; f < 180; f++) {
                if (online_cancel_requested()) { cancelled = true; break; }
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
    }

    menu_clear();

    const cui_transport_t *tr = net_connect_transport();
    TermState ts; term_init(&ts);
    KeyboardState k; keyboard_reset(&k);
    console_init();
    online_settle_input();
    keyboard_reset(&k);

    const int LR_DISCONNECT_HOLD = 45;
    int lr_hold = 0;
    int sug_index = 0;
    char sug_last[256] = "";
    int last_scan_lines = -1;
    for (;;) {
        term_service(&ts, tr, ZATURN_RX_BUDGET);

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

        check_soft_reset();
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind != SATURN_KEY_NONE) g_kbd_visible = false;
        bool pad = (ke.kind == SATURN_KEY_NONE);
        if (pad && g_pad->AnyPressed()) g_kbd_visible = true;
        pad_repeat_update();
        chord_tick();

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
        pad_scroll_update();

        bool did_submit = k.submitted;
        if (k.submitted) {
            g_scroll = 0;
            history_push(k.input);
            if (is_reboot_command(k.input)) {
                confirm_return_to_title("reboot back to the title screen?");
                keyboard_reset(&k);
                online_settle_input();
            } else {
                term_submit_line(tr, &k);
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
