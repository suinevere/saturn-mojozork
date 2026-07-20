# Audio bin/cue split + merge helpers. Source with: . lib/audio.sh
# (sources cuelib.sh from the same lib dir).
. "$(dirname "${BASH_SOURCE[0]}")/cuelib.sh"

# split_bincue <source.cue> <source.bin> <out_dir>
# Writes trackNN.bin for every AUDIO track; slices at 2352 bytes/sector.
split_bincue() {
  local cue="$1" bin="$2" out="$3"
  mkdir -p "$out"
  local total; total=$(file_size "$bin")
  local nums=() types=() starts=()
  local tnum="" ttype=""

  while IFS= read -r line; do
    case "$line" in
      *TRACK\ *)
        tnum=$(echo "$line" | sed -E 's/.*TRACK 0*([0-9]+).*/\1/')

        # [DEBUG TEST] Prevent grep from causing an exit 1 if track type isn't recognized
        if ! ttype=$(echo "$line" | grep -oE 'AUDIO|MODE1|MODE2'); then
          echo "[DEBUG] Warning: ttype grep failed (unrecognized format) on line: $line" >&2
          ttype="UNKNOWN"
        fi
        ;;

      *INDEX\ 01\ *)
        local msf
        # [DEBUG TEST] Prevent grep from causing an exit 1 if timestamp formatting is weird
        if ! msf=$(echo "$line" | grep -oE '[0-9]{2}:[0-9]{2}:[0-9]{2}'); then
          echo "[DEBUG] Warning: msf timestamp grep failed on line: $line" >&2
        else
          nums+=("$tnum")
          types+=("$ttype")
          starts+=("$(msf_to_frames "$msf")")
        fi
        ;;
    esac
  done < "$cue"

  local i n; n=${#nums[@]}
  for ((i=0;i<n;i++)); do
    [ "${types[$i]}" = "AUDIO" ] || continue
    local startf=${starts[$i]} endf
    if [ $((i+1)) -lt "$n" ]; then endf=${starts[$((i+1))]}; else endf=$(( total / 2352 )); fi
    local count=$(( endf - startf )) name
    printf -v name "track%02d.bin" "${nums[$i]}"

    # [DEBUG TEST] Print variables and ensure count is valid before running dd
    echo "[DEBUG] $name -> startf: $startf | endf: $endf | count: $count" >&2

    if [ -z "$count" ] || [ "$count" -le 0 ]; then
      echo "[DEBUG ERROR] Invalid sector count ($count) for $name! Skipping." >&2
      continue
    fi

    # [DEBUG TEST] Capture dd's stderr to a temp file instead of /dev/null so we can see why it fails
    if ! dd if="$bin" of="$out/$name" bs=2352 skip="$startf" count="$count" 2>"/tmp/dd_debug_${name}.log"; then
      echo "[DEBUG ERROR] dd failed for $name! Output was:" >&2
      cat "/tmp/dd_debug_${name}.log" >&2
    fi

    echo "  split -> $name ($count sectors)"
  done
}

# merge_disc <game_dir> <audio_dir> <out_dir>
# Copies game bin/cue + audio bins into out_dir and rebuilds a multi-FILE cue:
# track 1 verbatim from the game cue, audio tracks regenerated from track*.bin.
merge_disc() {
  local gdir="$1" adir="$2" out="$3"
  mkdir -p "$out"
  local gcue gbin
  gcue=$(find "$gdir" -maxdepth 1 -iname '*.cue' | head -n1)
  gbin=$(find "$gdir" -maxdepth 1 -iname '*.bin' | head -n1)
  [ -n "$gcue" ] && [ -n "$gbin" ] || { echo "ERROR: need one bin+cue in $gdir"; return 1; }
  local audios=( "$adir"/track*.bin )
  [ -e "${audios[0]}" ] || { echo "ERROR: no audio bins in $adir"; return 1; }
  cp "$gbin" "$out/"; cp "$adir"/track*.bin "$out/"
  local outcue="$out/$(basename "${gcue%.cue}").cue"
  # Track 1: copy verbatim up to (but not including) the 2nd FILE line.
  awk 'BEGIN{keep=1} /^FILE/{n++} n>=2{keep=0} keep{print}' "$gcue" > "$outcue"
  # Audio tracks: regenerate, numbered from 02.
  local tn=2 first=1 f base
  for f in $(printf '%s\n' "${audios[@]}" | sort); do
    base=$(basename "$f")
    printf 'FILE "%s" BINARY\n' "$base" >> "$outcue"
    printf '  TRACK %02d AUDIO\n' "$tn" >> "$outcue"
    if [ "$first" = 1 ]; then printf '    PREGAP 00:02:00\n' >> "$outcue"; first=0; fi
    printf '    INDEX 01 00:00:00\n' >> "$outcue"
    tn=$((tn+1))
  done
  echo "Merged disc -> $outcue ($(( tn - 2 )) audio tracks)"
}
