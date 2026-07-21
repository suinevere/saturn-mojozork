#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. lib/games.sh    # sources cuelib.sh; defines inject_games. No main runs.

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    echo "SKIP: bash inject_games is unsupported under git-bash/MSYS (MSYS rewrites the /Z3 in-ISO path into a host path, so xorriso creates a bogus /C_/PROGRAM_FILES tree). The bash path is exercised on Linux CI; the Windows path is lib/games.ps1."; exit 0;;
esac

# Requires a real base ISO + xorriso + iso2raw. Skip cleanly if unavailable.
BASE="../../saturn/BuildDrop/Zaturn (USA) (Netlink Edition).iso"
if [ ! -f "$BASE" ] || ! resolve_tool xorriso >/dev/null 2>&1; then
  echo "SKIP: base ISO or xorriso unavailable"; exit 0
fi
work=$(mktemp -d); mkdir -p "$work/games" "$work/out"
printf 'ZTEST' > "$work/games/TESTGAME.Z3"

inject_games "$BASE" "$work/games" "$work/out" "mojozork"

fail=0
# 1) IP.BIN (first 32768 bytes of ISO) preserved — compare to base.
head -c 32768 "$BASE" > "$work/base_ip"
head -c 32768 "$work/out/mojozork_injected.iso" > "$work/out_ip" 2>/dev/null || true
if [ -f "$work/out/mojozork_injected.iso" ] && cmp -s "$work/base_ip" "$work/out_ip"; then
  echo "ok: IP.BIN preserved"; else echo "FAIL: IP.BIN changed"; fail=1; fi
# 2) Raw track produced and cue emitted.
[ -f "$work/out/mojozork.bin" ] && echo "ok: raw bin" || { echo "FAIL: no raw bin"; fail=1; }
grep -q 'TRACK 01 MODE1/2352' "$work/out/mojozork.cue" && echo "ok: cue" || { echo "FAIL: cue"; fail=1; }
# 3) Injected game present in the ISO listing.
resolve_tool xorriso >/dev/null && "$(resolve_tool xorriso)" -indev "$work/out/mojozork_injected.iso" -find /Z3 2>/dev/null | grep -qi 'TESTGAME' \
  && echo "ok: game injected" || { echo "FAIL: game not in ISO"; fail=1; }
# 4) Plain ISO9660 directory records — no Rock Ridge/SUSP.
#    Rock Ridge inflates the "." record from 34 bytes to ~132 and the Saturn CD
#    block's ISO9660 parser (used by the BIOS to locate 0.BIN) then fails to
#    boot the disc. Read the root dir LBA out of the PVD and check record 1.
iso="$work/out/mojozork_injected.iso"
root_lba=$(od -A n -t u4 -j $((32768 + 158)) -N 4 "$iso" | tr -d ' ')
dot_reclen=$(od -A n -t u1 -j $((root_lba * 2048)) -N 1 "$iso" | tr -d ' ')
if [ "$dot_reclen" = "34" ]; then
  echo "ok: plain ISO9660 (root '.' record = 34 bytes)"
else
  echo "FAIL: directory records carry system-use data ('.' record = ${dot_reclen} bytes, expected 34) — xorriso needs -rockridge off"
  fail=1
fi
exit $fail
