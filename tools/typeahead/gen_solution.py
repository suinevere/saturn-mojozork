#!/usr/bin/env python3
"""Generate a compiled typeahead "solution" overlay from winning walkthroughs.

The runtime builds the grammar layer (verb->preposition, verb->object class) for
whatever game is loaded. This overlay adds the "easy mode" refinement on top:
base completion weights from word frequency, and context transitions from command
word-bigrams, biased toward the winning path.

Each game is identified by its Z-machine *release number* + *serial* (from the
story header at 0x02 / 0x12), so an overlay only applies to the exact story it was
built for. Words are emitted in the runtime's own spelling (dictionary full-word
recovery), so they resolve against the trie the device builds.

Input (per game): a walkthrough text file -- one command per line, '#' comments.

Usage:
  python gen_solution.py \
      --game ../../saturn/cd/data/Z3/ZORK1.Z3:ZORK1.WIN \
      --out  ../../saturn/src/typeahead_solution.c
"""

import argparse
import re
import sys
from gen_typeahead import (extract_dictionary, harvest_full_words, full_word_map,
                           parse_script, _w16)


def runtime_form_fn(data):
    """Return f(token) -> the spelling the on-device extractor will have in vocab."""
    dict_words = extract_dictionary(data)
    dictset = {w for w in dict_words if re.fullmatch(r"[a-z]+", w)}
    fullmap = full_word_map(dict_words, harvest_full_words(data))

    # The extractor expands truncated diagonals to full names; mirror that so
    # walkthrough words like "northeast" resolve against the on-device trie.
    diag = {"northe": "northeast", "northw": "northwest",
            "southe": "southeast", "southw": "southwest"}

    def form(t):
        k = t[:6]
        if k in fullmap:
            r = fullmap[k]          # runtime recovered the full spelling
        elif k in dictset:
            r = k                   # runtime keeps the (truncated) dict form
        else:
            r = t
        return diag.get(r, r)

    return form


def build_game(story_path, script_path):
    data = open(story_path, "rb").read()
    release = _w16(data, 0x02)
    serial = bytes(data[0x12:0x18]).decode("latin1")
    form = runtime_form_fn(data)
    freq, bigrams = parse_script(script_path)

    words = {}
    for t, c in freq.items():
        f = form(t)
        words[f] = max(words.get(f, 0), min(100, 40 + min(c, 60)))
    links = {}
    for (a, b), c in bigrams.items():
        fa, fb = form(a), form(b)
        if fa == fb:
            continue
        links[(fa, fb)] = max(links.get((fa, fb), 0), min(96, 50 + 8 * c))
    return release, serial, words, links


C_TOP = """// GENERATED FILE -- do not edit by hand.
// Produced by tools/typeahead/gen_solution.py.
//
// Per-game "solution" overlay: base-weight and transition boosts derived from a
// winning walkthrough, applied on top of the runtime grammar layer. Keyed by the
// story's release number + serial, so it only touches the game it was built for.

#include "typeahead_solution.h"
#include <string.h>

typedef struct { const char* w; short wt; } SolWord;
typedef struct { const char* a; const char* b; short wt; } SolLink;
typedef struct {
    unsigned short release; const char* serial;
    const SolWord* words; int nwords;
    const SolLink* links; int nlinks;
} Solution;

"""

C_APPLY = """
void apply_solution_overlay(TrieNode* root, const unsigned char* story, unsigned int len) {
    if (len < 0x1a) return;
    unsigned short release = (unsigned short)((story[2] << 8) | story[3]);
    const char* serial = (const char*) (story + 0x12);
    for (int i = 0; i < (int)(sizeof(SOLUTIONS) / sizeof(SOLUTIONS[0])); i++) {
        if (SOLUTIONS[i].release != release) continue;
        if (memcmp(SOLUTIONS[i].serial, serial, 6) != 0) continue;
        for (int j = 0; j < SOLUTIONS[i].nwords; j++) {
            DictionaryWord* w = find_exact_word(root, SOLUTIONS[i].words[j].w);
            if (w && SOLUTIONS[i].words[j].wt > w->base_weight)
                w->base_weight = SOLUTIONS[i].words[j].wt;
        }
        for (int j = 0; j < SOLUTIONS[i].nlinks; j++) {
            DictionaryWord* a = find_exact_word(root, SOLUTIONS[i].links[j].a);
            DictionaryWord* b = find_exact_word(root, SOLUTIONS[i].links[j].b);
            if (a && b) add_next_word(a, b, SOLUTIONS[i].links[j].wt);
        }
        return;
    }
}
"""


def emit(games, out_path):
    def cstr(s):
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

    parts = [C_TOP]
    entries = []
    for gi, (release, serial, words, links) in enumerate(games):
        wl = ", ".join(f'{{{cstr(w)},{wt}}}' for w, wt in sorted(words.items()))
        ll = ", ".join(f'{{{cstr(a)},{cstr(b)},{wt}}}'
                       for (a, b), wt in sorted(links.items()))
        parts.append(f"static const SolWord g{gi}_words[] = {{ {wl} }};\n")
        parts.append(f"static const SolLink g{gi}_links[] = {{ {ll} }};\n")
        entries.append(f"    {{ {release}, {cstr(serial)}, "
                       f"g{gi}_words, {len(words)}, g{gi}_links, {len(links)} }},")
    parts.append("\nstatic const Solution SOLUTIONS[] = {\n" + "\n".join(entries) + "\n};\n")
    parts.append(C_APPLY)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(parts))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", action="append", required=True, metavar="STORY:SCRIPT",
                    help="a story file and its walkthrough, colon-separated (repeatable)")
    ap.add_argument("--out", required=True, help="output C file")
    args = ap.parse_args()

    games = []
    for spec in args.game:
        story, script = spec.rsplit(":", 1)
        release, serial, words, links = build_game(story, script)
        print(f"{story}: release {release} serial {serial!r} -> "
              f"{len(words)} word boosts, {len(links)} transitions")
        games.append((release, serial, words, links))
    emit(games, args.out)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
