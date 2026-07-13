# libmultizork + Zork 1 Module (Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor icculus's `multizorkd.c` into a reusable `libmultizork` core plus a per-game C module, delivering a multi-game-capable telnet server that hosts **Zork 1 byte-for-byte identically** to the real multizork (proven by a golden-master test), with a text lobby and the `zmp-scan` offset finder in place.

**Architecture:** Vendor upstream `mojozork`/`multizorkd` unchanged, lock its behavior behind a scripted 2-player golden-master transcript, then extract the game-agnostic runtime into a static library while pushing every Zork-1 specific (Kind A constants, Kind B byte offsets) into a `games/zork1.c` module implementing a fixed `mz_game_module` interface. Each extraction step is gated by the golden-master test staying green.

**Tech Stack:** C (C99/C11), CMake, SQLite3, POSIX `poll()`/sockets (Linux/Unix only). Tests are shell-driven telnet-session replays plus small C unit tests. `zmp-scan` consumes an existing disassembly (`zork1-disassembly.txt` / `txd`).

## Global Constraints

- **Platform:** Build and run on **Linux/Unix only** (`multizorkd` uses `poll()`, `signal()`, `fork`-free unix sockets). On the user's Windows box, all build/test commands run under **WSL** or a Linux host. Native Windows/PowerShell will not build the server.
- **New project root:** `multizork-server/` at the overlay repo root (sibling to `coup-saturn/`, `zork-netlink/`).
- **Upstream is vendored, not modified in place:** upstream files live under `multizork-server/upstream/`; extraction copies/moves code into `multizork-server/core/`, `games/`, `tools/`. Record the upstream commit SHA in `multizork-server/UPSTREAM.txt`.
- **License:** MojoZork ships under the zlib license (`LICENSE.txt`); preserve it. Zork 1 data (`zork1.dat`) is redistributed by Activision for free and is included upstream.
- **The golden-master test is the regression oracle.** After every extraction task it MUST produce a byte-identical diff. A non-empty diff means the extraction changed behavior — fix before proceeding.
- **Engine interception seam:** `mojozork.c` gives each `ZMachineState` its own opcode dispatch table `GState->opcodes` (built by `initOpcodeTable`). Multiplayer overrides are installed by swapping entries in that table after init — do not fork the engine's dispatch.
- **`Instance` layout invariant:** `ZMachineState zmachine_state` MUST remain the **first field** of `Instance` so the existing `(Instance *) GState` upcast stays valid.

---

## File Structure

```
multizork-server/
  UPSTREAM.txt                 # vendored commit SHA + date
  CMakeLists.txt               # builds libmultizork, multizorkd, zmp-scan, tests
  upstream/                    # pristine vendored icculus/mojozork (reference)
    mojozork.c  multizorkd.c  zork1.dat  zork1-disassembly.txt  ...
  core/
    mojozork.c                 # z-machine engine (moved from upstream, unmodified)
    mojozork.h                 # engine public surface used by the runtime
    mz_game_module.h           # the per-game plug-in interface (NEW)
    mz_registry.h / .c         # module registry: register + lookup by id (NEW)
    mz_runtime.c               # game-agnostic runtime: instances, players,
                               #   fabrication, turn loop, output routing
                               #   (extracted from multizorkd.c)
    mz_server.c                # telnet poll loop, lobby, connection lifecycle
                               #   (extracted from multizorkd.c)
    mz_db.c / mz_db.h          # sqlite persistence (extracted from multizorkd.c)
  games/
    zork1.c                    # Zork 1 module: constants + patch table (NEW)
  app/
    main.c                     # thin entry point: parse args, register modules,
                               #   run server (extracted from multizorkd.c main)
  tools/
    zmp_scan.c                 # Kind B offset finder (NEW)
  tests/
    golden/
      zork1-2player.inputs     # scripted two-player input streams
      zork1-2player.expected   # captured golden transcript (the oracle)
    golden_replay.sh           # runs multizorkd, replays inputs, diffs output
    test_registry.c            # unit test for mz_registry
    test_zmp_scan.sh           # asserts zmp-scan rediscovers Zork 1's 5 offsets
  deploy/
    multizorkd.service         # systemd unit (adapted from upstream)
    README.md                  # build, run, "how to add a game"
```

---

### Task 1: Vendor upstream and reproduce the baseline build

**Files:**
- Create: `multizork-server/upstream/` (vendored tree), `multizork-server/UPSTREAM.txt`

**Interfaces:**
- Consumes: nothing (bootstrap).
- Produces: a working `multizorkd` binary built from pristine upstream, and the local source the rest of the plan reads/relocates.

