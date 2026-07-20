:; # === Linux & macOS Execution Block ===
:; set -euo pipefail
:; cd "$(dirname "$0")"
:; . lib/audio.sh
:; cfg() { grep -m1 "^$1=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r'; }
:; AUDIO_URL=$(cfg AUDIO_URL); AUDIO_DIR=$(cfg AUDIO_DIR); AUDIO_DIR=${AUDIO_DIR:-./audio}
:; tmp=$(mktemp -d)
:; echo "Downloading audio image: $AUDIO_URL"
:; unzip -qo "$tmp/audio.zip" -d "$tmp/img" || { echo "ERROR: Downloaded file is not a valid zip. Check the AUDIO_URL."; exit 1; }
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
IF ERRORLEVEL 1 ( ECHO ERROR: audio download failed & EXIT /B 1 )
powershell -NoProfile -Command "Expand-Archive -Path '%TEMP%\mzaudio.zip' -DestinationPath '%TMP_IMG%' -Force"
IF ERRORLEVEL 1 ( ECHO ERROR: failed to extract audio zip & EXIT /B 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\split.ps1" -ImgDir "%TMP_IMG%" -OutDir "%AUDIO_DIR%" -Dd ".\bin\win\dd.exe"
IF ERRORLEVEL 1 ( ECHO ERROR: audio split failed & EXIT /B 1 )
ECHO Audio split complete -^> %AUDIO_DIR%
FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="GAME_DIR" SET "GAME_DIR=%%B"
    IF "%%A"=="OUTPUT_DIR" SET "OUTPUT_DIR=%%B"
)
IF NOT DEFINED GAME_DIR SET "GAME_DIR=.\game"
IF NOT DEFINED OUTPUT_DIR SET "OUTPUT_DIR=.\output"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\merge.ps1" -GameDir "%GAME_DIR%" -AudioDir "%AUDIO_DIR%" -OutDir "%OUTPUT_DIR%"
IF ERRORLEVEL 1 ( ECHO ERROR: disc merge failed & EXIT /B 1 )
ENDLOCAL
GOTO :eof