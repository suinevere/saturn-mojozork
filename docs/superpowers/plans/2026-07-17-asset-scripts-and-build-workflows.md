# Asset-Download Scripts + OSS/Full Build Workflows Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `tools/assets/` into a two-script pipeline that builds a full Saturn disc (engine + all Infocom games + CD-DA audio) from a license-clean repo, add a "full image" CI workflow, and reshape `release.yml` into an open-source build-it-yourself kit.

**Architecture:** `games.bat` injects downloaded game files into a base `.iso` with xorriso (ripping/restoring the 32 KB Saturn IP.BIN boot header around the injection) and converts to a raw MODE1/2352 data track. `music.bat` downloads a CD-DA bin/cue image, splits its audio tracks into per-track bins, and merges a burnable **multi-FILE** cue that preserves track 1 verbatim and regenerates every audio track. Two GitHub workflows consume this: `release.yml` (open-source kit, no scripts run in CI) and `full-image.yml` (runs the scripts, uploads the full disc as an artifact).

**Tech Stack:** Bash/Batch polyglot scripts (`:;` dual-shell pattern), xorriso, iso2raw, dd, curl, unzip, GitHub Actions.

## Global Constraints

- **Sector size:** all tracks are **2352 bytes/sector** (MODE1/2352 data and raw CD-DA). 75 sectors/sec. MSF→bytes: `(((MM*60)+SS)*75 + FF) * 2352`.
- **IP.BIN:** the Saturn boot header occupies the **first 16 sectors = 32768 bytes** of the data track's ISO (2048-byte logical sectors: `16 * 2048 = 32768`). It MUST be ripped before xorriso rewrites the filesystem and restored after, in ISO space, before `iso2raw`.
- **Keep committed:** only `ZORK1.Z3`, `ZORK2.Z3`, `ZORK3.Z3` under `saturn/cd/data/Z3/` (open-sourced by Activision/Microsoft). All other `*.z3`/`*.blb` are untracked and fetched by `games.bat`.
- **Cue layout:** multi-FILE (one `FILE` per track). Track 1 copied **verbatim** from the game cue; audio tracks regenerated, numbered sequentially from `TRACK 02`.
- **Track-1 cue block (SDK canonical form):**
  ```
  FILE "<name>.bin" BINARY
    TRACK 01 MODE1/2352
      INDEX 01 00:00:00
  ```
- **Scripts:** keep the existing dual `:;`-bash / `@ECHO OFF`-batch polyglot pattern; every script runs unchanged on CI (ubuntu/bash) and Windows.
- **Bundled tooling (already added as zips in `tools/assets/bin/`):** `dd.exe` + `xorriso.exe` (+ `cygwin1.dll`, `cygiconv-2.dll`) are Windows-only, under `bin/win/`. `iso2raw` is bundled for every OS (`bin/win/iso2raw.exe`, `bin/lin/iso2raw`, `bin/mac/{amd64,arm64}/iso2raw`). On macOS/Linux `dd` is native and **`xorriso` must be installed by the user** — if it is missing, error and instruct: `brew install xorriso` (macOS) or `sudo apt-get install xorriso` (Linux). Keep each binary's GPL license file.
- **No copyrighted output in public releases:** the full disc (games + audio) is only ever a workflow artifact, never attached to a release.
- **Commit style:** plain messages, no Claude/AI attribution trailer.

---

## File Structure

- `tools/assets/CONFIG.ME` — modify: add `AUDIO_URL`, `BASE_ISO`, `GAME_DIR`, `AUDIO_DIR`, `OUTPUT_DIR`, `DISC_NAME`.
- `tools/assets/games.bat` — modify: after download, inject games into base ISO + IP.BIN dance + iso2raw → `./game/`.
- `tools/assets/music.bat` — modify: download + split audio into `./audio/`, then merge `./game`+`./audio` → `./output/` with rebuilt cue.
- `tools/assets/lib/cuelib.sh` — create: shared bash primitives (MSF→frames, `file_size`, `resolve_tool`). Sourced by the other libs.
- `tools/assets/lib/audio.sh` — create: `split_bincue` + `merge_disc` (sources `cuelib.sh`). Sourced by `music.bat` and the split/merge tests.
- `tools/assets/lib/inject.sh` — create: `inject_games` (sources `cuelib.sh`). Sourced by `games.bat` and the inject test.
- `tools/assets/lib/{split,merge,inject}.ps1` — create: Windows mirrors of the three bash functions, invoked from the `.bat` Windows blocks.
- `tools/assets/bin/{win,lin,mac}/…` — create by extracting the already-present zips: `bin/win/{dd.exe,xorriso.exe,cygwin1.dll,cygiconv-2.dll,iso2raw.exe}`, `bin/lin/iso2raw`, `bin/mac/{amd64,arm64}/iso2raw`, plus `bin/LICENSE-*.txt`.
- `tools/assets/README.md` — create: kit usage instructions.
- `tools/assets/.gitkeep` scaffolding: `tools/assets/game/.gitkeep`, `audio/.gitkeep`, `output/.gitkeep` — create.
- `tools/assets/tests/` — create: fixtures + verification scripts for split/merge logic.
- `.gitignore` — modify: keep only ZORK1/2/3 under `saturn/cd/data/Z3/`.
- `.github/workflows/release.yml` — modify: package the OSS kit + quick-play disc.
- `.github/workflows/full-image.yml` — create: run scripts, upload full disc artifact.

---

## Task 1: Untrack non-Zork games

**Files:**
- Modify: `.gitignore`
- Untrack (index only): 24 files under `saturn/cd/data/Z3/` (all except `ZORK1.Z3`, `ZORK2.Z3`, `ZORK3.Z3`)

