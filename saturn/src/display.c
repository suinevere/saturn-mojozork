#include "display.h"

/* Background colors, in selector order. RGB values approximate the ANSI codes
   in the design spec's source table; Dark/Deep/Medium Blue all emit \033[44m
   and collapse to one entry, as do White and White/Light Gray (\033[47m). */
static const unsigned short BG_RGB[DISP_BG_COLOR_N] = {
    DISP_RGB555(0x00, 0x00, 0x00),   /* Black          ANSI 40  */
    DISP_RGB555(0xFF, 0xB0, 0x00),   /* Glowing Amber  ANSI 43  */
    DISP_RGB555(0x00, 0x00, 0xAA),   /* Blue           ANSI 44  */
    DISP_RGB555(0xAA, 0xAA, 0xAA),   /* Light Gray     ANSI 47  */
    DISP_RGB555(0x55, 0xFF, 0xFF),   /* Bright Cyan    ANSI 106 */
    DISP_RGB555(0x00, 0xAA, 0x00),   /* Green          ANSI 42  */
    DISP_RGB555(0xFF, 0xFF, 0xFF)    /* Bright White   ANSI 107 */
};

static const char *const BG_NAME[DISP_BG_COLOR_N] = {
    "Black", "Glowing Amber", "Blue", "Light Gray",
    "Bright Cyan", "Green", "Bright White"
};

/* Text colors, in selector order. White is ANSI 37 (light gray), not true
   white -- that is what makes the BBC Micro and MSX presets look right. */
static const unsigned short TEXT_RGB[DISP_TEXT_N] = {
    DISP_RGB555(0xFF, 0xAF, 0x00),   /* Bright Amber   ANSI 38;5;214 */
    DISP_RGB555(0x00, 0x00, 0x00),   /* Black          ANSI 30  */
    DISP_RGB555(0x00, 0xAA, 0x00),   /* Green          ANSI 32  */
    DISP_RGB555(0x55, 0x55, 0xFF),   /* Light Blue     ANSI 94  */
    DISP_RGB555(0x00, 0xAA, 0xAA),   /* Cyan           ANSI 36  */
    DISP_RGB555(0xFF, 0xFF, 0x55),   /* Bright Yellow  ANSI 93  */
    DISP_RGB555(0xAA, 0xAA, 0xAA),   /* White          ANSI 37  */
    DISP_RGB555(0x55, 0xFF, 0x55)    /* Bright Green   ANSI 92  */
};

static const char *const TEXT_NAME[DISP_TEXT_N] = {
    "Bright Amber", "Black", "Green", "Light Blue",
    "Cyan", "Bright Yellow", "White", "Bright Green"
};

/* Microcomputer presets. Names are shortened where the full hardware name
   would overflow the selector field. Note two deliberate duplicate combos:
   C64 == Atari 800, and IBM PC MDA == Commodore PET. They stay separate
   entries because the stored palette index -- not the color pair -- is what
   identifies the machine. */
typedef struct { const char *name; int bg; int text; } DisplayPreset;

static const DisplayPreset PRESETS[DISP_PRESET_N] = {
    { "Toshiba T3100",   DISP_BG_BLACK,        DISP_TEXT_BRIGHT_AMBER  },
    { "Monochrome P3",   DISP_BG_AMBER,        DISP_TEXT_BLACK         },
    { "Apple II Plus",   DISP_BG_BLACK,        DISP_TEXT_GREEN         },
    { "Commodore 64",    DISP_BG_BLUE,         DISP_TEXT_LIGHT_BLUE    },
    { "ZX Spectrum",     DISP_BG_LIGHT_GRAY,   DISP_TEXT_BLACK         },
    { "VIC-20",          DISP_BG_LIGHT_GRAY,   DISP_TEXT_CYAN          },
    { "TI-99/4A",        DISP_BG_BRIGHT_CYAN,  DISP_TEXT_BLACK         },
    { "Amstrad CPC 464", DISP_BG_BLUE,         DISP_TEXT_BRIGHT_YELLOW },
    { "BBC Micro",       DISP_BG_BLACK,        DISP_TEXT_WHITE         },
    { "MSX Standard",    DISP_BG_BLUE,         DISP_TEXT_WHITE         },
    { "TRS-80 CoCo",     DISP_BG_GREEN,        DISP_TEXT_BLACK         },
    { "Atari 800",       DISP_BG_BLUE,         DISP_TEXT_LIGHT_BLUE    },
    { "IBM PC (MDA)",    DISP_BG_BLACK,        DISP_TEXT_BRIGHT_GREEN  },
    { "Commodore PET",   DISP_BG_BLACK,        DISP_TEXT_BRIGHT_GREEN  },
    { "Mac Classic",     DISP_BG_BRIGHT_WHITE, DISP_TEXT_BLACK         }
};

static const char *const g_custom_label = "Custom";

unsigned short display_bg_rgb(int index) {
    if (index < 0 || index >= DISP_BG_COLOR_N) index = DISP_BG_BLACK;
    return BG_RGB[index];
}

unsigned short display_text_rgb(int index) {
    if (index < 0 || index >= DISP_TEXT_N) index = DISP_TEXT_BRIGHT_GREEN;
    return TEXT_RGB[index];
}

const char *display_bg_color_name(int index) {
    if (index < 0 || index >= DISP_BG_COLOR_N) return "?";
    return BG_NAME[index];
}

