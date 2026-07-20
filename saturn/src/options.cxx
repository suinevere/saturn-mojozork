/*----------------------
 | options.cxx
 | Description: Load/save of the persisted MOJOOPTS blob and the runtime apply
 |   of display settings to VDP2. Owns no menu UI -- the option pages call in
 |   here.
 | Author: suinevere
 | Dependencies: app_state.h, input.h, display.h, saturn_backup.h, music.h,
 |   title.h, SRL
 ----------------------*/

#include <srl.hpp>

#include "options.h"
#include "app_state.h"
#include "input.h"
#include "display.h"
#include "title.h"

extern "C" {
#include "saturn_backup.h"
#include "music.h"
}

/*----------------------
 | text_set_color
 | Description: Writes the Saturn RGB555 word `rgb555` into the two VDP2 CRAM
 |   entries that color the SGL debug font and the block cursor, via the raw
 |   VDP2_COLRAM address (a bare integer, hence the cast; it reaches this file
 |   through <srl.hpp>). The font lives in ASCII palette 0, not palette 1:
 |   colorBank's declarator initializes it to 1 << 12, but
 |   Core::Initialize -> VDP2::Initialize calls ASCII::SetPalette(0) before any
 |   of our code runs and nothing here calls SetPalette again, and NBG3 is
 |   COL_TYPE_16 (4bpp), so palette 0 is CRAM entries 0-15 (bytes 0-31). Two
 |   entries matter and they are not adjacent: entry 1 is the glyph
 |   foreground (VDP2::Initialize seeds it via SetPrintPaletteColor(0, White),
 |   which writes 1 + (index << 8); its other six calls, index 1..6, land on
 |   entries 257, 513, ... which a 4bpp cell cannot reach, so index 0 is the
 |   only one that colors anything), and entry 15 is the cursor
 |   (install_block_glyph() fills its tile with 0xFF, and 4bpp pixel value 15
 |   selects entry 15). SRL::ASCII::SetColor cannot be used for the glyphs: it
 |   indexes from (colorBank >> 6), which is 0 here, so SetColor(c, i) writes
 |   entry i -- that reaches the cursor at i=15 but never the glyphs, which is
 |   why changing Text previously appeared to do nothing. SRL::Debug::
 |   PrintColorSet is likewise unusable: it sets slCurColor while Debug::Print
 |   reads ASCII::colorBank. The CRAM address is in the SH-2's uncached
 |   mirror, so no flush is needed, and the only DMA into CRAM
 |   (CRAM::Palette::Load) targets bank 1 at entries 256+ and never overlaps.
 | Author: suinevere
 | Dependencies: SRL
 | Globals: N/A
 | Params: rgb555 -- Saturn RGB555 color word
 | Returns: N/A
 ----------------------*/
void text_set_color(unsigned short rgb555) {
    volatile unsigned short *cram = (volatile unsigned short *) VDP2_COLRAM;
    cram[1]  = rgb555;   // glyph foreground
    cram[15] = rgb555;   // install_block_glyph()'s cursor tile
}

/*----------------------
 | display_apply
 | Description: Recolors via text_set_color (writes both the glyph CRAM entry
 |   and install_block_glyph's cursor entry in one call; SRL::Debug::
 |   PrintColorSet is unusable here because it sets slCurColor while
 |   Debug::Print reads ASCII::colorBank). Sets the back plane BEFORE any
 |   image load, because it is what shows through the transparent menu frames
 |   and is on screen during the 1-2s CD read. On image-load failure it drops
 |   to a color preset -- and if the failed palette WAS the image preset,
 |   rewrites it to preset 12 (IBM PC/MDA) so the broken picture is not
 |   re-selected.
 | Author: suinevere
 | Dependencies: display.h, title.h, SRL
 | Globals: g_display
 | Params: N/A
 | Returns: true if applied; false if a load failed and the fallback was
 |   installed
 ----------------------*/
bool display_apply(void) {
    text_set_color(display_text_rgb(g_display.text));
    SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
    if (display_is_image(&g_display)) {
        if (!title_bg_show(display_image_file(g_display.image))) {
            int p = g_display.palette;
            if (p >= DISP_PRESET_N || p < 0) p = 12;   // IBM PC (MDA), the startup default
            g_display.palette = p;
            g_display.bg      = display_preset_bg(p);
            g_display.text    = display_preset_text(p);
            g_display.image   = DISP_IMAGE_NONE;
            text_set_color(display_text_rgb(g_display.text));
            title_bg_hide();
            SRL::VDP2::SetBackColor(SRL::Types::HighColor(display_bg_rgb(g_display.bg)));
            return false;
        }
    } else {
        title_bg_hide();
    }
    return true;
}

