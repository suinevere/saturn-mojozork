#!/usr/bin/env python3
"""Generate saturn/src/game_titles.c: a (release, serial) -> display-title table.

The Z-machine header has no title, only a release number (0x02) and serial
(0x12, 6 ASCII bytes). This scans a reference collection of named .z3 files
(one per known version), reads each header's release/serial, and emits a lookup
table keyed by those, mapping to a curated display title. All known versions of
a game map to the same title, so any disc that carries one of them resolves.

Usage:
  python gen_titles.py --ref  ../../allgamefiles/gamefiles \
                       --out  ../../saturn/src/game_titles.c
"""

import argparse, glob, os, re

# Curated display titles by short name (the reference filename's leading token).
# Kept concise so they fit the ~36-char selection menu. Games not listed (test
# harnesses etc.) are skipped and fall back to the filename in the UI.
TITLES = {
    "advent":           "Colossal Cave Adventure (1977)",
    "amfv":             "A Mind Forever Voyaging",
    "ballyhoo":         "Ballyhoo",
    "cutthroats":       "Cutthroats",
    "deadline":         "Deadline",
    "enchanter":        "Enchanter",
    "hitchhiker":       "Hitchhiker's Guide",
    "hollywoodhijinx":  "Hollywood Hijinx",
    "hypochondriac":    "Hypochondriac",
    "infidel":          "Infidel",
    "leathergoddesses": "Leather Goddesses of Phobos",
    "lurkinghorror":    "The Lurking Horror",
    "minizork":         "Mini-Zork I",
    "minizork2":        "Mini-Zork II",
    "moonmist":         "Moonmist",
    "planetfall":       "Planetfall",
    "plunderedhearts":  "Plundered Hearts",
    "sampler":          "Infocom Sampler",
    "seastalker":       "Seastalker",
    "sorcerer":         "Sorcerer",
    "spellbreaker":     "Spellbreaker",
    "starcross":        "Starcross",
    "stationfall":      "Stationfall",
    "suspect":          "Suspect",
    "suspended":        "Suspended",
    "wishbringer":      "Wishbringer",
    "witness":          "The Witness",
    "zork1":            "Zork I",
    "zork2":            "Zork II",
    "zork3":            "Zork III",
}

# Category id per short name (matches GAME_CAT_* in game_titles.h). Anything not
# listed (amfv, minizork, sampler, hypochondriac, ...) falls into Other (5).
CATEGORY = {
    "advent": 0,
    "zork1": 0, "zork2": 0, "zork3": 0, "enchanter": 0, "sorcerer": 0, "spellbreaker": 0,
    "planetfall": 1, "stationfall": 1,
    "deadline": 2, "witness": 2, "suspect": 2, "moonmist": 2,
    "infidel": 3, "cutthroats": 3, "seastalker": 3, "wishbringer": 3,
    "ballyhoo": 3, "hollywoodhijinx": 3, "plunderedhearts": 3,
    "starcross": 4, "suspended": 4, "lurkinghorror": 4,
    "hitchhiker": 5, "leathergoddesses": 5
}
CAT_OTHER = 5

# Games that ship on a disc but have no file in the reference collection, keyed
# by (release, serial) so a regenerate keeps them. Merged over the scan results.
MANUAL = {
    #(6, "151001"): ("Colossal Cave Adventure (1977)", 0)
}


def scan(refdir):
    entries = {}   # (release, serial) -> (title, category)
    seen = set()
    for f in glob.glob(os.path.join(refdir, "*.z3")) + glob.glob(os.path.join(refdir, "*.Z3")):
        key = os.path.normcase(f)
        if key in seen:
            continue
        seen.add(key)
        d = open(f, "rb").read()
        if len(d) < 0x1a or d[0] != 3:
            continue
        rel = (d[2] << 8) | d[3]
        serial = bytes(d[0x12:0x18]).decode("latin1", "replace")
        if not re.fullmatch(r"[0-9A-Za-z]{6}", serial):
            continue
        name = os.path.basename(f).lower()
        m = re.match(r"(.+?)-(?:[a-z0-9]+-)?r\d+-s\d+\.z3$", name)
        short = m.group(1) if m else None
        if short in TITLES:
            entries[(rel, serial)] = (TITLES[short], CATEGORY.get(short, CAT_OTHER))
    return entries


C_TMPL = """// GENERATED FILE -- do not edit by hand.
// Produced by tools/gametitles/gen_titles.py.
//
// A display title for a Z-machine story, keyed by its header release number
// (0x02) + serial (0x12). The header carries no title, so this maps the known
// releases/serials of the Infocom games to a curated name.

#include "game_titles.h"
#include <string.h>

typedef struct {{ unsigned short release; const char* serial; const char* title; int cat; }} GameTitle;

static const GameTitle TITLES[] = {{
{rows}
}};

static const GameTitle* find(unsigned short release, const char* serial) {{
    for (int i = 0; i < (int)(sizeof(TITLES) / sizeof(TITLES[0])); i++)
        if (TITLES[i].release == release && memcmp(TITLES[i].serial, serial, 6) == 0)
            return &TITLES[i];
    return 0;
}}

// Title for (release, serial), or NULL if unknown. `serial` is the 6 raw header
// bytes at 0x12 (not necessarily NUL-terminated).
const char* game_title(unsigned short release, const char* serial) {{
    const GameTitle* g = find(release, serial);
    return g ? g->title : 0;
}}

// Category for (release, serial); GAME_CAT_OTHER if unknown.
int game_category(unsigned short release, const char* serial) {{
    const GameTitle* g = find(release, serial);
    return g ? g->cat : GAME_CAT_OTHER;
}}
"""


def emit(entries, out):
    rows = "\n".join(
        f'    {{ {rel}, "{serial}", "{title}", {cat} }},'
        for (rel, serial), (title, cat) in sorted(entries.items(), key=lambda kv: (kv[1][1], kv[1][0]))
    )
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(C_TMPL.format(rows=rows))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", required=True, help="reference folder of named .z3 files")
    ap.add_argument("--out", required=True, help="output C file")
    args = ap.parse_args()
    entries = scan(args.ref)
    entries.update(MANUAL)   # disc-only games with no reference file
    emit(entries, args.out)
    print(f"{len(entries)} (release,serial) title entries -> {args.out}")


if __name__ == "__main__":
    main()
