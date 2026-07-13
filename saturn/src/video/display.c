/*----------------------
 | display.c
 | Description: The display-appearance model: the background/text color tables, the
 |   microcomputer palette presets, the registered background-image list, and the
 |   cycling/selection logic behind Display Options, plus the save-blob encode/
 |   decode for persisting a chosen appearance. Pure model and data -- title.cxx
 |   loads the actual images and applies colors to VDP2.
 | Author: suinevere
 | Dependencies: display.h (DISP_* constants, DisplayState, DISP_RGB555)
 ----------------------*/
#include "display.h"

/*----------------------
 | BG_RGB / BG_NAME
 | Description: Background colors and their display names, in selector order. RGB
 |   values approximate the design spec's ANSI codes; ANSI collisions collapse to
 |   one entry (the three blues to \033[44m, the two whites to \033[47m).
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | TEXT_RGB / TEXT_NAME
 | Description: Text colors and their names, in selector order. ANSI 37 is a light
 |   gray, named "Gray" here -- it makes the BBC Micro and MSX presets look right
 |   but is not what a player asking for white expects, so true white (ANSI 97) is
 |   its own entry appended last, keeping the indices already stored in save blobs.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | DisplayPreset / PRESETS
 | Description: The microcomputer presets (name + background + text index). Names
 |   are shortened where the full hardware name would overflow the selector field.
 |   Two deliberate duplicate combos (C64 == Atari 800, IBM PC MDA == Commodore
 |   PET) stay separate entries because the stored palette index -- not the color
 |   pair -- identifies the machine.
 | Author: suinevere
 ----------------------*/
typedef struct { const char *name; int bg; int text; } DisplayPreset;

static const DisplayPreset PRESETS[DISP_PRESET_N] = {
    { "IBM PC (MDA)",    DISP_BG_BLACK,        DISP_TEXT_BRIGHT_GREEN  },
    { "Apple II Plus",   DISP_BG_BLACK,        DISP_TEXT_GREEN         },
    { "Toshiba T3100",   DISP_BG_BLACK,        DISP_TEXT_BRIGHT_AMBER  },
    { "BBC Micro",       DISP_BG_BLACK,        DISP_TEXT_GRAY          },
    { "Commodore PET",   DISP_BG_BLACK,        DISP_TEXT_BRIGHT_GREEN  },
    { "Commodore 64",    DISP_BG_BLUE,         DISP_TEXT_LIGHT_BLUE    },
    { "Amstrad CPC 464", DISP_BG_BLUE,         DISP_TEXT_BRIGHT_YELLOW },
    { "MSX Standard",    DISP_BG_BLUE,         DISP_TEXT_GRAY          },
    { "Atari 800",       DISP_BG_BLUE,         DISP_TEXT_LIGHT_BLUE    },
    { "VIC-20",          DISP_BG_LIGHT_GRAY,   DISP_TEXT_CYAN          },
    { "ZX Spectrum",     DISP_BG_LIGHT_GRAY,   DISP_TEXT_BLACK         },
    { "TRS-80 CoCo",     DISP_BG_GREEN,        DISP_TEXT_BLACK         },
    { "TI-99/4A",        DISP_BG_BRIGHT_CYAN,  DISP_TEXT_BLACK         },
    { "Mac Classic",     DISP_BG_BRIGHT_WHITE, DISP_TEXT_BLACK         },
    { "Monochrome P3",   DISP_BG_AMBER,        DISP_TEXT_BLACK         }
};

/*----------------------
 | g_custom_label
 | Description: The name shown when the current colors/image match no preset.
 | Author: suinevere
 ----------------------*/
static const char *const g_custom_label = "Custom";

/*----------------------
 | display_bg_rgb / display_text_rgb
 | Description: The RGB555 value for a background or text color index, falling back
 |   to a safe default on an out-of-range index.
 | Author: suinevere
 ----------------------*/
unsigned short display_bg_rgb(int index) {
    if (index < 0 || index >= DISP_BG_COLOR_N) index = DISP_BG_BLACK;
    return BG_RGB[index];
}