/*----------------------
 | display_cycle_row
 | Description: For DCR_BG/DCR_TEXT, steps that field and applies -- a plain
 |   color change that cannot fail to load. For DCR_PALETTE, repeatedly steps
 |   the palette in `dir` and applies, restoring the pre-step state and
 |   retrying whenever the candidate fails to load, for up to
 |   display_palette_count() tries; only the Palette row can hit that failure
 |   path, since it is the one carrying picture presets. The restore-and-retry
 |   matters because display_apply() installs a color-preset fallback on
 |   failure, which rewrites the very index being cycled -- without restoring
 |   it first, the next press would resume from the fallback and land on the
 |   same bad image, making every image past it unreachable. If every
 |   candidate fails (a disc whose images are all unreadable), the loop gives
 |   up and lets the fallback from the last display_apply() call stand.
 | Author: suinevere
 | Dependencies: display.h
 | Globals: g_display
 | Params: which -- DCR_PALETTE, DCR_BG, or DCR_TEXT; dir -- -1 or +1
 | Returns: N/A
 ----------------------*/
void display_cycle_row(DisplayCycleRow which, int dir) {
    if (which != DCR_PALETTE) {
        if (which == DCR_BG) display_cycle_bg(&g_display, dir);
        else                 display_cycle_text(&g_display, dir);
        display_apply();     // colours only; nothing here can fail to load
        return;
    }
    int tries = display_palette_count();
    while (tries-- > 0) {
        display_cycle_palette(&g_display, dir);
        DisplayState want = g_display;
        if (display_apply()) return;   // showing what was asked for
        g_display = want;              // keep our place and step past the bad entry
    }
    display_apply();
}

