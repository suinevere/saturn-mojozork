/*----------------------
 | display.h
 | Description: The display-appearance model's interface: the color/preset
 |   constants, the DisplayState, the cycling/selection API behind Display Options,
 |   and the save-blob encode/decode. Implemented in display.c; images are loaded
 |   and colors applied to VDP2 by title.cxx.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/
#ifndef DISPLAY_H
#define DISPLAY_H

/*----------------------
 | DISP_RGB555
 | Description: Packs 8-bit RGB into a Saturn RGB555 word (blue high, red low,
 |   opaque bit set), matching SRL::Types::HighColor(r, g, b).
 | Author: suinevere
 ----------------------*/
#define DISP_RGB555(r, g, b) \
    ((unsigned short)(0x8000 | (((b) >> 3) << 10) | (((g) >> 3) << 5) | ((r) >> 3)))

/*----------------------
 | DISP_BG_COLOR_N / DISP_TEXT_N / DISP_PRESET_N / DISP_IMAGE_MAX
 | Description: Counts: selectable background colors (palette indices >= this are
 |   images), text colors, microcomputer presets, and the cap on bitmaps read from
 |   the disc's TGA folder.
 | Author: suinevere
 ----------------------*/
#define DISP_BG_COLOR_N 7
#define DISP_TEXT_N     9
#define DISP_PRESET_N   15
#define DISP_IMAGE_MAX  8

/*----------------------
 | DISP_BG_* / DISP_TEXT_* (color indices)
 | Description: Named indices into the background and text color tables, in
 |   selector order.
 | Author: suinevere
 ----------------------*/
#define DISP_BG_BLACK        0
#define DISP_BG_AMBER        1
#define DISP_BG_BLUE         2
#define DISP_BG_LIGHT_GRAY   3
#define DISP_BG_BRIGHT_CYAN  4
#define DISP_BG_GREEN        5
#define DISP_BG_BRIGHT_WHITE 6

#define DISP_TEXT_BRIGHT_AMBER  0
#define DISP_TEXT_BLACK         1
#define DISP_TEXT_GREEN         2
#define DISP_TEXT_LIGHT_BLUE    3
#define DISP_TEXT_CYAN          4
#define DISP_TEXT_BRIGHT_YELLOW 5
#define DISP_TEXT_GRAY          6
#define DISP_TEXT_BRIGHT_GREEN  7
#define DISP_TEXT_WHITE         8

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | DISP_IMAGE_NONE
 | Description: The image-slot sentinel meaning "no picture".
 | Author: suinevere
 ----------------------*/
#define DISP_IMAGE_NONE (-1)

/*----------------------
 | DisplayState
 | Description: A chosen appearance: the preset index ("Custom" is derived, never
 |   stored), the background color, the text color, and the image slot (or
 |   DISP_IMAGE_NONE).
 | Author: suinevere
 ----------------------*/
typedef struct {
    int palette;   /* preset index 0..display_palette_count()-1; "Custom" is derived, never stored */
    int bg;        /* 0..DISP_BG_COLOR_N-1: always a color */
    int text;      /* 0..DISP_TEXT_N-1 */
    int image;     /* image slot, or DISP_IMAGE_NONE */
} DisplayState;

/*----------------------
 | color / preset lookups (display_bg_rgb .. display_preset_text)
 | Description: RGB and display-name lookups by color index, and a preset's name,
 |   background, and text color by palette index.
 | Author: suinevere
 ----------------------*/
unsigned short display_bg_rgb(int index);
unsigned short display_text_rgb(int index);
const char *display_bg_color_name(int index);
const char *display_text_name(int index);
const char *display_preset_name(int index);
int display_preset_bg(int index);
int display_preset_text(int index);

/*----------------------
 | display_palette_name / display_defaults
 | Description: palette_name returns the preset name (machine or image) when the
 |   state still matches that preset's bg/text/image, else "Custom"; defaults resets
 |   a state to the default appearance.
 | Author: suinevere
 ----------------------*/
