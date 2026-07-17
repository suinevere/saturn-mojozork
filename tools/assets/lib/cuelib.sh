# Shared helpers for the asset scripts. Source with: . lib/cuelib.sh
# POSIX-ish bash; used only in the bash (Linux/macOS/git-bash) code paths.

# msf_to_frames "MM:SS:FF" -> integer frames (75 frames/sec)
msf_to_frames() {
  local msf="$1" mm ss ff
  mm=${msf%%:*}; msf=${msf#*:}
  ss=${msf%%:*}; ff=${msf#*:}
  echo $(( (10#$mm * 60 + 10#$ss) * 75 + 10#$ff ))
}

# file_size <path> -> bytes (GNU or BSD stat)
file_size() {
  stat -c%s "$1" 2>/dev/null || stat -f%z "$1"
}

# platform_subdir -> win | lin | mac
platform_subdir() {
  if [ -n "${OS:-}" ]; then echo win; return; fi
  case "$(uname -s)" in
    Linux)  echo lin;;
    Darwin) echo mac;;
    *)      echo unknown;;
  esac
}

# resolve_tool <name> -> executable path.
#  - iso2raw: bundled for every OS (bin/<plat>[/<arch>]/iso2raw[.exe])
#  - dd:      bundled on Windows; system dd elsewhere (always present)
#  - xorriso: bundled on Windows; system xorriso on mac/linux, and if missing
#             prints an install hint to stderr and returns 1 (does NOT exit).
resolve_tool() {
  local name="$1" plat; plat=$(platform_subdir)
  case "$name" in
    iso2raw)
      case "$plat" in
        win) echo "./bin/win/iso2raw.exe";;
        lin) echo "./bin/lin/iso2raw";;
        mac) if [ "$(uname -m)" = "arm64" ]; then echo "./bin/mac/arm64/iso2raw";
             else echo "./bin/mac/amd64/iso2raw"; fi;;
        *)   echo "ERROR: unsupported platform for iso2raw" >&2; return 1;;
      esac;;
    dd)
      if [ "$plat" = win ]; then echo "./bin/win/dd.exe"; else command -v dd; fi;;
    xorriso)
      if [ "$plat" = win ]; then echo "./bin/win/xorriso.exe"; return 0; fi
      local sys; sys=$(command -v xorriso 2>/dev/null || true)
      if [ -n "$sys" ]; then echo "$sys"; return 0; fi
      echo "ERROR: xorriso is not installed." >&2
      if [ "$plat" = mac ]; then echo "  Install it with: brew install xorriso" >&2;
      else echo "  Install it with: sudo apt-get install xorriso" >&2; fi
      return 1;;
    *) command -v "$name";;
  esac
}