- [ ] **Step 1: Clone upstream into a temp dir and record the commit**

```bash
cd /tmp
git clone https://github.com/icculus/mojozork.git mojozork-upstream
cd mojozork-upstream
git rev-parse HEAD    # note this SHA
```

- [ ] **Step 2: Vendor the tree into the project**

```bash
mkdir -p "$REPO/multizork-server/upstream"
# copy source + data + build files; drop .git
cp mojozork.c multizorkd.c multizorkd.service CMakeLists.txt LICENSE.txt \
   zork1.dat zork1-disassembly.txt zork1-script.txt notes.txt \
   "$REPO/multizork-server/upstream/"
printf 'icculus/mojozork @ %s\nvendored %s\n' "$(git rev-parse HEAD)" "$(date -I)" \
   > "$REPO/multizork-server/UPSTREAM.txt"
```

- [ ] **Step 3: Install build deps and build upstream `multizorkd`**

```bash
sudo apt-get install -y build-essential cmake libsqlite3-dev   # Debian/Ubuntu/WSL
cd "$REPO/multizork-server/upstream"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target multizorkd
```
Expected: `build/multizorkd` is produced with no errors. (If the upstream CMake target name differs, read `CMakeLists.txt` and use the actual `multizorkd` target.)

- [ ] **Step 4: Smoke-run the baseline server**

```bash
cd "$REPO/multizork-server/upstream"
./build/multizorkd --port 2323 zork1.dat &   # exact args per its main(); see --port/--uid/--gid
sleep 1
printf '\n' | nc -q2 127.0.0.1 2323 | head -20    # expect the Zork 1 banner text
kill %1
```
Expected: the multizork banner ("Welcome" / "West of House" region text) prints. This confirms the baseline runs before any refactor.

- [ ] **Step 5: Commit**

```bash
git add multizork-server/upstream multizork-server/UPSTREAM.txt
git commit -m "vendor icculus/mojozork upstream; reproduce baseline multizorkd build"
```

---

### Task 2: Golden-master harness (the regression oracle)

**Files:**
- Create: `multizork-server/tests/golden/zork1-2player.inputs`, `multizork-server/tests/golden/zork1-2player.expected`, `multizork-server/tests/golden_replay.sh`

**Interfaces:**
- Consumes: a `multizorkd` binary path via `$MZ_BIN` (defaults to the upstream build).
- Produces: `golden_replay.sh` returning exit 0 iff the two-player session output matches `zork1-2player.expected` byte-for-byte. Every later task re-runs this.

- [ ] **Step 1: Write the two-player input script**

Deterministic, avoids RNG-driven combat. Each line is a turn; the harness alternates players by access code / turn order (see Step 3 for the exact driving convention this project uses).

```
# zork1-2player.inputs  (format: PLAYER<TAB>COMMAND)
1	open mailbox
1	read leaflet
2	north
1	north
2	east
1	south
2	west
1	quit
2	quit
```

- [ ] **Step 2: Write the replay harness**

```bash
#!/usr/bin/env bash
# tests/golden_replay.sh — drive a 2-player multizorkd session and diff output.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MZ_BIN="${MZ_BIN:-$HERE/../upstream/build/multizorkd}"
STORY="${MZ_STORY:-$HERE/../upstream/zork1.dat}"
PORT="${MZ_PORT:-2399}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"; kill "${SRV:-0}" 2>/dev/null || true' EXIT

"$MZ_BIN" --port "$PORT" "$STORY" >"$WORK/server.log" 2>&1 & SRV=$!
sleep 1
# Two netcat connections whose interleaving is fixed by the driver below.
python3 "$HERE/drive_two_players.py" \
    --host 127.0.0.1 --port "$PORT" \
    --script "$HERE/golden/zork1-2player.inputs" \
    --out "$WORK/actual.transcript"

if [ "${GOLDEN_RECORD:-0}" = "1" ]; then
    cp "$WORK/actual.transcript" "$HERE/golden/zork1-2player.expected"
    echo "recorded golden transcript"; exit 0
fi
diff -u "$HERE/golden/zork1-2player.expected" "$WORK/actual.transcript"
echo "GOLDEN OK"
```

- [ ] **Step 3: Write the deterministic two-player driver**

`tests/drive_two_players.py` opens two TCP sockets, walks each connection through the lobby to a shared instance (player 1 starts a new game and gets an access code; player 2 joins with that code), then plays turns in the script order, recording every byte each player receives with a `P1>`/`P2>` prefix into `--out`. (Implement the exact lobby/access-code steps observed from the baseline server in Task 1 Step 4; capture them here so the transcript is reproducible.)

