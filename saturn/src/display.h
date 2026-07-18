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
    int palette;   /* preset index 0..DISP_PRESET_N-1; "Custom" is derived, never stored */
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

/* The machine name when bg/text still match PRESETS[d->palette], else "Custom". */
const char *display_palette_name(const DisplayState *d);

void display_defaults(DisplayState *d);

/* Register the bitmaps discovered in the disc's TGA folder. Names are borrowed,
   not copied: the caller must keep the array alive for the program's lifetime.
   count is clamped to DISP_IMAGE_MAX. */
void display_set_images(const char *const *names, int count);
int display_image_count(void);
int display_bg_count(void);          /* DISP_BG_COLOR_N + display_image_count() */

int display_is_image(const DisplayState *d);
const char *display_bg_name(const DisplayState *d);   /* color name or file name */

/* dir is +1 or -1. Cycling bg or text skips any candidate that would make the
   text the same color as the background. */
void display_cycle_bg(DisplayState *d, int dir);
void display_cycle_text(DisplayState *d, int dir);
void display_cycle_palette(DisplayState *d, int dir);

#ifdef __cplusplus
}
#endif

#endif
