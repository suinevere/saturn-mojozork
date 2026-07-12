# Typeahead vocabulary pipeline

Generates the Saturn client's typeahead dictionary straight from a Z-machine v3
story file, weighted by a winning walkthrough so completion nudges the player
toward the winning move ("easy mode").

## What it does

1. **Words** — decodes the story's own parser dictionary (the exact set of words
   the game accepts). Z3 stores each word truncated to 6 characters (v4+ store
   9); that's all the parser ever compares, so these forms are authoritative.
   To show full spellings anyway, truncated entries are matched against clean
   words harvested from the story's own **object short-names** and
   **abbreviations table** (e.g. `screwd` -> `screwdriver`, `lanter` ->
   `lantern`). Pass `--no-fullwords` to keep the raw 6-char forms.
2. **Grammar (no walkthrough needed)** — decodes the story's part-of-speech flags
   and verb grammar table to build context transitions automatically: verb→the
   prepositions it accepts (`put`→`in`/`on`/`under`, `kill`→`with`, `tie`→`to`),
   and verb/preposition→the *object class* it expects, resolved against object
   attributes (`eat`→food, `open`→openables, `attack…with`→weapons, `climb`→
   tree/ladder/stairs). This is the game designers' own semantic model.
3. **Weights + combinations (refinement)** — a walkthrough (one command per line)
   sets base completion weights by word frequency and boosts the specific
   winning-path pairings on top of the grammar baseline
   (`kill`→`troll`, `dig`→`sand`).
4. **Codegen** — emits `saturn/src/typeahead_zork.c`, a table-driven
   `build_zork_typeahead()` compiled into the client.

`saturn/src/typeahead_zork.c` is **generated** — do not hand-edit it; change the
inputs and regenerate.

## Usage

```sh
# Inspect the raw dictionary of any Z3 game:
python gen_typeahead.py --story ../../saturn/cd/data/Z3/ZORK1.Z3 --dump

# Regenerate the compiled-in vocabulary (Zork I + walkthrough):
python gen_typeahead.py \
    --story  ../../saturn/cd/data/Z3/ZORK1.Z3 \
    --script ZORK1.WIN \
    --out    ../../saturn/src/typeahead_zork.c

# Then rebuild the client:
cd ../../saturn && ./compile.bat
```

Omit `--script` to get the full dictionary with flat weights and no transitions.

## Runtime vs. build-time

The device now decodes the loaded game's dictionary + grammar **at runtime**
(`saturn/src/typeahead_extract.c`), so every v3 game gets the grammar layer with
no baked-in table. `gen_typeahead.py` remains useful for inspecting/prototyping
output on the host (`--dump`, `--out`), but its emitted table is no longer
compiled in.

The winning-path "easy mode" ships as a compiled **solution overlay** applied on
top of the runtime grammar layer.

## Solution overlay (easy mode)

A *solution file* is the winning walkthrough: plain text, **one command per
line**, `#` for comments (e.g. `zork1_walkthrough.txt`). `gen_solution.py` turns
one or more of these into `saturn/src/typeahead_solution.c`:

```sh
python gen_solution.py \
    --game ../../saturn/cd/data/Z3/ZORK1.Z3:ZORK1.WIN \
    --out  ../../saturn/src/typeahead_solution.c
# add more with repeated --game STORY:SCRIPT
```

- Each overlay is keyed by the story's **release number + serial** (Z-machine
  header 0x02 / 0x12), so it only applies to the exact game it was built for;
  other games fall back to the grammar layer.
- It carries **base-weight boosts** (word frequency in the solve) and
  **transition boosts** (command word-bigrams), emitted in the runtime's own
  spelling so they resolve against the on-device trie.
- At load, `apply_solution_overlay()` raises those base weights and adds the
  winning-path transitions on top of the grammar build.

## Tuning "easy mode"

`zork1_walkthrough.txt` is the winning command script (one command per line;
`#` comments allowed). Replace or extend it with your own optimal solve to
sharpen the hints — every `verb object` and `verb obj prep obj` line the solve
uses becomes a stronger suggestion.

## Notes

- Memory is comfortable across the whole v3 library. The trie uses a compact
  first-child/next-sibling node (~20 bytes on the SH2), so even the largest
  dictionary here (Sorcerer, ~1000 words) is ~54 KB, leaving ~350 KB of the
  572 KB HWRAM heap free after the story image loads.
- Works on any v3 (`.z3`) story, not just Zork — point `--story` at any of the
  games under `saturn/cd/data/Z3/`. Full-word recovery is v3-only for now; v4+
  dictionaries still decode (9-char words) but keep their truncated forms.
