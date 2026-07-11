# Typeahead vocabulary pipeline

Generates the Saturn client's typeahead dictionary straight from a Z-machine v3
story file, weighted by a winning walkthrough so completion nudges the player
toward the winning move ("easy mode").

## What it does

1. **Words** — decodes the story's own parser dictionary (the exact set of words
   the game accepts). Z3 stores each word truncated to 6 characters; that's all
   the parser ever compares, so these forms are authoritative.
2. **Weights + combinations** — reads a walkthrough (one command per line). Word
   frequency becomes the base completion weight; consecutive words within a
   command become weighted context transitions (`kill`→`troll`→`with`→`sword`).
3. **Codegen** — emits `saturn/src/typeahead_zork.c`, a table-driven
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
    --script zork1_walkthrough.txt \
    --out    ../../saturn/src/typeahead_zork.c

# Then rebuild the client:
cd ../../saturn && ./compile.bat
```

Omit `--script` to get the full dictionary with flat weights and no transitions.

## Tuning "easy mode"

`zork1_walkthrough.txt` is the winning command script (one command per line;
`#` comments allowed). Replace or extend it with your own optimal solve to
sharpen the hints — every `verb object` and `verb obj prep obj` line the solve
uses becomes a stronger suggestion.

## Notes

- Fits comfortably in HWRAM for Zork I (~221 KB trie, ~240 KB headroom). Games
  with much larger dictionaries may want a more compact trie node (the current
  `TrieNode` holds 26 child pointers); ask if you want that optimization.
- Works on any v3 (`.z3`) story, not just Zork — point `--story` at any of the
  games under `saturn/cd/data/Z3/`.
