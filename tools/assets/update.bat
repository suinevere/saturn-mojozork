:; # === Linux & macOS Execution Block ===
:; SCRIPT1="games.bat"
:; SCRIPT2="music.bat"
:;
:; echo "Starting master execution..."
:;
:; if [ -f "$SCRIPT1" ]; then
:;     echo "Running $SCRIPT1..."
:;     bash "$SCRIPT1"
:; else
:;     echo "Error: $SCRIPT1 not found."
:; fi
:;
:; if [ -f "$SCRIPT2" ]; then
:;     echo "Running $SCRIPT2..."
:;     bash "$SCRIPT2"
:; else
:;     echo "Error: $SCRIPT2 not found."
:; fi
:;
:; echo "Master execution complete."
:; exit

@ECHO OFF
REM === Windows Execution Block ===
SETLOCAL

SET "SCRIPT1=games.bat"
SET "SCRIPT2=music.bat"

ECHO Starting master execution...

IF EXIST "%SCRIPT1%" (
    ECHO Running %SCRIPT1%...
    REM Use CALL so control returns to this master script after execution
    CALL "%SCRIPT1%"
) ELSE (
    ECHO Error: %SCRIPT1% not found.
)

IF EXIST "%SCRIPT2%" (
    ECHO Running %SCRIPT2%...
    CALL "%SCRIPT2%"
) ELSE (
    ECHO Error: %SCRIPT2% not found.
)

ECHO Master execution complete.
ENDLOCAL
GOTO :eof