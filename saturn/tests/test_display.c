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

static void test_encode_decode_roundtrip(void) {
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];
    int n;

    display_set_images(NULL, 0);
    display_defaults(&a);
    a.palette = 7; a.bg = display_preset_bg(7); a.text = display_preset_text(7);

    n = display_encode(&a, buf);
    assert(n == DISP_BLOB_BYTES);
    assert(buf[0] == 2);            /* sentinel: name-bearing form */

    assert(display_decode(buf, n, &b) == 1);
    assert(b.palette == a.palette && b.bg == a.bg && b.text == a.text);
}

static void test_collisions_roundtrip(void) {
    /* The regression the stored palette index exists to prevent: identical
       colors must still reload as the machine the player picked. */
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];
    int pairs[2][2] = { { 3, 11 }, { 12, 13 } };
    int p, s;

    display_set_images(NULL, 0);
    for (p = 0; p < 2; p++) {
        for (s = 0; s < 2; s++) {
            int idx = pairs[p][s];
            a.palette = idx;
            a.bg = display_preset_bg(idx);
            a.text = display_preset_text(idx);
            display_encode(&a, buf);
            assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
            assert(b.palette == idx);
            assert(strcmp(display_palette_name(&b), display_preset_name(idx)) == 0);
        }
    }
}

static void test_custom_state_roundtrips(void) {
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];

    display_set_images(NULL, 0);
    display_defaults(&a);
    a.text = DISP_TEXT_CYAN;                     /* diverged -> Custom */
    assert(strcmp(display_palette_name(&a), "Custom") == 0);

    display_encode(&a, buf);
    assert(buf[1] == 12);                        /* the machine index survives */
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 1);
    assert(b.text == DISP_TEXT_CYAN);
    assert(strcmp(display_palette_name(&b), "Custom") == 0);
}

static void test_decode_rejects_bad_input(void) {
    DisplayState d, def;
    unsigned char buf[DISP_BLOB_BYTES];

    display_set_images(NULL, 0);
    display_defaults(&def);

    /* Absent block. */
    assert(display_decode(NULL, 0, &d) == 0);
    assert(d.palette == def.palette && d.bg == def.bg && d.text == def.text);

    /* Truncated block. */
    buf[0] = 1; buf[1] = 3; buf[2] = 0;
    assert(display_decode(buf, 3, &d) == 0);
    assert(d.palette == def.palette);

    /* Wrong sentinel. */
    buf[0] = 9; buf[1] = 3; buf[2] = 2; buf[3] = 3;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.palette == def.palette);

    /* Out-of-range palette, background, and text each fall back. */
    buf[0] = 1; buf[1] = 99; buf[2] = 2; buf[3] = 3;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.palette == def.palette);

    buf[0] = 1; buf[1] = 3; buf[2] = 99; buf[3] = 3;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.bg == def.bg);

    buf[0] = 1; buf[1] = 3; buf[2] = 2; buf[3] = 99;
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.text == def.text);
}

static void test_decode_missing_image_falls_back(void) {
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];

    /* Saved while two images were on the disc... */
    display_set_images(IMAGES, 2);
    a.palette = 4;                 /* ZX Spectrum: Light Gray bg, Black text */
    a.bg = DISP_BG_COLOR_N;        /* first image slot (exists at encode time) */
    a.text = DISP_TEXT_BLACK;
    display_encode(&a, buf);

    /* ...reloaded on a disc with none: the background falls back to Black,
       which would clash with the surviving Black text and blank the screen.
       The pairwise guard in display_decode catches this and restores both
       fields from the saved palette's own pair (ZX Spectrum: Light Gray/Black)
       instead of leaving Black-on-Black. */
    display_set_images(NULL, 0);
    assert(display_decode(buf, DISP_BLOB_BYTES, &b) == 0);
    assert(!display_is_image(&b));
    assert(display_bg_rgb(b.bg) != display_text_rgb(b.text));
    assert(b.palette == 4);                    /* palette byte survived */
    assert(b.bg == DISP_BG_LIGHT_GRAY);         /* restored from ZX Spectrum's own pair */
    assert(b.text == DISP_TEXT_BLACK);          /* restored from ZX Spectrum's own pair */
    display_set_images(IMAGES, 2);
}

