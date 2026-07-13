# MultiZork for Any Game — Shared-World Multiplayer as a C Library — Design Spec

**Date:** 2026-07-08
**Status:** Draft (design), pending spec review
**Relationship to prior work:** Server-side companion to the Saturn dumb-terminal
client ([2026-07-08-mojozork-saturn-multizork-telnet-design.md](2026-07-08-mojozork-saturn-multizork-telnet-design.md)).
That project connects *to* a multizork server; this project generalizes the
**server** so one host can serve many shared-world games.

## Goal

Generalize icculus's `multizorkd` (shared-world multiplayer Zork 1) so a **single
hosted server** can run **multiple** shared-world Infocom games — starting with
**Zork 1, then Zork 2, then Zork 3** — with new games added as self-contained
plug-in modules. The reusable multiplayer machinery is extracted into a C library
(`libmultizork`); each game is a small C module against a fixed interface.

**Shared-world** means several telnet players inhabit **one** game world (shared
map, independent inventory/movement, seeing each other) — the multizork model, not
independent single-player sessions.

## Background: why this is hard (the three kinds of surgery)

icculus achieved multiplayer Zork 1 by hand-surgery welded into `multizorkd.c`.
Reviewing that source, the game-specific work falls into three kinds. This
taxonomy drives the whole design.

**Kind A — named magic constants.** The player ("adventurer") object id, the
global that points at the current player, the "visited" attribute:

```c
playerptr += 9 * (ZORK1_PLAYER_OBJID-1);   // ZORK 1 SPECIFIC MAGIC
uint8 *glob111 = (uint8 *) &globals[111];  // PLAYER global -> this player's object
*roomobjptr |= (0x80 >> 3);                // in zork1, TOUCHBIT is attribute 3
```

**Kind B — hand-found story-file byte patches.** The *compiled* game has
instructions that test "is this object the adventurer (#4)?". To make each player
be "the adventurer" from their own view, icculus overwrites those bytes at
absolute offsets he found by disassembling this exact story file:

```c
// ZORK 1 SPECIFIC MAGIC: hardcoded checks for the ADVENTURER object (4).
//  There may be others I've missed.
GState->story[0x6B3F] = playerobj8;  // 6b3d:  JE  G6f,#04 [TRUE]  6b47
GState->story[0x93E4] = playerobj8;  // 93e2:  JE  G6f,#04 [FALSE] 93fd
GState->story[0x9411] = playerobj8;  // 9410:  JE  #04,G6f [TRUE]  9424
GState->story[0xD748] = playerobj8;  // d743:  JE  L02,#bf,#72,#04
GState->story[0xE1AF] = playerobj8;  // e1ad:  JE  G6f,#04 [FALSE] e1c0
```

His own comment — *"There may be others I've missed"* — is the tell: producing
this list is skilled reverse-engineering of one specific binary.

**Kind C — genuinely new multiplayer behavior.** Players are not real objects in
the story file, so the interpreter *fabricates* them (fake objects above a base
id, property data in per-player C structs, fake property addresses mapped to the
top of the 16-bit address space), and every object/property opcode is intercepted
to special-case them. Plus game-specific single-actor assumptions (e.g. Zork 1's
thief) that must be handled case by case.

### Could the surgery be pure data (JSON/YAML)?

Partly. Kind A is trivially declarative. Kind B *can be stored* as an offset table
but is *produced* by disassembly + human judgment (which `JE …,#adventurer` checks
really mean "is this the player"). Kind C is authored logic, not a value. A pure
data manifest therefore cannot express Kind C — which is why this design factors
game-specifics into **C code modules** (which can hold Kind C hooks), not YAML.

## Chosen approach: `libmultizork` core + per-game C modules

One reusable C library owns everything game-agnostic; each game is a plug-in module
implementing a fixed interface. A module's story file may be either the original
`.dat` patched at load (the classic multizork route) or — as a documented, deferred
escape hatch — a ZIL-recompiled `.z3` (see "ZIL escape hatch"). The library is
indifferent to which.

```
libmultizork (core runtime)
  |- zork1.c   (.dat + offset patches)   <- golden master vs live multizork
  |- zork2.c   (.dat + offset patches)
  |- zork3.c   (.dat + offset patches)
        \-- any module MAY instead point story_path at a ZIL-rebuilt .z3
```

*Rejected alternatives:*
- **Pure YAML manifest (data-only).** Cannot express Kind C game-specific behavior;
  would still need C escape hatches, so the library/module split is cleaner.
- **Drop C, go ZIL-only.** Dissolves Kind B but forfeits the proven mojozork idiom,
  the Zork-1 golden-master validation, a no-toolchain start, and the reusable C
  library artifact — all for a benefit we retain optionally anyway.
