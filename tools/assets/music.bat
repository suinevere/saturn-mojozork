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
:; GAME_DIR=$(cfg GAME_DIR); GAME_DIR=${GAME_DIR:-./game}
:; OUTPUT_DIR=$(cfg OUTPUT_DIR); OUTPUT_DIR=${OUTPUT_DIR:-./output}
:; merge_disc "$GAME_DIR" "$AUDIO_DIR" "$OUTPUT_DIR"
:; exit

@ECHO OFF
REM === Windows Execution Block ===
SETLOCAL
CD /D "%~dp0"

REM Parse all configuration variables
FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="AUDIO_URL" SET "AUDIO_URL=%%B"
    IF "%%A"=="GAME_DIR" SET "GAME_DIR=%%B"
    IF "%%A"=="OUTPUT_DIR" SET "OUTPUT_DIR=%%B"
)

IF NOT DEFINED GAME_DIR SET "GAME_DIR=.\game"
IF NOT DEFINED OUTPUT_DIR SET "OUTPUT_DIR=.\output"

SET "TMP_IMG=%TEMP%\mzaudio"

ECHO Processing files and merging directories...
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\split.ps1" -BinDir "%GAME_DIR%" -CueMusicDir "%TMP_IMG%" -OutDir "%OUTPUT_DIR%"
IF ERRORLEVEL 1 ( ECHO ERROR: disc processing failed & EXIT /B 1 )

ECHO Process complete -^> %OUTPUT_DIR%
ENDLOCAL
GOTO :eof