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
    """Return the list of dictionary words decoded from a story image.

    The encoded word is 4 bytes (6 z-chars) in v1-v3 and 6 bytes (9 z-chars) in
    v4+; the parser only ever compares those characters, so the decoded forms
    are authoritative even though longer real words get truncated here.
    """
    version = data[0]
    word_bytes = 4 if version < 4 else 6
    p = _w16(data, 0x08)                 # dictionary address (header offset 0x08)
    n_sep = data[p]; p += 1
    p += n_sep                           # skip word separators
    entry_len = data[p]; p += 1
    count = _w16(data, p); p += 2
    words = []
    for k in range(count):
        off = p + k * entry_len
        z = []
        for b in range(0, word_bytes, 2):
            w = _w16(data, off + b)
            z += [(w >> 10) & 0x1f, (w >> 5) & 0x1f, w & 0x1f]
        words.append(_decode(z).rstrip())
    return words


# --- Full-word recovery from the story's own text ------------------------------
#
# The dictionary truncates words, so to show full spellings we harvest clean
# words from two places that are at fixed header offsets and decode reliably:
# object short-names and the abbreviations table. Each decoded word is matched
# back to a dictionary entry by its truncated prefix.

def _decode_zstring(data, addr, abbr_table, depth=0):
    """Decode a Z-string at a byte address, expanding abbreviations (v3)."""
    zs, a = [], addr
    while True:
        w = _w16(data, a); a += 2
        zs += [(w >> 10) & 0x1f, (w >> 5) & 0x1f, w & 0x1f]
        if w & 0x8000 or a + 1 >= len(data):
            break
    out, alpha, i = [], 0, 0
    while i < len(zs):
        c = zs[i]
        if c == 0:
            out.append(' '); alpha = 0; i += 1; continue
        if c in (1, 2, 3) and depth == 0:          # abbreviations don't nest in v3
            if i + 1 < len(zs):
                idx = 32 * (c - 1) + zs[i + 1]
                aa = _w16(data, abbr_table + 2 * idx) * 2
                out.append(_decode_zstring(data, aa, abbr_table, 1)); i += 2; alpha = 0; continue
            i += 1; continue
        if c == 4:
            alpha = 1; i += 1; continue
        if c == 5:
            alpha = 2; i += 1; continue
        if alpha == 0:
            out.append(A0[c - 6])
        elif alpha == 1:
            out.append(A1[c - 6])
        else:
            if c == 6:
                if i + 2 < len(zs):
                    zc = (zs[i + 1] << 5) | zs[i + 2]
                    if 32 <= zc < 127:
                        out.append(chr(zc))
                    i += 3; alpha = 0; continue
                i += 1; continue
            out.append(A2[c - 6])
        alpha = 0; i += 1
    return ''.join(out)


def extract_objects(data):
    """Return [(short_name, attribute_set)] for every object (v3)."""
    if data[0] >= 4:
        return []                        # object format differs; v3-only for now
    obj_table = _w16(data, 0x0a)
    abbr_table = _w16(data, 0x18)
    base = obj_table + 31 * 2            # v3: 31 two-byte property defaults
    min_prop, ends, k = 1 << 30, [], 0
    while base + k * 9 + 9 <= len(data) and base + k * 9 < min_prop:
        e = base + k * 9
        prop = _w16(data, e + 7)
        if 0 < prop < min_prop:
            min_prop = prop
        ends.append(e); k += 1
    objs = []
    for e in ends:
        bits = (data[e] << 24) | (data[e + 1] << 16) | (data[e + 2] << 8) | data[e + 3]
        attrs = {n for n in range(32) if bits & (1 << (31 - n))}
        prop = _w16(data, e + 7)
        name = ""
        if 0 < prop < len(data) and data[prop] != 0:
            name = _decode_zstring(data, prop + 1, abbr_table)
        objs.append((name, attrs))
    return objs


def harvest_full_words(data):
    """Return a set of clean words from object short-names + abbreviations."""
    if data[0] >= 4:
        return set()
    abbr_table = _w16(data, 0x18)
    texts = [name for name, _ in extract_objects(data)]
    for idx in range(96):               # abbreviation strings themselves
        aa = _w16(data, abbr_table + 2 * idx) * 2
        if aa < len(data):
            texts.append(_decode_zstring(data, aa, abbr_table, 1))
    words = set()
    for t in texts:
        words.update(re.findall(r"[a-z]{3,}", t.lower()))
    return words


# --- Parser model: part-of-speech flags + verb grammar -------------------------
#
# The dictionary tags each word's part of speech, and a verb grammar table at the
# start of static memory lists each verb's sentence structures: which prepositions
# it accepts and, per object slot, a "FIND" attribute number naming the class of
# object expected. Resolving that attribute against the object table's attribute
# bits yields the concrete nouns a verb wants -- e.g. eat -> {garlic, lunch}.

