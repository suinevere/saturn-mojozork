# Game injection into the Saturn data ISO. Source with: . lib/inject.sh
. "$(dirname "${BASH_SOURCE[0]}")/cuelib.sh"

# inject_games <base.iso> <games_dir> <out_dir> <disc_name>
inject_games() {
  local base="$1" gdir="$2" out="$3" name="$4"
  mkdir -p "$out"
  local XORRISO ISO2RAW inj
  XORRISO=$(resolve_tool xorriso) || return 1   # prints brew/apt hint if missing
  ISO2RAW=$(resolve_tool iso2raw) || return 1
  inj="$out/${name}_injected.iso"
  # 1) rip IP.BIN (first 16 * 2048 = 32768 bytes)
  dd if="$base" of="$out/ip.bin" bs=2048 count=16 2>/dev/null
  # 2) inject game files into /Z3 (rewrites the ISO, clobbering the system area)
  "$XORRISO" -indev "$base" -outdev "$inj" -map "$gdir" /Z3 -commit >/dev/null 2>&1
  # 3) restore IP.BIN onto the front, in ISO space, before raw conversion
  dd if="$out/ip.bin" of="$inj" bs=2048 count=16 conv=notrunc 2>/dev/null
  # 4) verify preservation
  if ! cmp -s <(head -c 32768 "$base") <(head -c 32768 "$inj"); then
    echo "ERROR: IP.BIN not preserved after injection"; return 1; fi
  # 5) ISO -> MODE1/2352 raw
  "$ISO2RAW" "$inj" -o "$out/${name}.bin"
  # 6) track-1 cue (SDK canonical form)
  { printf 'FILE "%s.bin" BINARY\n' "$name";
    printf '  TRACK 01 MODE1/2352\n';
    printf '    INDEX 01 00:00:00\n'; } > "$out/${name}.cue"
  echo "Injected games -> $out/${name}.bin"
}