/*----------------------
 | options_load
 | Description: Restores persisted game options from the backup-RAM MOJOOPTS
 |   blob into the app_state/input globals, defaulting any field the blob
 |   lacks. Layout, in order: difficulty (1 byte, accepted only if <=
 |   DIFF_HARD); the dial number as a NUL-terminated string (copied up to
 |   DIALNUM_MAX chars, but the scan still advances to the STORED string's own
 |   NUL rather than the copy's, because a blob written before the 11-digit
 |   cap can hold a longer number and every field below is located relative to
 |   that terminator); two audio-level bytes [music][pcm] each 0..7 -- a
 |   legacy blob instead stored one sound flag here (1 = off, else on), so
 |   when the pair doesn't parse as two in-range levels it is read as that
 |   flag and mapped to pcm 0 (off) or 4, music forced to 7; a controller-
 |   mapping block, format sentinel 2 followed by FA_N face-button bytes then
 |   CA_N chord-slot bytes, each byte accepted only if within range, applied
 |   only when the sentinel matches (an older/absent blob keeps the compiled
 |   default mapping); a sound block, sentinel 1 followed by [mix][track];
 |   and finally a display block handed to display_decode() with whatever
 |   bytes remain in the 64-byte buffer rather than a fixed width, so the
 |   older 4-byte form still parses even when a long stored dial number
 |   leaves too little room for the name-bearing current form. buf is
 |   zero-filled up front, so bytes past whatever was actually written read as
 |   an absent block. Must run after display_scan_images(), since
 |   display_decode() resolves an image reference by name against the disc's
 |   current TGA list.
 | Author: suinevere
 | Dependencies: saturn_backup.h, display.h, input.h, music.h
 | Globals: g_difficulty, g_dialnum, g_music_level, g_pcm_level, g_face_btn,
 |   g_chord_slot, g_mix_mode, g_sel_track, g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_load(void) {
    uint8_t buf[64];
    for (int z = 0; z < (int) sizeof(buf); z++) buf[z] = 0;
    if (!saturn_bup_read(SATURN_BUP_CONSOLE, "MOJOOPTS", buf)) return;
    if (buf[0] <= DIFF_HARD) g_difficulty = (int) buf[0];
    int i = 1;   // tracks the offset of the dial number's terminating NUL
    if (buf[1]) {
        int j;
        for (j = 0; buf[1 + j] && j < (int) sizeof(g_dialnum) - 1; j++) g_dialnum[j] = (char) buf[1 + j];
        g_dialnum[j] = '\0';
        while (buf[1 + j] && 1 + j < (int) sizeof(buf) - 1) j++;
        i = 1 + j;
    }
    if (i + 1 < (int) sizeof(buf)) {
        uint8_t a = buf[i + 1], b = (i + 2 < (int) sizeof(buf)) ? buf[i + 2] : 0xFF;
        if (a <= 7 && b <= 7) { g_music_level = a; g_pcm_level = b; }
        else { g_pcm_level = (a == 1) ? 0 : 4; g_music_level = 7; }   // legacy sound flag
    }
    int m = i + 3;
    if (m + 1 + FA_N + CA_N <= (int) sizeof(buf) && buf[m] == 2) {
        for (int a = 0; a < FA_N; a++) { int v = buf[m + 1 + a];        if (v < 3)    g_face_btn[a]   = v; }
        for (int a = 0; a < CA_N; a++) { int v = buf[m + 1 + FA_N + a]; if (v < SL_N) g_chord_slot[a] = v; }
    }
    int s = m + 1 + FA_N + CA_N;
    if (s + 2 < (int) sizeof(buf) && buf[s] == 1) {
        if (buf[s + 1] <= MIX_RANDOM) g_mix_mode = buf[s + 1];
        if (buf[s + 2] >= MUSIC_TRACK_MIN && buf[s + 2] <= MUSIC_TRACK_MAX) g_sel_track = buf[s + 2];
    }
    int dsp = s + 3;
    if (dsp + 4 <= (int) sizeof(buf)) {
        display_decode(buf + dsp, (int) sizeof(buf) - dsp, &g_display);
    }
}

/*----------------------
 | options_save
 | Description: Serializes the current option globals into the same MOJOOPTS
 |   layout options_load reads: difficulty byte; NUL-terminated dial number;
 |   music and pcm level bytes; controller-mapping sentinel byte (2) followed
 |   by the face-button and chord-slot bytes; sound-block sentinel byte (1)
 |   followed by mix mode and selected track; then the display block from
 |   display_encode(), appended only if it fits the remaining space in the
 |   62-byte payload. Writes the assembled buffer to backup RAM under the
 |   "MOJOOPTS" filename.
 | Author: suinevere
 | Dependencies: saturn_backup.h, display.h, input.h
 | Globals: g_difficulty, g_dialnum, g_music_level, g_pcm_level, g_face_btn,
 |   g_chord_slot, g_mix_mode, g_sel_track, g_display
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void options_save(void) {
    uint8_t buf[64]; int n = 0;
    buf[n++] = (uint8_t) g_difficulty;
    for (int i = 0; g_dialnum[i] && n < 62; i++) buf[n++] = (uint8_t) g_dialnum[i];
    buf[n++] = 0;
    buf[n++] = (uint8_t) g_music_level;           // audio levels: [music][pcm], 0..7
    buf[n++] = (uint8_t) g_pcm_level;
    buf[n++] = 2;                                 // controller-mapping format sentinel
    for (int a = 0; a < FA_N && n < 62; a++) buf[n++] = (uint8_t) g_face_btn[a];
    for (int a = 0; a < CA_N && n < 62; a++) buf[n++] = (uint8_t) g_chord_slot[a];
    buf[n++] = 1;                                 // sound-block sentinel
    buf[n++] = (uint8_t) g_mix_mode;              // 0..3
    buf[n++] = (uint8_t) g_sel_track;             // 2..32
    if (n + DISP_BLOB_BYTES <= 62) n += display_encode(&g_display, buf + n);
    saturn_bup_write(SATURN_BUP_CONSOLE, "MOJOOPTS", "options", buf, (uint32_t) n);
}

/*----------------------
 | valid_dialnum
 | Description: Scans `s` and rejects it unless every character is a digit,
 |   the string is non-empty, and its length is at most DIALNUM_MAX (the
 |   fixed storage in g_dialnum has no room past that).
 | Author: suinevere
 | Dependencies: app_state.h
 | Globals: N/A
 | Params: s -- candidate dial-number string
 | Returns: true if `s` passes validation
 ----------------------*/
bool valid_dialnum(const char *s) {
    if (!s[0]) return false;
    int i = 0;
    for (; s[i]; i++) if (s[i] < '0' || s[i] > '9') return false;
    return i <= DIALNUM_MAX;   // g_dialnum has no room past this
}
