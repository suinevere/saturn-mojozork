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

const char *display_preset_name(int index) {
    if (index < 0 || index >= DISP_PRESET_N) return "?";
    return PRESETS[index].name;
}

int display_preset_bg(int index) {
    if (index < 0 || index >= DISP_PRESET_N) return DISP_BG_BLACK;
    return PRESETS[index].bg;
}

int display_preset_text(int index) {
    if (index < 0 || index >= DISP_PRESET_N) return DISP_TEXT_BRIGHT_GREEN;
    return PRESETS[index].text;
}

const char *display_palette_name(const DisplayState *d) {
    if (d->palette >= 0 && d->palette < DISP_PRESET_N
        && d->bg   == PRESETS[d->palette].bg
        && d->text == PRESETS[d->palette].text) {
        return PRESETS[d->palette].name;
    }
    return "Custom";
}

void display_defaults(DisplayState *d) {
    d->palette = 12;                        /* IBM PC (MDA): closest to the */
    d->bg      = PRESETS[12].bg;            /* previous hardcoded appearance */
    d->text    = PRESETS[12].text;
}
