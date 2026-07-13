# promote_game_track <final_out> <base_name>
# games.bat writes the injected data track as "<base>.bin", but the cue refers
# to it as "<base> (Track 01).bin" alongside the audio tracks. Rename it into
# place. Idempotent: a no-op if music.bat has already run.
promote_game_track() {
  local out="$1" base="$2"
  local src="$out/$base.bin" dst="$out/$base (Track 01).bin"
  if [ -f "$src" ]; then
    mv -f "$src" "$dst"
    echo "Renamed game track -> $base (Track 01).bin"
  elif [ -f "$dst" ]; then
    echo "Game track already named -> $base (Track 01).bin"
  else
    echo "Warning: no game track in $out -- run games.bat first, or track 01 will be missing"
  fi
}

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
  # FILE lines must match the names process_audio wrote, i.e. "$base (Track NN).bin".
  tr -d '\r' < "${cues[0]}" | awk -v base="$base" '
    BEGIN { t = 1 }
    /^[[:space:]]*FILE/ {
      printf "FILE \"%s (Track %02d).bin\" BINARY\n", base, t
      t++
      next
    }
    { print $0 }
  ' > "$out/$base.cue"

  echo "Processed and copied CUE file -> $base.cue"
}