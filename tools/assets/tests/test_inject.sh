#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. lib/inject.sh    # sources cuelib.sh; defines inject_games. No main runs.

# Requires a real base ISO + xorriso + iso2raw. Skip cleanly if unavailable.
BASE="../../saturn/BuildDrop/mojozork.iso"
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
exit $fail