static void test_decode_missing_image_never_clashes(void) {
    /* General form of the regression above: any saved image background paired
       with a text color that also exists as a background color must decode,
       with no images registered, to a legible pair -- for every palette, since
       the restored bg/text come from PRESETS[d->palette]. Black and Green are
       the two colors present in both the background and text tables. */
    static const int clashing_texts[2] = { DISP_TEXT_BLACK, DISP_TEXT_GREEN };
    DisplayState a, b;
    unsigned char buf[DISP_BLOB_BYTES];
    int p, t;

    for (p = 0; p < DISP_PRESET_N; p++) {
        for (t = 0; t < 2; t++) {
            display_set_images(IMAGES, 2);
            a.palette = p;
            a.bg = DISP_BG_COLOR_N;             /* first image slot */
            a.text = clashing_texts[t];
            display_encode(&a, buf);

            display_set_images(NULL, 0);
            display_decode(buf, DISP_BLOB_BYTES, &b);
            assert(display_bg_rgb(b.bg) != display_text_rgb(b.text));
        }
    }
    display_set_images(IMAGES, 2);
}

static void test_decode_multi_field_corruption(void) {
    DisplayState d, def;
    unsigned char buf[DISP_BLOB_BYTES];

    display_set_images(NULL, 0);
    display_defaults(&def);

    /* Valid sentinel and length, valid non-default background, but both
       palette and text are out of range. Both corrupt fields fall back
       independently while the valid background is accepted. */
    buf[0] = 1;                 /* valid sentinel */
    buf[1] = 99;                /* out-of-range palette */
    buf[2] = DISP_BG_AMBER;     /* valid, non-default background */
    buf[3] = 99;                /* out-of-range text */
    assert(display_decode(buf, 4, &d) == 0);
    assert(d.bg == DISP_BG_AMBER);              /* valid field accepted */
    assert(d.palette == def.palette);           /* corrupt field fell back */
    assert(d.text == def.text);                 /* corrupt field fell back */
}

static void test_palette_count_includes_images(void) {
    static const char *const names[] = { "FOREST.TGA", "CASTLE.TGA" };
    display_set_images(NULL, 0);
    assert(display_palette_count() == DISP_PRESET_N);
    display_set_images(names, 2);
    assert(display_palette_count() == DISP_PRESET_N + 2);
    display_set_images(NULL, 0);
}

static void test_image_presets_pin_white_text(void) {
    static const char *const names[] = { "FOREST.TGA", "CASTLE.TGA" };
    display_set_images(names, 2);
    for (int i = 0; i < 2; i++) {
        int p = DISP_PRESET_N + i;
        assert(display_preset_bg(p)   == DISP_BG_COLOR_N + i);
        assert(display_preset_text(p) == DISP_TEXT_WHITE);
        assert(strcmp(display_preset_name(p), names[i]) == 0);
    }
    display_set_images(NULL, 0);
}

static void test_cycle_palette_reaches_images(void) {
    static const char *const names[] = { "FOREST.TGA", "CASTLE.TGA" };
    DisplayState d;
    display_set_images(names, 2);
    display_defaults(&d);
    /* Walk forward from the last color preset into the image presets. */
    d.palette = DISP_PRESET_N - 1;
    d.bg      = display_preset_bg(d.palette);
    d.text    = display_preset_text(d.palette);
    display_cycle_palette(&d, 1);
    assert(d.palette == DISP_PRESET_N);
    assert(d.bg      == DISP_BG_COLOR_N);
    assert(d.text    == DISP_TEXT_WHITE);
    display_cycle_palette(&d, 1);
    assert(d.palette == DISP_PRESET_N + 1);
    /* Past the last image it wraps to preset 0. */
    display_cycle_palette(&d, 1);
    assert(d.palette == 0);
    /* Backward from preset 0 lands on the last image. */
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PRESET_N + 1);
    display_set_images(NULL, 0);
}

