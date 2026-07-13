/*----------------------
 | mojozork_saturn.c
 | Description: The single translation unit that pulls in the mojozork Z-Machine
 |   core (via #include of ../../mojozork.c, mirroring mojozork-libretro.c) so this
 |   file can reach the core's file-static symbols. MOJOZORK_SATURN excludes the
 |   core's stdio main/die and enables the Saturn guards. Owns the interpreter
 |   state and the boot/run entry points main.cxx calls, plus the accessor that
 |   exposes the loaded story image to the typeahead.
 | Author: suinevere
 | Dependencies: saturn_compat.h, ../../mojozork.c (the Z-Machine core),
 |   saturn_glue.h (the hooks wired into ZMachineState)
 ----------------------*/
#define MOJOZORK_SATURN 1
#include "saturn_compat.h"
#include "../../mojozork.c"

#include "saturn_glue.h"

/*----------------------
 | g_zmachine
 | Description: The interpreter's state block, pointed to by the core's global
 |   GState for the life of the program.
 | Author: suinevere
 ----------------------*/
static ZMachineState g_zmachine;

/*----------------------
 | mojo_boot
 | Description: Wires the Saturn hooks into the Z-Machine state (die, writestr,
 |   readline, sound_effect), seeds the RNG, and loads the story image. The core
 |   takes ownership of the buffer -- initStory frees the previous story on the
 |   next boot -- so the caller must not free it.
 | Author: suinevere
 | Dependencies: ../../mojozork.c (GState, initStory), saturn_glue.h (the hooks)
 | Globals: g_zmachine, GState, random_seed
 | Params: story -- the loaded story bytes; len -- their length; seed -- RNG seed
 | Returns: N/A
 ----------------------*/
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

/*----------------------
 | mojo_run
 | Description: Runs the interpreter until it sets quit, executing one instruction
 |   per iteration. Input/output happen inside the instructions via the wired
 |   hooks.
 | Author: suinevere
 | Dependencies: ../../mojozork.c (runInstruction, GState->quit)
 | Globals: GState
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void mojo_run(void) {
    while (!GState->quit) {
        runInstruction();
    }
}

/*----------------------
 | saturn_story_data
 | Description: Exposes the loaded story image so the typeahead can decode the
 |   game's own dictionary/grammar at runtime.
 | Author: suinevere
 | Dependencies: ../../mojozork.c (GState)
 | Globals: GState
 | Params: len_out -- receives the story length (may be NULL); set to 0 when none
 | Returns: the story bytes, or NULL before a story is loaded
 ----------------------*/
const uint8_t* saturn_story_data(uint32_t* len_out) {
    if (GState == NULL || GState->story == NULL) { if (len_out) *len_out = 0; return NULL; }
    if (len_out) *len_out = (uint32_t) GState->story_len;
    return GState->story;
}
