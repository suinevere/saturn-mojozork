#include <srl.hpp>

extern "C" {
#include "console.h"
#include "keyboard.h"
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
    SRL::Debug::PrintClearLine(23);
    SRL::Debug::Print(0, 23, "> %s_", k.input);
    for (int r = 0; r < KB_ROWS; r++) {
        char rowbuf[KB_COLS * 2 + 1];
        int p = 0;
        for (int c = 0; c < KB_COLS; c++) {
            rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
            rowbuf[p++] = KB_LAYOUT[r][c];
        }
        rowbuf[p] = '\0';
        SRL::Debug::PrintClearLine(24 + r);
        SRL::Debug::Print(2, 24 + r, "%s", rowbuf);
    }
    SRL::Debug::Print(0, 28, "A=type B=del START=enter");
}

// ---- hooks (extern "C" so the C core can call them) ------------------------

extern "C" void saturn_writestr(const char *str, size_t slen) {
    console_write(str, (unsigned int) slen);
}

extern "C" void saturn_readline(char *buf, int maxlen) {
    KeyboardState k;
    keyboard_reset(&k);
    while (!k.submitted) {
        if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
        if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
        if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
        if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
        if (g_pad->WasPressed(Button::A))     keyboard_type(&k);
        if (g_pad->WasPressed(Button::B))     keyboard_backspace(&k);
        if (g_pad->WasPressed(Button::START)) keyboard_submit(&k);
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

extern "C" void saturn_die(const char *fmt, ...) {
    (void) fmt;
    console_write("\n*** interpreter halted ***\n", 28);
    while (1) { render_console(); SRL::Core::Synchronize(); }
}

// ---- boot ------------------------------------------------------------------

// Title screen: wait for START; use elapsed frames as an RNG seed.
static int title_and_seed(void) {
    int frames = 0;
    while (!g_pad->WasPressed(Button::START)) {
        SRL::Debug::Print(6, 12, "M O J O Z O R K   ---   Z O R K   I");
        SRL::Debug::Print(12, 15, "Press START to begin");
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

    // Load the story from CD into high work RAM.
    SRL::Cd::File file("ZORK1.DAT");
    file.Open();
    uint32_t len = (uint32_t) file.Size.Bytes;
    uint8_t *story = (uint8_t *) SRL::Memory::HighWorkRam::Malloc(len);
    file.Read((int32_t) len, story);

    mojo_boot(story, len, seed);
    mojo_run();

    // Game ended: keep the final screen up.
    while (1) { render_console(); SRL::Core::Synchronize(); }
    return 0;
}
