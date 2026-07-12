#!/usr/bin/env python3
"""Filter a raw walkthrough command dump into a clean .WIN overlay script.

Reads a raw list of one-command-per-line text on stdin (as produced by scraping
a walkthrough), and writes a cleaned list on stdout:
  - drops blank lines, markdown headers, and any prose/notes
  - drops pure movement commands (GO NORTH, S, ENTER, etc.) -- the runtime
    grammar already handles bare directions, so they add only noise
  - keeps meaningful commands, including GO <place> when <place> is a real noun
    (e.g. GO MINIATURIZATION BOOTH), uppercased and de-duplicated of adjacent
    repeats

Usage:  python filter_win.py "<source-url>" < raw.txt > GAME.WIN
"""
import re
import sys

DIRS = {
    "NORTH", "SOUTH", "EAST", "WEST", "UP", "DOWN",
    "N", "S", "E", "W", "U", "D",
    "NE", "NW", "SE", "SW",
    "NORTHEAST", "NORTHWEST", "SOUTHEAST", "SOUTHWEST",
    "IN", "OUT", "ENTER", "EXIT",
    "PORT", "STARBOARD", "FORE", "AFT", "LAND",
}
MOVE_VERBS = {"GO", "WALK", "RUN"}


def is_movement(cmd):
    parts = cmd.split()
    if not parts:
        return True
    if len(parts) == 1 and parts[0] in DIRS:
        return True
    if len(parts) == 2 and parts[0] in MOVE_VERBS and parts[1] in DIRS:
        return True
    return False


def main():
    url = sys.argv[1] if len(sys.argv) > 1 else ""
    out = [f"# source: {url}"] if url else []
    prev = None
    for line in sys.stdin:
        s = line.strip()
        if not s or s.startswith("#") or s.startswith("*"):
            continue
        # a real command is letters/digits/spaces only; skip prose sentences
        if not re.fullmatch(r"[A-Za-z0-9 ,;:'\"\.\-]+", s):
            continue
        cmd = re.sub(r"\s+", " ", s).upper().strip(" .")
        if not cmd or is_movement(cmd):
            continue
        if cmd == prev:          # collapse adjacent duplicates
            continue
        out.append(cmd)
        prev = cmd
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
