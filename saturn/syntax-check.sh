#!/bin/sh
# Type-check a Saturn source file against the real SRL/SGL headers without
# building anything.
#
# This is NOT a build: -fsyntax-only writes no object files, no ELF, no ISO,
# and never touches BuildDrop/. It exists so an edit to main.cxx can be
# verified before handing the tree back for a real ./compile.bat run.
#
# Usage:  sh syntax-check.sh [file ...]     (default: src/main.cxx)
# Exit:   0 = clean, non-zero = errors (printed to stderr)
#
# The -D values mirror shared.mk's defaults (see its SYSFLAGS/CCFLAGS blocks).
# They only need to be self-consistent enough to parse; a real build supplies
# the same names from the project's SRL_* settings.
set -e

cd "$(dirname "$0")"

CXX=../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe
M=../SaturnRingLib/modules
SDK=../SaturnRingLib/saturnringlib

if [ ! -x "$CXX" ]; then
    echo "syntax-check: SH-2 compiler not found at $CXX" >&2
    echo "syntax-check: run SaturnRingLib/setup_compiler.bat first" >&2
    exit 2
fi

[ $# -gt 0 ] || set -- src/main.cxx

exec "$CXX" -fsyntax-only -std=gnu++2b -m2 \
    -DSRL_MODE_DEBUG -DSRL_FRAMERATE=1 \
    -DSRL_MAX_TEXTURES=100 -DSRL_MAX_CD_BACKGROUND_JOBS=5 \
    -DSRL_MAX_CD_FILES=256 -DSRL_MAX_CD_RETRIES=5 \
    -DSRL_DEBUG_MAX_PRINT_LENGTH=64 -DSRL_DEBUG_MAX_LOG_LENGTH=80 \
    -DSGL_MAX_VERTICES=2500 -DSGL_MAX_POLYGONS=1700 \
    -DSGL_MAX_EVENTS=64 -DSGL_MAX_WORKS=256 \
    -I"$M/dummy" -I"$M/SaturnMathPP" -I"$M/sgl/INC" -I"$M/danny/INC" \
    -I"$SDK" -Isrc \
    "$@"
