#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. lib/audio.sh    # defines merge_disc (and split_bincue); no main runs.

work=$(mktemp -d); mkdir -p "$work/game" "$work/audio" "$work/out"
printf 'DATA' > "$work/game/mojozork.bin"
cat > "$work/game/mojozork.cue" <<'CUE'
FILE "mojozork.bin" BINARY
  TRACK 01 MODE1/2352
    INDEX 01 00:00:00
CUE
printf 'A' > "$work/audio/track02.bin"
printf 'B' > "$work/audio/track03.bin"

merge_disc "$work/game" "$work/audio" "$work/out"

expected=$(cat <<'CUE'
FILE "mojozork.bin" BINARY
  TRACK 01 MODE1/2352
    INDEX 01 00:00:00
FILE "track02.bin" BINARY
  TRACK 02 AUDIO
    PREGAP 00:02:00
    INDEX 01 00:00:00
FILE "track03.bin" BINARY
  TRACK 03 AUDIO
    INDEX 01 00:00:00
CUE
)
fail=0
[ -f "$work/out/mojozork.bin" ] && [ -f "$work/out/track02.bin" ] && [ -f "$work/out/track03.bin" ] \
  && echo "ok: files copied" || { echo "FAIL: files not copied"; fail=1; }
if diff <(printf '%s\n' "$expected") "$work/out/mojozork.cue" >/dev/null; then
  echo "ok: cue matches"
else
  echo "FAIL: cue mismatch"; diff <(printf '%s\n' "$expected") "$work/out/mojozork.cue" || true; fail=1
fi
exit $fail
