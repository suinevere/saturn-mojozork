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

int main(void) {
    test_tables_well_formed();
    test_known_colors();
    test_preset_contents();
    test_defaults_and_palette_name();
    printf("test_display: OK\n");
    return 0;
}