static void test_cycle_palette_from_custom_with_images(void) {
    /* Regression: the Custom re-entry branch of display_cycle_palette must
       land on the last *image* preset when images are registered, not on
       DISP_PRESET_N - 1 (the last color preset). */
    static const char *const names[] = { "FOREST.TGA", "CASTLE.TGA" };
    DisplayState d;
    display_set_images(names, 2);

    /* Genuine Custom state: palette 0's pair is Black/Bright Amber, so pick a
       legible pair that does not match it. */
    d.palette = 0;
    d.bg      = DISP_BG_GREEN;
    d.text    = DISP_TEXT_WHITE;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);

    /* Backward from Custom lands on the last image preset. */
    display_cycle_palette(&d, -1);
    assert(d.palette == DISP_PRESET_N + 1);
    assert(d.bg      == display_preset_bg(DISP_PRESET_N + 1));
    assert(d.text    == display_preset_text(DISP_PRESET_N + 1));

    /* Forward from Custom lands on preset 0. */
    d.palette = 0;
    d.bg      = DISP_BG_GREEN;
    d.text    = DISP_TEXT_WHITE;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_cycle_palette(&d, 1);
    assert(d.palette == 0);
    assert(d.bg      == display_preset_bg(0));
    assert(d.text    == display_preset_text(0));

    display_set_images(NULL, 0);
}

static void test_image_preset_name_shown_not_custom(void) {
    static const char *const names[] = { "FOREST.TGA" };
    DisplayState d;
    display_set_images(names, 1);
    d.palette = DISP_PRESET_N;
    d.bg      = DISP_BG_COLOR_N;
    d.text    = DISP_TEXT_WHITE;
    assert(strcmp(display_palette_name(&d), "FOREST.TGA") == 0);
    /* Diverging from the pinned pair reads as Custom, same as color presets. */
    d.text = DISP_TEXT_GREEN;
    assert(strcmp(display_palette_name(&d), "Custom") == 0);
    display_set_images(NULL, 0);
}

static void test_decode_rejects_stale_image_preset(void) {
    static const char *const two[]  = { "FOREST.TGA", "CASTLE.TGA" };
    static const char *const one[]  = { "FOREST.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    /* Save while two images exist, selecting the second one. */
    display_set_images(two, 2);
    saved.palette = DISP_PRESET_N + 1;
    saved.bg      = DISP_BG_COLOR_N + 1;
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    /* Round-trips intact while both images are present. */
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(d.palette == DISP_PRESET_N + 1);

    /* On a disc with only one image, the stale preset index is rejected. */
    display_set_images(one, 1);
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 0);
    assert(d.palette < display_palette_count());
    display_set_images(NULL, 0);
}

/* --- image identity across discs ------------------------------------------
   The saved blob used to store only a slot index, so a disc whose TGA set had
   changed silently resolved that index to a different picture. These pin the
   behaviour to the file name instead. */

