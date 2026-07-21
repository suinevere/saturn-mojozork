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

/* Text colors, in selector order. ANSI 37 is a light gray, and it is named that
   here: it is what makes the BBC Micro and MSX presets look right, but it is not
   what a player asking for white expects. True white (ANSI 97) is its own entry,
   appended last so the indices already written into save blobs keep their
   colors. */
static const unsigned short TEXT_RGB[DISP_TEXT_N] = {
    DISP_RGB555(0xFF, 0xAF, 0x00),   /* Bright Amber   ANSI 38;5;214 */
    DISP_RGB555(0x00, 0x00, 0x00),   /* Black          ANSI 30  */
    DISP_RGB555(0x00, 0xAA, 0x00),   /* Green          ANSI 32  */
    DISP_RGB555(0x55, 0x55, 0xFF),   /* Light Blue     ANSI 94  */
    DISP_RGB555(0x00, 0xAA, 0xAA),   /* Cyan           ANSI 36  */
    DISP_RGB555(0xFF, 0xFF, 0x55),   /* Bright Yellow  ANSI 93  */
    DISP_RGB555(0xAA, 0xAA, 0xAA),   /* Gray           ANSI 37  */
    DISP_RGB555(0x55, 0xFF, 0x55),   /* Bright Green   ANSI 92  */
    DISP_RGB555(0xFF, 0xFF, 0xFF)    /* White          ANSI 97  */
};

