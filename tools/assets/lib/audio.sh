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
    dd if="$bin" of="$out/$name" bs=2352 skip="$startf" count="$count" status=none
    echo "  split -> $name ($count sectors)"
  done
}
