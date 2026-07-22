# Typeahead vocabulary pipeline

The Saturn client's typeahead (autocomplete) helps the player enter commands,
weighted so completion nudges them toward the winning move ("easy mode").

## Where the vocabulary comes from

Two layers, built very differently:

1. **Dictionary + grammar — decoded at RUNTIME, on the device, for any v3 game.**
   `saturn/src/typeahead_extract.c` (`build_typeahead_from_story`) decodes the
   loaded story's own parser dictionary and verb-grammar table at load time:
   - the exact words the parser accepts (Z3 truncates to 6 chars; full spellings
     are recovered from object short-names and the abbreviations table, e.g.
     `screwd` -> `screwdriver`),
   - context transitions from the game's grammar: verb -> the prepositions it
     accepts (`put`->`in`/`on`/`under`), and verb/prep -> the object *class* it
     expects (`eat`->food, `open`->openables, `attack…with`->weapons).

   **No table is baked into the build for this layer.** It works for every v3
   story with zero per-game files.

2. **Winning-path "solution" overlay — the only build-time artifact.**
   `gen_solution.py` turns per-game walkthroughs (`solutions/*.WIN`) into ONE
   generated C file, `saturn/src/typeahead_solution.c`, keyed by each story's
   release number + serial. At load, `apply_solution_overlay()` layers it on top
   of the runtime grammar build.

## Regenerating the solution overlay

`solutions/<GAME>.WIN` is a walkthrough: one command per line, `#` for comments
(the first line is usually `# source: <url>`). Directions can be omitted — the
runtime grammar already handles them.

Regenerate the overlay for **all** games in one shot (from `tools/typeahead/`):

```powershell
./gen_all.ps1                       # -> saturn/src/typeahead_solution.c (all games)
cd ../../saturn ; ./compile.bat     # rebuild the client
```

`gen_all.ps1` enumerates `saturn/cd/data/Z3/*.Z3`, skips games with no non-empty
`.WIN`, and calls `gen_solution.py` **once** with every game.

> It must be one file with all games, not one file per game. The overlay is
> dispatched at runtime by story release+serial, so a single
> `apply_solution_overlay` + `SOLUTIONS[]` table is the whole mechanism.
> Emitting one file per game would redefine those symbols and fail to link.

To (re)generate for a single game or a hand-picked set, call `gen_solution.py`
directly with one or more `--game STORY:SCRIPT` pairs:

```sh
python gen_solution.py \
    --game ../../saturn/cd/data/Z3/ZORK1.Z3:solutions/ZORK1.WIN \
    --out  ../../saturn/src/typeahead_solution.c
```

### `.netbin` guarding (`--netbin-keep`)

The `.netbin` build (PlanetWeb loader) embeds only Zork I and must stay under a
400 KB ceiling, so it cannot afford the other ~24 games' overlays. `gen_all.ps1`
passes `--netbin-keep 840726` (Zork I's Z-machine serial, story header `0x12`);
`gen_solution.py` then wraps every game whose serial is **not** kept in
`#ifndef NETBIN … #endif` — both its `gN_words`/`gN_links` arrays and its
`SOLUTIONS[]` row. The CD build (no `-DNETBIN`) still compiles all games; the
netbin compiles only the kept one, dropping the rest from `.rodata`.

`--netbin-keep` is repeatable and matches on the 6-char serial. Omit it (as any
single-game invocation does) and the output is unguarded — every game compiles
in both builds. **Because this guarding is emitted by the generator, never add or
remove the `#ifndef NETBIN` guards by hand — regenerate instead.**

## What the overlay carries

- **Base-weight boosts** from word frequency in the solve.
- **Winning-path transitions** from command word-bigrams. These are marked as
  solution links so they rank *above* on-screen words at runtime, and are
  weighted by first-appearance order so they surface in walkthrough order
  (after `turn pc`: `on` then `off`).
- **Walkthrough-only words** the parser dictionary lacks (passwords, magic words
  like `xyzzy`, `uhlersoth`) are *inserted* into the trie by
  `apply_solution_overlay` so they become suggestible. (Purely numeric tokens
  such as a numeric password are excluded — the trie is a-z only.)

Everything is emitted in the runtime's own word spellings so it resolves against
the on-device trie.

## gen_typeahead.py — inspection / prototyping only

`gen_typeahead.py` decodes a story's dictionary + grammar on the host. It is
useful for **inspecting** a game:

```sh
python gen_typeahead.py --story ../../saturn/cd/data/Z3/ZORK1.Z3 --dump
```

Its `--out` mode emits a baked `build_zork_typeahead()` table, but **that table
is NOT compiled into the client** — the device builds the same data at runtime
(layer 1 above). `build_zork_typeahead()` has no callers; don't generate it for
the build. `gen_solution.py` (via `gen_all.ps1`) is the only generator whose
output ships.

## Notes

- Memory is comfortable across the whole v3 library: the trie uses a compact
  first-child/next-sibling node (~20 bytes on the SH2), so even the largest
  dictionary (Sorcerer, ~1000 words) is ~54 KB, leaving ~350 KB of the 572 KB
  HWRAM heap free after the story image loads.
- Full-word recovery is v3-only for now; v4+ dictionaries still decode (9-char
  words) but keep their truncated forms.
