# process_audio <cue_music_dir> <final_out> <base_name>
# Copies and renames all audio BINs, ignoring Track 1.
process_audio() {
  local am_dir="$1" out="$2" base="$3"
  shopt -s nullglob
  local f name tnum
  for f in "$am_dir"/*.bin; do
    name=$(basename "$f")

    if [[ "$name" =~ Track[[:space:]]*0?1([^0-9]|$) ]]; then
      echo "Skipping Track 1 from music dir: $name"
      continue
    fi

    if [[ "$name" =~ Track[[:space:]]*([0-9]+) ]]; then
      # Force base-10 math to avoid octal errors on 08/09
      tnum=$(printf "%02d" "$((10#${BASH_REMATCH[1]}))")
      cp "$f" "$out/$base (Track $tnum).bin"
      echo "Copied $name -> $base (Track $tnum).bin"
    fi
  done
}

# process_cue <cue_music_dir> <final_out> <base_name>
# Overwrites FILE lines to match the requested hyphenated string.
process_cue() {
  local am_dir="$1" out="$2" base="$3"
  shopt -s nullglob
  local cues=("$am_dir"/*.cue)
  [ ${#cues[@]} -gt 0 ] || { echo "Warning: No .cue file found in $am_dir"; return 0; }

  # Strip Windows line endings, rewrite FILE lines, pass rest through
  tr -d '\r' < "${cues[0]}" | awk '
    BEGIN { t = 1 }
    /^[[:space:]]*FILE/ {
      printf "FILE \"Zaturn - Complete (USA) (Track %02d).bin\" BINARY\n", t
      t++
      next
    }
    { print $0 }
  ' > "$out/$base.cue"

  echo "Processed and copied CUE file -> $base.cue"
}