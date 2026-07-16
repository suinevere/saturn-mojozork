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
ENDLOCAL
GOTO :eof