:; # === Linux & macOS Execution Block ===
:; set -euo pipefail
:; cd "$(dirname "$0")"
:;
:; # 0. Load shared processing functions (process_audio / process_cue)
:; . lib/music.sh
:;
:; # 1. Parse Config
:; CFG_FILE="${1:-CONFIG.ME}"
:; [ -f "$CFG_FILE" ] || { echo "music: config not found: $CFG_FILE" >&2; exit 1; }
:; echo "music: using config $CFG_FILE"
:; cfg() { grep -m1 "^$1=" "$CFG_FILE" | cut -d'=' -f2- | tr -d '\r'; }
:; AUDIO_URL=$(cfg AUDIO_URL)
:; DISC_NAME=$(cfg DISC_NAME)
:; OUTPUT_DIR=$(cfg OUTPUT_DIR); OUTPUT_DIR=${OUTPUT_DIR:-./Zaturn - Complete (USA) (Netlink Edition)}
:;
:; # 2. Download and Extract Audio
:; tmp=$(mktemp -d)
:; echo "Downloading audio files: $AUDIO_URL"
:; curl -L -o "$tmp/audio.zip" "$AUDIO_URL"
:; unzip -qo "$tmp/audio.zip" -d "$tmp/img"
:;
:; # 3. Setup Final Output Directory -- OUTPUT_DIR *is* the disc folder; DISC_NAME
:; # only names the files inside it (matching what games.bat writes there).
:; FINAL_OUT="$OUTPUT_DIR"
:; mkdir -p "$FINAL_OUT"
:; echo "Processing files into -> $FINAL_OUT"
:;
:; # 4. Execute new logic
:; promote_game_track "$FINAL_OUT" "$DISC_NAME"
:; process_audio "$tmp/img" "$FINAL_OUT" "$DISC_NAME"
:; process_cue "$tmp/img" "$FINAL_OUT" "$DISC_NAME"
:;
:; # 5. Cleanup temp
:; rm -rf "$tmp"
:; echo "Process complete!"
:; exit

@ECHO OFF
REM === Windows Execution Block ===
SETLOCAL
CD /D "%~dp0"

REM Parse all configuration variables
IF "%~1"=="" (SET "CFG_FILE=CONFIG.ME") ELSE (SET "CFG_FILE=%~1")
IF NOT EXIST "%CFG_FILE%" ( ECHO ERROR: config not found: %CFG_FILE% & EXIT /B 1 )
ECHO Using config: %CFG_FILE%
FOR /F "usebackq tokens=1,* delims==" %%A IN ("%CFG_FILE%") DO (
    IF "%%A"=="AUDIO_URL" SET "AUDIO_URL=%%B"
    IF "%%A"=="OUTPUT_DIR" SET "OUTPUT_DIR=%%B"
    IF "%%A"=="DISC_NAME" SET "DISC_NAME=%%B"
)

IF NOT DEFINED OUTPUT_DIR SET "OUTPUT_DIR=./Zaturn - Complete (USA) (Netlink Edition)"

SET "TMP_IMG=%TEMP%\mzaudio"
IF EXIST "%TMP_IMG%" RMDIR /S /Q "%TMP_IMG%"
MKDIR "%TMP_IMG%"

ECHO Downloading audio files: %AUDIO_URL%
curl -L -o "%TEMP%\mzaudio.zip" "%AUDIO_URL%"
IF ERRORLEVEL 1 ( ECHO ERROR: audio download failed & EXIT /B 1 )

powershell -NoProfile -Command "Expand-Archive -Path '%TEMP%\mzaudio.zip' -DestinationPath '%TMP_IMG%' -Force"
IF ERRORLEVEL 1 ( ECHO ERROR: failed to extract audio zip & EXIT /B 1 )

ECHO Processing files and merging directories...
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\music.ps1" -CueMusicDir "%TMP_IMG%" -OutDir "%OUTPUT_DIR%" -DiscName "%DISC_NAME%"
IF ERRORLEVEL 1 ( ECHO ERROR: disc processing failed & EXIT /B 1 )

ECHO Process complete -^> %OUTPUT_DIR%
ENDLOCAL
GOTO :eof