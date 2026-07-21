:; # === Linux & macOS Execution Block ===
:; # Read properties and strip any Windows carriage returns (\r)
:; GAMES_URL=$(grep -m 1 "^GAMES_URL=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r')
:; LURKING_URL=$(grep -m 1 "^LURKING_URL=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r')
:; ADVENT_URL=$(grep -m 1 "^ADVENT_URL=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r')
:; DEST=$(grep -m 1 "^DEST=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r')
:; TEMP_DIR="${DEST}_games_temp"
:;
:; mkdir -p "$TEMP_DIR" "Z3"
:; echo "Downloading games archive from: $GAMES_URL"
:; curl -L -o temp_games.zip "$GAMES_URL"
:; unzip -qo temp_games.zip -d "$TEMP_DIR"
:; rm temp_games.zip
:;
:; echo "Parsing VERSIONS.ndjson and mapping files..."
:; if [ -f "VERSIONS.ndjson" ]; then
:;     while IFS= read -r line; do
:;         [[ -z "$line" ]] && continue
:;         # Extract values using regex to handle arbitrary whitespace in the NDJSON
:;         version=$(echo "$line" | grep -o '"version"\s*:\s*[0-9]*' | grep -o '[0-9]*')
:;         release=$(echo "$line" | grep -o '"release"\s*:\s*"[^"]*"' | cut -d'"' -f4)
:;         title=$(echo "$line" | grep -o '"title"\s*:\s*"[^"]*"' | cut -d'"' -f4)
:;
:;         # Find matching file (using both version and release strings for precision)
:;         match=$(find "$TEMP_DIR" -type f -name "*${release}*" -name "*${version}*" -print -quit)
:;         if [ -n "$match" ]; then
:;             mv "$match" "Z3/$title"
:;             echo " -> Matched and moved: $title"
:;         fi
:;     done < VERSIONS.ndjson
:; fi
:;
:; rm -rf "$TEMP_DIR"
:; echo "Downloading LURKING.BLB into Z3..."
:; curl -L -o "Z3/LURKING.BLB" "$LURKING_URL"
:;
:; echo "Downloading ADVENT.Z3 into Z3..."
:; curl -L -o "Z3/ADVENT.Z3" "$ADVENT_URL"
:;
:; echo "Complete."
:;
:; . lib/merge.sh
:; cfg() { grep -m1 "^$1=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r'; }
:; BASE_ISO=$(cfg BASE_ISO); OUTPUT_DIR=$(cfg OUTPUT_DIR); DISC_NAME=$(cfg DISC_NAME)
:; BASE_ISO=${BASE_ISO:-./Zaturn (USA) (Netlink Edition)/zaturn.iso}
:; DISC_NAME=${DISC_NAME:-Zaturn - Complete (USA) (Netlink Edition)}
:; OUTPUT_DIR=${OUTPUT_DIR:-./$DISC_NAME}
:; inject_games "$BASE_ISO" "Z3" "$OUTPUT_DIR" "$DISC_NAME"
:; exit

@ECHO OFF
REM === Windows Execution Block ===
SETLOCAL

FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="GAMES_URL" SET "GAMES_URL=%%B"
    IF "%%A"=="LURKING_URL" SET "LURKING_URL=%%B"
    IF "%%A"=="ADVENT_URL" SET "ADVENT_URL=%%B"
    IF "%%A"=="DEST" SET "DEST=%%B"
)

SET "TEMP_DIR=%DEST%_games_temp"
IF NOT EXIST "%TEMP_DIR%" MKDIR "%TEMP_DIR%"
IF NOT EXIST "Z3" MKDIR "Z3"

ECHO Downloading games archive from: %GAMES_URL%
curl -L -o temp_games.zip "%GAMES_URL%"
powershell -NoProfile -Command "Expand-Archive -Path 'temp_games.zip' -DestinationPath '%TEMP_DIR%' -Force"
DEL temp_games.zip

ECHO Parsing VERSIONS.ndjson and mapping files...
REM Process the NDJSON, pipe through standard regex matchers, and move the hit
powershell -NoProfile -Command "if (Test-Path 'VERSIONS.ndjson') { Get-Content 'VERSIONS.ndjson' | ForEach-Object { $obj = $_ | ConvertFrom-Json; $match = Get-ChildItem -Path '%TEMP_DIR%' -Recurse -File | Where-Object { $_.Name -match $obj.release -and $_.Name -match $obj.version } | Select-Object -First 1; if ($match) { Move-Item -Path $match.FullName -Destination \"Z3\$($obj.title)\" -Force; Write-Host \" -> Matched and moved: $($obj.title)\" } } }"

RMDIR /S /Q "%TEMP_DIR%"

ECHO Downloading LURKING.BLB into Z3...
curl -L -o "Z3\LURKING.BLB" "%LURKING_URL%"

ECHO Downloading ADVENT.Z3 into Z3...
curl -L -o "Z3\ADVENT.Z3" "%ADVENT_URL%"

ECHO Complete.

FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="BASE_ISO" SET "BASE_ISO=%%B"
    IF "%%A"=="OUTPUT_DIR" SET "OUTPUT_DIR=%%B"
    IF "%%A"=="DISC_NAME" SET "DISC_NAME=%%B"
)
IF NOT DEFINED BASE_ISO SET "BASE_ISO=./Zaturn (USA) (Netlink Edition)/zaturn.iso"
IF NOT DEFINED OUTPUT_DIR SET "OUTPUT_DIR=./Zaturn - Complete (USA) (Netlink Edition)"
IF NOT DEFINED DISC_NAME SET "DISC_NAME=Zaturn - Complete (USA) (Netlink Edition)"

REM Normalize any forward slashes so CMD's IF EXIST / COPY behave.
SET "BASE_ISO=%BASE_ISO:/=\%"
SET "OUTPUT_DIR=%OUTPUT_DIR:/=\%"

REM Stage the base ISO from the SDK build output if it hasn't been placed yet.
REM (CI stages it in full-image.yml; a local run must do the same.)
IF NOT EXIST "%BASE_ISO%" (
    IF EXIST "..\..\saturn\BuildDrop\zaturn.iso" (
        ECHO Staging base ISO from saturn\BuildDrop -^> %BASE_ISO%
        FOR %%I IN ("%BASE_ISO%") DO IF NOT EXIST "%%~dpI" MKDIR "%%~dpI"
        COPY /Y "..\..\saturn\BuildDrop\zaturn.iso" "%BASE_ISO%" >NUL
    )
)
IF NOT EXIST "%BASE_ISO%" (
    ECHO ERROR: base ISO not found: %BASE_ISO%
    ECHO Build the Saturn disc first ^(cd saturn ^&^& compile.bat^), then re-run.
    EXIT /B 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\merge.ps1" -BaseIso "%BASE_ISO%" -GamesDir "Z3" -OutDir "%OUTPUT_DIR%" -Name "%DISC_NAME%" -Dd ".\bin\win\dd.exe" -Xorriso ".\bin\win\xorriso.exe" -Iso2raw ".\bin\win\iso2raw.exe"
IF ERRORLEVEL 1 ( ECHO ERROR: game injection failed & EXIT /B 1 )

ENDLOCAL
GOTO :eof