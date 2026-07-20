:; "../SaturnRingLib/tools/scripts/run.sh" mednafen; exit;
@ECHO Off
REM zaturn: project is in saturn/, SDK is the ../SaturnRingLib submodule.
REM The SDK's run.bat locates its bundled emulator via ../../emulators (correct only from
REM SaturnRingLib/Projects/<name>), so on Windows we launch the project-local mednafen
REM directly with absolute %~dp0 paths, falling back to a mednafen.exe on PATH.
SETLOCAL
SET "CUE=%~dp0BuildDrop\zaturn.cue"
IF NOT EXIST "%CUE%" (ECHO Build first: compile.bat debug & GOTO :eof)
SET "MEDNAFEN=%~dp0..\SaturnRingLib\emulators\mednafen\mednafen.exe"
IF NOT EXIST "%MEDNAFEN%" SET "MEDNAFEN=mednafen.exe"
START "" "%MEDNAFEN%" "%CUE%"
ENDLOCAL