- [ ] **Step 4: Record the golden transcript against the pristine baseline**

```bash
chmod +x tests/golden_replay.sh
GOLDEN_RECORD=1 tests/golden_replay.sh
```
Expected: `tests/golden/zork1-2player.expected` is created from the *unmodified* upstream binary. This is the oracle: it captures correct behavior before we touch anything.

- [ ] **Step 5: Verify the harness re-passes against the same baseline**

```bash
tests/golden_replay.sh
```
Expected: `GOLDEN OK` (empty diff). Confirms determinism.

- [ ] **Step 6: Commit**

```bash
git add multizork-server/tests
git commit -m "add golden-master 2-player replay harness + recorded Zork 1 oracle"
```

---

### Task 3: Reorganize the build into libmultizork + thin app (no behavior change)

**Files:**
- Create: `multizork-server/CMakeLists.txt`, `multizork-server/core/` (mojozork.c, mojozork.h), `multizork-server/app/main.c`
- Modify: relocate `multizorkd.c` bodies into `core/` TUs (mechanical split; see below)

**Interfaces:**
- Consumes: the vendored source (Task 1).
- Produces: a `libmultizork` static library target and a `multizorkd` executable target that links it; `$MZ_BIN` now points at the new build. Golden test stays green.

- [ ] **Step 1: Map the upstream source into a symbol inventory**

Read `upstream/multizorkd.c` end to end and write `core/EXTRACTION_MAP.md` listing each top-level symbol grouped as: **engine** (already in mojozork.c), **runtime** (instances, players, fabrication, opcode overrides, turn loop, output routing), **server** (poll loop, connection lifecycle, lobby input fns), **db** (sqlite), **app** (`main`, arg parsing, privilege drop). This map drives Tasks 3–7. (No fabrication: this is inventory of real symbols.)

- [ ] **Step 2: Move the engine and add a minimal engine header**

Copy `upstream/mojozork.c` → `core/mojozork.c` unmodified. Create `core/mojozork.h` declaring only the surface the runtime already uses (types `ZMachineState`, `Opcode`; globals `GState`; fns `initStory`, `runInstruction`, `initOpcodeTable`; the `READUI16`/`WRITEUI16` macros). If those are `static` in `mojozork.c`, follow the established front-end pattern (`mojozork-libretro.c`, `mojozork-sdl3.c` `#include "mojozork.c"`): the runtime TU that needs them `#include`s `mojozork.c` directly rather than linking. Record which approach in `EXTRACTION_MAP.md`.

- [ ] **Step 3: Split multizorkd.c into core TUs + app/main.c**

Per the map, move bodies verbatim into `core/mz_runtime.c`, `core/mz_server.c`, `core/mz_db.c` (+ headers), and the `main`/arg-parsing into `app/main.c`. Keep all logic identical — this is a pure file split, not a rewrite. Shared file-scope globals move with their owners; expose the few cross-TU symbols via headers.

- [ ] **Step 4: Write the project CMake**

```cmake
cmake_minimum_required(VERSION 3.16)
project(multizork_server C)
set(CMAKE_C_STANDARD 11)
find_package(SQLite3 REQUIRED)

add_library(multizork STATIC
  core/mz_runtime.c core/mz_server.c core/mz_db.c core/mz_registry.c)
target_include_directories(multizork PUBLIC core)
target_link_libraries(multizork PUBLIC SQLite::SQLite3)

add_executable(multizorkd app/main.c games/zork1.c)
target_link_libraries(multizorkd PRIVATE multizork)

add_executable(zmp-scan tools/zmp_scan.c)

enable_testing()
add_executable(test_registry tests/test_registry.c)
target_link_libraries(test_registry PRIVATE multizork)
add_test(NAME registry COMMAND test_registry)
add_test(NAME golden COMMAND ${CMAKE_SOURCE_DIR}/tests/golden_replay.sh)
add_test(NAME zmp_rediscovery COMMAND ${CMAKE_SOURCE_DIR}/tests/test_zmp_scan.sh)
```
(Comment out `games/zork1.c`, `core/mz_registry.c`, `zmp-scan`, and the not-yet-written tests until the tasks that create them; add each back in its task.)

- [ ] **Step 5: Build and run the golden test against the NEW binary**