const char *display_text_name(int index) {
    if (index < 0 || index >= DISP_TEXT_N) return "?";
    return TEXT_NAME[index];
}

static const char *const *g_image_names = 0;
static int g_image_count = 0;

void display_set_images(const char *const *names, int count) {
    if (!names || count <= 0) { g_image_names = 0; g_image_count = 0; return; }
    if (count > DISP_IMAGE_MAX) count = DISP_IMAGE_MAX;
    g_image_names = names;
    g_image_count = count;
}

int display_image_count(void) { return g_image_count; }
int display_bg_count(void)    { return DISP_BG_COLOR_N + g_image_count; }

/* Palette indices run [0, DISP_PRESET_N) over the microcomputer presets, then
   one entry per registered image. An image preset pairs that image's
   background slot with white text -- the pairing the Display page offers so a
   picture never sits under a color that vanishes into it. */
int display_palette_count(void) { return DISP_PRESET_N + g_image_count; }

const char *display_preset_name(int index) {
    if (index < 0 || index >= display_palette_count()) return "?";
    if (index >= DISP_PRESET_N) return g_image_names[index - DISP_PRESET_N];
    return PRESETS[index].name;
}

int display_preset_bg(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_BG_BLACK;
    if (index >= DISP_PRESET_N) return DISP_BG_COLOR_N + (index - DISP_PRESET_N);
    return PRESETS[index].bg;
}

int display_preset_text(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_TEXT_BRIGHT_GREEN;
    if (index >= DISP_PRESET_N) return DISP_TEXT_WHITE;
    return PRESETS[index].text;
}

const char *display_palette_name(const DisplayState *d) {
    if (d->palette >= 0 && d->palette < display_palette_count()
        && d->bg   == display_preset_bg(d->palette)
        && d->text == display_preset_text(d->palette)) {
        return display_preset_name(d->palette);
    }
    return g_custom_label;
}

void display_defaults(DisplayState *d) {
    d->palette = 12;                        /* IBM PC (MDA): closest to the */
    d->bg      = PRESETS[12].bg;            /* previous hardcoded appearance */
    d->text    = PRESETS[12].text;
}

int display_is_image(const DisplayState *d) {
    return d->bg >= DISP_BG_COLOR_N && d->bg < display_bg_count();
}

const char *display_bg_name(const DisplayState *d) {
    if (display_is_image(d)) return g_image_names[d->bg - DISP_BG_COLOR_N];
    return display_bg_color_name(d->bg);
}

/* True when this background/text pairing would render text invisible. An image
   background has no single color to clash with, so the guard is inactive. */
static int clashes(int bg, int text) {
    if (bg >= DISP_BG_COLOR_N) return 0;
    return display_bg_rgb(bg) == display_text_rgb(text);
}

static int step(int value, int dir, int count) {
    value += dir;
    if (value >= count) value = 0;
    if (value < 0)      value = count - 1;
    return value;
}

void display_cycle_bg(DisplayState *d, int dir) {
    int count = display_bg_count();
    int next  = step(d->bg, dir, count);
    int tries = count;
    while (clashes(next, d->text) && tries-- > 0) next = step(next, dir, count);
    d->bg = next;
}

void display_cycle_text(DisplayState *d, int dir) {
    int next  = step(d->text, dir, DISP_TEXT_N);
    int tries = DISP_TEXT_N;
    while (clashes(d->bg, next) && tries-- > 0) next = step(next, dir, DISP_TEXT_N);
    d->text = next;
}

void display_cycle_palette(DisplayState *d, int dir) {
    int count = display_palette_count();
    int next;
    if (display_palette_name(d) == g_custom_label) {
        /* Currently Custom: enter the list at whichever end we are heading for. */
        next = (dir > 0) ? 0 : count - 1;
    } else {
        next = step(d->palette, dir, count);
    }
    d->palette = next;
    d->bg      = display_preset_bg(next);
    d->text    = display_preset_text(next);
}

int display_encode(const DisplayState *d, unsigned char *out) {
    out[0] = 1;                                /* block sentinel */
    out[1] = (unsigned char) d->palette;       /* always a real preset index */
    out[2] = (unsigned char) d->bg;
    out[3] = (unsigned char) d->text;
    return DISP_BLOB_BYTES;
}

int display_decode(const unsigned char *buf, int len, DisplayState *d) {
    int ok = 1;
    display_defaults(d);

    if (!buf || len < DISP_BLOB_BYTES || buf[0] != 1) return 0;

    /* Image presets exist only while their image is on the disc, so validate
       against the live count, not the compile-time preset total. */
    if (buf[1] < display_palette_count()) d->palette = (int) buf[1];  else ok = 0;
    /* An image index is valid only if that image is on the disc right now. */
    if (buf[2] < display_bg_count()) d->bg   = (int) buf[2];  else ok = 0;
    if (buf[3] < DISP_TEXT_N)    d->text    = (int) buf[3];  else ok = 0;

    /* Each field above was validated independently, but an image background
       that fell back to a color can still land on the same color as text that
       was independently accepted -- e.g. a saved image background paired with
       Black text, decoded on a disc where that image is gone: bg falls back to
       Black and both fields blank the screen. d->palette is a real preset index
       by this point, and every preset pair is guaranteed legible, so restore
       both fields from it rather than from display_defaults(). */
    if (clashes(d->bg, d->text)) {
        d->bg   = display_preset_bg(d->palette);
        d->text = display_preset_text(d->palette);
        ok = 0;
    }

    return ok;
}
