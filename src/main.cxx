#include <srl.hpp>

extern "C" {
#include "console.h"
#include "keyboard.h"
#include "saturn_keyboard.h"
#include "saturn_glue.h"
}

using namespace SRL::Types;
using Button = SRL::Input::Digital::Button;

// One shared gamepad on port 0, used by the readline hook.
static SRL::Input::Digital *g_pad = nullptr;

// ---- rendering -------------------------------------------------------------

static const int CONSOLE_ROWS = 22;   // rows 0..21

static void render_console(void) {
    int total = console_line_count();
    int start = (total > CONSOLE_ROWS) ? (total - CONSOLE_ROWS) : 0;
    for (int r = 0; r < CONSOLE_ROWS; r++) {
        SRL::Debug::PrintClearLine(r);
        int li = start + r;
        if (li < total) {
            SRL::Debug::Print(0, r, "%s", console_get_line(li));
        }
    }
}

static void render_keyboard(const KeyboardState &k) {
    SRL::Debug::PrintClearLine(22);
    SRL::Debug::Print(0, 22, "> %s_", k.input);
    for (int r = 0; r < KB_ROWS; r++) {
        char rowbuf[KB_COLS * 2 + 1];
        int p = 0;
        for (int c = 0; c < KB_COLS; c++) {
            rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
            rowbuf[p++] = KB_LAYOUT[r][c];
        }
        rowbuf[p] = '\0';
        SRL::Debug::PrintClearLine(23 + r);
        SRL::Debug::Print(2, 23 + r, "%s", rowbuf);
    }
    SRL::Debug::Print(0, 27, "A=enter B=delete C=type X=space");
}

// ---- hooks (extern "C" so the C core can call them) ------------------------

// Search a writestr chunk for a literal substring (no libc dependency).
static bool chunk_contains(const char *s, size_t n, const char *needle) {
    size_t nl = 0; while (needle[nl]) nl++;
    if (nl == 0 || n < nl) return false;
    for (size_t i = 0; i + nl <= n; i++) {
        size_t j = 0;
        while (j < nl && s[i + j] == needle[j]) j++;
        if (j == nl) return true;
    }
    return false;
}

extern "C" void saturn_writestr(const char *str, size_t slen) {
    console_write(str, (unsigned int) slen);
    // After Zork prints its banner (the "Release 88 / Serial number ..." line),
    // append the Saturn port credit, once.
    static int credit_shown = 0;
    if (!credit_shown && chunk_contains(str, slen, "All rights reserved.")) {
        credit_shown = 1;
        static const char credit[] = "\nSaturn port (c) 2026 by Suinevere.\n";
        console_write(credit, (unsigned int) (sizeof(credit) - 1));
    }
}

extern "C" void saturn_readline(char *buf, int maxlen) {
    if (maxlen < 2) { if (maxlen > 0) buf[0] = '\0'; return; }
    // Keep the keyboard state (and cursor position) across prompts, so the picker
    // stays where the player left it instead of jumping back to 'a' every command.
    static KeyboardState k;
    static int kbd_inited = 0;
    if (!kbd_inited) { keyboard_reset(&k); kbd_inited = 1; }
    // Start a fresh input line but preserve cursor_row/cursor_col.
    k.input_len = 0;
    k.input[0] = '\0';
    k.submitted = 0;
    // The interpreter runs between reads without refreshing input, so a button
    // still held from dismissing the title (or the previous command's submit) can
    // read as a fresh press here and instantly submit/type. Refresh once to turn
    // any held button into "held", not "just pressed", before we poll.
    SRL::Core::Synchronize();
    while (!k.submitted) {
        if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
        if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
        if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
        if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
        if (g_pad->WasPressed(Button::C))     keyboard_type(&k);       // C = type letter
        if (g_pad->WasPressed(Button::X))     keyboard_type_char(&k, ' '); // X = space
        if (g_pad->WasPressed(Button::B))     keyboard_backspace(&k);  // B = delete
        if (g_pad->WasPressed(Button::A) ||
            g_pad->WasPressed(Button::START)) keyboard_submit(&k);     // A = enter/submit
        // A real Saturn keyboard (if connected) works alongside the on-screen picker.
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind == SATURN_KEY_CHAR)           keyboard_type_char(&k, ke.ch);
        else if (ke.kind == SATURN_KEY_BACKSPACE) keyboard_backspace(&k);
        else if (ke.kind == SATURN_KEY_ENTER)     keyboard_submit(&k);
        render_console();
        render_keyboard(k);
        SRL::Core::Synchronize();
    }
    int n = k.input_len;
    if (n > maxlen - 2) n = maxlen - 2;
    for (int i = 0; i < n; i++) buf[i] = k.input[i];
    buf[n]     = '\n';   // opcode_read strips this, matching fgets contract
    buf[n + 1] = '\0';
}

// Re-read the story image from CD (used by opcode_restart). The GFS size read can
// come back garbage, so retry the stat until it reports the expected size, then read
// the whole file. Returns 1 on success, 0 on failure.
extern "C" int saturn_read_story_file(uint8_t *buf, uint32_t len) {
    for (int attempt = 0; attempt < 300; attempt++) {
        SRL::Cd::File f("ZORK1.DAT");
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

extern "C" void saturn_die(const char *fmt, ...) {
    (void) fmt;
    console_write("\n*** interpreter halted ***\n", 28);
    while (1) { render_console(); SRL::Core::Synchronize(); }
}

// ---- boot ------------------------------------------------------------------

// Title screen: wait for any face button; use elapsed frames as an RNG seed.
static int title_and_seed(void) {
    int frames = 0;
    for (;;) {
        // Advance on any gamepad face button or any keyboard key. Polling the
        // keyboard here (rather than a raw "key down" check) also records the
        // held key so it doesn't immediately type into the first prompt.
        bool advance =
            g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START) ||
            (saturn_keyboard_poll().kind != SATURN_KEY_NONE);
        if (advance) break;
        SRL::Debug::Print(6, 12, "M O J O Z O R K   ---   Z O R K   I");
        SRL::Debug::Print(7, 15, "Press any button or key to begin");
        SRL::Core::Synchronize();
        frames++;
    }
    return frames | 1;   // avoid a zero seed
}

int main(void) {
    SRL::Core::Initialize(HighColor::Colors::Black);
    console_init();

    static SRL::Input::Digital pad(0);
    g_pad = &pad;

    int seed = title_and_seed();

    // Load ZORK1.DAT from CD. GFS_GetFileSize (used by SRL::Cd::File's ctor) can
    // return an uninitialized size on first access, and File::Read relies on that
    // size for both its work-buffer and its read bound. So retry the stat until it
    // reports a sane size (2048-byte data sectors, plausible length), then allocate
    // exactly that and read the whole file. A wrong size corrupts the story buffer
    // and makes the interpreter crash / report bogus "Out of memory".
    uint8_t *story = nullptr;
    uint32_t len = 0;
    for (int attempt = 0; attempt < 300 && story == nullptr; attempt++) {
        SRL::Cd::File f("ZORK1.DAT");
        int32_t bytes = f.Size.Bytes;
        int32_t ssz   = f.Size.SectorSize;
        SRL::Debug::Print(2, 20, "loading ZORK1.DAT  try=%d size=%d ssz=%d      ",
                          attempt, (int) bytes, (int) ssz);
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
    if (story == nullptr) { saturn_die("Could not load ZORK1.DAT from CD"); }

    mojo_boot(story, len, seed);
    mojo_run();

    // Game ended: keep the final screen up.
    while (1) { render_console(); SRL::Core::Synchronize(); }
    return 0;
}
