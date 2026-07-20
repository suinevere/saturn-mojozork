/*----------------------
 | app_state.cxx
 | Description: Single definitions of the cross-cutting globals declared extern
 |   in app_state.h -- persisted game options, save/restore session state, the
 |   soft-reset jump target, the loaded story file, and the console scroll
 |   position. Initializers are carried over verbatim from main.cxx.
 | Author: suinevere
 | Dependencies: app_state.h, music.h (MIX_DYNAMIC)
 ----------------------*/

#include "app_state.h"
#include "music.h"

int g_difficulty = DIFF_EASY;

int g_music_level = 7;
int g_pcm_level   = 4;
int g_mix_mode  = MIX_DYNAMIC;
int g_sel_track = 10;

DisplayState g_display;

char g_dialnum[DIALNUM_MAX + 1] = "199403";

int g_restore_device = -1;
int g_restore_slot   = -1;
const char *g_autocmd = nullptr;

int g_last_device = -1;
int g_last_slot   = -1;

int g_save_device = -1;
int g_save_slot   = -1;

jmp_buf  g_title_jmp;
bool     g_title_jmp_armed = false;

const char *g_story_filename = "ZORK1.Z3";

int g_scroll = 0;
