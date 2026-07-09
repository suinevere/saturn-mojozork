#include <srl.hpp>

extern "C" {
#include "console.h"
#include "keyboard.h"
#include "saturn_keyboard.h"
#include "saturn_backup.h"
#include "saturn_glue.h"
}

// snprintf links from newlib; the SRL dummy <stdio.h> omits its declaration.
extern "C" int snprintf(char *str, size_t size, const char *fmt, ...);

#define SAVE_SLOTS 5

using namespace SRL::Types;
using Button = SRL::Input::Digital::Button;

// One shared gamepad on port 0, used by the readline hook.
static SRL::Input::Digital *g_pad = nullptr;

// The story file currently loaded from CD (set in main after game selection).
// saturn_read_story_file re-reads this for save/restart, so it must track the
// chosen game rather than a hard-coded name.
static const char *g_story_filename = "ZORK1.Z3";

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
        // Prefer the keyboard: only read the gamepad on frames with no keyboard
        // event, so a keyboard keypress (which also bleeds into pad button bits)
        // doesn't double-trigger. The gamepad still works whenever the keyboard is
        // idle, so on-screen navigation never gets stuck.
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind == SATURN_KEY_NONE) {
            if (g_pad->WasPressed(Button::Up))    keyboard_move(&k, 0, -1);
            if (g_pad->WasPressed(Button::Down))  keyboard_move(&k, 0,  1);
            if (g_pad->WasPressed(Button::Left))  keyboard_move(&k, -1, 0);
            if (g_pad->WasPressed(Button::Right)) keyboard_move(&k,  1, 0);
            if (g_pad->WasPressed(Button::C))     keyboard_type(&k);       // C = type letter
            if (g_pad->WasPressed(Button::X))     keyboard_type_char(&k, ' '); // X = space
            if (g_pad->WasPressed(Button::B))     keyboard_backspace(&k);  // B = delete
            if (g_pad->WasPressed(Button::A) ||
                g_pad->WasPressed(Button::START)) keyboard_submit(&k);     // A = enter/submit
        }
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

extern "C" void saturn_die(const char *fmt, ...) {
    (void) fmt;
    console_write("\n*** interpreter halted ***\n", 28);
    while (1) { render_console(); SRL::Core::Synchronize(); }
}

// ---- save / restore menu ---------------------------------------------------

static void menu_clear(void) {
    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
}

// Wait for any button/key (used for "press any key" prompts).
static void menu_wait(void) {
    SRL::Core::Synchronize();
    for (;;) {
        if (g_pad->WasPressed(Button::A) || g_pad->WasPressed(Button::B) ||
            g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return;
        if (saturn_keyboard_poll().kind != SATURN_KEY_NONE) return;
        SRL::Core::Synchronize();
    }
}

// Modal list menu. Returns the chosen index, or -1 if cancelled. Navigable by
// gamepad (D-pad + A/C/Start, B cancels) or keyboard (number keys pick directly,
// Enter picks the highlighted item, Backspace cancels).
static int menu_select(const char *title, const char *const *items, int count) {
    int sel = 0;
    SRL::Core::Synchronize();   // consume any stale button/key edge
    for (;;) {
        if (g_pad->WasPressed(Button::Up))    sel = (sel - 1 + count) % count;
        if (g_pad->WasPressed(Button::Down))  sel = (sel + 1) % count;
        bool pick = g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START);
        bool cancel = g_pad->WasPressed(Button::B);
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind == SATURN_KEY_ENTER) pick = true;
        else if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) cancel = true;
        else if (ke.kind == SATURN_KEY_CHAR && ke.ch >= '1' && ke.ch <= '9') {
            int idx = (int) (ke.ch - '1');
            if (idx < count) { sel = idx; pick = true; }
        }
        if (cancel) return -1;
        if (pick)   return sel;

        menu_clear();
        SRL::Debug::Print(2, 2, "%s", title);
        for (int i = 0; i < count; i++)
            SRL::Debug::Print(4, 4 + i, "%c %d) %s", (i == sel) ? '>' : ' ', i + 1, items[i]);
        SRL::Debug::Print(2, 6 + count, "pad/num pick   C/Ent=ok   Esc/B=back");
        SRL::Core::Synchronize();
    }
}