static const char *const TEXT_NAME[DISP_TEXT_N] = {
    "Bright Amber", "Black", "Green", "Light Blue",
    "Cyan", "Bright Yellow", "Gray", "Bright Green", "White"
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
    { "BBC Micro",       DISP_BG_BLACK,        DISP_TEXT_GRAY          },
    { "MSX Standard",    DISP_BG_BLUE,         DISP_TEXT_GRAY          },
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

/* The Background row offers colors only. Pictures are chosen from the System
   Palette row, where each comes paired with the black background and white
   text that keep menu text legible over artwork. */
int display_bg_count(void)    { return DISP_BG_COLOR_N; }

const char *display_image_file(int slot) {
    if (!g_image_names || slot < 0 || slot >= g_image_count) return "";
    return g_image_names[slot] ? g_image_names[slot] : "";
}

const char *display_image_label(int slot) {
    /* Two buffers so a single screen draw can hold two labels at once -- the
       Palette row prints one while display_palette_name resolves
       another. */
    static char ring[2][DISP_IMAGE_NAME_MAX];
    static int turn = 0;
    const char *src = display_image_file(slot);
    char *out = ring[turn];
    int i = 0;

    turn = (turn + 1) & 1;
    for (; src[i] && src[i] != '.' && i < DISP_IMAGE_NAME_MAX - 1; i++) {
        char c = src[i];
        if (i == 0) { if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A'); }
        else        { if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a'); }
        out[i] = c;
    }
    out[i] = '\0';
    return out;
}

/* Palette indices run [0, DISP_PRESET_N) over the microcomputer presets, then
   one entry per registered image. */
int display_palette_count(void) { return DISP_PRESET_N + g_image_count; }

int display_preset_image(int index) {
    if (index < DISP_PRESET_N || index >= display_palette_count()) return DISP_IMAGE_NONE;
    return index - DISP_PRESET_N;
}

const char *display_preset_name(int index) {
    if (index < 0 || index >= display_palette_count()) return "?";
    if (index >= DISP_PRESET_N) return display_image_label(index - DISP_PRESET_N);
    return PRESETS[index].name;
}

/* An image preset puts its picture over black with white text: black so the
   frames and letterboxing around the image read as deliberate, white so menu
   text stays legible whatever the artwork does. */
int display_preset_bg(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_BG_BLACK;
    if (index >= DISP_PRESET_N) return DISP_BG_BLACK;
    return PRESETS[index].bg;
}

int display_preset_text(int index) {
    if (index < 0 || index >= display_palette_count()) return DISP_TEXT_BRIGHT_GREEN;
    if (index >= DISP_PRESET_N) return DISP_TEXT_WHITE;
    return PRESETS[index].text;
}

const char *display_palette_name(const DisplayState *d) {
    if (d->palette >= 0 && d->palette < display_palette_count()
        && d->bg    == display_preset_bg(d->palette)
        && d->text  == display_preset_text(d->palette)
        && d->image == display_preset_image(d->palette)) {
        return display_preset_name(d->palette);
    }
    return g_custom_label;
}

void display_defaults(DisplayState *d) {
    d->palette = 12;                        /* IBM PC (MDA): closest to the */
    d->bg      = PRESETS[12].bg;            /* previous hardcoded appearance */
    d->text    = PRESETS[12].text;
    d->image   = DISP_IMAGE_NONE;
}

int display_is_image(const DisplayState *d) {
    return d->image >= 0 && d->image < g_image_count;
}

const char *display_bg_name(const DisplayState *d) {
    return display_bg_color_name(d->bg);
}

/* True when this background/text pairing would render text invisible. */
static int clashes(int bg, int text) {
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
    d->image   = display_preset_image(next);
}

/* Registered name for an image slot, or "" when the slot is not present. */
static const char *image_slot_name(int slot) {
    if (!g_image_names || slot < 0 || slot >= g_image_count) return "";
    return g_image_names[slot] ? g_image_names[slot] : "";
}

/* Slot currently holding `name`, or -1 if this disc does not carry it. */
static int image_slot_of(const char *name) {
    int i;
    if (!g_image_names || !name[0]) return -1;
    for (i = 0; i < g_image_count; i++) {
        const char *have = g_image_names[i];
        int j = 0;
        if (!have) continue;
        while (have[j] && have[j] == name[j]) j++;
        if (have[j] == '\0' && name[j] == '\0') return i;
    }
    return -1;
}

#define DISP_BLOB_IMAGE 0xFF   /* palette/bg marker: "the image named below" */

int display_encode(const DisplayState *d, unsigned char *out) {
    const char *name = "";
    int i;

    out[0] = 3;                                /* block sentinel: bg color + image name */
    out[1] = (d->palette >= DISP_PRESET_N) ? DISP_BLOB_IMAGE : (unsigned char) d->palette;
    out[2] = (unsigned char) d->bg;            /* always a color now */
    out[3] = (unsigned char) d->text;

    /* The picture on screen. A palette still on an image preset names the same
       one; if the two ever disagree, what is displayed wins. */
    if (d->image >= 0)                     name = image_slot_name(d->image);
    else if (d->palette >= DISP_PRESET_N)  name = image_slot_name(d->palette - DISP_PRESET_N);

    for (i = 0; i < DISP_IMAGE_NAME_MAX - 1 && name[i]; i++)
        out[4 + i] = (unsigned char) name[i];
    for (; i < DISP_IMAGE_NAME_MAX; i++)
        out[4 + i] = 0;
    return DISP_BLOB_BYTES;
}

int display_decode(const unsigned char *buf, int len, DisplayState *d) {
    int ok = 1;
    display_defaults(d);

    if (!buf || len < 4) return 0;

    if (buf[0] == 1) {
        /* Original form: slot numbers, no name. Colors still mean what they
           said, but an image slot cannot be trusted to be the same picture, so
           it is refused rather than guessed at. */
        if (buf[1] < DISP_PRESET_N)   d->palette = (int) buf[1];  else ok = 0;
        if (buf[2] < DISP_BG_COLOR_N) d->bg      = (int) buf[2];  else ok = 0;
        if (buf[3] < DISP_TEXT_N)     d->text    = (int) buf[3];  else ok = 0;
    } else if (buf[0] == 2 || buf[0] == 3) {
        const char *name = (const char *) (buf + 4);
        int slot, n = 0;

        if (len < DISP_BLOB_BYTES) return 0;
        while (n < DISP_IMAGE_NAME_MAX && name[n]) n++;
        if (n >= DISP_IMAGE_NAME_MAX) return 0;   /* name never terminates */
        slot = image_slot_of(name);

        if (buf[1] == DISP_BLOB_IMAGE) {
            if (slot >= 0) d->palette = DISP_PRESET_N + slot;  else ok = 0;
        } else if (buf[1] < DISP_PRESET_N) {
            d->palette = (int) buf[1];
        } else ok = 0;

        if (buf[2] == DISP_BLOB_IMAGE) {
            /* Sentinel-2 form packed the image into bg, so the color beneath it
               was never stored. Black is what an image preset pairs with. */
            d->bg = DISP_BG_BLACK;
            if (slot < 0) ok = 0;
        } else if (buf[2] < DISP_BG_COLOR_N) {
            d->bg = (int) buf[2];
        } else ok = 0;

        if (buf[3] < DISP_TEXT_N) d->text = (int) buf[3];  else ok = 0;

        /* A name present means a picture was showing. Drop it, reporting the
           loss, when this disc no longer carries it. */
        if (name[0]) {
            if (slot >= 0) d->image = slot;  else ok = 0;
        }
    } else {
        return 0;
    }

    /* Each field above was validated independently, so an accepted background
       can still land on the same color as an accepted text -- most easily when
       a picture that was hiding the pairing is gone from this disc. d->palette
       is a real preset index by this point and every preset pair is legible, so
       restore both from it rather than from display_defaults(). */
    if (clashes(d->bg, d->text)) {
        d->bg   = display_preset_bg(d->palette);
        d->text = display_preset_text(d->palette);
        ok = 0;
    }

    return ok;
}
