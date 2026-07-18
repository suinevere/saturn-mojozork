# Display decode legibility-guard fix

## Bug

`display_decode` validated `palette`, `bg`, and `text` independently but never
checked the resulting pair. An image background saved with Black text (legal,
since the legibility guard is inactive over images) would, on a later boot
where that image is no longer on the disc, fall back `bg` to `DISP_BG_BLACK`
while still accepting `text = Black` — producing a Black-on-Black screen with
no visible way back to the options menu.

## Probe output

Before fix:
```
rc=0 bg=0 text=1 identical=1
```
(`bg=0` is `DISP_BG_BLACK`, `text=1` is `DISP_TEXT_BLACK` — identical RGB, blank screen.)

After fix:
```
rc=0 bg=0 text=7 identical=0
```
(`text=7` is `DISP_TEXT_BRIGHT_GREEN`, restored from `PRESETS[12]` — the probe's
default palette, IBM PC (MDA) — because the corrected bg/text pair clashed and
was replaced by the saved palette's own legible pair.)

## Fix (`saturn/src/display.c`, `display_decode`)

After the three fields are assembled (each still independently validated exactly
as before), added a final pairwise check reusing the existing `clashes()` helper:

```c
    /* Each field above was validated independently, but an image background
       that fell back to a color can still land on the same color as text that
       was independently accepted -- e.g. a saved image background paired with
       Black text, decoded on a disc where that image is gone: bg falls back to
       Black and both fields blank the screen. d->palette is a real preset index
       by this point, and every preset pair is guaranteed legible, so restore
       both fields from it rather than from display_defaults(). */
    if (clashes(d->bg, d->text)) {
        d->bg   = PRESETS[d->palette].bg;
        d->text = PRESETS[d->palette].text;
        ok = 0;
    }
```

`clashes()` already returns 0 for image backgrounds (`bg >= DISP_BG_COLOR_N`),
so this only fires when the *resolved* background is a color, which is exactly
the case that matters. Restoring from `PRESETS[d->palette]` (rather than
`display_defaults()`) preserves the machine the player picked and is guaranteed
legible by the existing `test_cycle_palette` assertion. `d->palette` is a real
preset index 0..14 at this point because it was range-checked earlier in the
same function. Returns 0 (a field was not honored as saved), consistent with
the function's existing contract.

No SRL/SaturnRingLib headers were added; `display.c` still only includes
`display.h`.

## Test changes (`saturn/tests/test_display.c`)

### Updated `test_decode_missing_image_falls_back` (previously asserted the bug)

Old assertions treated Black-on-Black as correct:
```c
assert(b.bg == DISP_BG_BLACK);      /* falls back to the default background */
assert(b.palette == 4);
assert(b.text == DISP_TEXT_BLACK);
```

New body asserts the corrected behavior — the pair must not clash, and both
fields come back as ZX Spectrum's (`palette == 4`) own combination:

```c
static void test_decode_missing_image_falls_back(void) {
    DisplayState a, b;
    unsigned char buf[8];

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
    assert(display_decode(buf, 4, &b) == 0);
    assert(!display_is_image(&b));
    assert(display_bg_rgb(b.bg) != display_text_rgb(b.text));
    assert(b.palette == 4);                    /* palette byte survived */
    assert(b.bg == DISP_BG_LIGHT_GRAY);         /* restored from ZX Spectrum's own pair */
    assert(b.text == DISP_TEXT_BLACK);          /* restored from ZX Spectrum's own pair */
    display_set_images(IMAGES, 2);
}
```

### New `test_decode_missing_image_never_clashes`

Covers the scenario generally: every preset palette, with an image background
paired with each text color that also exists as a background color (Black and
Green), decoded with no images registered, must never produce a clashing pair.

```c
static void test_decode_missing_image_never_clashes(void) {
    /* General form of the regression above: any saved image background paired
       with a text color that also exists as a background color must decode,
       with no images registered, to a legible pair -- for every palette, since
       the restored bg/text come from PRESETS[d->palette]. Black and Green are
       the two colors present in both the background and text tables. */
    static const int clashing_texts[2] = { DISP_TEXT_BLACK, DISP_TEXT_GREEN };
    DisplayState a, b;
    unsigned char buf[8];
    int p, t;

    for (p = 0; p < DISP_PRESET_N; p++) {
        for (t = 0; t < 2; t++) {
            display_set_images(IMAGES, 2);
            a.palette = p;
            a.bg = DISP_BG_COLOR_N;             /* first image slot */
            a.text = clashing_texts[t];
            display_encode(&a, buf);

            display_set_images(NULL, 0);
            display_decode(buf, 4, &b);
            assert(display_bg_rgb(b.bg) != display_text_rgb(b.text));
        }
    }
    display_set_images(IMAGES, 2);
}
```

Registered in `main()` immediately after `test_decode_missing_image_falls_back`.

## Full suite output

```
$ /c/msys64/mingw64/bin/gcc -std=c11 -Wall -Wextra -I src tests/test_display.c src/display.c -o /tmp/d && /tmp/d
test_display: OK

$ /c/msys64/mingw64/bin/gcc -std=c11 -I src tests/test_console.c src/console.c -o /tmp/c && /tmp/c
test_console: OK

$ /c/msys64/mingw64/bin/gcc -std=c11 -I src tests/test_keyboard.c src/keyboard.c -o /tmp/k && /tmp/k
test_keyboard: OK
```

All three built and ran clean (test_display built with `-Wall -Wextra`, zero
warnings). `saturn/src/main.cxx` was not touched.