```bash
cd "$REPO/multizork-server"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target multizorkd
MZ_BIN="$PWD/build/multizorkd" MZ_STORY="$PWD/upstream/zork1.dat" tests/golden_replay.sh
```
Expected: `GOLDEN OK`. The reorganized build behaves identically to upstream.

- [ ] **Step 6: Commit**

```bash
git add multizork-server/CMakeLists.txt multizork-server/core multizork-server/app
git commit -m "split multizorkd into libmultizork core + thin app; golden test green"
```

---

### Task 4: Define the module interface and registry

**Files:**
- Create: `multizork-server/core/mz_game_module.h`, `multizork-server/core/mz_registry.h`, `multizork-server/core/mz_registry.c`, `multizork-server/tests/test_registry.c`

**Interfaces:**
- Consumes: engine types from `mojozork.h`.
- Produces: `mz_game_module`, `mz_patch_site`, and registry fns `mz_registry_add(const mz_game_module*)`, `const mz_game_module *mz_registry_get(const char *id)`, `int mz_registry_count(void)`, `const mz_game_module *mz_registry_at(int i)`.

- [ ] **Step 1: Write the failing registry test**

```c
/* tests/test_registry.c */
#include <assert.h>
#include <string.h>
#include "mz_game_module.h"
#include "mz_registry.h"

static const mz_game_module dummy = { .id = "zork1", .display_name = "Zork I",
    .story_path = "zork1.dat", .z_version = 3, .max_players = 4,
    .player_object_id = 4, .player_global = 111, .visited_attribute = 3 };

int main(void) {
    assert(mz_registry_count() == 0);
    mz_registry_add(&dummy);
    assert(mz_registry_count() == 1);
    assert(mz_registry_get("zork1") == &dummy);
    assert(mz_registry_get("nope") == NULL);
    assert(strcmp(mz_registry_at(0)->display_name, "Zork I") == 0);
    return 0;
}
```

- [ ] **Step 2: Run it to confirm it fails to build**

```bash
cmake --build build --target test_registry
```
Expected: FAIL — `mz_game_module.h` / `mz_registry.h` not found.

- [ ] **Step 3: Write the interface header**

```c
/* core/mz_game_module.h */
#ifndef MZ_GAME_MODULE_H
#define MZ_GAME_MODULE_H
#include <stdint.h>
#include <stdbool.h>
#include "mojozork.h"   /* ZMachineState, uint16, etc. */

typedef struct mz_instance mz_instance;   /* opaque; defined in mz_runtime.c */

typedef struct mz_patch_site {
    uint32_t addr;      /* absolute offset into the story image */
    uint8_t  expected;  /* byte that MUST be there at load (guards wrong files) */
    /* each turn, the byte at addr is rewritten to the acting player's object id */
} mz_patch_site;

typedef struct mz_game_module {
    const char *id;                 /* "zork1" */
    const char *display_name;       /* "Zork I: The Great Underground Empire" */
    const char *story_path;         /* classic .dat OR a ZIL-rebuilt .z3 */
    const char *story_sha256;       /* refuse to load a mismatched file */
    int         z_version;          /* 3 for Zork 1/2/3 */
    int         max_players;

    uint16_t player_object_id;      /* Kind A */
    uint16_t player_global;
    uint8_t  visited_attribute;
    uint16_t extern_mem_objects_base;
    uint16_t multiplayer_prop_datalen;

    const mz_patch_site *patches;   /* Kind B (may be empty for ZIL modules) */
    int                  num_patches;

    void (*on_player_join)(mz_instance*, int player);            /* Kind C hooks */
    void (*on_room_enter)(mz_instance*, int player, uint16_t room);
    bool (*intercept_verb)(mz_instance*, int player, const char *cmd);
} mz_game_module;

#endif
```

- [ ] **Step 4: Write the registry**

```c
/* core/mz_registry.h */
#ifndef MZ_REGISTRY_H
#define MZ_REGISTRY_H
#include "mz_game_module.h"
void mz_registry_add(const mz_game_module *m);
const mz_game_module *mz_registry_get(const char *id);
int mz_registry_count(void);
const mz_game_module *mz_registry_at(int i);
#endif
```
```c
/* core/mz_registry.c */
#include <string.h>
#include "mz_registry.h"
#define MZ_MAX_GAMES 32
static const mz_game_module *g_games[MZ_MAX_GAMES];
static int g_count = 0;
void mz_registry_add(const mz_game_module *m) {
    if (g_count < MZ_MAX_GAMES) g_games[g_count++] = m;
}
const mz_game_module *mz_registry_get(const char *id) {
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_games[i]->id, id) == 0) return g_games[i];
    return NULL;
}
int mz_registry_count(void) { return g_count; }
const mz_game_module *mz_registry_at(int i) {
    return (i >= 0 && i < g_count) ? g_games[i] : NULL;
}
```
Uncomment `core/mz_registry.c` and the `test_registry` target in CMake.