static void test_decode_resolves_image_after_reorder(void) {
    static const char *const before[] = { "AMIGA.TGA", "FOREST.TGA", "CASTLE.TGA" };
    static const char *const after[]  = { "CASTLE.TGA", "AMIGA.TGA", "FOREST.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    /* Save CASTLE, which is slot 2 on the disc we saved from. */
    display_set_images(before, 3);
    saved.palette = DISP_PRESET_N + 2;
    saved.bg      = DISP_BG_COLOR_N + 2;
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    /* Same three images, different scan order: CASTLE is slot 0 now. Resolving
       positionally would hand back FOREST. */
    display_set_images(after, 3);
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(d.bg == DISP_BG_COLOR_N + 0);
    assert(strcmp(display_bg_name(&d), "CASTLE.TGA") == 0);
    assert(d.palette == DISP_PRESET_N + 0);
    display_set_images(NULL, 0);
}

static void test_decode_follows_image_when_earlier_one_removed(void) {
    static const char *const before[] = { "AMIGA.TGA", "FOREST.TGA", "CASTLE.TGA" };
    static const char *const after[]  = { "AMIGA.TGA", "CASTLE.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_set_images(before, 3);
    saved.palette = DISP_PRESET_N + 2;      /* CASTLE */
    saved.bg      = DISP_BG_COLOR_N + 2;
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    /* Dropping FOREST shifts CASTLE from slot 2 to slot 1. The old index 2 is
       still in range on this disc, so a range check alone would accept it and
       show nothing at all. */
    display_set_images(after, 2);
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(strcmp(display_bg_name(&d), "CASTLE.TGA") == 0);
    display_set_images(NULL, 0);
}

static void test_decode_image_gone_falls_back(void) {
    static const char *const before[] = { "AMIGA.TGA", "FOREST.TGA" };
    static const char *const after[]  = { "AMIGA.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_set_images(before, 2);
    saved.palette = DISP_PRESET_N + 1;      /* FOREST */
    saved.bg      = DISP_BG_COLOR_N + 1;
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    display_set_images(after, 1);
    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 0);   /* reports the fallback */
    assert(!display_is_image(&d));
    assert(d.bg < DISP_BG_COLOR_N);
    assert(d.palette < display_palette_count());
    display_set_images(NULL, 0);
}

static void test_color_state_needs_no_image(void) {
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_set_images(NULL, 0);
    saved.palette = 3;
    saved.bg      = display_preset_bg(3);
    saved.text    = display_preset_text(3);
    display_encode(&saved, blob);

    assert(display_decode(blob, DISP_BLOB_BYTES, &d) == 1);
    assert(d.palette == 3 && d.bg == saved.bg && d.text == saved.text);
}

static void test_legacy_blob_still_decodes(void) {
    /* A blob written before names were stored: sentinel 1, four bytes,
       positional. Must keep working rather than resetting someone's colors. */
    unsigned char legacy[4];
    DisplayState d;

    display_set_images(NULL, 0);
    legacy[0] = 1;
    legacy[1] = 5;
    legacy[2] = (unsigned char) display_preset_bg(5);
    legacy[3] = (unsigned char) display_preset_text(5);
    assert(display_decode(legacy, 4, &d) == 1);
    assert(d.palette == 5);
    assert(d.bg == display_preset_bg(5));
    assert(d.text == display_preset_text(5));
}

static void test_legacy_blob_image_index_rejected(void) {
    /* A legacy blob naming an image slot carries no name to verify it with, so
       it cannot be trusted to mean the same picture. Fall back rather than
       guess. */
    unsigned char legacy[4];
    static const char *const one[] = { "AMIGA.TGA" };
    DisplayState d;

    display_set_images(one, 1);
    legacy[0] = 1;
    legacy[1] = DISP_PRESET_N;              /* image preset */
    legacy[2] = DISP_BG_COLOR_N;            /* image slot */
    legacy[3] = DISP_TEXT_WHITE;
    assert(display_decode(legacy, 4, &d) == 0);
    assert(!display_is_image(&d));
    display_set_images(NULL, 0);
}

static void test_decode_truncated_name_block(void) {
    static const char *const one[] = { "AMIGA.TGA" };
    DisplayState d, saved;
    unsigned char blob[DISP_BLOB_BYTES];

    display_set_images(one, 1);
    saved.palette = DISP_PRESET_N;
    saved.bg      = DISP_BG_COLOR_N;
    saved.text    = DISP_TEXT_WHITE;
    display_encode(&saved, blob);

    /* Sentinel says a name follows, but the block is cut short. */
    assert(display_decode(blob, DISP_BLOB_BYTES - 1, &d) == 0);
    assert(!display_is_image(&d));
    display_set_images(NULL, 0);
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
    test_palette_count_includes_images();
    test_image_presets_pin_white_text();
    test_cycle_palette_reaches_images();
    test_cycle_palette_from_custom_with_images();
    test_image_preset_name_shown_not_custom();
    test_decode_rejects_stale_image_preset();
    test_decode_resolves_image_after_reorder();
    test_decode_follows_image_when_earlier_one_removed();
    test_decode_image_gone_falls_back();
    test_color_state_needs_no_image();
    test_legacy_blob_still_decodes();
    test_legacy_blob_image_index_rejected();
    test_decode_truncated_name_block();
    test_encode_decode_roundtrip();
    test_collisions_roundtrip();
    test_custom_state_roundtrips();
    test_decode_rejects_bad_input();
    test_decode_missing_image_falls_back();
    test_decode_missing_image_never_clashes();
    test_decode_multi_field_corruption();
    printf("test_display: OK\n");
    return 0;
}