unsigned short display_text_rgb(int index) {
    if (index < 0 || index >= DISP_TEXT_N) index = DISP_TEXT_BRIGHT_GREEN;
    return TEXT_RGB[index];
}

/*----------------------
 | display_bg_color_name / display_text_name
 | Description: The display name for a background or text color index ("?" if out
 |   of range).
 | Author: suinevere
 ----------------------*/
const char *display_bg_color_name(int index) {
    if (index < 0 || index >= DISP_BG_COLOR_N) return "?";
    return BG_NAME[index];
}

const char *display_text_name(int index) {
    if (index < 0 || index >= DISP_TEXT_N) return "?";
    return TEXT_NAME[index];
}

/*----------------------
 | g_image_names / g_image_count
 | Description: The registered background-image list (borrowed from title.cxx) and
 |   its length; the palette row appends one entry per image.
 | Author: suinevere
 ----------------------*/
static const char *const *g_image_names = 0;
static int g_image_count = 0;

/*----------------------
 | display_set_images
 | Description: Registers the background-image name list (or clears it), capped at
 |   DISP_IMAGE_MAX.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_image_names, g_image_count
 | Params: names -- the image name array; count -- its length
 | Returns: N/A
 ----------------------*/
void display_set_images(const char *const *names, int count) {
    if (!names || count <= 0) { g_image_names = 0; g_image_count = 0; return; }
    if (count > DISP_IMAGE_MAX) count = DISP_IMAGE_MAX;
    g_image_names = names;
    g_image_count = count;
}

/*----------------------
 | display_image_count
 | Description: The number of registered background images.
 | Author: suinevere
 ----------------------*/
int display_image_count(void) { return g_image_count; }

/*----------------------
 | display_bg_count
 | Description: The number of selectable background colors. The Background row
 |   offers colors only; pictures are chosen from the System Palette row, each
 |   paired with black background + white text so menu text stays legible.
 | Author: suinevere
 ----------------------*/
int display_bg_count(void)    { return DISP_BG_COLOR_N; }

/*----------------------
 | display_image_file
 | Description: The disc filename for an image slot, or "" if out of range.
 | Author: suinevere
 ----------------------*/
const char *display_image_file(int slot) {
    if (!g_image_names || slot < 0 || slot >= g_image_count) return "";
    return g_image_names[slot] ? g_image_names[slot] : "";
}

/*----------------------
 | display_image_label
 | Description: A display label for an image slot: the filename without extension,
 |   first letter upper, rest lower. Uses two rotating buffers so a single screen
 |   draw can hold two labels at once (the Palette row prints one while
 |   display_palette_name resolves another).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: (via display_image_file)
 | Params: slot -- the image slot
 | Returns: the formatted label (in a rotating static buffer)
 ----------------------*/
