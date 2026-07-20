# Audio bin/cue split + merge helpers. Source with: . lib/audio.sh
# (sources cuelib.sh from the same lib dir).
. "$(dirname "${BASH_SOURCE[0]}")/cuelib.sh"

# split_bincue <source.cue> <source.bin> <out_dir>
# Writes trackNN.bin for every AUDIO track; slices at 2352 bytes/sector.
split_bincue() {
  echo "enter split_bincue"
  local cue="$1" bin="$2" out="$3"
  mkdir -p "$out"
  echo "step1"
  local total; total=$(file_size "$bin")
  local nums=() types=() starts=()
  local tnum="" ttype=""
  echo "step2"
  while IFS= read -r line; do
    case "$line" in
      *TRACK\ *) tnum=$(echo "$line" | sed -E 's/.*TRACK 0*([0-9]+).*/\1/');
                 ttype=$(echo "$line" | grep -oE 'AUDIO|MODE1|MODE2');;
      *INDEX\ 01\ *) local msf; msf=$(echo "$line" | grep -oE '[0-9]{2}:[0-9]{2}:[0-9]{2}');
                     nums+=("$tnum"); types+=("$ttype"); starts+=("$(msf_to_frames "$msf")");;
    esac
  done < "$cue"
  local i n; n=${#nums[@]}
  for ((i=0;i<n;i++)); do
    [ "${types[$i]}" = "AUDIO" ] || continue
    local startf=${starts[$i]} endf
    if [ $((i+1)) -lt "$n" ]; then endf=${starts[$((i+1))]}; else endf=$(( total / 2352 )); fi
    local count=$(( endf - startf )) name
    printf -v name "track%02d.bin" "${nums[$i]}"
    dd if="$bin" of="$out/$name" bs=2352 skip="$startf" count="$count" 2>/dev/null
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
