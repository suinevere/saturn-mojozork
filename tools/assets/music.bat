:; # === Linux & macOS Execution Block ===
:; # Read properties and strip any Windows carriage returns (\r) to prevent variable corruption
:; URL=$(grep -m 1 "^URL=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r')
:; DEST=$(grep -m 1 "^DEST=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r')
:; echo "Downloading from: $URL"
:; mkdir -p "$DEST"
:; curl -L -o temp_download.zip "$URL"
:; unzip -qo temp_download.zip -d "$DEST"
:; rm temp_download.zip
:; echo "Extraction complete."
:; exit

@ECHO OFF
REM === Windows Execution Block ===
SETLOCAL

REM Read properties from CONFIG.ME safely
FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="URL" SET "URL=%%B"
    IF "%%A"=="DEST" SET "DEST=%%B"
)

ECHO Downloading from: %URL%
IF NOT EXIST "%DEST%" MKDIR "%DEST%"

REM Windows 10+ includes curl natively
curl -L -o temp_download.zip "%URL%"

REM Use built-in PowerShell cmdlet to handle zip extraction without needing 7-Zip
powershell -NoProfile -Command "Expand-Archive -Path 'temp_download.zip' -DestinationPath '%DEST%' -Force"

DEL temp_download.zip
ECHO Extraction complete.

ENDLOCAL
GOTO :eof