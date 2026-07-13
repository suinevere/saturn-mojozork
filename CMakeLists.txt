cmake_minimum_required(VERSION 3.16)
project(saturn_mojozork)

# This CMakeLists.txt exists purely so CLion (and other CMake-aware IDEs) can
# resolve #includes and navigate the Sega Saturn port under saturn/src and the
# SaturnRingLib SDK submodule. It is NOT the real build: the bootable Saturn
# disc is produced by the SH-2 cross-compiler makefile via saturn/compile.bat
# (see saturn/makefile and SaturnRingLib/saturnringlib/shared.mk). The target
# below is a SOURCES-only add_custom_target, so CLion indexes the files
# without ever trying to invoke a host compiler on SH-2/Saturn-specific code.
#
# saturn/CMakeLists.txt is a separate, genuinely buildable project (the
# upstream desktop mojozork/multizorkd/mojozork-sdl3 targets) and is
# untouched by this file.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 17)

set(SATURN_ROOT  "${CMAKE_SOURCE_DIR}/saturn")
set(SRL_ROOT     "${CMAKE_SOURCE_DIR}/SaturnRingLib")
set(SRL_MODULES  "${SRL_ROOT}/modules")

# --- Include paths -----------------------------------------------------------
# Mirrors saturn/makefile's `-I$(shell find src -type d)`: every subfolder
# under saturn/src (engine/video/sound/net/input/menu/system, plus any added
# later) goes on the include path so the project's bare #include "foo.h"
# resolves across folders exactly like the real build does.
file(GLOB_RECURSE SATURN_SRC_ENTRIES LIST_DIRECTORIES true "${SATURN_ROOT}/src/*")
foreach(entry ${SATURN_SRC_ENTRIES})
    if(IS_DIRECTORY ${entry})
        include_directories(${entry})
    endif()
endforeach()
include_directories(${SATURN_ROOT}/src)
include_directories(${SATURN_ROOT}/tests)
include_directories(${SATURN_ROOT}/tests/net)

# SaturnRingLib SDK headers actually on shared.mk's CCFLAGS -I list
# (DUMMYIDIR/SATURNMATHPPDIR/SGLIDIR/STDDIR/SDK_ROOT).
include_directories(${SRL_ROOT}/saturnringlib)
include_directories(${SRL_MODULES}/dummy)
include_directories(${SRL_MODULES}/SaturnMathPP)
include_directories(${SRL_MODULES}/sgl/INC)
include_directories(${SRL_MODULES}/tlsf)

# The SH-2 cross-compiler's own bundled libc/libstdc++ headers (<stdint.h>,
# <string>, ...). SaturnRingLib/Compiler ships a prebuilt toolchain per host
# OS (linux/, mac/, and a bare sh2eb-elf/ for Windows); only the one
# setup_compiler.bat actually installed for THIS host has real content, so
# only that tree is added -- adding all three used to shadow this host's
# headers with another platform's copy, which is what broke #include
# resolution here before. Enumerated by globbing (not a hand-written list) so
# a future GCC version bump (currently 14.2.0) can't go stale the way the old
# hardcoded add_executable() source list did.
if(WIN32)
    set(SRL_HOST_COMPILER_DIR "${SRL_ROOT}/Compiler/sh2eb-elf")
elseif(APPLE)
    set(SRL_HOST_COMPILER_DIR "${SRL_ROOT}/Compiler/mac/sh2eb-elf")
else()
    set(SRL_HOST_COMPILER_DIR "${SRL_ROOT}/Compiler/linux/sh2eb-elf")
endif()
if(EXISTS ${SRL_HOST_COMPILER_DIR})
    file(GLOB_RECURSE SRL_COMPILER_ENTRIES LIST_DIRECTORIES true "${SRL_HOST_COMPILER_DIR}/*")
    foreach(entry ${SRL_COMPILER_ENTRIES})
        if(IS_DIRECTORY ${entry})
            include_directories(${entry})
        endif()
    endforeach()
endif()

# --- Indexing-only target -----------------------------------------------------
# CONFIGURE_DEPENDS re-globs on the next CMake reload whenever a file is
# added/moved/removed, so this list (unlike the old hardcoded one) can't
# silently drift out of sync with saturn/src's actual layout again.
#
# This MUST be add_library(... OBJECT ...), not add_custom_target(SOURCES ...):
# CMake's File API (what CLion reads for #include resolution) only emits a
# "compile group" -- the actual per-target include-path/flags set -- for
# targets it considers genuinely compilable. add_custom_target's SOURCES are
# just IDE-tree entries with no compile group at all, which is why every file
# here previously showed an empty search path. EXCLUDE_FROM_ALL keeps it out
# of the default "Build Project" action; OBJECT means it never links, so
# nothing here is ever meant to actually produce a usable binary -- it exists
# purely so CMake computes a real compile group for CLion to resolve against.
file(GLOB_RECURSE SATURN_MOJOZORK_SOURCES CONFIGURE_DEPENDS
    "${SATURN_ROOT}/src/*.c"
    "${SATURN_ROOT}/src/*.cxx"
    "${SATURN_ROOT}/src/*.h"
    "${SATURN_ROOT}/tests/*.c"
    "${SATURN_ROOT}/tests/*.h"
    "${SATURN_ROOT}/*.c"
    "${SATURN_ROOT}/*.cxx"
    "${SATURN_ROOT}/*.h"
)
file(GLOB SRL_HEADERS CONFIGURE_DEPENDS "${SRL_ROOT}/saturnringlib/*.hpp")

add_library(saturn_mojozork OBJECT EXCLUDE_FROM_ALL ${SATURN_MOJOZORK_SOURCES} ${SRL_HEADERS})

# end of CMakeLists.txt ...
