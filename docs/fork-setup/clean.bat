:; export SRL_INSTALL_ROOT="../SaturnRingLib"; "../SaturnRingLib/tools/scripts/make.sh" clean "../SaturnRingLib/Compiler"; exit;
@ECHO Off
REM See compile.bat: put the toolchain on PATH absolute (%~dp0) and call make directly.
SETLOCAL
SET "SRL_INSTALL_ROOT=../SaturnRingLib"
SET "CDIR=%~dp0..\SaturnRingLib\Compiler"
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
make clean
ENDLOCAL