# Dictionary flag bits (Infocom v3).
FL_NOUN, FL_VERB, FL_ADJ, FL_DIR, FL_PREP = 0x80, 0x40, 0x20, 0x10, 0x08

# Canonical display word for each shared preposition id (synonyms collapse to one id).
PREP_CANON = {254: 'with', 240: 'under', 251: 'in', 249: 'on', 250: 'down',
              252: 'up', 253: 'out', 255: 'to', 248: 'from', 243: 'at',
              244: 'off', 245: 'across', 246: 'over', 247: 'away',
              239: 'behind', 242: 'for', 241: 'around'}


def extract_lexicon(data):
    """Return per-word info: {word: (flags, id)}, plus verb-id and prep-id maps."""
    p = _w16(data, 0x08)
    n_sep = data[p]; p += 1 + n_sep
    entry_len = data[p]; p += 1
    count = _w16(data, p); p += 2
    words, verbs_by_id, preps = {}, {}, {}
    for k in range(count):
        off = p + k * entry_len
        z = []
        for b in (0, 2):
            w = _w16(data, off + b)
            z += [(w >> 10) & 0x1f, (w >> 5) & 0x1f, w & 0x1f]
        word = _decode(z).rstrip()
        flags, wid = data[off + 4], data[off + 5]
        words[word] = (flags, wid)
        if flags & FL_VERB:
            verbs_by_id.setdefault(wid, word)   # first spelling wins as canonical
        if flags & FL_PREP:
            preps.setdefault(wid, word)
    return words, verbs_by_id, preps


def parse_grammar(data, verbs_by_id):
    """Return {verb_id: [ (prep1_id, prep2_id, obj1_attr, obj2_attr), ... ]}."""
    flen = len(data)
    base = _w16(data, 0x0e)              # verb table starts at static-memory base
    out = {}
    for wid in verbs_by_id:
        a = _w16(data, base + 2 * (255 - wid))
        if not (base <= a < flen):
            continue
        n = data[a]
        if not (1 <= n <= 12):
            continue
        rows = []
        for e in range(n):
            r = data[a + 1 + e * 8:a + 1 + e * 8 + 8]
            if len(r) < 5:
                break
            rows.append((r[1], r[2], r[3], r[4]))
        out[wid] = rows
    return out


def nouns_by_attribute(objects, vocab):
    """Map each object attribute -> set of concrete noun words (in vocab)."""
    by_attr = {}
    for name, attrs in objects:
        toks = [t for t in re.findall(r"[a-z]+", name.lower()) if t in vocab]
        if not toks:
            continue
        noun = toks[-1]                 # the head noun is the last matched token
        for a in attrs:
            by_attr.setdefault(a, set()).add(noun)
    return by_attr


def adjective_links(objects, vocab, lexicon):
    """From multi-word object names, link each adjective -> the head noun."""
    links = []
    for name, _ in objects:
        toks = [t for t in re.findall(r"[a-z]+", name.lower()) if t in vocab]
        if len(toks) < 2:
            continue
        noun = toks[-1]
        for adj in toks[:-1]:
            if lexicon.get(adj, (0, 0))[0] & FL_ADJ:
                links.append((adj, noun))
    return links


def full_word_map(dict_words, harvested):
    """Map each 6-char (truncated) dict form to a full spelling when we have one."""
    by_prefix = {}
    for w in harvested:
        by_prefix.setdefault(w[:6], []).append(w)
    mapping = {}
    for d in dict_words:
        if len(d) < 6:
            continue                     # not truncated -- already the full word
        cands = by_prefix.get(d, [])
        if d in cands or not cands:
            continue                     # complete word, or nothing better found
        # Prefer the shortest completion (avoids over-long / plural noise).
        mapping[d] = min(cands, key=len)
    return mapping


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

# Grammar-derived transition weights (baseline; the solution file boosts above).
W_VERB_PREP = 60      # verb -> a preposition it accepts (put -> in)
W_PREP_NOUN = 55      # preposition -> an object of the class it governs (with -> sword)
W_VERB_NOUN = 55      # verb -> an object of the class it takes (eat -> garlic)
W_ADJ_NOUN  = 52      # adjective -> its head noun (brass -> lantern)
# Only build verb/prep -> noun links for reasonably specific object classes;
# huge/generic classes (e.g. "any visible object") add noise, not ranking.
MAX_CLASS = 40