- [ ] **Step 5: Build and run — expect PASS**

```bash
cmake -S . -B build && cmake --build build --target test_registry && ctest --test-dir build -R registry -V
```
Expected: test `registry` passes.

- [ ] **Step 6: Commit**

```bash
git add multizork-server/core/mz_game_module.h multizork-server/core/mz_registry.* \
        multizork-server/tests/test_registry.c multizork-server/CMakeLists.txt
git commit -m "add mz_game_module interface + module registry with unit test"
```

---

### Task 5: Extract Zork 1 Kind A constants into games/zork1.c

**Files:**
- Create: `multizork-server/games/zork1.c`
- Modify: `core/mz_runtime.c` (replace `ZORK1_*` constant uses with reads from the active module)

**Interfaces:**
- Consumes: `mz_game_module`, registry; the runtime's per-instance "active module" pointer.
- Produces: `extern const mz_game_module zork1_module;` and a runtime that sources all Kind A constants from `inst->module` instead of `#define`s.

- [ ] **Step 1: Add the active-module pointer to the instance and a golden re-baseline check**

In `mz_runtime.c`, add `const mz_game_module *module;` to the `Instance` struct (NOT before `zmachine_state`, which must stay first) and set it when an instance is created (temporarily hardcode `&zork1_module`). Re-run golden test — still `GOLDEN OK` (no behavior change yet, pointer unused).

- [ ] **Step 2: Author the Zork 1 module (Kind A only for now)**

```c
/* games/zork1.c */
#include "mz_game_module.h"
#include "mz_registry.h"

/* Kind A constants lifted verbatim from upstream's ZORK1_* defines / magic.
   Confirm each value against upstream/multizorkd.c before trusting it:
     player_object_id       <- ZORK1_PLAYER_OBJID
     player_global          <- the "glob111" index (111)
     visited_attribute      <- "TOUCHBIT is attribute 3"
     extern_mem_objects_base<- ZORK1_EXTERN_MEM_OBJS_BASE
     multiplayer_prop_datalen<- MULTIPLAYER_PROP_DATALEN
   patches[] is added in Task 6. */
const mz_game_module zork1_module = {
    .id = "zork1",
    .display_name = "Zork I: The Great Underground Empire",
    .story_path = "zork1.dat",
    .story_sha256 = NULL,              /* filled once we hash the shipped file */
    .z_version = 3,
    .max_players = 4,
    .player_object_id = 4,
    .player_global = 111,
    .visited_attribute = 3,
    .extern_mem_objects_base = 0,      /* set from ZORK1_EXTERN_MEM_OBJS_BASE */
    .multiplayer_prop_datalen = 0,     /* set from MULTIPLAYER_PROP_DATALEN */
    .patches = NULL, .num_patches = 0,
};
```

- [ ] **Step 3: Fill the two placeholder values from upstream and register the module**

Grep `upstream/multizorkd.c` for `ZORK1_EXTERN_MEM_OBJS_BASE` and `MULTIPLAYER_PROP_DATALEN`, copy their exact numeric values into the struct. In `app/main.c`, call `mz_registry_add(&zork1_module);` at startup (add `extern const mz_game_module zork1_module;`).

- [ ] **Step 4: Replace Kind A `#define` uses in the runtime**

In `mz_runtime.c`, replace each `ZORK1_PLAYER_OBJID` / `glob111`-index `111` / attribute-`3` / `ZORK1_EXTERN_MEM_OBJS_BASE` / `MULTIPLAYER_PROP_DATALEN` use with the corresponding `inst->module->...` field. (These are the `// ZORK 1 SPECIFIC MAGIC` sites — the object-table walk, the `WRITEUI16(glob111, ...)`, the TOUCHBIT set, and the fabricated-object base comparisons.) One site per edit; rebuild between edits.

- [ ] **Step 5: Build and run the golden test**

```bash
cmake --build build --target multizorkd
MZ_BIN="$PWD/build/multizorkd" MZ_STORY="$PWD/upstream/zork1.dat" tests/golden_replay.sh
```
Expected: `GOLDEN OK`. Kind A now flows from the module with identical behavior.

- [ ] **Step 6: Commit**

```bash
git add multizork-server/games/zork1.c multizork-server/core/mz_runtime.c multizork-server/app/main.c
git commit -m "source Zork 1 Kind A constants from games/zork1.c module; golden green"
```

