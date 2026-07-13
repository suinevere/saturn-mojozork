/*----------------------
 | main.cxx
 | Description: The Saturn client entry point and boot orchestrator. Initializes
 |   the console and peripherals, arms the soft-reset return-to-title target,
 |   front-loads every CD read into the title screen's silent window (game
 |   catalogue, background art, and the online Zork I vocabulary), starts the menu
 |   music, runs the top-level mode menu (Play Local / Play Online / Load Save
 |   Game / Options), loads the chosen story, wires the music engine to the game,
 |   and hands control to the Z-Machine. Every subsystem lives in its own module;
 |   this file only sequences them. The interpreter hooks it depends on
 |   (saturn_readline etc.) live in saturn_glue.cxx; the soft reset lives in
 |   soft_reset.cxx.
 | Author: suinevere
 | Dependencies: app_state.h, console.h, console_view.h, display.h, options.h,
 |   menu.h, menu_pages.h, save_ui.h, title.h, game_catalog.h, online.h,
 |   soft_reset.h, saturn_glue.h, saturn_backup.h, sound.h, music.h, input.h,
 |   SRL/GFS/SGL.
 ----------------------*/

#include <srl.hpp>
#include <setjmp.h>

extern "C" {
#include "console.h"
#include "display.h"
#include "saturn_backup.h"
#include "saturn_glue.h"
#include "sound.h"
#include "music.h"
}
#include "app_state.h"
#include "input.h"
#include "console_view.h"
#include "options.h"
#include "soft_reset.h"
#include "menu.h"
#include "menu_pages.h"
#include "save_ui.h"
#include "title.h"
#include "game_catalog.h"
#include "online.h"

using namespace SRL::Types;

/*----------------------
 | main
 | Description: Boots the client and never returns to its caller (it ends by
 |   soft-resetting to the title). Order matters at several points: cd_capture_root
 |   precedes any GFS_SetDir; display_scan_images precedes options_load so saved
 |   image indices validate against the real list. setjmp arms g_title_jmp so the
 |   soft reset (chord or typed reboot/quit) longjmps back here; because that jump
 |   skips destructors, the re-entry path hand-clears g_menu_backing_depth (else
 |   NBG3 stays opaque and hides the title image) and disables the NBG0 image
 |   window, and does NOT re-scan /TGA -- the first-boot scan's list and g_tga_tbl
 |   are plain static RAM that survives the longjmp, and a destructive post-reset
 |   re-scan once wiped the list and made every options background vanish. The
 |   story image is owned by the Z-machine (initStory frees the prior one), so it
 |   is never freed here. music_reset before the menu track clears stale engine
 |   state so a menu-frame music_tick cannot leak a game track. Every CD read is
 |   front-loaded here, under the title art, because the single drive head cannot
 |   play CD-DA while reading data; after this the menu never touches the CD, so
 |   the track plays uninterrupted (and on soft-reset re-entry the preloads are
 |   cached no-ops, so the music starts cleanly). The Z3 load retries the flaky
 |   first-access GFS size stat before allocating and reading. Enabling sound
 |   keys off a sibling <base>.BLB, and the music engine is wired to the CD-DA
 |   backend and seeded from the story's release/serial. When mojo_run returns
 |   (only a death/victory-screen QUIT reaches here, since the prompt intercepts
 |   typed quit), the final screen is held until acknowledged, then it soft-resets
 |   to the title -- the same place every other exit lands.
 | Author: suinevere
 | Dependencies: title.h, game_catalog.h, online.h, options.h, menu.h,
 |   menu_pages.h, save_ui.h, soft_reset.h, saturn_glue.h, saturn_backup.h,
 |   display.h, console.h, console_view.h, sound.h, music.h, input.h, SRL/GFS/SGL
 | Globals: g_display, g_pad, g_title_jmp, g_title_jmp_armed, g_z3_dir_valid,
 |   g_menu_backing_depth, g_music_level, g_pcm_level, g_mix_mode, g_sel_track,
 |   g_story_filename, g_restore_device, g_restore_slot, g_autocmd, g_output_start
 | Params: N/A
 | Returns: 0 nominally, but it never actually returns
 ----------------------*/
