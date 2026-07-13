#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. lib/audio.sh    # sources cuelib.sh; defines split_bincue. No main runs.

work=$(mktemp -d)
# Synthetic source.bin: track1=2 sectors data, track2=3 sectors, track3=1 sector
# (2352 bytes/sector). Fill each with a distinct byte for identity checks.
python3 - "$work/source.bin" <<'PY'
import sys
secs = [(b'\x11',2),(b'\x22',3),(b'\x33',1)]
with open(sys.argv[1],'wb') as f:
    for byte,n in secs:
        f.write(byte*2352*n)
PY
cat > "$work/source.cue" <<'CUE'
FILE "source.bin" BINARY
  TRACK 01 MODE1/2352
    INDEX 01 00:00:00
  TRACK 02 AUDIO
    INDEX 01 00:00:02
  TRACK 03 AUDIO
    INDEX 01 00:00:05
CUE

mkdir -p "$work/audio"
split_bincue "$work/source.cue" "$work/source.bin" "$work/audio"

fail=0
[ "$(file_size "$work/audio/track02.bin")" = "$((2352*3))" ] && echo "ok: track02 size" || { echo "FAIL track02"; fail=1; }
[ "$(file_size "$work/audio/track03.bin")" = "$((2352*1))" ] && echo "ok: track03 size" || { echo "FAIL track03"; fail=1; }
[ ! -e "$work/audio/track01.bin" ] && echo "ok: no data track emitted" || { echo "FAIL track01 emitted"; fail=1; }
od -An -tx1 -N1 "$work/audio/track02.bin" | grep -q 22 && echo "ok: track02 content" || { echo "FAIL content"; fail=1; }
exit $fail
