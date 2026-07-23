/*----------------------
 | soft_reset.cxx
 | Description: The in-process soft reset -- the Sega-mandated A+B+C+Start chord
 |   and the typed "reboot"/"quit" commands that return the player to the title
 |   screen via longjmp, without an SMPC/hardware reset. Owns the reboot/quit
 |   command recognizers, the chord poller, the modal confirm, and the longjmp
 |   itself. main.cxx arms the setjmp target (g_title_jmp, in app_state) just
 |   before the title screen.
 | Author: suinevere
 | Dependencies: soft_reset.h, app_state.h (g_title_jmp), net/net_connect.h
 |   (drops the modem link), sound.h (stops audio), input.h (g_pad), menu.h +
 |   menu_layout.h (the confirm box), console_view.h (hint/device/render),
 |   saturn_keyboard.h (key events), SRL/SGL.
 ----------------------*/

#include <srl.hpp>

#include "soft_reset.h"
#include "menu.h"
#include "console_view.h"
#include "input.h"
extern "C" {
#include "menu_layout.h"
#include "saturn_keyboard.h"
#include "sound.h"
#include "net/net_connect.h"
}
#include "app_state.h"

using namespace SRL::Types;

/*----------------------
 | is_reboot_command
 | Description: Case-insensitive exact match against "reboot", one char at a time
 |   so no <string.h> is pulled in.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: line -- the raw input line to test
 | Returns: non-zero when the line is exactly the reboot command
 ----------------------*/
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

/*----------------------
 | is_alnum_ch
 | Description: True for ASCII letters and digits -- the word-character test the
 |   quit recognizer breaks tokens on.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: c -- byte to classify
 | Returns: non-zero for [A-Za-z0-9]
 ----------------------*/
static int is_alnum_ch(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/*----------------------
 | is_quit_command
 | Description: Matches "q" or "quit" as the first word of `line`, a safety
 |   boundary that intercepts the story's QUIT before the interpreter's own quit
 |   opcode (which crashes on Saturn) can run. It errs toward over-matching: a
 |   Z-machine tokenizer breaks on its declared separators (Zork I: , . ") with or
 |   without a space, so "quit." reads as QUIT to the game -- splitting on spaces
 |   alone would let it through. Any non-alphanumeric char therefore ends the
 |   word and the rest is ignored ("quit now" is caught too); the worst case for
 |   over-matching is a confirm prompt the player declines.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: line -- the raw input line to test
 | Returns: non-zero when the first word is q/quit
 ----------------------*/
int is_quit_command(const char *line) {
    while (*line != '\0' && !is_alnum_ch(*line)) line++;
    char w[6];
    int n = 0;
    for (; is_alnum_ch(line[n]) && n < 5; n++) {
        char c = line[n];
        w[n] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
    }
    w[n] = '\0';
    return w[0] == 'q' && (w[1] == '\0' ||
           (w[1] == 'u' && w[2] == 'i' && w[3] == 't' && w[4] == '\0'));
}

/*----------------------
 | soft_reset_to_title
 | Description: Drops any live modem link and stops all audio, then longjmps back
 |   to the title screen armed in main -- an in-process restart, so the console
 |   never re-reads the CD. If the jump target is not armed yet (impossible once
 |   main is running), falls back to the SMPC reset-button NMI and spins. Never
 |   returns.
 | Author: suinevere
 | Dependencies: net/net_connect.h, sound.h, SGL (slNMIRequest)
 | Globals: g_title_jmp, g_title_jmp_armed
 | Params: N/A
 | Returns: N/A (does not return)
 ----------------------*/
void soft_reset_to_title(void) {
    net_connect_close();
    sound_stop_all();
    if (g_title_jmp_armed) longjmp(g_title_jmp, 1);
    slNMIRequest();
    while (1) {}
}

/*----------------------
 | SOFT_RESET_HOLD
 | Description: Frames the A+B+C+Start chord must be held before it fires. A
 |   debounce, not a feature: it rejects the garbage peripheral read on the very
 |   first frame (before the first vsync has polled real input), which otherwise
 |   reads as "all held" and resets instantly. ~0.5s is well below any real
 |   four-button hold.
 | Author: suinevere
 ----------------------*/
static const int SOFT_RESET_HOLD = 30;

/*----------------------
 | soft_reset_chord_held
 | Description: True when A, B, C and Start are all physically held this frame.
 | Author: suinevere
 | Dependencies: input.h (g_pad)
 | Globals: g_pad
 | Params: N/A
 | Returns: true while the reset chord is held
 ----------------------*/
bool soft_reset_chord_held(void) {
    return g_pad->IsHeld(Button::A) && g_pad->IsHeld(Button::B) &&
           g_pad->IsHeld(Button::C) && g_pad->IsHeld(Button::START);
}

/*----------------------
 | check_soft_reset
 | Description: Counts consecutive frames the chord is held (a file-static
 |   counter) and soft-resets to the title once it reaches SOFT_RESET_HOLD. Call
 |   once per frame from every input loop; never returns once the threshold is
 |   crossed.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void check_soft_reset(void) {
    static int hold = 0;
    hold = soft_reset_chord_held() ? (hold + 1) : 0;
    if (hold >= SOFT_RESET_HOLD) soft_reset_to_title();
}

/*----------------------
 | confirm_return_to_title
 | Description: Draws a modal Y/N box over the still-rendered console (no
 |   menu_clear -- the game text staying visible behind the box is the point) and
 |   loops until the player answers. The box width is budgeted unconditionally for
 |   the widest row, the pad hint "(A) (C) (Start) = yes" (21 cols), so it does
 |   not resize when the player switches between pad and keyboard mid-prompt. Yes
 |   soft-resets to the title (never returns); No returns false so the caller
 |   resumes the game.
 | Author: suinevere
 | Dependencies: menu.h, menu_layout.h, console_view.h, saturn_keyboard.h,
 |   input.h, SRL
 | Globals: g_pad, g_kbd_visible
 | Params: question -- the yes/no question shown in the box
 | Returns: false when the player declines (Yes does not return)
 ----------------------*/
bool confirm_return_to_title(const char *question) {
    MenuBacking backing;
    int qlen = 0;
    while (question[qlen]) qlen++;

    int content_w = qlen;
    if (content_w < 21) content_w = 21;
    int x0, y0, w, h;
    menu_box_fit("RETURN TO TITLE", content_w, 5, &x0, &y0, &w, &h);

    SRL::Core::Synchronize();
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        note_input_device(ke);
        bool yes = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'y' || ke.ch == 'Y' || ke.ch == '1'))
                 || ke.kind == SATURN_KEY_ENTER
                 || g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::START)
                 || g_pad->WasPressed(Button::C);
        bool no  = (ke.kind == SATURN_KEY_CHAR && (ke.ch == 'n' || ke.ch == 'N' || ke.ch == '2'))
                 || ke.kind == SATURN_KEY_ESCAPE || g_pad->WasPressed(Button::B);
        if (yes) { soft_reset_to_title(); }
        if (no) { return false; }

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
