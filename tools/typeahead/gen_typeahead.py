#!/usr/bin/env python3
"""Generate the Saturn typeahead vocabulary from a Z-machine v3 story file.

Pipeline:
  1. Decode the story's dictionary table -> the exact set of words the game's
     parser accepts (Z3 truncates these to 6 characters, which is all the
     parser ever compares).
  2. Optionally read a "winning" command script (a walkthrough, one command per
     line). Word frequency becomes the base completion weight and consecutive
     words within a command become weighted context transitions -- so the
     typeahead nudges the player toward the winning move ("easy mode").
  3. Emit a compact, table-driven C file implementing build_zork_typeahead().

Usage:
  python gen_typeahead.py --story ZORK1.Z3 [--script walkthrough.txt] \
                          --out ../../saturn/src/typeahead_zork.c
  python gen_typeahead.py --story ZORK1.Z3 --dump      # just print the words
"""

import argparse
import re
import sys
from collections import Counter

# --- Z-machine v3 text decoding ------------------------------------------------

A0 = 'abcdefghijklmnopqrstuvwxyz'
A1 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'
# A2 for z-chars 6..31 (index 0 == z-char 6). z-char 6 escapes to a 10-bit code.
A2 = ['\0', '\n', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
      '.', ',', '!', '?', '_', '#', "'", '"', '/', '\\', '-', ':', '(', ')']


def _w16(b, a):
    return (b[a] << 8) | b[a + 1]


def _decode(zchars):
    out, alpha, i = [], 0, 0
    while i < len(zchars):
        c = zchars[i]
        if c == 0:
            out.append(' '); alpha = 0; i += 1; continue
        if c in (1, 2, 3):          # abbreviation reference (never in dict words)
            i += 2; alpha = 0; continue
        if c == 4:
            alpha = 1; i += 1; continue
        if c == 5:
            alpha = 2; i += 1; continue
        if alpha == 0:
            out.append(A0[c - 6])
        elif alpha == 1:
            out.append(A1[c - 6])
        else:
            if c == 6:              # 10-bit ZSCII escape
                if i + 2 < len(zchars):
                    zs = (zchars[i + 1] << 5) | zchars[i + 2]
                    if 32 <= zs < 127:
                        out.append(chr(zs))
                    i += 3; alpha = 0; continue
                i += 1; continue
            out.append(A2[c - 6])
        alpha = 0; i += 1
    return ''.join(out)


def extract_dictionary(data):
    """Return the list of dictionary words decoded from a v3 story image."""
    if data[0] != 3:
        print(f"warning: story version is {data[0]}, expected 3", file=sys.stderr)
    p = _w16(data, 0x08)                 # dictionary address (header offset 0x08)
    n_sep = data[p]; p += 1
    p += n_sep                           # skip word separators
    entry_len = data[p]; p += 1
    count = _w16(data, p); p += 2
    words = []
    for k in range(count):
        off = p + k * entry_len
        w1, w2 = _w16(data, off), _w16(data, off + 2)
        z = [(w1 >> 10) & 0x1f, (w1 >> 5) & 0x1f, w1 & 0x1f,
             (w2 >> 10) & 0x1f, (w2 >> 5) & 0x1f, w2 & 0x1f]
        words.append(_decode(z).rstrip())
    return words


# --- Walkthrough parsing -------------------------------------------------------

TOKEN = re.compile(r"[a-z]+")


def parse_script(path):
    """Return (word_freq, bigram_freq) from a walkthrough (one command/line)."""
    freq, bigrams = Counter(), Counter()
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.strip().lower()
            if not line or line.startswith('#'):
                continue
            toks = TOKEN.findall(line)
            for t in toks:
                freq[t] += 1
            for a, b in zip(toks, toks[1:]):
                bigrams[(a, b)] += 1
    return freq, bigrams


# --- Weight model --------------------------------------------------------------

def build_model(dict_words, freq, bigrams):
    """Merge dictionary + walkthrough into {word: base_weight} and transitions."""
    # Keep dictionary words that are plain letters (the trie only stores a-z).
    base = {}
    for w in dict_words:
        if re.fullmatch(r"[a-z]+", w):
            base[w] = 30

    # A walkthrough word is the full spelling of a (truncated) dictionary word.
    # Prefer the full spelling for display; the parser only reads its first 6.
    for full in list(freq):
        key = full[:6]
        if key in base and key != full:
            base.pop(key, None)
        base[full] = base.get(full, 30)

    # Frequency in the winning path drives base completion weight.
    for t, c in freq.items():
        base[t] = min(100, 40 + min(c, 60))

    # Any bigram endpoint must exist as a word.
    for a, b in bigrams:
        base.setdefault(a, 42)
        base.setdefault(b, 42)

    transitions = []
    for (a, b), c in bigrams.items():
        transitions.append((a, b, min(96, 50 + 8 * c)))

    return base, transitions


