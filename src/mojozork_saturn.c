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
    random_seed = (sint32) seed;
    initStory("ZORK1.DAT", story, len);
}

void mojo_run(void) {
    while (!GState->quit) {
        runInstruction();
    }
}