const char *display_palette_name(const DisplayState *d);
void display_defaults(DisplayState *d);

/*----------------------
 | display_set_images / display_image_count / display_bg_count
 | Description: set_images registers the disc's TGA bitmaps (names borrowed, not
 |   copied -- keep the array alive for the program's life; count clamped to
 |   DISP_IMAGE_MAX); image_count is how many were registered; bg_count is
 |   DISP_BG_COLOR_N (the Background row is colors only).
 | Author: suinevere
 ----------------------*/
void display_set_images(const char *const *names, int count);
int display_image_count(void);
int display_bg_count(void);

/*----------------------
 | display_image_file / display_image_label
 | Description: file is the on-disc name to open ("HOUSE.TGA"), or "" for an
 |   unregistered slot; label is the player-facing form (extension dropped,
 |   capitalized -> "House"), held in a small rotating buffer -- copy it if you need
 |   to keep it past a couple of uses.
 | Author: suinevere
 ----------------------*/
const char *display_image_file(int slot);
const char *display_image_label(int slot);

/*----------------------
 | display_palette_count / display_preset_image
 | Description: palette_count is DISP_PRESET_N color presets plus one per registered
 |   image (call display_set_images first); preset_image is the image slot a palette
 |   index selects, or DISP_IMAGE_NONE for a color preset.
 | Author: suinevere
 ----------------------*/
int display_palette_count(void);
int display_preset_image(int index);

/*----------------------
 | display_is_image / display_bg_name
 | Description: is_image is true when the state shows a present image; bg_name is
 |   the current background color's display name.
 | Author: suinevere
 ----------------------*/
int display_is_image(const DisplayState *d);
const char *display_bg_name(const DisplayState *d);

/*----------------------
 | display_cycle_bg / display_cycle_text / display_cycle_palette
 | Description: Cycle the background, text, or palette one step (dir +1/-1). bg and
 |   text skip any candidate that would make text the same color as the background.
 | Author: suinevere
 ----------------------*/
void display_cycle_bg(DisplayState *d, int dir);
void display_cycle_text(DisplayState *d, int dir);
void display_cycle_palette(DisplayState *d, int dir);

/*----------------------
 | DISP_IMAGE_NAME_MAX
 | Description: The longest image filename stored, plus its NUL. Disc names are
 |   ISO9660 8.3, which GFS_FNAME_LEN caps at 12.
 | Author: suinevere
 ----------------------*/
#define DISP_IMAGE_NAME_MAX 13

/*----------------------
 | DISP_BLOB_BYTES
 | Description: The save-block size and layout: [sentinel=3][palette][bg][text]
 |   [image name, NUL-padded]. bg is always a color and the name says which picture
 |   sits over it (empty for none) -- both stored because they are independent (an
 |   image hides its color, but the color still shows through the menu frames and
 |   survives switching the picture off). palette holds 0xFF for an image preset,
 |   identified by name (slot numbers index the disc's TGA scan order, so
 |   add/remove/reorder would repoint a saved setting). Two older forms are still
 |   read: sentinel 2 packed the image into bg (decoding to that image over black,
 |   losing the color beneath); sentinel 1 is the original four-byte, no-name form
 |   (colors honored, any image reference refused).
 | Author: suinevere
 ----------------------*/
#define DISP_BLOB_BYTES (4 + DISP_IMAGE_NAME_MAX)

/*----------------------
 | display_encode / display_decode
 | Description: encode writes the display block (out must have DISP_BLOB_BYTES
 |   room), returning the bytes written. decode reads it, defaulting any field that
 |   is absent, truncated, mis-sentinelled, out of range, or names an image this
 |   disc lacks; returns 1 if fully accepted, 0 if anything was defaulted. Call
 |   display_set_images first so image references resolve against present names.
 | Author: suinevere
 ----------------------*/
int display_encode(const DisplayState *d, unsigned char *out);
int display_decode(const unsigned char *buf, int len, DisplayState *d);

#ifdef __cplusplus
}
#endif

#endif