**Interfaces:**
- Produces: a repo where `git ls-files saturn/cd/data/Z3/` lists exactly the three Zork files. The physical files remain on disk for local full builds.

- [ ] **Step 1: Verify current tracked set (baseline check)**

Run:
```bash
git ls-files 'saturn/cd/data/Z3/*' | wc -l          # expect 27
git ls-files 'saturn/cd/data/Z3/*' | grep -viE 'ZORK[123]\.Z3$' | wc -l   # expect 24
```
Expected: `27` then `24`.

- [ ] **Step 2: Append ignore rules to `.gitignore`**

Add at the end of `.gitignore`:
```gitignore
# --- Non-open-source game files ---------------------------------------------
# Fetched by tools/assets/games.bat; never committed. Only Zork 1/2/3 are
# open-sourced (Activision/Microsoft) and kept in the repo.
saturn/cd/data/Z3/*.Z3
saturn/cd/data/Z3/*.z3
saturn/cd/data/Z3/*.BLB
saturn/cd/data/Z3/*.blb
!saturn/cd/data/Z3/ZORK1.Z3
!saturn/cd/data/Z3/ZORK2.Z3
!saturn/cd/data/Z3/ZORK3.Z3
```

- [ ] **Step 3: Remove the 24 non-Zork files from the index (keep on disk)**

Run:
```bash
git ls-files 'saturn/cd/data/Z3/*' \
  | grep -viE 'ZORK[123]\.Z3$' \
  | while IFS= read -r f; do git rm --cached --quiet "$f"; done
```

- [ ] **Step 4: Verify the result**

Run:
```bash
git ls-files 'saturn/cd/data/Z3/*'            # expect ONLY the 3 ZORK files
ls saturn/cd/data/Z3 | wc -l                  # expect 27 (files still on disk)
git status --porcelain saturn/cd/data/Z3 | grep -c '^D '   # expect 24
```
Expected: three `ZORK*.Z3` tracked; 27 files still present on disk; 24 staged deletions.

- [ ] **Step 5: Commit**

```bash
git add .gitignore
git commit -m "Untrack non-Zork game files; keep only Zork 1/2/3 committed"
```

---

## Task 2: Extract the bundled tool zips into a per-OS layout

The zips are already present in `tools/assets/bin/`:
`dd-0.6beta3.zip`, `xorriso-exe-for-windows-master.zip`, `iso2raw-{linux-amd64,macos-amd64,macos-arm64,windows-amd64}.zip`.
This task extracts them into a clean layout the scripts resolve against, then removes the zips.

**Files:**
- Create: `tools/assets/bin/win/{dd.exe,xorriso.exe,cygwin1.dll,cygiconv-2.dll,iso2raw.exe}`
- Create: `tools/assets/bin/lin/iso2raw`, `tools/assets/bin/mac/amd64/iso2raw`, `tools/assets/bin/mac/arm64/iso2raw`
- Create: `tools/assets/bin/LICENSE-dd.txt`, `tools/assets/bin/LICENSE-xorriso.txt`, `tools/assets/bin/README.md`
- Delete: the six `*.zip` files under `tools/assets/bin/`

**Interfaces:**
- Produces the exact paths `resolve_tool` (Task 3) returns: `bin/win/{dd,xorriso,iso2raw}.exe`, `bin/lin/iso2raw`, `bin/mac/{amd64,arm64}/iso2raw`.

- [ ] **Step 1: Extract into the per-OS layout**

Run from repo root:
```bash
cd tools/assets/bin
mkdir -p win lin mac/amd64 mac/arm64
# Windows dd (chrysocome dd 0.6beta3): dd.exe + license
unzip -o dd-0.6beta3.zip -d _dd && cp _dd/dd.exe win/dd.exe && cp _dd/Copying.txt LICENSE-dd.txt
# Windows xorriso + required cygwin DLLs + license
unzip -o xorriso-exe-for-windows-master.zip -d _xo
cp _xo/xorriso-exe-for-windows-master/xorriso.exe      win/xorriso.exe
cp _xo/xorriso-exe-for-windows-master/cygwin1.dll      win/cygwin1.dll
cp _xo/xorriso-exe-for-windows-master/cygiconv-2.dll   win/cygiconv-2.dll
cp _xo/xorriso-exe-for-windows-master/LICENSE          LICENSE-xorriso.txt
# iso2raw per OS (rename the version-suffixed binary to plain iso2raw)
unzip -o iso2raw-windows-amd64.zip -d _i && cp _i/iso2raw-windows-amd64.exe win/iso2raw.exe
unzip -o iso2raw-linux-amd64.zip   -d _i && cp _i/iso2raw-linux-amd64       lin/iso2raw
unzip -o iso2raw-macos-amd64.zip   -d _i && cp _i/iso2raw-macos-amd64       mac/amd64/iso2raw
unzip -o iso2raw-macos-arm64.zip   -d _i && cp _i/iso2raw-macos-arm64       mac/arm64/iso2raw
# make the unix binaries executable (git tracks the mode)
chmod +x lin/iso2raw mac/amd64/iso2raw mac/arm64/iso2raw
rm -rf _dd _xo _i
cd ../../..
```

- [ ] **Step 2: Verify the layout**

Run:
```bash
find tools/assets/bin -type f | sort
```
Expected (exactly these, no `*.zip`):
```
tools/assets/bin/LICENSE-dd.txt
tools/assets/bin/LICENSE-xorriso.txt
tools/assets/bin/README.md            (after Step 4)
tools/assets/bin/lin/iso2raw
tools/assets/bin/mac/amd64/iso2raw
tools/assets/bin/mac/arm64/iso2raw
tools/assets/bin/win/cygiconv-2.dll
tools/assets/bin/win/cygwin1.dll
tools/assets/bin/win/dd.exe
tools/assets/bin/win/iso2raw.exe
tools/assets/bin/win/xorriso.exe
```