# --- C emission ----------------------------------------------------------------

C_HEADER = """// GENERATED FILE -- do not edit by hand.
// Produced by tools/typeahead/gen_typeahead.py from:
//   story:      {story}
//   walkthrough:{script}
//   words: {nwords}   transitions: {nlinks}
//
// Vocabulary is the story's own parser dictionary; base weights and context
// transitions come from a winning command script (frequency and word bigrams),
// so completion favours the winning move.

#include "typeahead_zork.h"

typedef struct {{ const char* t; short wt; }} ZWord;
typedef struct {{ short a, b, wt; }} ZLink;

static const ZWord ZWORDS[] = {{
{words}
}};

static const ZLink ZLINKS[] = {{
{links}
}};

void build_zork_typeahead(TrieNode* root) {{
    const int nwords = (int)(sizeof(ZWORDS) / sizeof(ZWORDS[0]));
    const int nlinks = (int)(sizeof(ZLINKS) / sizeof(ZLINKS[0]));
    DictionaryWord** w = (DictionaryWord**)TYPEAHEAD_MALLOC(nwords * sizeof(DictionaryWord*));
    for (int i = 0; i < nwords; i++) {{
        w[i] = create_word(ZWORDS[i].t, TYPE_UNKNOWN, ZWORDS[i].wt);
        insert_trie(root, w[i]);
    }}
    for (int i = 0; i < nlinks; i++) {{
        add_next_word(w[ZLINKS[i].a], w[ZLINKS[i].b], ZLINKS[i].wt);
    }}
    TYPEAHEAD_FREE(w);
}}
"""


def emit_c(base, transitions, out_path, story, script):
    words = sorted(base)                    # alphabetical -> stable indices
    idx = {t: i for i, t in enumerate(words)}

    word_lines = []
    row = []
    for t in words:
        row.append(f'{{"{t}",{base[t]}}}')
        if len(row) == 6:
            word_lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        word_lines.append("    " + ", ".join(row) + ",")

    link_lines = []
    row = []
    # Deterministic order; drop links whose endpoints somehow fell out.
    for a, b, wt in sorted(transitions):
        if a in idx and b in idx:
            row.append(f'{{{idx[a]},{idx[b]},{wt}}}')
            if len(row) == 8:
                link_lines.append("    " + ", ".join(row) + ",")
                row = []
    if row:
        link_lines.append("    " + ", ".join(row) + ",")

    text = C_HEADER.format(
        story=story, script=script or "(none)",
        nwords=len(words),
        nlinks=sum(1 for a, b, _ in transitions if a in idx and b in idx),
        words="\n".join(word_lines),
        links="\n".join(link_lines) if link_lines else "    {0,0,0}",
    )
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


# --- CLI -----------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--story", required=True, help="path to a Z-machine v3 (.z3) story file")
    ap.add_argument("--script", help="winning command walkthrough (one command per line)")
    ap.add_argument("--out", help="output C file (default: print a summary only)")
    ap.add_argument("--dump", action="store_true", help="print the decoded dictionary and exit")
    args = ap.parse_args()

    data = open(args.story, "rb").read()
    dict_words = extract_dictionary(data)

    if args.dump:
        for w in dict_words:
            print(w)
        print(f"\n{len(dict_words)} dictionary entries", file=sys.stderr)
        return

    freq, bigrams = (Counter(), Counter())
    if args.script:
        freq, bigrams = parse_script(args.script)

    base, transitions = build_model(dict_words, freq, bigrams)
    print(f"dictionary words: {len(dict_words)}")
    print(f"script words: {len(freq)}   bigrams: {len(bigrams)}")
    print(f"vocabulary: {len(base)}   transitions: {len(transitions)}")

    if args.out:
        emit_c(base, transitions, args.out, args.story, args.script)
        print(f"wrote {args.out}")
    else:
        print("(no --out; nothing written)")


if __name__ == "__main__":
    main()