int main(void) {
    SRL::Core::Initialize(HighColor::Colors::Black);
    saturn_bup_init();
    cd_capture_root();
    display_scan_images();
    display_defaults(&g_display);
    options_load();

    static MultiPad pads;
    g_pad = &pads;

    int cd_reentry = setjmp(g_title_jmp);
    (void) cd_reentry;
    g_title_jmp_armed = true;
    GFS_Reset();
    cd_capture_root();
    g_z3_dir_valid = false;
    g_menu_backing_depth = 0;
    slScrWindowModeNbg0(0);
    console_init();

    music_reset();

    for (int r = 0; r <= 28; r++) SRL::Debug::PrintClearLine(r);
    text_set_color(DISP_RGB555(0xFF, 0xFF, 0xFF));
    title_bg_show("HOUSE.TGA");
    title_draw_art();
    SRL::Core::Synchronize();

    preload_game_catalog();
    display_preload_images();
    ensure_online_typeahead();

    music_set_level(g_music_level);
    music_cdda_play(g_sel_track);

    int seed = title_and_seed();
    display_apply();

    static const char *modes[] = { "Play Local (single player)", "Play Online (multizork)",
                                   "Load Save Game", "Options" };
    const char* game_file = nullptr;

    for (;;) {
        int mode = menu_select("Z-ATURN", modes, 4);
        if (mode < 0) continue;
        if (mode == 3) { options_menu(); continue; }
        if (mode == 1) { online_mode(); continue; }
        if (mode == 2) {
            game_file = game_select();
            if (game_file == nullptr) continue;
            g_story_filename = game_file;
            int device, slot;
            if (!choose_dest("LOAD - device?", "LOAD - slot?", &device, &slot)) continue;
            g_restore_device = device; g_restore_slot = slot;
            g_autocmd = "restore";
            break;
        }
        game_file = game_select();
        if (game_file == nullptr) continue;
        break;
    }
    g_story_filename = game_file;

    uint8_t *story = nullptr;
    uint32_t len = 0;
    for (int attempt = 0; attempt < 300 && story == nullptr; attempt++) {
        SRL::Cd::File f(game_file);
        int32_t bytes = f.Size.Bytes;
        int32_t ssz   = f.Size.SectorSize;
        SRL::Debug::Print(1, 26, "loading %s...           ", game_file);
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t *buf = (uint8_t *) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf != nullptr && f.Open()) {
                int32_t got = f.Read(bytes, buf);
                f.Close();
                if (got == bytes) { story = buf; len = (uint32_t) bytes; break; }
            }
            if (buf != nullptr) { SRL::Memory::HighWorkRam::Free(buf); }
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    if (story == nullptr) { saturn_die("Could not load %s from CD", game_file); }

    mojo_boot(story, len, seed);

    {
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
        blb[i] = '.'; blb[i+1] = 'B'; blb[i+2] = 'L'; blb[i+3] = 'B'; blb[i+4] = '\0';
        sound_init(blb);
        sound_set_level(g_pcm_level);
        music_set_level(g_music_level);
        music_set_backend(music_cdda_play_mode);
        music_set_isplaying(music_cdda_is_playing);
        music_set_isshort(music_cdda_is_short);
        music_set_game((unsigned int)((story[2] << 8) | story[3]), (const char*) (story + 0x12));
        music_seed((unsigned int) seed);
        music_reset();
        music_set_mix(g_mix_mode, g_sel_track);
        music_start();
    }

    g_output_start = console_total_lines();
    mojo_run();

    render_console();
    SRL::Debug::Print(1, 27, "(press any key/button for the title screen)");
    menu_wait();
    soft_reset_to_title();
    return 0;
}
