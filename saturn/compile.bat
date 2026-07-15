:; export SRL_INSTALL_ROOT="../SaturnRingLib"; "../SaturnRingLib/tools/scripts/make.sh" "${1:-debug}" "../SaturnRingLib/Compiler"; exit;
@ECHO Off
REM saturn-mojozork: project lives at <repo>/saturn, SDK is the sibling submodule <repo>/SaturnRingLib.
REM Stock SaturnRingLib projects sit at SaturnRingLib/Projects/<name> and reach the SDK via ../.. .
REM From <repo>/saturn we reach the SDK at ../SaturnRingLib. We put the toolchain on PATH ourselves
REM (absolute, via %~dp0) and call make directly, instead of the SDK's make.bat -- its
REM `SET COMPILER_DIR=%2` captures surrounding quotes into PATH and breaks executable lookup.
SETLOCAL
IF "%~1"=="" (SET "TGT=debug") ELSE (SET "TGT=%~1")
SET "SRL_INSTALL_ROOT=../SaturnRingLib"
SET "CDIR=%~dp0..\SaturnRingLib\Compiler"
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
IF /I "%TGT%"=="clean"   GOTO doclean
IF /I "%TGT%"=="release" GOTO dorelease
make all DEBUG=1
GOTO done
:dorelease
make all
GOTO done
:doclean
make clean
:done
ENDLOCAL