- **Offline byte-patcher (`.z3` -> `.z3'`) as the whole solution.** Same RE as the
  classic route, and compiled Z-code cannot have objects/routines *inserted*
  (packed absolute addresses), so player fabrication still happens at runtime.
  Buys almost nothing on its own.

## The C module interface

```c
typedef struct mz_patch_site {
    uint32 addr;         // absolute offset into the story image
    uint8  expected;     // byte that MUST be there at load (guards wrong/patched files)
    // each turn, the byte at addr is rewritten to the acting player's object id
} mz_patch_site;

typedef struct mz_game_module {
    const char *id;                    // "zork1"
    const char *display_name;          // "Zork I: The Great Underground Empire"
    const char *story_path;            // classic .dat OR a ZIL-rebuilt .z3
    const char *story_sha256;          // refuse to load a mismatched file
    int         z_version;             // 3 for Zork 1/2/3
    int         max_players;

    /* Kind A -- named constants */
    uint16 player_object_id;
    uint16 player_global;
    uint8  visited_attribute;
    uint16 extern_mem_objects_base;
    uint16 multiplayer_prop_datalen;

    /* Kind B -- adventurer-check patch sites (may be empty for ZIL modules) */
    const mz_patch_site *patches;
    int                  num_patches;

    /* Kind C -- optional behavior hooks (NULL => library default) */
    void (*on_player_join)(mz_instance*, int player);
    void (*on_room_enter)(mz_instance*, int player, uint16 room);
    bool (*intercept_verb)(mz_instance*, int player, const char *cmd);
} mz_game_module;
```

Adding a game = writing one `mz_game_module` (and, for a classic module, finding
its Kind B offsets — aided by `zmp-scan`). This struct *is* the generalization,
expressed as a code interface so Kind C is reachable.

## What the library owns (game-agnostic)

All of this exists inside `multizorkd.c` today, tangled with Zork-1 specifics; the
work is to extract it behind clean seams:

- **Z-machine execution** (the mojozork v3 engine) plus the opcode-interception
  framework used for fabricated player objects and property routing.
- **Player fabrication**: fake player objects above `extern_mem_objects_base`,
  per-player property data, object-id remapping so each player sees themselves as
  the adventurer.
- **Turn scheduler + I/O routing**: pick the acting player, apply Kind A/B per-turn
  patches, run until the story reads input, capture output produced during the
  turn, route it to that player (broadcast for room-wide events).
- **Telnet server**: connection multiplexing, poll loop, local-echo-agnostic byte
  stream (no IAC negotiation — matches the verified server behavior the Saturn
  client relies on).
- **Text lobby / game registry** (below).
- **Persistence + access codes** (below).

## Story <-> server turn/I-O model

**Classic modules (server-driven — the existing multizork mechanism).** The server
owns the loop: before each turn it sets the PLAYER global (Kind A) to the acting
player's fabricated object and applies the `patches[]` sites to that object id,
feeds that player's queued input line, runs the z-machine until it next reads
input, and captures/routes the output. Room-wide events are broadcast by the
library's output-routing layer. This is faithful to `multizorkd` and needs no new
in-story protocol.

**ZIL modules (escape hatch — `ACTOR`-global convention).** A ZIL-rebuilt story can
cooperate: a reserved `ACTOR` global names the acting player and the story's main
loop uses it instead of a fixed adventurer; a recognized `TELL-ROOM` routine marks
broadcasts. Because players become *real* objects in a ZIL rebuild, presence
rendering ("you see Bob here") comes for free from the game's own room description
code, and `patches[]` shrinks toward empty. This path is **deferred** and only
adopted for a specific game if its classic offset-hunting proves too costly.

## Lobby & multi-game hosting

One `multizorkd` process, one telnet port (**:23**). On connect:

```
Welcome to MultiZork.
[code]  enter an access code to rejoin a game in progress
[1] Zork I    [2] Zork II    [3] Zork III
> 2
(start a new instance of Zork II, or join an open one)
```

Pure text, so the Saturn dumb terminal and any `nc`/telnet client work unchanged.
The lobby lists the registered `mz_game_module`s; the selection routes the
connection to that module's instance. **A single hosted server handles any number
of games simultaneously** — the per-game cost is the one-time module authoring, not
the hosting.

## Persistence & access codes

Reuse multizork's model: SQLite stores each instance's dynamic memory blob plus its
game identity, and prints an **access code** the player re-enters to rejoin (no
client-side save). The existing schema already carries a per-instance
`story_filename` and a `dynamic_memory` blob, so it is structurally multi-game
today; we add the **module id + story sha** to each instance row so a rejoin binds
to the correct game and rejects a story-file change.

## `zmp-scan` — the Kind B offset finder (module-author aid)

