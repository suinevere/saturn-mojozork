#ifndef DISPLAY_H
#define DISPLAY_H

/* Pack 8-bit RGB into a Saturn RGB555 word (blue high, red low, opaque bit set).
   Matches SRL::Types::HighColor(uint8_t r, uint8_t g, uint8_t b). */
#define DISP_RGB555(r, g, b) \
    ((unsigned short)(0x8000 | (((b) >> 3) << 10) | (((g) >> 3) << 5) | ((r) >> 3)))

#define DISP_BG_COLOR_N 7    /* background colors; indices >= this are images */
#define DISP_TEXT_N     8
#define DISP_PRESET_N   15
#define DISP_IMAGE_MAX  8    /* cap on bitmaps read from the disc's TGA folder */

/* Background color indices. */
#define DISP_BG_BLACK        0
#define DISP_BG_AMBER        1
#define DISP_BG_BLUE         2
#define DISP_BG_LIGHT_GRAY   3
#define DISP_BG_BRIGHT_CYAN  4
#define DISP_BG_GREEN        5
#define DISP_BG_BRIGHT_WHITE 6

/* Text color indices. */
#define DISP_TEXT_BRIGHT_AMBER  0
#define DISP_TEXT_BLACK         1
#define DISP_TEXT_GREEN         2
#define DISP_TEXT_LIGHT_BLUE    3
#define DISP_TEXT_CYAN          4
#define DISP_TEXT_BRIGHT_YELLOW 5
#define DISP_TEXT_WHITE         6
#define DISP_TEXT_BRIGHT_GREEN  7

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int palette;   /* preset index 0..display_palette_count()-1; "Custom" is derived, never stored */
    int bg;        /* 0..DISP_BG_COLOR_N-1 = color; >= DISP_BG_COLOR_N = image slot */
    int text;      /* 0..DISP_TEXT_N-1 */
} DisplayState;

unsigned short display_bg_rgb(int index);          /* color indices only */
unsigned short display_text_rgb(int index);
const char *display_bg_color_name(int index);
const char *display_text_name(int index);
const char *display_preset_name(int index);
int display_preset_bg(int index);
int display_preset_text(int index);

/* The preset name (machine or image) when bg/text still match
   display_preset_bg/text(d->palette), else "Custom". */
const char *display_palette_name(const DisplayState *d);

void display_defaults(DisplayState *d);

/* Register the bitmaps discovered in the disc's TGA folder. Names are borrowed,
   not copied: the caller must keep the array alive for the program's lifetime.
   count is clamped to DISP_IMAGE_MAX. */
void display_set_images(const char *const *names, int count);
int display_image_count(void);
int display_bg_count(void);          /* DISP_BG_COLOR_N + display_image_count() */

/* DISP_PRESET_N color presets followed by one preset per registered image.
   Image presets pair that image's background with white text. Call
   display_set_images() first -- the count moves with the disc's TGA list. */
int display_palette_count(void);

int display_is_image(const DisplayState *d);
const char *display_bg_name(const DisplayState *d);   /* color name or file name */

/* dir is +1 or -1. Cycling bg or text skips any candidate that would make the
   text the same color as the background. */
void display_cycle_bg(DisplayState *d, int dir);
void display_cycle_text(DisplayState *d, int dir);
void display_cycle_palette(DisplayState *d, int dir);

#define DISP_BLOB_BYTES 4    /* [sentinel=1][palette][bg][text] */

/* Write the display block. out must have room for DISP_BLOB_BYTES.
   Returns the number of bytes written. */
int display_encode(const DisplayState *d, unsigned char *out);

/* Read the display block. Any field that is absent, truncated, mis-sentinelled,
   out of range, or naming an image slot the current disc does not have falls
   back to its default. Returns 1 when the whole block was accepted, 0 when any
   part of it was defaulted. Call display_set_images() first, so image-slot
   validation has the real count. */
int display_decode(const unsigned char *buf, int len, DisplayState *d);

#ifdef __cplusplus
}
#endif

#endif