---

### Task 6: Move Kind B byte patches + player-global write into the module

**Files:**
- Modify: `games/zork1.c` (add `patches[]`), `core/mz_runtime.c` (replace inline offset pokes with a module-driven loop)

**Interfaces:**
- Consumes: `mz_patch_site`, `inst->module`.
- Produces: a runtime with **zero** hardcoded Zork-1 offsets; all five live in `zork1_module.patches`.

- [ ] **Step 1: Add the patch table to the Zork 1 module**

```c
/* games/zork1.c — the five ADVENTURER (#04) check sites icculus hand-found.
   expected byte is 0x04 (the adventurer object id) at each addr. */
static const mz_patch_site zork1_patches[] = {
    { 0x6B3F, 0x04 },  /* 6b3d: JE G6f,#04 [TRUE]  6b47 */
    { 0x93E4, 0x04 },  /* 93e2: JE G6f,#04 [FALSE] 93fd */
    { 0x9411, 0x04 },  /* 9410: JE #04,G6f [TRUE]  9424 */
    { 0xD748, 0x04 },  /* d743: JE L02,#bf,#72,#04    */
    { 0xE1AF, 0x04 },  /* e1ad: JE G6f,#04 [FALSE] e1c0 */
};
/* then in zork1_module: .patches = zork1_patches,
   .num_patches = (int)(sizeof zork1_patches / sizeof zork1_patches[0]), */
```

- [ ] **Step 2: Add a load-time guard that expected bytes match**

In the instance-creation path in `mz_runtime.c`, after loading the story, assert each `patches[i].expected == story[patches[i].addr]`; on mismatch, refuse the instance and log (this is the wrong/patched-file guard from the spec). Re-run golden — still `GOLDEN OK` (guard passes on the correct file).

- [ ] **Step 3: Replace the inline patch block with a module-driven loop**

Find the `// ZORK 1 SPECIFIC MAGIC` block that does `GState->story[0x6B3F] = playerobj8; ...` and the `WRITEUI16(glob111, playerobj)` write. Replace with:
```c
/* per-turn: point the PLAYER global and every adventurer-check at THIS player */
uint8_t *glob = (uint8_t *) &globals[inst->module->player_global];
WRITEUI16(glob, playerobj);
for (int i = 0; i < inst->module->num_patches; i++)
    GState->story[inst->module->patches[i].addr] = (uint8_t) playerobj;
```
Delete the now-unused `ZORK1_*` offset literals from the runtime.

- [ ] **Step 4: Grep to prove no Zork-1 offsets remain in core**

```bash
grep -rnE "0x6B3F|0x93E4|0x9411|0xD748|0xE1AF|glob111|ZORK1_" core/ && echo "LEAK" || echo "CLEAN"
```
Expected: `CLEAN` (all moved to `games/zork1.c`).

- [ ] **Step 5: Build and run the golden test**

```bash
cmake --build build --target multizorkd
MZ_BIN="$PWD/build/multizorkd" MZ_STORY="$PWD/upstream/zork1.dat" tests/golden_replay.sh
```
Expected: `GOLDEN OK`. The runtime is now fully game-agnostic for Zork 1.

- [ ] **Step 6: Commit**

```bash
git add multizork-server/games/zork1.c multizork-server/core/mz_runtime.c
git commit -m "move Zork 1 Kind B offset patches into module; runtime now game-agnostic"
```

---

### Task 7: Multi-game text lobby + per-instance game binding

**Files:**
- Modify: `core/mz_server.c` (lobby input handler), `core/mz_db.c`/`mz_db.h` (add `module_id`, `story_sha256` columns), `app/main.c` (register modules)
- Create: `multizork-server/tests/test_lobby.sh`

**Interfaces:**
- Consumes: registry (`mz_registry_count`/`_at`/`_get`), instance creation bound to a `const mz_game_module*`.
- Produces: a connect flow that offers access-code rejoin OR a numbered game picker, and persists which module an instance belongs to.

- [ ] **Step 1: Extend the DB schema and instance row**

Add `module_id TEXT` and `story_sha256 TEXT` to the `instances` table (the row already stores `story_filename` + `dynamic_memory`). Update `db_insert_instance` / the load query to write/read them. On rejoin, refuse if the stored `module_id` is unknown or `story_sha256` changed. Re-run golden — `GOLDEN OK` (single-game path unaffected).

- [ ] **Step 2: Write the lobby integration test (failing)**

