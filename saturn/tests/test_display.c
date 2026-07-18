#include "../src/display.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Recompute the RGB555 packing independently so a typo in the macro is caught. */
static unsigned short rgb(int r, int g, int b) {
    return (unsigned short)(0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
}

static void test_tables_well_formed(void) {
    int i;
    for (i = 0; i < DISP_PRESET_N; i++) {
        assert(display_preset_bg(i)   >= 0 && display_preset_bg(i)   < DISP_BG_COLOR_N);
        assert(display_preset_text(i) >= 0 && display_preset_text(i) < DISP_TEXT_N);
        assert(display_preset_name(i) != NULL);
        assert(display_preset_name(i)[0] != '\0');
        /* Must fit the selector field: 40-column screen, value drawn at x+16. */
        assert(strlen(display_preset_name(i)) <= 22);
    }
    for (i = 0; i < DISP_BG_COLOR_N; i++)  assert(display_bg_color_name(i) != NULL);
    for (i = 0; i < DISP_TEXT_N; i++)      assert(display_text_name(i)     != NULL);
}

static void test_known_colors(void) {
    assert(display_bg_rgb(DISP_BG_BLACK)        == rgb(0x00, 0x00, 0x00));
    assert(display_bg_rgb(DISP_BG_AMBER)        == rgb(0xFF, 0xB0, 0x00));
    assert(display_bg_rgb(DISP_BG_BLUE)         == rgb(0x00, 0x00, 0xAA));
    assert(display_bg_rgb(DISP_BG_BRIGHT_WHITE) == rgb(0xFF, 0xFF, 0xFF));
    assert(display_text_rgb(DISP_TEXT_BRIGHT_AMBER) == rgb(0xFF, 0xAF, 0x00));
    assert(display_text_rgb(DISP_TEXT_BRIGHT_GREEN) == rgb(0x55, 0xFF, 0x55));
    /* ANSI 37 is light gray, not true white -- keeps BBC Micro / MSX authentic. */
    assert(display_text_rgb(DISP_TEXT_WHITE) == rgb(0xAA, 0xAA, 0xAA));
}

static void test_preset_contents(void) {
    /* Spot-check the ends and the two collision pairs from the spec. */
    assert(display_preset_bg(0)  == DISP_BG_BLACK);
    assert(display_preset_text(0) == DISP_TEXT_BRIGHT_AMBER);          /* Toshiba T3100 */
    assert(display_preset_bg(1)  == DISP_BG_AMBER);
    assert(display_preset_text(1) == DISP_TEXT_BLACK);                 /* Monochrome P3 */
    assert(display_preset_bg(14) == DISP_BG_BRIGHT_WHITE);
    assert(display_preset_text(14) == DISP_TEXT_BLACK);                /* Mac Classic */
    /* C64 (3) and Atari 800 (11) share a combo but not a name. */
    assert(display_preset_bg(3) == display_preset_bg(11));
    assert(display_preset_text(3) == display_preset_text(11));
    assert(strcmp(display_preset_name(3), display_preset_name(11)) != 0);
    /* IBM PC MDA (12) and Commodore PET (13) likewise. */
    assert(display_preset_bg(12) == display_preset_bg(13));
    assert(display_preset_text(12) == display_preset_text(13));
    assert(strcmp(display_preset_name(12), display_preset_name(13)) != 0);
}

static void test_defaults_and_palette_name(void) {
    DisplayState d;
    display_defaults(&d);
    assert(d.palette == 12);                       /* IBM PC (MDA Monitor) */
    assert(d.bg == DISP_BG_BLACK);
    assert(d.text == DISP_TEXT_BRIGHT_GREEN);
    assert(strcmp(display_palette_name(&d), display_preset_name(12)) == 0);

    /* Diverge from the preset -> Custom; restore -> the name comes back. */
    d.text = DISP_TEXT_CYAN;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    d.text = DISP_TEXT_BRIGHT_GREEN;
    assert(strcmp(display_palette_name(&d), display_preset_name(12)) == 0);

    /* The stored index disambiguates the collision: same colors, different name. */
    d.palette = 13;
    assert(strcmp(display_palette_name(&d), display_preset_name(13)) == 0);
}

static const char *const IMAGES[] = { "HOUSE.TGA", "CAVE.TGA" };

static void test_image_registration(void) {
    display_set_images(NULL, 0);
    assert(display_image_count() == 0);
    assert(display_bg_count() == DISP_BG_COLOR_N);

    display_set_images(IMAGES, 2);
    assert(display_image_count() == 2);
    assert(display_bg_count() == DISP_BG_COLOR_N + 2);

    /* Over-cap registration clamps rather than overflowing. */
    display_set_images(IMAGES, DISP_IMAGE_MAX + 5);
    assert(display_image_count() == DISP_IMAGE_MAX);
    display_set_images(IMAGES, 2);
}

static void test_bg_name_and_is_image(void) {
    DisplayState d;
    display_set_images(IMAGES, 2);
    display_defaults(&d);
    assert(!display_is_image(&d));
    assert(strcmp(display_bg_name(&d), "Black") == 0);

    d.bg = DISP_BG_COLOR_N;          /* first image slot */
    assert(display_is_image(&d));
    assert(strcmp(display_bg_name(&d), "HOUSE.TGA") == 0);
    /* An image is never a preset background, so the palette reads Custom. */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
}

static void test_cycle_bg_wraps_through_images(void) {
    DisplayState d;
    display_set_images(IMAGES, 2);
    display_defaults(&d);
    d.text = DISP_TEXT_BRIGHT_AMBER;   /* matches no bg color: guard inactive */
    d.bg = DISP_BG_BRIGHT_WHITE;       /* last color */

    display_cycle_bg(&d, 1);
    assert(d.bg == DISP_BG_COLOR_N);       /* into the images */
    display_cycle_bg(&d, 1);
    assert(d.bg == DISP_BG_COLOR_N + 1);
    display_cycle_bg(&d, 1);
    assert(d.bg == DISP_BG_BLACK);         /* wraps back to the first color */
    display_cycle_bg(&d, -1);
    assert(d.bg == DISP_BG_COLOR_N + 1);   /* and backwards past the end */
}

static void test_legibility_guard(void) {
    DisplayState d;
    int i;
    display_set_images(NULL, 0);          /* colors only: guard fully active */
    display_defaults(&d);

    /* Black text must never be able to sit on the Black background. */
    d.text = DISP_TEXT_BLACK;
    d.bg   = DISP_BG_BRIGHT_WHITE;
    for (i = 0; i < 40; i++) {
        display_cycle_bg(&d, 1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }
    for (i = 0; i < 40; i++) {
        display_cycle_bg(&d, -1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }

    /* Same guard from the text side: Green text vs the Green background. */
    d.bg   = DISP_BG_GREEN;
    d.text = DISP_TEXT_BLACK;
    for (i = 0; i < 40; i++) {
        display_cycle_text(&d, 1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }
    for (i = 0; i < 40; i++) {
        display_cycle_text(&d, -1);
        assert(display_bg_rgb(d.bg) != display_text_rgb(d.text));
    }
}

static void test_guard_inactive_over_images(void) {
    DisplayState d;
    int i, seen_black = 0;
    display_set_images(IMAGES, 2);
    display_defaults(&d);
    d.bg = DISP_BG_COLOR_N;      /* image background: every text color reachable */
    for (i = 0; i < DISP_TEXT_N; i++) {
        display_cycle_text(&d, 1);
        if (d.text == DISP_TEXT_BLACK) seen_black = 1;
    }
    assert(seen_black);
}

static void test_cycle_palette(void) {
    DisplayState d;
    display_set_images(NULL, 0);
    display_defaults(&d);                  /* palette 12 */

    display_cycle_palette(&d, 1);
    assert(d.palette == 13);
    assert(d.bg == display_preset_bg(13) && d.text == display_preset_text(13));
    assert(strcmp(display_palette_name(&d), "Commodore PET") == 0);

    display_cycle_palette(&d, -1);
    assert(d.palette == 12);
    assert(strcmp(display_palette_name(&d), "IBM PC (MDA)") == 0);

    /* Wraps at both ends. */
    d.palette = DISP_PRESET_N - 1;
    d.bg = display_preset_bg(d.palette); d.text = display_preset_text(d.palette);
    display_cycle_palette(&d, 1);
    assert(d.palette == 0);
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PRESET_N - 1);

    /* From a Custom state, forward lands on preset 0 and back on the last. */
    display_defaults(&d);
    d.text = DISP_TEXT_CYAN;               /* now Custom */
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_cycle_palette(&d, 1);
    assert(d.palette == 0);
    assert(d.bg == display_preset_bg(0) && d.text == display_preset_text(0));

    display_defaults(&d);
    d.text = DISP_TEXT_CYAN;
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PRESET_N - 1);

    /* Selecting a preset always yields a legible pair. */
    {
        int i;
        for (i = 0; i < DISP_PRESET_N; i++) {
            assert(display_bg_rgb(display_preset_bg(i))
                != display_text_rgb(display_preset_text(i)));
        }
    }
}

int main(void) {
    test_tables_well_formed();
    test_known_colors();
    test_preset_contents();
    test_defaults_and_palette_name();
    test_image_registration();
    test_bg_name_and_is_image();
    test_cycle_bg_wraps_through_images();
    test_legibility_guard();
    test_guard_inactive_over_images();
    test_cycle_palette();
    printf("test_display: OK\n");
    return 0;
}