const char *display_image_label(int slot) {
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

/*----------------------
 | display_palette_count
 | Description: The palette row length: the microcomputer presets [0, DISP_PRESET_N)
 |   followed by one entry per registered image.
 | Author: suinevere
 ----------------------*/
int display_palette_count(void) { return DISP_PRESET_N + g_image_count; }

/*----------------------
 | display_preset_image
 | Description: The image slot a palette index maps to, or DISP_IMAGE_NONE for a
 |   color preset.
 | Author: suinevere
 ----------------------*/
int display_preset_image(int index) {
    if (index < DISP_PRESET_N || index >= display_palette_count()) return DISP_IMAGE_NONE;
    return index - DISP_PRESET_N;
}

/*----------------------
 | display_preset_name
 | Description: The name for a palette index -- a preset's name, or an image slot's
 |   label ("?" if out of range).
 | Author: suinevere
 ----------------------*/
const char *display_preset_name(int index) {
    if (index < 0 || index >= display_palette_count()) return "?";
    if (index >= DISP_PRESET_N) return display_image_label(index - DISP_PRESET_N);
    return PRESETS[index].name;
}

/*----------------------
 | display_preset_bg / display_preset_text
 | Description: The background and text color for a palette index. An image preset
 |   puts its picture over black with white text -- black so the frames and
 |   letterboxing read as deliberate, white so menu text stays legible over the art.
 | Author: suinevere
 ----------------------*/
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

/*----------------------
 | display_palette_name
 | Description: The current palette's name if the state still matches that preset's
 |   bg/text/image exactly, otherwise "Custom".
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the display state
 | Returns: the preset name or the custom label
 ----------------------*/
const char *display_palette_name(const DisplayState *d) {
    if (d->palette >= 0 && d->palette < display_palette_count()
        && d->bg    == display_preset_bg(d->palette)
        && d->text  == display_preset_text(d->palette)
        && d->image == display_preset_image(d->palette)) {
        return display_preset_name(d->palette);
    }
    return g_custom_label;
}

/*----------------------
 | display_defaults
 | Description: Resets a DisplayState to the default appearance (IBM PC MDA, the
 |   closest match to the previous hardcoded look, with no image).
 | Author: suinevere
 | Dependencies: N/A
 | Globals: PRESETS
 | Params: d -- the state to reset
 | Returns: N/A
 ----------------------*/
void display_defaults(DisplayState *d) {
    d->palette = 0;                        /* IBM PC (MDA): closest to the */
    d->bg      = PRESETS[0].bg;            /* previous hardcoded appearance */
    d->text    = PRESETS[0].text;
    d->image   = DISP_IMAGE_NONE;
}

/*----------------------
 | display_is_image
 | Description: True when the state's image index refers to a present image slot.
 | Author: suinevere
 ----------------------*/
int display_is_image(const DisplayState *d) {
    return d->image >= 0 && d->image < g_image_count;
}

/*----------------------
 | display_bg_name
 | Description: The current background color's display name.
 | Author: suinevere
 ----------------------*/
const char *display_bg_name(const DisplayState *d) {
    return display_bg_color_name(d->bg);
}

/*----------------------
 | clashes
 | Description: True when a background/text pairing would render text invisible
 |   (identical colors).
 | Author: suinevere
 ----------------------*/
static int clashes(int bg, int text) {
    return display_bg_rgb(bg) == display_text_rgb(text);
}

/*----------------------
 | step
 | Description: Advances `value` by `dir` with wraparound over [0, count).
 | Author: suinevere
 ----------------------*/
static int step(int value, int dir, int count) {
    value += dir;
    if (value >= count) value = 0;
    if (value < 0)      value = count - 1;
    return value;
}

/*----------------------
 | display_cycle_bg / display_cycle_text
 | Description: Cycle the background or text color one step in `dir`, skipping past
 |   any choice that would clash (invisible text) against the other, bounded so it
 |   cannot loop forever.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the state to update; dir -- +1/-1
 | Returns: N/A
 ----------------------*/
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

/*----------------------
 | display_cycle_palette
 | Description: Cycle the palette one step in `dir`, applying the new preset's
 |   bg/text/image. From the Custom state it enters the list at whichever end it is
 |   heading toward.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: d -- the state to update; dir -- +1/-1
 | Returns: N/A
 ----------------------*/
void display_cycle_palette(DisplayState *d, int dir) {
    int count = display_palette_count();
    int next;
    if (display_palette_name(d) == g_custom_label) {
        next = (dir > 0) ? 0 : count - 1;
    } else {
        next = step(d->palette, dir, count);
    }
    d->palette = next;
    d->bg      = display_preset_bg(next);
    d->text    = display_preset_text(next);
    d->image   = display_preset_image(next);
}

/*----------------------
 | image_slot_name
 | Description: The registered name for an image slot, or "" when the slot is not
 |   present.
 | Author: suinevere
 ----------------------*/
static const char *image_slot_name(int slot) {
    if (!g_image_names || slot < 0 || slot >= g_image_count) return "";
    return g_image_names[slot] ? g_image_names[slot] : "";
}

/*----------------------
 | image_slot_of
 | Description: The slot currently holding `name`, or -1 if this disc does not
 |   carry it -- so a saved appearance can be matched back to a slot by name.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: g_image_names, g_image_count
 | Params: name -- the image filename
 | Returns: the slot index, or -1
 ----------------------*/
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

/*----------------------
 | DISP_BLOB_IMAGE
 | Description: The palette/bg marker byte in a save blob meaning "the image named
 |   below", distinguishing an image from a plain color index.
 | Author: suinevere
 ----------------------*/
#define DISP_BLOB_IMAGE 0xFF

/*----------------------
 | display_encode
 | Description: Serializes a DisplayState into a save blob (sentinel 3): palette,
 |   background, and text bytes (an image palette is marked DISP_BLOB_IMAGE), plus
 |   the on-screen picture's name so it can be matched back by name on a different
 |   disc. If image and palette disagree, what is displayed wins.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: (via image_slot_name)
 | Params: d -- the state; out -- the blob buffer
 | Returns: DISP_BLOB_BYTES (the bytes written)
 ----------------------*/
int display_encode(const DisplayState *d, unsigned char *out) {
    const char *name = "";
    int i;

    out[0] = 3;                                /* block sentinel: bg color + image name */
    out[1] = (d->palette >= DISP_PRESET_N) ? DISP_BLOB_IMAGE : (unsigned char) d->palette;
    out[2] = (unsigned char) d->bg;            /* always a color now */
    out[3] = (unsigned char) d->text;

    if (d->image >= 0)                     name = image_slot_name(d->image);
    else if (d->palette >= DISP_PRESET_N)  name = image_slot_name(d->palette - DISP_PRESET_N);

    for (i = 0; i < DISP_IMAGE_NAME_MAX - 1 && name[i]; i++)
        out[4 + i] = (unsigned char) name[i];
    for (; i < DISP_IMAGE_NAME_MAX; i++)
        out[4 + i] = 0;
    return DISP_BLOB_BYTES;
}

/*----------------------
 | display_decode
 | Description: Restores a DisplayState from a save blob, defaulting first so a bad
 |   blob leaves a sane state. Handles the original slot-only form (sentinel 1,
 |   image slots refused since the picture cannot be trusted), and the named forms
 |   (sentinels 2/3): resolves the image by name, refusing one this disc lacks, and
 |   validates each field independently. A picture that had been hiding a
 |   background/text clash can be gone from this disc, so a final clash check
 |   restores both colors from the (legible) preset. Returns whether everything was
 |   accepted verbatim.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: (via image_slot_of / display_preset_*)
 | Params: buf -- the blob; len -- its length; d -- the state to fill
 | Returns: 1 if fully accepted, 0 if anything was defaulted/refused
 ----------------------*/
int display_decode(const unsigned char *buf, int len, DisplayState *d) {
    int ok = 1;
    display_defaults(d);

    if (!buf || len < 4) return 0;

    if (buf[0] == 1) {
        /* Original form: slot numbers, no name. Colors still mean what they said,
           but an image slot cannot be trusted, so it is refused not guessed. */
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
            /* Sentinel-2 packed the image into bg, so the color beneath it was
               never stored. Black is what an image preset pairs with. */
            d->bg = DISP_BG_BLACK;
            if (slot < 0) ok = 0;
        } else if (buf[2] < DISP_BG_COLOR_N) {
            d->bg = (int) buf[2];
        } else ok = 0;

        if (buf[3] < DISP_TEXT_N) d->text = (int) buf[3];  else ok = 0;

        /* A name present means a picture was showing; drop it, reporting the loss,
           when this disc no longer carries it. */
        if (name[0]) {
            if (slot >= 0) d->image = slot;  else ok = 0;
        }
    } else {
        return 0;
    }

    /* Fields were validated independently, so an accepted bg can still match an
       accepted text -- most easily when a picture that was hiding the pairing is
       gone. d->palette is a real preset index here and every preset pair is
       legible, so restore both from it. */
    if (clashes(d->bg, d->text)) {
        d->bg   = display_preset_bg(d->palette);
        d->text = display_preset_text(d->palette);
        ok = 0;
    }

    return ok;
}