`tests/test_lobby.sh` connects one client, expects the banner to list registered games (`[1] Zork I`), sends `1`, and asserts a new instance starts (banner region text appears) and an access code is printed. Run it: expect FAIL (lobby not implemented; server jumps straight into the single game).

- [ ] **Step 3: Implement the lobby input handler**

In `mz_server.c`, replace the "connect → straight into the one game" entry with: print banner → prompt "enter access code, or pick a game:" → list `mz_registry_at(i)->display_name` for `i in [0,count)` → on a code, rejoin; on a number `N`, create/join an instance bound to `mz_registry_at(N-1)`. Reuse the existing access-code and instance-creation code paths; only the front-of-house routing is new.

- [ ] **Step 4: Run the lobby test — expect PASS**

```bash
cmake --build build --target multizorkd
MZ_BIN="$PWD/build/multizorkd" tests/test_lobby.sh
```
Expected: PASS (game list shown, selection starts Zork 1, code printed).

- [ ] **Step 5: Re-run the golden test (via the lobby now)**

Update `drive_two_players.py` if needed so player 1 picks game `1` and player 2 rejoins by code, then:
```bash
MZ_BIN="$PWD/build/multizorkd" MZ_STORY="$PWD/upstream/zork1.dat" tests/golden_replay.sh
```
Expected: `GOLDEN OK` — in-game behavior is unchanged; only the lobby precedes it. (If the transcript legitimately changed because the lobby text is new, re-record with `GOLDEN_RECORD=1` **once**, inspect the diff to confirm only lobby lines were added, and commit the new oracle.)

- [ ] **Step 6: Commit**

```bash
git add multizork-server/core multizork-server/app multizork-server/tests
git commit -m "add multi-game text lobby + per-instance module binding + schema"
```

---

### Task 8: zmp-scan offset finder + Zork 1 rediscovery test

**Files:**
- Create: `multizork-server/tools/zmp_scan.c`, `multizork-server/tests/test_zmp_scan.sh`

**Interfaces:**
- Consumes: a disassembly file (upstream ships `zork1-disassembly.txt`) + seed constants `--player-object N`.
- Produces: `zmp-scan --disasm FILE --player-object N` printing candidate patch offsets (one hex addr per line, plus the disasm line as a comment).

- [ ] **Step 1: Write the failing rediscovery test**

```bash
#!/usr/bin/env bash
# tests/test_zmp_scan.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${ZMP_BIN:-$HERE/../build/zmp-scan}"
DIS="$HERE/../upstream/zork1-disassembly.txt"
out="$("$BIN" --disasm "$DIS" --player-object 4)"
for a in 6b3f 93e4 9411 d748 e1af; do
    echo "$out" | grep -iq "$a" || { echo "MISSING $a"; exit 1; }
done
echo "ZMP REDISCOVERY OK"
```
Run: expect FAIL (no binary).

- [ ] **Step 2: Implement zmp-scan**

```c
/* tools/zmp_scan.c — scan a Z-machine disassembly for comparison opcodes that
   test against the player (adventurer) object, emitting candidate patch sites.
   Consumes an existing disassembly (txd / zork1-disassembly.txt) rather than
   decoding z-code itself. Candidates only — a human confirms keep/skip. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *disasm = NULL; long player = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disasm") && i+1 < argc) disasm = argv[++i];
        else if (!strcmp(argv[i], "--player-object") && i+1 < argc) player = atol(argv[++i]);
    }
    if (!disasm || player < 0) { fprintf(stderr, "usage: zmp-scan --disasm F --player-object N\n"); return 2; }
    FILE *f = fopen(disasm, "r"); if (!f) { perror(disasm); return 1; }
    char line[512], needle[32];
    snprintf(needle, sizeof needle, "#%02lx", player);   /* e.g. "#04" */
    while (fgets(line, sizeof line, f)) {
        /* a candidate: a JE/JG/JZ-family branch whose operands include #<player> */
        if ((strstr(line, "JE") || strstr(line, "JG") || strstr(line, "JZ")) &&
            strstr(line, needle)) {
            /* disassembly lines start with a hex address like "6b3d: ..." */
            unsigned addr; char rest[480];
            if (sscanf(line, "%x: %479[^\n]", &addr, rest) == 2)
                printf("0x%04X  # %s\n", addr, rest);
        }
    }
    fclose(f);
    return 0;
}
```
Add the `zmp-scan` target and the `zmp_rediscovery` test back into CMake.

- [ ] **Step 3: Build and run — tune until it rediscovers all five**