- [ ] **Step 3: Smoke-test the Windows binaries (on this Windows dev box)**

Run:
```bash
tools/assets/bin/win/dd.exe --version 2>&1 | head -1        # chrysocome dd banner
tools/assets/bin/win/xorriso.exe -version 2>&1 | head -3    # must NOT error on missing DLL
tools/assets/bin/win/iso2raw.exe 2>&1 | head -1 || true
```
Expected: `dd` and `xorriso` print version/usage text (proves the cygwin DLLs resolved). If `xorriso.exe` reports a missing DLL, a `cygwin*.dll` was not copied next to it — fix and re-run.

- [ ] **Step 4: Remove the zips and write `tools/assets/bin/README.md`**

```bash
rm -f tools/assets/bin/*.zip
```
Create `tools/assets/bin/README.md`:
```markdown
# Bundled tools

These GPL-licensed binaries let the asset scripts run without installing a
toolchain. `iso2raw` is bundled for every OS. `dd`/`xorriso` are bundled only
for Windows; on macOS/Linux the scripts use the system `dd` and require the
user to install `xorriso` (`brew install xorriso` / `sudo apt-get install xorriso`).

| File | Upstream | License |
|------|----------|---------|
| win/dd.exe | chrysocome dd 0.6beta3 | GPL — LICENSE-dd.txt |
| win/xorriso.exe (+ cygwin dlls) | PeyTy/xorriso-exe-for-windows | GPLv3 — LICENSE-xorriso.txt |
| win/iso2raw.exe, lin/iso2raw, mac/{amd64,arm64}/iso2raw | sftwninja/iso2raw | see upstream release |

Source for the GPL binaries is available from the upstream projects above.
```

- [ ] **Step 5: Commit**

```bash
git add tools/assets/bin
git status --short tools/assets/bin   # confirm binaries staged, no *.zip
git commit -m "Extract bundled dd/xorriso/iso2raw into per-OS bin layout with licenses"
```

---

## Task 3: Shared bash helper library (`cuelib.sh`)

**Files:**
- Create: `tools/assets/lib/cuelib.sh`
- Create: `tools/assets/tests/test_cuelib.sh`

**Interfaces:**
- Produces (sourced by later tasks):
  - `msf_to_frames "MM:SS:FF"` → echoes integer frame count.
  - `file_size <path>` → echoes byte size (cross-platform stat).
  - `platform_subdir` → `win` | `lin` | `mac`.
  - `resolve_tool <name>` → echoes the executable path per the bundled/system policy; for `xorriso` on macOS/Linux, if it is not installed it prints an install instruction to stderr and **returns 1** (never `exit`, so callers in `$( … )` or `if` decide). Paths are relative to the assets dir (scripts `cd` there first).

- [ ] **Step 1: Write the failing test**

Create `tools/assets/tests/test_cuelib.sh`:
```bash
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash tools/assets/tests/test_cuelib.sh`
Expected: FAIL — `lib/cuelib.sh` does not exist yet (`No such file`).

- [ ] **Step 3: Write `tools/assets/lib/cuelib.sh`**

