/* Single-TU wrapper: pull in the Z-Machine core so we can reach its
   file-static symbols (mirrors mojozork-libretro.c). MOJOZORK_SATURN excludes
   the core's stdio main/die and enables the Saturn guards. */
#define MOJOZORK_SATURN 1
#include "saturn_compat.h"
#include "../mojozork.c"

#include "saturn_glue.h"

static ZMachineState g_zmachine;

void mojo_boot(uint8_t *story, uint32_t len, int seed) {
    GState = &g_zmachine;
    GState->startup_script = NULL;
    GState->die      = saturn_die;
    GState->writestr = saturn_writestr;
    GState->readline = saturn_readline;
    GState->sound_effect = saturn_sound_effect;
    random_seed = (sint32) seed;
    initStory("ZORK1.Z3", story, len);
}

void mojo_run(void) {
    while (!GState->quit) {
        runInstruction();
    }
}

// Expose the loaded story image so the typeahead can decode the game's own
// dictionary/grammar at runtime. Returns NULL (len 0) before a story is loaded.
const uint8_t* saturn_story_data(uint32_t* len_out) {
    if (GState == NULL || GState->story == NULL) { if (len_out) *len_out = 0; return NULL; }
    if (len_out) *len_out = (uint32_t) GState->story_len;
    return GState->story;
}