A standalone tool that helps author a classic module's `patches[]`. Input: a story
file + the two hand-seeded Kind A constants (`player_object_id`, `player_global`).
It scans the code for comparison opcodes (`JE`/`JG`/…) using the player object as
an immediate operand, and references to the player global, emitting each as a
**candidate** with disassembly context for a human to confirm (keep/skip). It never
auto-accepts a site. To stay cheap and reliable it consumes an existing
disassembler's output (`txd`/ztools) rather than reimplementing a v3 decoder.

**Its acceptance test is the Zork 1 golden master:** run on `zork1.dat`, it must
rediscover icculus's five known offsets (`0x6B3F, 0x93E4, 0x9411, 0xD748, 0xE1AF`).
If it cannot reproduce the known-good answer, it is not trusted on Zork 2/3.

## Hosting / deployment

- **One `multizorkd` on telnet :23**, run under the existing `multizorkd.service`
  systemd unit; SQLite for persistence.
- **Add your own game**: drop in the story file, write the module, register it, rebuild.
- **Retro-hardware reach**: the Saturn client's `199403` transparent DreamPi tunnel
  entry can point at this server; the text lobby needs no client changes.
- **Answer to "could a single hosted server handle any game?"** Yes for hosting many
  games at once; the only per-game cost is the one-time conversion (module + offset
  finding + playtest). The server was never the blocker.

## Kind C boundary (scope of "multiplayer-correct")

The library's fabrication/routing plumbing (keyed off Kind A/B) makes a game
**playable** as a shared world. Game-specific single-actor assumptions (each game's
thief/NPC/puzzle logic) are handled via the module's Kind C hooks **as needed**, and
remaining rough edges are **documented, not perfected**, in MVP — the same bargain
multizork itself makes. Refinement is per-game and incremental.

## Phasing (risk front-loaded)

1. **Library extraction + Zork 1 module (golden master).** Refactor `multizorkd.c`
   into `libmultizork` + a `zork1` module; behavior must match the live
   `multizork.icculus.org` on a scripted multiplayer transcript. Defines the module
   API by extracting from working code. *No new toolchain.*
2. **`zmp-scan`**, validated by rediscovering Zork 1's five known offsets.
3. **Zork 2 module** (classic): author constants + `patches[]` via `zmp-scan`;
   multiplayer playtest; document Kind C quirks.
4. **Zork 3 module** (classic), same pattern.
5. **Multi-game hosting polish + deploy docs.** *(Escape hatch, only if a game's
   offsets prove too costly: rebuild that game's story from ZIL via ZILF and switch
   its module to the `ACTOR` convention. Not on the critical path.)*

## Testing strategy

- **Golden master (Zork 1):** library-based `zork1` module vs live multizork /
  icculus's binary on a scripted 2-player transcript — must match.
- **`zmp-scan` rediscovery:** must reproduce Zork 1's five known offsets.
- **Module load guards:** `story_sha256` and every `expected` byte must match the
  loaded file, or that game is refused (others stay up).
- **Per-game smoke transcripts:** scripted 2-player sessions for Zork 2/3.
- **Manual playtest:** real multiplayer sessions per game.

## Error handling

| Condition | Behavior |
|-----------|----------|
| Story sha / `expected` byte mismatch | Refuse that module; log; other games keep running. |
| Unknown lobby selection | Re-prompt. |
| Access code not found | Message; offer to start a new game. |
| Mid-session disconnect | Persist; player rejoins via access code. |
| `zmp-scan` unconfirmed site | Emitted as candidate only; never auto-patched. |

## Risks & assumptions

- **Kind B RE per classic game (contained).** `zmp-scan` reduces but does not
  eliminate it; the ZIL escape hatch is the pressure valve if a game is nasty.
- **Extraction fidelity (top risk).** Untangling `multizorkd.c` must preserve exact
  Zork 1 behavior — the golden-master test is the gate.
- **Non-Zork-idiom games** (different authors/compilers) may not fit the fabrication
  assumptions; out of scope beyond Zork 1/2/3 for now.
- **ZIL escape hatch dependencies** (only if invoked): ZILF toolchain, ZIL source
  availability, murky Infocom-source license, non-identical rebuilt output.

## Deliverables checklist

- [ ] `libmultizork` extracted from `multizorkd.c` (clean core/module seam).
- [ ] `mz_game_module` interface + registry + text lobby.
- [ ] `zork1` module; golden-master parity test passing.
- [ ] `zmp-scan` finder; Zork 1 rediscovery test passing.
- [ ] `zork2` module + smoke transcript + quirks doc.
- [ ] `zork3` module + smoke transcript + quirks doc.
- [ ] Multi-game hosting + `multizorkd.service` + deploy/how-to-add-a-game docs.
- [ ] (Deferred) ZIL escape-hatch note: `ACTOR` convention + ZILF rebuild path.
