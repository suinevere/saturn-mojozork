#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. lib/cuelib.sh

fail=0
check() { # check <label> <actual> <expected>
  if [ "$2" = "$3" ]; then echo "ok: $1"; else echo "FAIL: $1 got '$2' want '$3'"; fail=1; fi
}

check "msf 00:00:00"  "$(msf_to_frames 00:00:00)" "0"
check "msf 00:02:00"  "$(msf_to_frames 00:02:00)" "150"     # 2s * 75
check "msf 00:25:73"  "$(msf_to_frames 00:25:73)" "1948"    # 25*75+73
check "msf 02:18:31"  "$(msf_to_frames 02:18:31)" "10381"   # (2*60+18)*75+31

printf 'abcdefghij' > /tmp/cuelib_fs.bin
check "file_size"     "$(file_size /tmp/cuelib_fs.bin)" "10"

# resolve_tool: iso2raw is bundled for every OS -> must resolve to an existing file.
iso2raw_path="$(resolve_tool iso2raw)"
[ -f "$iso2raw_path" ] && echo "ok: iso2raw resolves ($iso2raw_path)" \
  || { echo "FAIL: iso2raw path '$iso2raw_path' missing"; fail=1; }

exit $fail