```bash
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash tools/assets/tests/test_cuelib.sh`
Expected: five `ok:` lines + `ok: iso2raw resolves (...)`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add tools/assets/lib/cuelib.sh tools/assets/tests/test_cuelib.sh
git commit -m "Add cuelib.sh MSF/size/tool helpers with per-OS tool resolution"
```

---

## Task 4: `lib/audio.sh` split + `music.bat` download+split wiring

**Design note:** the reusable bash logic lives in **sourced libraries** (`lib/audio.sh`, later `lib/inject.sh`), so tests source the library — never the `.bat` — and there is no need to guard a `main` from executing on source. The `.bat` bodies are thin: read config, download, call the library function.

**Files:**
- Create: `tools/assets/lib/audio.sh` (defines `split_bincue`; sources `cuelib.sh`)
- Modify: `tools/assets/music.bat` (bash block: source `lib/audio.sh`, download, call `split_bincue`; Windows block: download + `split.ps1`)
- Create: `tools/assets/lib/split.ps1`
- Modify: `tools/assets/CONFIG.ME` (rename `URL`→`AUDIO_URL`; add `AUDIO_DIR=./audio`, `DISC_NAME=mojozork`)
- Create: `tools/assets/tests/test_split.sh`

**Interfaces:**
- Consumes: `cuelib.sh` (`msf_to_frames`, `resolve_tool`, `file_size`).
- Produces: `split_bincue <source.cue> <source.bin> <out_dir>` (in `lib/audio.sh`) — writes `trackNN.bin` (NN = 02,03,…) for every `TRACK NN AUDIO` in the source cue, slicing `[INDEX 01 of track] .. [INDEX 01 of next track | EOF]` at 2352 bytes/sector. The source's data track 1 is skipped.

- [ ] **Step 1: Write the failing test (synthetic 3-track image)**

Create `tools/assets/tests/test_split.sh`:
```bash
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash tools/assets/tests/test_split.sh`
Expected: FAIL — `lib/audio.sh: No such file or directory`.

- [ ] **Step 3: Create `tools/assets/lib/audio.sh` with `split_bincue`**

```bash
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash tools/assets/tests/test_split.sh`
Expected: `ok: track02 size`, `ok: track03 size`, `ok: no data track emitted`, `ok: track02 content`; exit 0.

- [ ] **Step 5: Rewrite the bash block of `music.bat` to source the lib and call it**

Replace the Linux/macOS block (keep the polyglot header):
```bash
:; # === Linux & macOS Execution Block ===
:; set -euo pipefail
:; cd "$(dirname "$0")"
:; . lib/audio.sh
:; cfg() { grep -m1 "^$1=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r'; }
:; AUDIO_URL=$(cfg AUDIO_URL); AUDIO_DIR=$(cfg AUDIO_DIR); AUDIO_DIR=${AUDIO_DIR:-./audio}
:; tmp=$(mktemp -d)
:; echo "Downloading audio image: $AUDIO_URL"
:; curl -L -o "$tmp/audio.zip" "$AUDIO_URL"
:; unzip -qo "$tmp/audio.zip" -d "$tmp/img"
:; srccue=$(find "$tmp/img" -iname '*.cue' | head -n1)
:; srcbin=$(find "$tmp/img" -iname '*.bin' | head -n1)
:; [ -n "$srccue" ] && [ -n "$srcbin" ] || { echo "ERROR: no bin/cue in audio download"; exit 1; }
:; split_bincue "$srccue" "$srcbin" "$AUDIO_DIR"
:; echo "Audio split complete -> $AUDIO_DIR"
:; # (merge call appended in Task 5)
:; exit
```

- [ ] **Step 6: Update `CONFIG.ME` (rename URL, add audio dir + disc name)**

In `tools/assets/CONFIG.ME`: rename `URL=` to `AUDIO_URL=` (same value) and append:
```
AUDIO_DIR=./audio
DISC_NAME=mojozork
```

- [ ] **Step 7: Rewrite the Windows block of `music.bat` (split via bundled dd)**

Replace the `@ECHO OFF` block so Windows mirrors the bash split. Use PowerShell to parse the cue and call the bundled `dd.exe` per track:
```bat
@ECHO OFF
REM === Windows Execution Block ===
SETLOCAL
CD /D "%~dp0"
FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="AUDIO_URL" SET "AUDIO_URL=%%B"
    IF "%%A"=="AUDIO_DIR" SET "AUDIO_DIR=%%B"
)
IF NOT DEFINED AUDIO_DIR SET "AUDIO_DIR=.\audio"
IF NOT EXIST "%AUDIO_DIR%" MKDIR "%AUDIO_DIR%"
SET "TMP_IMG=%TEMP%\mzaudio"
IF EXIST "%TMP_IMG%" RMDIR /S /Q "%TMP_IMG%"
MKDIR "%TMP_IMG%"
ECHO Downloading audio image: %AUDIO_URL%
curl -L -o "%TEMP%\mzaudio.zip" "%AUDIO_URL%"
powershell -NoProfile -Command "Expand-Archive -Path '%TEMP%\mzaudio.zip' -DestinationPath '%TMP_IMG%' -Force"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\split.ps1" -ImgDir "%TMP_IMG%" -OutDir "%AUDIO_DIR%" -Dd ".\bin\win\dd.exe"
ECHO Audio split complete -^> %AUDIO_DIR%
ENDLOCAL
GOTO :eof
```
Create `tools/assets/lib/split.ps1`:
```powershell
param([string]$ImgDir,[string]$OutDir,[string]$Dd)
$cue = Get-ChildItem -Path $ImgDir -Recurse -Filter *.cue | Select-Object -First 1
$bin = Get-ChildItem -Path $ImgDir -Recurse -Filter *.bin | Select-Object -First 1
if (-not $cue -or -not $bin) { Write-Error "no bin/cue in audio download"; exit 1 }
$total = (Get-Item $bin.FullName).Length
function MsfToFrames($m){ $p=$m -split ':'; return ((([int]$p[0])*60+[int]$p[1])*75+[int]$p[2]) }
$tracks=@(); $tnum=$null; $ttype=$null
foreach ($line in Get-Content $cue.FullName) {
  if ($line -match 'TRACK\s+0*(\d+)\s+(\w+/?\w*)') { $tnum=[int]$Matches[1]; $ttype=$Matches[2] }
  elseif ($line -match 'INDEX\s+01\s+(\d{2}:\d{2}:\d{2})') {
    $tracks += [pscustomobject]@{ Num=$tnum; Type=$ttype; Start=(MsfToFrames $Matches[1]) }
  }
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
for ($i=0; $i -lt $tracks.Count; $i++) {
  if ($tracks[$i].Type -notlike 'AUDIO*') { continue }
  $start=$tracks[$i].Start
  $end = if ($i+1 -lt $tracks.Count) { $tracks[$i+1].Start } else { [int]($total/2352) }
  $count=$end-$start
  $name = "track{0:D2}.bin" -f $tracks[$i].Num
  & $Dd "if=$($bin.FullName)" "of=$OutDir\$name" bs=2352 skip=$start count=$count status=none
  Write-Host "  split -> $name ($count sectors)"
}
```

- [ ] **Step 8: Commit**

```bash
git add tools/assets/lib/audio.sh tools/assets/music.bat tools/assets/CONFIG.ME tools/assets/lib/split.ps1 tools/assets/tests/test_split.sh
git commit -m "music.bat: download + split source bin/cue into per-track audio bins"
```

---

## Task 5: `merge_disc` + `music.bat` — merge `./game` + `./audio` into `./output`

**Files:**
- Modify: `tools/assets/lib/audio.sh` (add `merge_disc` function)
- Modify: `tools/assets/music.bat` (bash block: call `merge_disc` after split; Windows block: `merge.ps1`)
- Create: `tools/assets/lib/merge.ps1`
- Modify: `tools/assets/CONFIG.ME` (add `GAME_DIR=./game`, `OUTPUT_DIR=./output`)
- Create: `tools/assets/tests/test_merge.sh`

**Interfaces:**
- Consumes: `cuelib.sh` (via `lib/audio.sh`).
- Produces: `merge_disc <game_dir> <audio_dir> <out_dir>` (in `lib/audio.sh`) — copies the single `*.bin`/`*.cue` from `game_dir` and every `track*.bin` from `audio_dir` into `out_dir`, then writes `out_dir/<cuebase>.cue` = track-1 block copied verbatim from the game cue + one regenerated `FILE/TRACK NN AUDIO` block per audio bin (sorted), numbering from 02, `PREGAP 00:02:00` on the first audio track.

- [ ] **Step 1: Write the failing test**

Create `tools/assets/tests/test_merge.sh`:
```bash
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash tools/assets/tests/test_merge.sh`
Expected: FAIL — `merge_disc: command not found`.

- [ ] **Step 3: Add `merge_disc` to `tools/assets/lib/audio.sh`**

Append this function to `lib/audio.sh` (plain bash — this file is sourced, not a `.bat`):
```bash
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
```

> The awk rule keeps every line until the 2nd `FILE` appears — i.e. exactly the track-1 block (single-FILE game cue). This is the "track 1 verbatim" preservation.

- [ ] **Step 4: Run test to verify it passes**

Run: `bash tools/assets/tests/test_merge.sh`
Expected: `ok: files copied`, `ok: cue matches`; exit 0.

- [ ] **Step 5: Wire the merge call into the bash block of `music.bat`**

Replace the placeholder line `:; # (merge call appended in Task 5)` (added in Task 4) with:
```bash
:; GAME_DIR=$(cfg GAME_DIR); GAME_DIR=${GAME_DIR:-./game}
:; OUTPUT_DIR=$(cfg OUTPUT_DIR); OUTPUT_DIR=${OUTPUT_DIR:-./output}
:; merge_disc "$GAME_DIR" "$AUDIO_DIR" "$OUTPUT_DIR"
```
(Keep the `:; exit` line last.)

- [ ] **Step 6: Add the Windows merge (mirror in `merge.ps1`) and wire it**

Append to the Windows block of `music.bat`, after the split call:
```bat
FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="GAME_DIR" SET "GAME_DIR=%%B"
    IF "%%A"=="OUTPUT_DIR" SET "OUTPUT_DIR=%%B"
)
IF NOT DEFINED GAME_DIR SET "GAME_DIR=.\game"
IF NOT DEFINED OUTPUT_DIR SET "OUTPUT_DIR=.\output"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\merge.ps1" -GameDir "%GAME_DIR%" -AudioDir "%AUDIO_DIR%" -OutDir "%OUTPUT_DIR%"
```
Create `tools/assets/lib/merge.ps1`:
```powershell
param([string]$GameDir,[string]$AudioDir,[string]$OutDir)
$gcue = Get-ChildItem -Path $GameDir -Filter *.cue | Select-Object -First 1
$gbin = Get-ChildItem -Path $GameDir -Filter *.bin | Select-Object -First 1
if (-not $gcue -or -not $gbin) { Write-Error "need one bin+cue in $GameDir"; exit 1 }
$audios = Get-ChildItem -Path $AudioDir -Filter track*.bin | Sort-Object Name
if ($audios.Count -eq 0) { Write-Error "no audio bins in $AudioDir"; exit 1 }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item $gbin.FullName $OutDir; $audios | ForEach-Object { Copy-Item $_.FullName $OutDir }
$outcue = Join-Path $OutDir ($gcue.BaseName + '.cue')
# Track 1 verbatim: keep lines until the 2nd FILE.
$lines = Get-Content $gcue.FullName; $n=0; $keep=@()
foreach ($l in $lines){ if ($l -match '^FILE'){$n++}; if ($n -ge 2){break}; $keep += $l }
$keep | Set-Content $outcue
$tn=2; $first=$true
foreach ($a in $audios) {
  Add-Content $outcue ('FILE "{0}" BINARY' -f $a.Name)
  Add-Content $outcue ('  TRACK {0:D2} AUDIO' -f $tn)
  if ($first) { Add-Content $outcue '    PREGAP 00:02:00'; $first=$false }
  Add-Content $outcue '    INDEX 01 00:00:00'
  $tn++
}
Write-Host "Merged disc -> $outcue"
```

- [ ] **Step 7: Update `CONFIG.ME` and commit**

Append to `tools/assets/CONFIG.ME`:
```
GAME_DIR=./game
OUTPUT_DIR=./output
```
Commit:
```bash
git add tools/assets/lib/audio.sh tools/assets/music.bat tools/assets/CONFIG.ME tools/assets/lib/merge.ps1 tools/assets/tests/test_merge.sh
git commit -m "music.bat: merge game+audio into output disc with rebuilt multi-FILE cue"
```

---

## Task 6: `lib/inject.sh` + `games.bat` — inject games into the base ISO with IP.BIN restore

**Files:**
- Create: `tools/assets/lib/inject.sh` (defines `inject_games`; sources `cuelib.sh`)
- Modify: `tools/assets/games.bat` (after the existing download, source `lib/inject.sh` and call `inject_games`; Windows block calls `inject.ps1`)
- Create: `tools/assets/lib/inject.ps1`
- Modify: `tools/assets/CONFIG.ME` (add `BASE_ISO=./base/mojozork.iso`)
- Create: `tools/assets/tests/test_inject.sh`

**Interfaces:**
- Consumes: `cuelib.sh` (`resolve_tool`), bundled/system `xorriso`, `iso2raw`, `dd`.
- Produces: `inject_games <base.iso> <games_dir> <out_dir> <disc_name>` (in `lib/inject.sh`) — rips IP.BIN (32768 bytes), xorriso-maps `games_dir` into `/Z3`, restores IP.BIN, `iso2raw`→`<out_dir>/<disc_name>.bin`, writes `<out_dir>/<disc_name>.cue` (track-1 block). Asserts the restored ISO's first 32768 bytes equal the base's.

- [ ] **Step 1: Write the failing test (IP.BIN preservation on a real base ISO)**

Create `tools/assets/tests/test_inject.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
. lib/inject.sh    # sources cuelib.sh; defines inject_games. No main runs.

# Requires a real base ISO + xorriso + iso2raw. Skip cleanly if unavailable.
BASE="../../saturn/BuildDrop/mojozork.iso"
if [ ! -f "$BASE" ] || ! resolve_tool xorriso >/dev/null 2>&1; then
  echo "SKIP: base ISO or xorriso unavailable"; exit 0
fi
work=$(mktemp -d); mkdir -p "$work/games" "$work/out"
printf 'ZTEST' > "$work/games/TESTGAME.Z3"

inject_games "$BASE" "$work/games" "$work/out" "mojozork"

fail=0
# 1) IP.BIN (first 32768 bytes of ISO) preserved — compare to base.
head -c 32768 "$BASE" > "$work/base_ip"
head -c 32768 "$work/out/mojozork_injected.iso" > "$work/out_ip" 2>/dev/null || true
if [ -f "$work/out/mojozork_injected.iso" ] && cmp -s "$work/base_ip" "$work/out_ip"; then
  echo "ok: IP.BIN preserved"; else echo "FAIL: IP.BIN changed"; fail=1; fi
# 2) Raw track produced and cue emitted.
[ -f "$work/out/mojozork.bin" ] && echo "ok: raw bin" || { echo "FAIL: no raw bin"; fail=1; }
grep -q 'TRACK 01 MODE1/2352' "$work/out/mojozork.cue" && echo "ok: cue" || { echo "FAIL: cue"; fail=1; }
# 3) Injected game present in the ISO listing.
resolve_tool xorriso >/dev/null && "$(resolve_tool xorriso)" -indev "$work/out/mojozork_injected.iso" -find /Z3 2>/dev/null | grep -qi 'TESTGAME' \
  && echo "ok: game injected" || { echo "FAIL: game not in ISO"; fail=1; }
exit $fail
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash tools/assets/tests/test_inject.sh`
Expected: FAIL — `lib/inject.sh: No such file or directory` (once `lib/inject.sh` exists but before you have xorriso, the guard prints `SKIP` and exits 0; so obtain xorriso via Task 2 first to get a real pass/fail on the dev box, where a base ISO exists in `saturn/BuildDrop`).

- [ ] **Step 3: Create `tools/assets/lib/inject.sh` with `inject_games`**

```bash
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
  dd if="$base" of="$out/ip.bin" bs=2048 count=16 status=none
  # 2) inject game files into /Z3 (rewrites the ISO, clobbering the system area)
  "$XORRISO" -indev "$base" -outdev "$inj" -map "$gdir" /Z3 -commit >/dev/null 2>&1
  # 3) restore IP.BIN onto the front, in ISO space, before raw conversion
  dd if="$out/ip.bin" of="$inj" bs=2048 count=16 conv=notrunc status=none
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `bash tools/assets/tests/test_inject.sh`
Expected: `ok: IP.BIN preserved`, `ok: raw bin`, `ok: cue`, `ok: game injected`; exit 0.

- [ ] **Step 5: Wire the inject call into the bash block of `games.bat`**

The existing bash block downloads games into a `Z3/` directory. After that (before `:; exit`), source the lib and call `inject_games`:
```bash
:; . lib/inject.sh
:; cfg() { grep -m1 "^$1=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r'; }
:; BASE_ISO=$(cfg BASE_ISO); GAME_DIR=$(cfg GAME_DIR); DISC_NAME=$(cfg DISC_NAME)
:; inject_games "${BASE_ISO:-./base/mojozork.iso}" "Z3" "${GAME_DIR:-./game}" "${DISC_NAME:-mojozork}"
```
(If `cfg` is already defined earlier in the bash block, don't redefine it.)

- [ ] **Step 6: Mirror the inject stage in the Windows block of `games.bat`**

After the existing Windows download section, add a call to a helper `inject.ps1` (rip via `dd.exe`, xorriso via `xorriso.exe`, iso2raw via `iso2raw.exe`), using the same 32768-byte rip/restore + cmp check. Create `tools/assets/lib/inject.ps1`:
```powershell
param([string]$BaseIso,[string]$GamesDir,[string]$OutDir,[string]$Name,
      [string]$Dd,[string]$Xorriso,[string]$Iso2raw)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$inj = Join-Path $OutDir "$Name`_injected.iso"
& $Dd "if=$BaseIso" "of=$OutDir\ip.bin" bs=2048 count=16 status=none
& $Xorriso -indev $BaseIso -outdev $inj -map $GamesDir /Z3 -commit 2>$null
& $Dd "if=$OutDir\ip.bin" "of=$inj" bs=2048 count=16 conv=notrunc status=none
$a=[System.IO.File]::ReadAllBytes($BaseIso)[0..32767]
$b=[System.IO.File]::ReadAllBytes($inj)[0..32767]
if (Compare-Object $a $b) { Write-Error "IP.BIN not preserved"; exit 1 }
& $Iso2raw $inj -o "$OutDir\$Name.bin"
"FILE `"$Name.bin`" BINARY","  TRACK 01 MODE1/2352","    INDEX 01 00:00:00" | Set-Content "$OutDir\$Name.cue"
Write-Host "Injected games -> $OutDir\$Name.bin"
```
Wire it in the Windows block:
```bat
FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="BASE_ISO" SET "BASE_ISO=%%B"
    IF "%%A"=="GAME_DIR" SET "GAME_DIR=%%B"
    IF "%%A"=="DISC_NAME" SET "DISC_NAME=%%B"
)
IF NOT DEFINED BASE_ISO SET "BASE_ISO=.\base\mojozork.iso"
IF NOT DEFINED GAME_DIR SET "GAME_DIR=.\game"
IF NOT DEFINED DISC_NAME SET "DISC_NAME=mojozork"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\inject.ps1" -BaseIso "%BASE_ISO%" -GamesDir "Z3" -OutDir "%GAME_DIR%" -Name "%DISC_NAME%" -Dd ".\bin\win\dd.exe" -Xorriso ".\bin\win\xorriso.exe" -Iso2raw ".\bin\win\iso2raw.exe"
```

- [ ] **Step 7: Update `CONFIG.ME` and commit**

Append to `tools/assets/CONFIG.ME`:
```
BASE_ISO=./base/mojozork.iso
```
Commit:
```bash
git add tools/assets/lib/inject.sh tools/assets/games.bat tools/assets/CONFIG.ME tools/assets/lib/inject.ps1 tools/assets/tests/test_inject.sh
git commit -m "games.bat: inject games into base ISO with IP.BIN rip/restore + iso2raw"
```

---

## Task 7: Kit scaffolding, README, and `download-all` verification

**Files:**
- Create: `tools/assets/game/.gitkeep`, `tools/assets/audio/.gitkeep`, `tools/assets/output/.gitkeep`
- Create: `tools/assets/README.md`
- Modify: `tools/assets/.gitignore` (ignore downloaded/produced artifacts, keep `.gitkeep`)
- Verify: `tools/assets/download-all.bat` runs games→music in order (no code change expected)

**Interfaces:**
- Produces: a self-contained kit directory whose only tracked contents are scripts, `lib/`, `bin/`, `CONFIG.ME`, `VERSIONS.ndjson`, `README.md`, and empty working folders.

- [ ] **Step 1: Create working-folder placeholders**

```bash
mkdir -p tools/assets/game tools/assets/audio tools/assets/output
touch tools/assets/game/.gitkeep tools/assets/audio/.gitkeep tools/assets/output/.gitkeep
```

- [ ] **Step 2: Add `tools/assets/.gitignore`**

```gitignore
# Downloaded / produced artifacts — never committed.
/game/*
/audio/*
/output/*
/base/*
/Z3/
*.zip
!*/.gitkeep
```

- [ ] **Step 3: Write `tools/assets/README.md`**

```markdown
# Zork — Infocom Collection: asset kit

This kit builds a full Saturn disc (all Infocom games + CD-DA audio) from the
open-source base image. The three Zork games are open-sourced; the rest of the
Infocom library and the CD audio are downloaded by these scripts, not shipped.

## Build

1. Ensure `base/<disc>.iso` is present (shipped with the kit).
2. Run `download-all.bat` (double-click on Windows, or `bash download-all.bat`).
   - `games.bat` downloads the Infocom set and injects it into the base ISO,
     preserving the Saturn IP.BIN boot header, producing `game/<disc>.bin`+`.cue`.
   - `music.bat` downloads the CD-DA image, splits its tracks into `audio/`,
     and merges the final burnable disc into `output/`.
3. Burn or mount `output/<disc>.cue`.

Linux/macOS use system `xorriso`/`dd`/`iso2raw`; Windows uses the bundled copies
in `bin/win/` (see `bin/README.md` for licenses).
```

- [ ] **Step 4: Verify download-all ordering (static check)**

Run:
```bash
grep -n 'games.bat\|music.bat' tools/assets/download-all.bat
```
Expected: `games.bat` referenced before `music.bat` in both the bash and batch blocks (games must run first — music merges from `./game`). No code change if already ordered; otherwise reorder.

- [ ] **Step 5: Commit**

```bash
git add tools/assets/game/.gitkeep tools/assets/audio/.gitkeep tools/assets/output/.gitkeep \
        tools/assets/.gitignore tools/assets/README.md
git commit -m "Add asset-kit scaffolding, .gitignore, and README"
```

---

## Task 8: New `full-image.yml` workflow

**Files:**
- Create: `.github/workflows/full-image.yml`

**Interfaces:**
- Consumes: `games.bat`, `music.bat`, the SDK build, `saturn/BuildDrop/mojozork.iso` as the base ISO.
- Produces: a workflow artifact `zaturn-complete-<sha>` containing `tools/assets/output/`.

- [ ] **Step 1: Write the workflow**

Create `.github/workflows/full-image.yml`:
```yaml
name: Build full disc image (games + audio)

# Manual only: this build downloads copyrighted games + audio and uploads the
# result as a workflow ARTIFACT. It is never attached to a public release.
on:
  workflow_dispatch:

permissions:
  contents: read

jobs:
  full-image:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install host tools
        run: sudo apt-get update -qq && sudo apt-get install -y -qq xorriso wget unzip zip curl

      - name: Cache SaturnRingLib toolchain
        id: toolchain
        uses: actions/cache@v4
        with:
          path: |
            SaturnRingLib/Compiler
            SaturnRingLib/tools/bin
          key: srl-toolchain-gcc14.2.0-iso2raw0.2.2

      - name: Fetch SH-2 toolchain + iso2raw
        if: steps.toolchain.outputs.cache-hit != 'true'
        working-directory: SaturnRingLib
        run: |
          set -e
          bash tools/scripts/getcompiler.sh 14.2.0
          bash tools/scripts/getiso2raw.sh v0.2.2

      - name: Build base Saturn disc
        working-directory: saturn
        run: |
          set -e
          export SRL_INSTALL_ROOT=../SaturnRingLib
          bash ../SaturnRingLib/tools/scripts/make.sh release ../SaturnRingLib/Compiler
          ls -la BuildDrop

      - name: Stage base ISO for the asset kit
        run: |
          set -e
          mkdir -p tools/assets/base
          cp saturn/BuildDrop/mojozork.iso tools/assets/base/mojozork.iso

      - name: Download games + inject
        working-directory: tools/assets
        run: bash games.bat

      - name: Download audio + split + merge
        working-directory: tools/assets
        run: bash music.bat

      - name: Sanity-check output cue
        working-directory: tools/assets
        run: |
          set -e
          cue=$(ls output/*.cue)
          echo "=== $cue ==="; cat "$cue"
          grep -q 'TRACK 01 MODE1/2352' "$cue"
          audio=$(grep -c 'TRACK .* AUDIO' "$cue")
          echo "audio tracks: $audio"
          [ "$audio" -ge 1 ]

      - name: Upload full disc artifact
        uses: actions/upload-artifact@v4
        with:
          name: zaturn-complete-${{ github.sha }}
          path: tools/assets/output
```

- [ ] **Step 2: Validate the workflow YAML**

Run:
```bash
python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/full-image.yml')); print('yaml ok')"
```
Expected: `yaml ok`.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/full-image.yml
git commit -m "Add full-image workflow: run asset scripts, upload full disc artifact"
```

---

## Task 9: Update `release.yml` to ship the OSS kit

**Files:**
- Modify: `.github/workflows/release.yml` (package step: add the build-it-yourself kit zip alongside the quick-play disc)

**Interfaces:**
- Consumes: the makefile build output (`mojozork.iso/.bin/.cue`), `tools/assets/` (scripts, `lib/`, `bin/`, config, README, empty folders).
- Produces: two release assets — the quick-play data disc zip (existing) and a `...-kit.zip` (base ISO + scripts + tools + folders). No scripts are run in CI.

- [ ] **Step 1: Add a kit-packaging step after the existing package step**

In `.github/workflows/release.yml`, after the current "Package release zip" step, add:
```yaml
      - name: Package build-it-yourself kit
        id: kit
        run: |
          set -e
          if [ "${GITHUB_REF_TYPE}" = "tag" ]; then VER="${GITHUB_REF_NAME}"; else VER="${GITHUB_SHA::7}"; fi
          STAGE=$(mktemp -d)
          KIT="$STAGE/zaturn-asset-kit"
          mkdir -p "$KIT/base"
          # Base ISO = injection input for games.bat (Zork 1/2/3 baked in).
          cp saturn/BuildDrop/mojozork.iso "$KIT/base/mojozork.iso"
          # Scripts + libs + bundled tools + config + docs + empty working dirs.
          cp -r tools/assets/. "$KIT/"
          # Ensure working dirs exist but are empty save for .gitkeep.
          for d in game audio output; do mkdir -p "$KIT/$d"; find "$KIT/$d" -type f ! -name .gitkeep -delete; done
          NAME="zaturn-asset-kit-${VER}"
          (cd "$STAGE" && zip -rq "$RUNNER_TEMP/${NAME}.zip" .)
          echo "zip_path=$RUNNER_TEMP/${NAME}.zip" >> "$GITHUB_OUTPUT"
          echo "zip_name=${NAME}.zip"              >> "$GITHUB_OUTPUT"
```

- [ ] **Step 2: Upload the kit on manual runs (artifact)**

After the existing "Upload as workflow artifact (manual runs)" step, add:
```yaml
      - name: Upload kit as workflow artifact (manual runs)
        if: github.event_name == 'workflow_dispatch'
        uses: actions/upload-artifact@v4
        with:
          name: ${{ steps.kit.outputs.zip_name }}
          path: ${{ steps.kit.outputs.zip_path }}
```

- [ ] **Step 3: Attach the kit on tag runs (release)**

In the existing "Attach to GitHub release (tag runs)" step, add a second upload line after the existing `gh release upload`:
```bash
          gh release upload "$TAG" "${{ steps.kit.outputs.zip_path }}" --clobber
```

- [ ] **Step 4: Validate the workflow YAML**

Run:
```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/release.yml')); print('yaml ok')"
```
Expected: `yaml ok`.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "release.yml: also package the open-source build-it-yourself kit"
```

---

## Self-Review Notes

- **Spec coverage:** games.bat inject + IP.BIN (Task 6) ✓; music.bat split (Task 4) + merge/cue (Task 5) ✓; untrack Zork-only (Task 1) ✓; extract bundled tools + licenses (Task 2) ✓; OSS release kit (Task 9) ✓; full-image workflow (Task 8) ✓; scaffolding/README (Task 7) ✓; cuelib + per-OS tool resolution (Task 3) ✓.
- **O1 (bundle vs fetch binaries):** resolved to **bundle** — the user supplied the zips in `tools/assets/bin/`; Task 2 extracts them.
- **O2 (IP.BIN via dd vs xorriso `-system_area`):** resolved to **dd rip/restore + cmp verification** (Task 6); the `cmp` gate makes any future switch to `-boot_image any system_area=` safe to validate.
- **xorriso availability:** bundled for Windows; on macOS/Linux `resolve_tool xorriso` errors with `brew install xorriso` / `sudo apt-get install xorriso` and returns 1 (Task 3); the full-image CI installs it via apt (Task 8).
- **Type/name consistency:** `split_bincue`, `merge_disc`, `inject_games`, `msf_to_frames`, `resolve_tool`, `file_size`, `platform_subdir` are used with identical names across tasks and their PS mirrors.
- **Ordering note:** Task 2 (extract tools) precedes Task 3 (its `resolve_tool` test needs bundled `iso2raw` present) and Task 6 (injection needs xorriso/iso2raw). Tasks 4/5 (pure split/merge) need no xorriso.
```