// Per-game backup filename for a slot: the story's base name (uppercased, up to
// 9 chars, extension dropped) + the slot digit. Gives each game its own 5 slots.
static void make_slot_name(char *out, int slot) {
    int i = 0;
    for (const char *p = g_story_filename; *p && *p != '.' && i < 9; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        out[i++] = c;
    }
    out[i++] = (char) ('0' + slot);
    out[i] = 0;
}

// Pick a backup device (cartridge only when inserted). Returns the device id, or
// -1 if cancelled.
static int choose_device(const char *title) {
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

// Combined save-slot picker + in-place name editor. Shows all slots for the
// device; the player picks one (C/Enter/number), then edits THAT slot's name
// right in the list -- the cursor stays on the line they picked. Backspace/B
// while editing goes back to slot selection; A/Enter confirms. Returns 1 with
// *out_slot/out_name set, or 0 if cancelled. out_name is empty if left blank.
static int pick_slot_and_name(int device, int *out_slot, char *out_name, int maxchars) {
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
    SRL::Core::Synchronize();   // consume the device-pick edge

    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();

        if (!editing) {
            bool pick = false, cancel = false;
            if (ke.kind == SATURN_KEY_ENTER) pick = true;
            else if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) cancel = true;
            else if (ke.kind == SATURN_KEY_CHAR && ke.ch >= '1' && ke.ch < (char) ('1' + SAVE_SLOTS)) {
                sel = (int) (ke.ch - '1'); pick = true;
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
                if (k.input_len > 0) {   // picker on the last char of the pre-filled name
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

        // Same slot menu either way; the picked line becomes editable in place.
        menu_clear();
        SRL::Debug::Print(2, 1, editing ? "Name this save:" : "Save - pick a slot:");
        for (int i = 0; i < SAVE_SLOTS; i++) {
            char mark = (i == sel) ? '>' : ' ';
            if (editing && i == sel) {
                SRL::Debug::Print(2, 3 + i, "%c %d) %s_", mark, i + 1, k.input);
            } else {
                const char *label = slotname[i][0] ? slotname[i] : "(empty)";
                SRL::Debug::Print(2, 3 + i, "%c %d) %s", mark, i + 1, label);
            }
        }
        if (!editing) {
            SRL::Debug::Print(2, 4 + SAVE_SLOTS, "pad/num pick   C/Ent=edit   Esc/B=back");
        } else {
            for (int r = 0; r < KB_ROWS; r++) {
                char rowbuf[KB_COLS * 2 + 1];
                int p = 0;
                for (int c = 0; c < KB_COLS; c++) {
                    rowbuf[p++] = (r == k.cursor_row && c == k.cursor_col) ? '[' : ' ';
                    rowbuf[p++] = KB_LAYOUT[r][c];
                }
                rowbuf[p] = '\0';
                SRL::Debug::Print(4, 4 + SAVE_SLOTS + r, "%s", rowbuf);
            }
            SRL::Debug::Print(2, 5 + SAVE_SLOTS + KB_ROWS, "C=type X=space  Esc/B=back  A/Ent=OK");
        }
        SRL::Core::Synchronize();
    }
}

// Yes/no confirmation. Returns true if confirmed (C / A / Start / Enter / Y),
// false if declined (B / N).
static bool menu_confirm(const char *line1, const char *line2) {
    SRL::Core::Synchronize();   // consume the edge that got us here
    for (;;) {
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind == SATURN_KEY_ENTER) return true;
        if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) return false;
        if (ke.kind == SATURN_KEY_CHAR) {
            if (ke.ch == 'y' || ke.ch == 'Y') return true;
            if (ke.ch == 'n' || ke.ch == 'N') return false;
        } else {
            if (g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START)) return true;
            if (g_pad->WasPressed(Button::B)) return false;
        }
        menu_clear();
        SRL::Debug::Print(2, 3, "%s", line1);
        if (line2 && line2[0]) SRL::Debug::Print(2, 4, "%s", line2);
        SRL::Debug::Print(2, 6, "C/A/Enter = Yes     Esc/B = No");
        SRL::Core::Synchronize();
    }
}

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
    int device = choose_device("SAVE - device?");
    if (device < 0) return 0;

    // Pick a slot and edit its name in place (cursor stays on the chosen line).
    int slot;
    char comment[12];
    if (!pick_slot_and_name(device, &slot, comment, 8)) return 0;
    if (comment[0] == 0) snprintf(comment, sizeof(comment), "Save %d", slot + 1);

    // Confirm before overwriting an existing save in that slot.
    char name[12];
    make_slot_name(name, slot);
    char existing[12];
    if (saturn_bup_info(device, name, existing)) {
        char q[40];
        snprintf(q, sizeof(q), "Overwrite \"%s\"?", existing);
        if (!menu_confirm(q, "Are you sure?")) return 0;
    }

    int ok = saturn_bup_write(device, name, comment, data, len);
    menu_clear();
    SRL::Debug::Print(2, 4, "%s", ok ? "Saved." : "Save FAILED (no space?).");
    SRL::Debug::Print(2, 6, "(press any key/button)");
    menu_wait();
    return ok;
}