def build_model(data, dict_words, freq, bigrams, fullmap):
    """Merge parser grammar + walkthrough into {word: weight} and transitions."""
    lexicon, verbs_by_id, prep_by_id = extract_lexicon(data)
    grammar = parse_grammar(data, verbs_by_id)
    objects = extract_objects(data)

    def disp(w):                        # dictionary word -> displayed (full) form
        return fullmap.get(w, w)

    # --- base vocabulary (full spellings) + part-of-speech priors ---
    base = {}
    for w in dict_words:
        if re.fullmatch(r"[a-z]+", w):
            base[disp(w)] = 30
    for w, (fl, _) in lexicon.items():  # priors so bare completion is sane w/o a solution
        if fl & FL_DIR:
            base[disp(w)] = max(base.get(disp(w), 0), 66)
        elif fl & FL_VERB:
            base[disp(w)] = max(base.get(disp(w), 0), 44)

    # A walkthrough word is the full spelling of a (truncated) dictionary word.
    for full in list(freq):
        key = full[:6]
        for existing in [b for b in base if b[:6] == key and b != full]:
            base.pop(existing, None)
        base[full] = base.get(full, 30)
    for t, c in freq.items():           # winning-path frequency drives base weight
        base[t] = min(100, 40 + min(c, 60))

    vocab = set(base)
    by_attr = nouns_by_attribute(objects, vocab)
    specific = {a: ns for a, ns in by_attr.items() if a != 0 and 1 <= len(ns) <= MAX_CLASS}

    # --- transitions: grammar baseline, then solution-file refinement ---
    trans = {}
    def add(a, b, w):
        if a in vocab and b in vocab and a != b:
            trans[(a, b)] = max(trans.get((a, b), 0), w)

    # Layer 1b/1c: per verb (every synonym spelling), from its grammar rows.
    for w, (fl, wid) in lexicon.items():
        if not (fl & FL_VERB):
            continue
        v = disp(w)
        for prep1, prep2, o1, o2 in grammar.get(wid, []):
            if prep1 in prep_by_id: add(v, PREP_CANON.get(prep1, prep_by_id[prep1]), W_VERB_PREP)
            if prep2 in prep_by_id: add(v, PREP_CANON.get(prep2, prep_by_id[prep2]), W_VERB_PREP)
            for n in specific.get(o1, ()):                 # verb -> direct-object nouns
                add(v, n, W_VERB_NOUN)
            # preposition -> the object it governs (prep2 governs obj2; prep1 governs obj1)
            if prep2 in prep_by_id:
                pw = PREP_CANON.get(prep2, prep_by_id[prep2])
                for n in specific.get(o2, ()): add(pw, n, W_PREP_NOUN)
            if prep1 in prep_by_id:
                pw = PREP_CANON.get(prep1, prep_by_id[prep1])
                for n in specific.get(o1, ()): add(pw, n, W_PREP_NOUN)

    # Adjective -> head noun (brass -> lantern), from multi-word object names.
    for adj, noun in adjective_links(objects, vocab, lexicon):
        add(disp(adj), disp(noun), W_ADJ_NOUN)

    # Layer 2: solution-file bigrams boost the specific winning-path pairings.
    for (a, b), c in bigrams.items():
        add(a, b, min(96, 50 + 8 * c))

    transitions = [(a, b, w) for (a, b), w in trans.items()]
    return base, transitions


# --- C emission ----------------------------------------------------------------

C_HEADER = """// GENERATED FILE -- do not edit by hand.
// Produced by tools/typeahead/gen_typeahead.py from:
//   story:      {story}
//   walkthrough:{script}
//   words: {nwords}   transitions: {nlinks}
//
// Vocabulary is the story's own parser dictionary. Context transitions are
// derived from the game's grammar (verb->preposition, and verb/preposition->the
// object class it expects, e.g. eat->food, open->openables, with->weapons), then
// refined by a winning command script (frequency + word bigrams) so completion
// favours the winning move.

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
    ap.add_argument("--game", action="append", required=True, metavar="STORY:SCRIPT",
                    help="a story file and its walkthrough, colon-separated (repeatable)")
    ap.add_argument("--out", help="output C file (default: print a summary only)")
    ap.add_argument("--dump", action="store_true", help="print the decoded dictionary and exit")
    ap.add_argument("--no-fullwords", action="store_true",
                    help="keep 6-char dictionary forms instead of recovering full spellings")
    args = ap.parse_args()

    for spec in args.game:
        story, script = spec.rsplit(":", 1)

        data = open(story, "rb").read()
        dict_words = extract_dictionary(data)
    
        fullmap = {} if args.no_fullwords else full_word_map(dict_words, harvest_full_words(data))
    
        if args.dump:
            for w in dict_words:
                print(fullmap.get(w, w))
            print(f"\n{len(dict_words)} dictionary entries; "
                  f"{len(fullmap)} expanded to full words", file=sys.stderr)
            return
    
        freq, bigrams = (Counter(), Counter())
        if script:
            freq, bigrams = parse_script(script)
    
        base, transitions = build_model(data, dict_words, freq, bigrams, fullmap)
        print(f"dictionary words: {len(dict_words)}   full-word expansions: {len(fullmap)}")
        print(f"script words: {len(freq)}   bigrams: {len(bigrams)}")
        print(f"vocabulary: {len(base)}   transitions: {len(transitions)}")
    
        if args.out:
            emit_c(base, transitions, args.out, story, script)
            print(f"wrote {args.out}")
        else:
            print("(no --out; nothing written)")


if __name__ == "__main__":
    main()