```bash
cmake --build build --target zmp-scan
ZMP_BIN="$PWD/build/zmp-scan" tests/test_zmp_scan.sh
```
Expected: `ZMP REDISCOVERY OK`. If a known offset is missed, inspect the corresponding `zork1-disassembly.txt` line and widen the opcode/operand match (e.g. the `d743: JE L02,#bf,#72,#04` multi-operand form) — do not hardcode the five addresses. The address emitted is the operand-byte site (may differ from the instruction start; align to how upstream's patch offsets relate to the disasm addresses, documented in a comment).

- [ ] **Step 4: Commit**

```bash
git add multizork-server/tools/zmp_scan.c multizork-server/tests/test_zmp_scan.sh multizork-server/CMakeLists.txt
git commit -m "add zmp-scan offset finder; rediscovers Zork 1's 5 known patch sites"
```

---

### Task 9: Deploy scaffolding + "how to add a game" docs

**Files:**
- Create: `multizork-server/deploy/multizorkd.service`, `multizork-server/deploy/README.md`

**Interfaces:**
- Consumes: the built `multizorkd`.
- Produces: a runnable systemd unit and operator docs. No new runtime behavior.

- [ ] **Step 1: Adapt the systemd unit**

Copy `upstream/multizorkd.service` → `deploy/multizorkd.service`; point `ExecStart` at the installed `multizorkd`, set the working dir to where story files + the sqlite DB live, and keep the unprivileged `--uid`/`--gid` drop. Add a comment that port 23 needs `CAP_NET_BIND_SERVICE` or a proxy.

- [ ] **Step 2: Write the operator + contributor docs**

`deploy/README.md` covering: build (`cmake`/`sqlite3` deps, Linux-only), run (`multizorkd --port 23 <storydir>`), the DreamPi `199403` transparent tunnel note for the Saturn client, and a **"How to add a game"** section: (1) drop the `.z3`/`.dat` in the story dir; (2) find Kind A constants with `infodump`; (3) run `zmp-scan` to get candidate Kind B offsets and confirm them; (4) write `games/<id>.c` as an `mz_game_module`; (5) `mz_registry_add` it in `app/main.c`; (6) add a golden transcript. Link the deferred ZIL escape hatch (`ACTOR` convention) from the spec.

- [ ] **Step 3: Verify the unit file parses**

```bash
systemd-analyze verify deploy/multizorkd.service || true   # warns if paths absent; check syntax
```
Expected: no syntax errors (path-not-found warnings are fine pre-install).

- [ ] **Step 4: Run the full test suite once**

```bash
ctest --test-dir build --output-on-failure
```
Expected: `registry`, `golden`, `zmp_rediscovery`, and `lobby` all pass.

- [ ] **Step 5: Commit**

```bash
git add multizork-server/deploy
git commit -m "add systemd unit + operator/contributor docs (how to add a game)"
```

---

## Follow-on plans (out of scope here)

- **Plan 2 — Zork 2 module:** author `games/zork2.c` (Kind A via `infodump`, Kind B via `zmp-scan` on `zork2` disassembly), capture a Zork 2 golden transcript, playtest, document Kind C quirks. Uses only interfaces delivered by this plan.
- **Plan 3 — Zork 3 module:** same pattern for `games/zork3.c`.
- **Deferred — ZIL escape hatch:** only if a game's Kind B proves too costly — rebuild that game's story from ZIL via ZILF and switch its module to the `ACTOR`-global convention (spec §"Story↔server turn/I-O model").

## Self-Review Notes

- **Spec coverage:** libmultizork/module split (Tasks 3–6), `mz_game_module` interface verbatim from spec (Task 4), Kind A (Task 5), Kind B + load-time expected-byte guard (Task 6), text lobby + multi-game hosting + module_id/sha persistence (Task 7), `zmp-scan` + Zork-1 rediscovery gate (Task 8), systemd/deploy/"add a game" docs (Task 9), golden-master parity as the oracle (Task 2, re-run every task). Zork 2/3 and ZIL are correctly deferred to follow-on plans per the spec's phasing.
- **Grounding caveat:** extraction steps (Tasks 3, 5, 6) operate on the vendored `multizorkd.c` the implementer reads locally; they name exact seams/symbols verified from upstream (`GState`, per-state `opcodes` table, the five `0x6B3F…` offsets, `glob111`, `ZORK1_EXTERN_MEM_OBJS_BASE`, `MULTIPLAYER_PROP_DATALEN`, the sqlite `instances` schema) rather than reproducing thousands of lines verbatim — the correct granularity for a refactor, with the golden test proving fidelity.