extern "C" int saturn_load_blob(uint8_t *buf, uint32_t maxlen) {
    (void) maxlen;
    int device, slot;
    if (!choose_dest("RESTORE - device?", "RESTORE - slot?", &device, &slot)) return 0;
    char name[12];
    make_slot_name(name, slot);
    int ok = saturn_bup_read(device, name, buf);
    if (!ok) {
        menu_clear();
        SRL::Debug::Print(2, 4, "No save in that slot.");
        SRL::Debug::Print(2, 6, "(press any key/button)");
        menu_wait();
    }
    return ok;
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
        SRL::Debug::Print(9, 15, "Press any button to begin");
        SRL::Core::Synchronize();
        frames++;
    }
    return frames | 1;   // avoid a zero seed
}

const char* game_select() {
    const char* title="Please Select a game:";
    const int count=3;
   const char *items[count];
   items[count-3] = "ZORK1.Z3";
   items[count-2] = "ZORK2.Z3";
   items[count-1] = "ZORK3.Z3";

    int sel = 0;
    SRL::Core::Synchronize();   // consume any stale button/key edge
    for (;;) {
        if (g_pad->WasPressed(Button::Up))    sel = (sel - 1 + count) % count;
        if (g_pad->WasPressed(Button::Down))  sel = (sel + 1) % count;
        bool pick = g_pad->WasPressed(Button::C) || g_pad->WasPressed(Button::START);
        bool cancel = g_pad->WasPressed(Button::B);
        SaturnKeyEvent ke = saturn_keyboard_poll();
        if (ke.kind == SATURN_KEY_ENTER) pick = true;
        else if (ke.kind == SATURN_KEY_ESCAPE || ke.kind == SATURN_KEY_BACKSPACE) cancel = true;
        else if (ke.kind == SATURN_KEY_CHAR && ke.ch >= '1' && ke.ch <= '9') {
            int idx = (int) (ke.ch - '1');
            if (idx < count) { sel = idx; pick = true; }
        }
        if (cancel) return "-1";
        if (pick)   return items[sel];

        menu_clear();
        SRL::Debug::Print(2, 2, "%s", title);
        for (int i = 0; i < count; i++)
            SRL::Debug::Print(4, 4 + i, "%c %d) %s", (i == sel) ? '>' : ' ', i + 1, items[i]);
        SRL::Debug::Print(2, 6 + count, "pad/num pick   C/Ent=ok   Esc/B=back");
        SRL::Core::Synchronize();
    }
}

int main(void) {
    SRL::Core::Initialize(HighColor::Colors::Black);
    console_init();
    saturn_bup_init();

    static SRL::Input::Digital pad(0);
    g_pad = &pad;

    int seed = title_and_seed();
    const char* game_file = game_select();
    g_story_filename = game_file;   // save/restart must re-read the selected game

    // Load *.Z3 from CD. GFS_GetFileSize (used by SRL::Cd::File's ctor) can
    // return an uninitialized size on first access, and File::Read relies on that
    // size for both its work-buffer and its read bound. So retry the stat until it
    // reports a sane size (2048-byte data sectors, plausible length), then allocate
    // exactly that and read the whole file. A wrong size corrupts the story buffer
    // and makes the interpreter crash / report bogus "Out of memory".
    uint8_t *story = nullptr;
    uint32_t len = 0;
    for (int attempt = 0; attempt < 300 && story == nullptr; attempt++) {
        SRL::Cd::File f(game_file);
        int32_t bytes = f.Size.Bytes;
        int32_t ssz   = f.Size.SectorSize;
        SRL::Debug::Print(2, 20, "loading %s...", game_file);
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

    mojo_boot(story, len, seed);
    mojo_run();

    // Game ended: keep the final screen up.
    while (1) { render_console(); SRL::Core::Synchronize(); }
    return 0;
}
