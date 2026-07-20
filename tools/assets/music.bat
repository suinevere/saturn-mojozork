:; # === Linux & macOS Execution Block ===
:; set -euo pipefail
:; cd "$(dirname "$0")"
:;
:; # 1. Parse Config
:; cfg() { grep -m1 "^$1=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r'; }
:; AUDIO_URL=$(cfg AUDIO_URL)
:; GAME_DIR=$(cfg GAME_DIR); GAME_DIR=${GAME_DIR:-./game}
:; OUTPUT_DIR=$(cfg OUTPUT_DIR); OUTPUT_DIR=${OUTPUT_DIR:-./output}
:;
:; # 2. Download and Extract Audio
:; tmp=$(mktemp -d)
:; echo "Downloading audio files: $AUDIO_URL"
:; curl -L -o "$tmp/audio.zip" "$AUDIO_URL"
:; unzip -qo "$tmp/audio.zip" -d "$tmp/img"
:;
:; # 3. Setup Final Output Directory
:; BASE_NAME="Zaturn - Complete (USA)"
:; FINAL_OUT="$OUTPUT_DIR/$BASE_NAME"
:; mkdir -p "$FINAL_OUT"
:; echo "Processing files into -> $FINAL_OUT"
:;
:; # 4. Execute new logic
:; process_bin "$GAME_DIR" "$FINAL_OUT" "$BASE_NAME"
:; process_audio "$tmp/img" "$FINAL_OUT" "$BASE_NAME"
:; process_cue "$tmp/img" "$FINAL_OUT" "$BASE_NAME"
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