# zaturn.netbin Build Target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a second build target that emits a self-contained Saturn executable (`zaturn.netbin`) linked at `0x06010000`, under 400 KB, with Zork I and the SGL sound driver embedded, requiring no CD at runtime.

**Architecture:** The existing source tree gains a `NETBIN=1` build mode. Binary payloads (story file, sound driver) are converted to C byte arrays by a Python generator — the same pattern `typeahead_solution.c` and `music_data.c` already use — and guarded by `#ifdef NETBIN` so the CD build is unaffected. CD-dependent boot steps are skipped under the same guard. A relocated copy of SRL's linker script is selected via a command-line `LDFILE` override, requiring no SDK modification.

**Tech Stack:** SH-2 GCC (SaturnRingLib toolchain), GNU make, Python 3 (generator), C/C++ (gnu++2b / c2x), MSYS2 sh.

## Global Constraints

- **Entry point / load base is exactly `0x06010000`.** Non-negotiable loader requirement.
- **Final `zaturn.netbin` must be under 400 KB (409,600 bytes).** The build fails hard if exceeded.
- **The CD build must continue to work unchanged.** Every change is additive or `#ifdef NETBIN`-guarded. Never modify behavior on the default path.
- **Never modify anything under `SaturnRingLib/`.** It is a pinned submodule. All overrides go through the project `Makefile`, `pre.makefile`/`post.makefile`, or command-line variables.
- **THE USER RUNS ALL SATURN BUILDS.** Do not invoke `compile.bat`, `make all`, or any SH-2 build yourself. Where a step requires a real build, prepare the change, run `syntax-check.sh`, and hand off to the user with the exact command. Host-side tests (`gcc`) and Python you DO run yourself.
- **`SRL_USE_SGL_SOUND_DRIVER` stays at `1`** in both builds. The sound stack must compile; the netbin re-initializes it from RAM.
- Debug output uses only `%c %s %d %0Nd` — SRL's `Debug::Print` supports nothing else, and a stray specifier garbles the whole line.
- A `*/` inside a `/*----box----*/` comment body closes the comment early and breaks the build. Never put one there.
- `menu_layout.h` is plain C with no `extern "C"` guard; any `.cxx` file including it must wrap the include.

---

## File Structure

**Created:**
- `tools/gen_blob.py` — converts a binary file into a C byte-array source. One responsibility: bytes → `.c` text.
- `saturn/src/netbin_blobs.h` — public accessors for embedded payloads.
- `saturn/src/netbin_blobs.c` — **generated**; the byte arrays. Regenerated, never hand-edited.
- `saturn/src/netbin_sound.h` / `saturn/src/netbin_sound.cxx` — RAM-based SGL sound driver initialization.
- `saturn/sgl-netbin.linker` — SRL's linker script relocated to `0x06010000`.
- `saturn/post.makefile` — netbin packaging + size gate.
- `saturn/compile-netbin.bat` — one-command netbin build for the user.
- `saturn/tests/test_netbin_blobs.c` — host test that the generator round-trips bytes.
- `tools/assets/CONFIG.NETLINK.ME` — music-only config for the companion disc.

**Modified:**
- `saturn/Makefile` — `NETBIN` block.
- `saturn/src/main.cxx` — `#ifdef NETBIN` boot guards and embedded story load.
- `tools/assets/music.bat` — config-path override.
- `tools/assets/README.md` — companion disc documentation.

---

### Task 1: Blob generator and embedded payload module

Converts binary files to C arrays. Nothing else depends on the build system yet, so this is verifiable entirely on the host.

**Files:**
- Create: `tools/gen_blob.py`
- Create: `saturn/src/netbin_blobs.h`
- Create: `saturn/src/netbin_blobs.c` (generated)
- Test: `saturn/tests/test_netbin_blobs.c`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `const unsigned char *netbin_story_data(void)` / `unsigned int netbin_story_size(void)`
  - `const unsigned char *netbin_sddrvs_data(void)` / `unsigned int netbin_sddrvs_size(void)`
  - `const unsigned char *netbin_bootsnd_data(void)` / `unsigned int netbin_bootsnd_size(void)`
  - All return `NULL` / `0` when `NETBIN` is not defined.

- [ ] **Step 1: Write the generator**

Create `tools/gen_blob.py`:

```python
#!/usr/bin/env python3
"""Convert binary files into a single C source of byte arrays.

Emits arrays guarded by #ifdef NETBIN so the generated file compiles to an
empty object in the CD build, which globs every src/*.c unconditionally.

Usage:
    python3 tools/gen_blob.py OUT.c NAME=PATH [NAME=PATH ...]
"""
import sys


def emit_array(name, data):
    out = [f"const unsigned char netbin_{name}_bytes[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        out.append("    " + "".join(f"0x{b:02x}," for b in chunk))
    out.append("};")
    out.append(f"const unsigned int netbin_{name}_len = {len(data)}u;")
    return "\n".join(out)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    out_path, specs = argv[1], argv[2:]

    parts = [
        "/* GENERATED by tools/gen_blob.py -- do not edit by hand. */",
        '#include "netbin_blobs.h"',
        "",
        "#ifdef NETBIN",
        "",
    ]
    accessors = []
    for spec in specs:
        if "=" not in spec:
            sys.stderr.write(f"gen_blob: bad spec {spec!r}, want NAME=PATH\n")
            return 2
        name, path = spec.split("=", 1)
        with open(path, "rb") as fh:
            data = fh.read()
        if not data:
            sys.stderr.write(f"gen_blob: {path} is empty\n")
            return 1
        parts.append(emit_array(name, data))
        parts.append("")
        accessors.append(
            f"const unsigned char *netbin_{name}_data(void)"
            f" {{ return netbin_{name}_bytes; }}\n"
            f"unsigned int netbin_{name}_size(void)"
            f" {{ return netbin_{name}_len; }}"
        )
        sys.stderr.write(f"gen_blob: {name} <- {path} ({len(data)} bytes)\n")

    parts.extend(accessors)
    parts.append("")
    parts.append("#else  /* !NETBIN -- CD build links no payload */")
    parts.append("")
    for spec in specs:
        name = spec.split("=", 1)[0]
        parts.append(
            f"const unsigned char *netbin_{name}_data(void) {{ return 0; }}\n"
            f"unsigned int netbin_{name}_size(void) {{ return 0u; }}"
        )
    parts.append("")
    parts.append("#endif")
    parts.append("")

    with open(out_path, "w", newline="\n") as fh:
        fh.write("\n".join(parts))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

- [ ] **Step 2: Write the header**

Create `saturn/src/netbin_blobs.h`:

```c
/*----------------------
 | netbin_blobs.h
 | Description: Accessors for the payloads embedded in the .netbin build --
 |   the Zork I story image and the SGL sound driver files. The CD build
 |   compiles the same translation unit but links no data: every accessor
 |   returns NULL/0 there, so callers branch on the size rather than on a
 |   build flag.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/

#ifndef NETBIN_BLOBS_H
#define NETBIN_BLOBS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Zork I story image. NULL/0 in the CD build. */
const unsigned char *netbin_story_data(void);
unsigned int         netbin_story_size(void);

/* SGL sound driver program (SDDRVS.TSK). NULL/0 in the CD build. */
const unsigned char *netbin_sddrvs_data(void);
unsigned int         netbin_sddrvs_size(void);

/* SGL sound area map (BOOTSND.MAP). NULL/0 in the CD build. */
const unsigned char *netbin_bootsnd_data(void);
unsigned int         netbin_bootsnd_size(void);

#ifdef __cplusplus
}
#endif

#endif /* NETBIN_BLOBS_H */
```

- [ ] **Step 3: Write the failing test**

Create `saturn/tests/test_netbin_blobs.c`:

```c
/* Verifies gen_blob.py round-trips bytes exactly, and that the non-NETBIN
   build links empty accessors. Built twice by the runner: once with -DNETBIN
   against a generated fixture, once without. */
#include "../src/netbin_blobs.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
#ifdef NETBIN
    /* fixture_blobs.c is generated from a known 4-byte pattern file. */
    assert(netbin_story_size() == 4);
    assert(netbin_story_data() != 0);
    assert(netbin_story_data()[0] == 0x00);
    assert(netbin_story_data()[1] == 0x7f);
    assert(netbin_story_data()[2] == 0x80);
    assert(netbin_story_data()[3] == 0xff);
    printf("test_netbin_blobs (NETBIN): OK\n");
#else
    assert(netbin_story_size() == 0);
    assert(netbin_story_data() == 0);
    printf("test_netbin_blobs (CD): OK\n");
#endif
    return 0;
}
```

- [ ] **Step 4: Run the test to verify it fails**

Run it before generating any fixture, so the failure is the real one — no
payload source exists yet:

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
gcc -DNETBIN -I saturn/src -o /tmp/tb_netbin saturn/tests/test_netbin_blobs.c
```

Expected: FAIL at link time with `undefined reference to 'netbin_story_size'`
and `undefined reference to 'netbin_story_data'`.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
printf '\x00\x7f\x80\xff' > /tmp/fixture.bin
python3 tools/gen_blob.py /tmp/fixture_blobs.c story=/tmp/fixture.bin
gcc -DNETBIN -I saturn/src -o /tmp/tb_netbin \
    saturn/tests/test_netbin_blobs.c /tmp/fixture_blobs.c && /tmp/tb_netbin
gcc -I saturn/src -o /tmp/tb_cd \
    saturn/tests/test_netbin_blobs.c /tmp/fixture_blobs.c && /tmp/tb_cd
```

Expected output:
```
test_netbin_blobs (NETBIN): OK
test_netbin_blobs (CD): OK
```

- [ ] **Step 6: Generate the real payload file**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
python3 tools/gen_blob.py saturn/src/netbin_blobs.c \
    story=saturn/cd/data/Z3/ZORK1.Z3 \
    sddrvs=saturn/cd/data/SDDRVS.TSK \
    bootsnd=saturn/cd/data/BOOTSND.MAP
```

Expected stderr:
```
gen_blob: story <- saturn/cd/data/Z3/ZORK1.Z3 (84876 bytes)
gen_blob: sddrvs <- saturn/cd/data/SDDRVS.TSK (26610 bytes)
gen_blob: bootsnd <- saturn/cd/data/BOOTSND.MAP (82 bytes)
```

- [ ] **Step 7: Verify the generated file compiles both ways**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
gcc -DNETBIN -I saturn/src -c -o /tmp/nb_netbin.o saturn/src/netbin_blobs.c
gcc          -I saturn/src -c -o /tmp/nb_cd.o     saturn/src/netbin_blobs.c
ls -l /tmp/nb_netbin.o /tmp/nb_cd.o
```

Expected: both compile with no errors; `nb_netbin.o` is ~111 KB, `nb_cd.o` is
under 2 KB. That size difference is the proof the `#ifdef` guard works and the
CD build pays nothing.

- [ ] **Step 8: Commit**

```bash
git add tools/gen_blob.py saturn/src/netbin_blobs.h saturn/src/netbin_blobs.c \
        saturn/tests/test_netbin_blobs.c
git commit -m "Add blob generator and embedded payload module for netbin"
```

---

### Task 2: NETBIN build target

Establishes the relocated link and the size gate **before** any boot surgery, so a relocation failure is diagnosed on its own rather than tangled with behavior changes. At the end of this task the netbin still behaves exactly like the CD build — it is simply linked 48 KB higher.

**Files:**
- Create: `saturn/sgl-netbin.linker`
- Create: `saturn/post.makefile`
- Create: `saturn/compile-netbin.bat`
- Modify: `saturn/Makefile`

**Interfaces:**
- Consumes: `netbin_blobs.c` from Task 1 (compiles, contributes ~111 KB when `NETBIN` is defined).
- Produces: `make NETBIN=1` emits `BuildDrop/zaturn.netbin`; the `-DNETBIN` macro is visible to all translation units.

- [ ] **Step 1: Create the relocated linker script**

Copy `SaturnRingLib/modules/sgl/sgl.linker` to `saturn/sgl-netbin.linker`, changing **only** the base address on line 4. The file must otherwise be byte-identical.

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
sed 's/PRELOADER 0x06004000/PRELOADER 0x06010000/' \
    SaturnRingLib/modules/sgl/sgl.linker > saturn/sgl-netbin.linker
```

- [ ] **Step 2: Verify exactly one line changed**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
diff SaturnRingLib/modules/sgl/sgl.linker saturn/sgl-netbin.linker
```

Expected output — exactly this, nothing more:
```
4c4
< 	PRELOADER 0x06004000 : {
---
> 	PRELOADER 0x06010000 : {
```

- [ ] **Step 3: Add the NETBIN block to the project Makefile**

In `saturn/Makefile`, insert immediately **before** the `# Include shared makefile` comment near the end:

```make
# --- .netbin build ------------------------------------------------------
# `make NETBIN=1` (see compile-netbin.bat) links a self-contained image for
# the PlanetWeb 4.0 loader: base 0x06010000 instead of 0x06004000, with the
# story file and SGL sound driver embedded rather than read from CD.
#
# LDFILE cannot be set here: shared.mk:10 assigns it with `=` and this file
# is included first, so the SDK would overwrite us. compile-netbin.bat passes
# it on the make command line, which does override a makefile assignment.
#
# SRL_USE_SGL_SOUND_DRIVER deliberately stays 1. The netbin still needs SRL's
# sound stack compiled; it only replaces where the driver bytes come from
# (see netbin_sound.cxx).
ifeq ($(strip $(NETBIN)),1)
SRL_CUSTOM_CCFLAGS += -DNETBIN
BUILD_NETBIN = $(BUILD_DROP)/zaturn.netbin
endif
```

- [ ] **Step 4: Add the packaging and size gate**

Create `saturn/post.makefile`:

```make
# Included automatically by SaturnRingLib's shared.mk when this file exists
# (shared.mk:223-229). Recipes run under MSYS2 sh with saturn/ as the working
# directory.
#
# For NETBIN=1 builds, flatten the ELF to the raw image the PlanetWeb loader
# expects and refuse to ship one that exceeds the loader's 400 KB ceiling.
NETBIN_MAX_BYTES = 409600

post_build:
ifeq ($(strip $(NETBIN)),1)
	$(info ****** Packaging zaturn.netbin ******)
	@$(OBJCOPY) -O binary "$(BUILD_ELF)" "$(BUILD_DROP)/zaturn.netbin"
	@sz=$$(stat -c%s "$(BUILD_DROP)/zaturn.netbin"); \
	 echo "zaturn.netbin: $$sz bytes (limit $(NETBIN_MAX_BYTES))"; \
	 if [ "$$sz" -gt "$(NETBIN_MAX_BYTES)" ]; then \
	     echo "ERROR: zaturn.netbin exceeds the $(NETBIN_MAX_BYTES)-byte loader limit" >&2; \
	     rm -f "$(BUILD_DROP)/zaturn.netbin"; \
	     exit 1; \
	 fi
else
	$(info ****** No post build steps ******)
endif
```

- [ ] **Step 5: Create the build script**

Create `saturn/compile-netbin.bat`, mirroring `compile.bat`'s structure (see its header comment for why the toolchain is put on PATH explicitly rather than via the SDK's `make.bat`):

```bat
:; export SRL_INSTALL_ROOT="../SaturnRingLib"; make all NETBIN=1 LDFILE=./sgl-netbin.linker ${1:+DEBUG=1}; exit;
@ECHO Off
REM Builds the PlanetWeb 4.0 .netbin variant. See compile.bat for why the
REM toolchain goes on PATH here instead of using the SDK's make.bat.
REM LDFILE must be passed on the command line: shared.mk:10 assigns it with
REM `=`, so a Makefile-side assignment would be overwritten.
SETLOCAL
SET "SRL_INSTALL_ROOT=../SaturnRingLib"
SET "CDIR=%~dp0..\SaturnRingLib\Compiler"
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
IF /I "%~1"=="clean" (
    make clean NETBIN=1
    GOTO done
)
IF /I "%~1"=="debug" (
    make all NETBIN=1 LDFILE=./sgl-netbin.linker DEBUG=1
    GOTO done
)
make all NETBIN=1 LDFILE=./sgl-netbin.linker
:done
ENDLOCAL
```

- [ ] **Step 6: Hand the build to the user**

**Do not run this yourself.** Ask the user to run, from `saturn/`:

```
.\compile-netbin.bat
```

Then have them report:
- the `zaturn.netbin: NNNNNN bytes (limit 409600)` line, and
- the output of `grep -n "PRELOADER" "BuildDrop/Zaturn (USA) (Netlink Edition).map"`

Expected: the size line prints a value under 409600, and the map shows
`PRELOADER 0x06010000`. If the map still shows `0x06004000`, the `LDFILE`
override did not take — check that it was passed on the make command line and
not set inside the Makefile.

- [ ] **Step 7: Confirm the CD build is untouched**

Ask the user to also run `.\compile.bat` and confirm it still succeeds and that
`BuildDrop/…​.map` shows `PRELOADER 0x06004000` again.

- [ ] **Step 8: Commit**

```bash
git add saturn/sgl-netbin.linker saturn/post.makefile \
        saturn/compile-netbin.bat saturn/Makefile
git commit -m "Add NETBIN build target linking at 0x06010000 with a 400 KB gate"
```

---

### Task 3: Extract TOC decoding into a host-testable module — DROPPED

> **Dropped on 2026-07-21 by user decision.** Implemented at 13b8770, never
> reviewed, then removed from the branch by rebase. Its only remaining consumer
> was Task 4, which is also skipped, so the extraction carried no netbin value.
> `music_cdda.cxx` keeps its original inline TOC decoding. Steps below are
> retained for the record — **do not execute them.**

`music_cdda.cxx` mixes pure TOC arithmetic with SRL calls, so none of it can be
tested off-hardware. The TOC-validity work in Task 4 changes exactly this logic,
so extracting it first buys real test coverage for the risky part.

**Files:**
- Create: `saturn/src/toc_decode.h`
- Create: `saturn/src/toc_decode.c`
- Modify: `saturn/src/music_cdda.cxx:32-55`
- Test: `saturn/tests/test_toc_decode.c`

**Interfaces:**
- Consumes: nothing.
- Produces (all pure C, no SRL):
  - `#define TOC_WORDS 102`, `TOC_FIRST_WORD 99`, `TOC_LAST_WORD 100`, `TOC_LEADOUT 101`
  - `int toc_ctrl(unsigned int w)`
  - `unsigned int toc_fad(unsigned int w)`
  - `int toc_is_audio(unsigned int w)`
  - `int toc_track_no(const unsigned int *toc, int word)`
  - `int toc_track_frames(const unsigned int *toc, int track)` — returns frame
    span of `track`, or `0` when the TOC is unreadable or the span is
    non-positive.

- [ ] **Step 1: Write the header**

Create `saturn/src/toc_decode.h`:

```c
/*----------------------
 | toc_decode.h
 | Description: Pure decoding of the Saturn BIOS's 102-longword CD TOC. Split
 |   out of music_cdda.cxx so the bit/field arithmetic can be unit-tested on
 |   the host: nothing here touches SRL, SGL, or the CD block.
 |
 |   TOC layout (CDC_TgetToc fills 102 longwords):
 |     [0..98]  one entry per CD track 1..99: (ctrladr << 24) | fad
 |              absent tracks read 0xFFFFFFFF
 |     [99]     first-track info: (ctrladr << 24) | (track number << 16) | ...
 |     [100]    last-track  info: same layout
 |     [101]    lead-out
 |   ctrladr's high nibble is the control field: bit 2 set = data track, clear
 |   = audio track; 0x0f marks the entry as absent. FAD is 1/75s frames.
 | Author: suinevere
 | Dependencies: none
 ----------------------*/

#ifndef TOC_DECODE_H
#define TOC_DECODE_H

#ifdef __cplusplus
extern "C" {
#endif

#define TOC_WORDS       102
#define TOC_FIRST_WORD  99
#define TOC_LAST_WORD   100
#define TOC_LEADOUT     101

/* Control nibble of a TOC entry. */
int toc_ctrl(unsigned int w);

/* Frame address (1/75s) of a TOC entry. */
unsigned int toc_fad(unsigned int w);

/* 1 when the entry is a present audio track (not absent, not a data track). */
int toc_is_audio(unsigned int w);

/* Track number recorded in TOC_FIRST_WORD / TOC_LAST_WORD, or 0 when the TOC
   reads bogus (no disc, or a read that came back before the drive was ready). */
int toc_track_no(const unsigned int *toc, int word);

/* Frame span of `track`: its start to the next track's start, or to the
   lead-out for the last track. 0 when the TOC is unreadable, `track` is out
   of range, or the computed span is non-positive. */
int toc_track_frames(const unsigned int *toc, int track);

#ifdef __cplusplus
}
#endif

#endif /* TOC_DECODE_H */
```

- [ ] **Step 2: Write the failing test**

Create `saturn/tests/test_toc_decode.c`:

```c
#include "../src/toc_decode.h"
#include <stdio.h>
#include <assert.h>

/* Build a TOC resembling the companion audio disc: track 1 data, tracks 2..4
   audio, lead-out after track 4. FADs are in 1/75s frames. */
static void build_toc(unsigned int *toc) {
    int i;
    for (i = 0; i < TOC_WORDS; i++) toc[i] = 0xFFFFFFFFu;
    toc[0] = (0x4u << 28) | 150u;      /* track 1: data  */
    toc[1] = (0x0u << 28) | 1000u;     /* track 2: audio */
    toc[2] = (0x0u << 28) | 1300u;     /* track 3: audio, 300 frames = 4s */
    toc[3] = (0x0u << 28) | 40000u;    /* track 4: audio */
    toc[TOC_FIRST_WORD] = (0x4u << 28) | (1u << 16);
    toc[TOC_LAST_WORD]  = (0x0u << 28) | (4u << 16);
    toc[TOC_LEADOUT]    = (0x0u << 28) | 80000u;
}

int main(void) {
    unsigned int toc[TOC_WORDS];
    build_toc(toc);

    /* field extraction */
    assert(toc_ctrl(toc[0]) == 0x4);
    assert(toc_fad(toc[0]) == 150u);
    assert(toc_fad(toc[3]) == 40000u);

    /* data vs audio vs absent */
    assert(toc_is_audio(toc[0]) == 0);          /* data track   */
    assert(toc_is_audio(toc[1]) == 1);          /* audio track  */
    assert(toc_is_audio(0xFFFFFFFFu) == 0);     /* absent entry */

    /* first/last track numbers */
    assert(toc_track_no(toc, TOC_FIRST_WORD) == 1);
    assert(toc_track_no(toc, TOC_LAST_WORD) == 4);

    /* track spans: track 2 runs 1000->1300, track 3 runs 1300->40000 */
    assert(toc_track_frames(toc, 2) == 300);
    assert(toc_track_frames(toc, 3) == 38700);
    /* last track measures against the lead-out */
    assert(toc_track_frames(toc, 4) == 40000);

    /* out-of-range tracks are rejected, not read out of bounds */
    assert(toc_track_frames(toc, 0) == 0);
    assert(toc_track_frames(toc, 100) == 0);

    /* an all-absent TOC yields no track numbers and no spans */
    {
        unsigned int empty[TOC_WORDS];
        int i;
        for (i = 0; i < TOC_WORDS; i++) empty[i] = 0xFFFFFFFFu;
        assert(toc_track_no(empty, TOC_LAST_WORD) == 0);
        assert(toc_track_frames(empty, 2) == 0);
    }

    /* an audio-first disc (no data track 1) still decodes -- this is the
       companion-disc layout the netbin must handle */
    {
        unsigned int audio_first[TOC_WORDS];
        int i;
        for (i = 0; i < TOC_WORDS; i++) audio_first[i] = 0xFFFFFFFFu;
        audio_first[0] = (0x0u << 28) | 150u;
        audio_first[1] = (0x0u << 28) | 500u;
        audio_first[TOC_FIRST_WORD] = (0x0u << 28) | (1u << 16);
        audio_first[TOC_LAST_WORD]  = (0x0u << 28) | (2u << 16);
        audio_first[TOC_LEADOUT]    = (0x0u << 28) | 900u;
        assert(toc_is_audio(audio_first[0]) == 1);
        assert(toc_track_no(audio_first, TOC_FIRST_WORD) == 1);
        assert(toc_track_frames(audio_first, 1) == 350);
        assert(toc_track_frames(audio_first, 2) == 400);
    }

    printf("test_toc_decode: OK\n");
    return 0;
}
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
gcc -o /tmp/ttoc saturn/tests/test_toc_decode.c saturn/src/toc_decode.c
```

Expected: FAIL — `gcc: error: saturn/src/toc_decode.c: No such file or directory`

- [ ] **Step 4: Write the implementation**

Create `saturn/src/toc_decode.c`:

```c
/*----------------------
 | toc_decode.c
 | Description: Implementation of the pure Saturn CD TOC field decoding
 |   declared in toc_decode.h. No SRL/SGL/CD-block dependencies, so this
 |   builds and runs on the host under saturn/tests/test_toc_decode.c.
 | Author: suinevere
 | Dependencies: toc_decode.h
 ----------------------*/

#include "toc_decode.h"

int toc_ctrl(unsigned int w) { return (int)((w >> 28) & 0xfu); }

unsigned int toc_fad(unsigned int w) { return w & 0x00ffffffu; }

int toc_is_audio(unsigned int w) {
    int c = toc_ctrl(w);
    return (c != 0xf) && ((c & 0x4) == 0);
}

int toc_track_no(const unsigned int *toc, int word) {
    int n;
    if (toc == 0 || word < 0 || word >= TOC_WORDS) return 0;
    n = (int)((toc[word] >> 16) & 0xffu);
    return (n >= 1 && n <= 99) ? n : 0;
}

int toc_track_frames(const unsigned int *toc, int track) {
    int last, frames;
    unsigned int start, end;

    if (toc == 0 || track < 1 || track > 99) return 0;
    last = toc_track_no(toc, TOC_LAST_WORD);
    if (last == 0) return 0;                 /* unreadable TOC */

    start = toc_fad(toc[track - 1]);
    end   = (track >= last) ? toc_fad(toc[TOC_LEADOUT]) : toc_fad(toc[track]);
    frames = (int)(end - start);
    return (frames > 0) ? frames : 0;
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
gcc -o /tmp/ttoc saturn/tests/test_toc_decode.c saturn/src/toc_decode.c && /tmp/ttoc
```

Expected output:
```
test_toc_decode: OK
```

- [ ] **Step 6: Rewire music_cdda.cxx onto the shared module**

In `saturn/src/music_cdda.cxx`, delete the `TOC_WORDS`/`TOC_FIRST_WORD`/
`TOC_LAST_WORD`/`TOC_LEADOUT` defines and the `toc_ctrl`, `toc_fad`,
`toc_is_audio`, `toc_track_no` static functions (the block spanning lines
32-55), keeping the explanatory comment above them but retargeting it. Add the
include near the top of the file, alongside the existing includes:

```cpp
extern "C" {
#include "toc_decode.h"
}
```

Then update **every** call site in the file, not just the ones shown here:
`toc_track_no(word)` becomes `toc_track_no(toc_raw(), word)`, and any
`toc_is_audio`/`toc_fad`/`toc_ctrl` calls now resolve to the shared module.
Find them all first:

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/saturn
grep -n "toc_track_no\|toc_is_audio\|toc_fad\|toc_ctrl" src/music_cdda.cxx
```

The track-selector enumeration further down the file uses `toc_is_audio` to
decide which tracks to list; it must keep compiling unchanged.

`music_cdda_is_short` becomes:

```cpp
extern "C" int music_cdda_is_short(int track) {
    static signed char cache[100];   /* 0 unknown, 1 short, 2 long; index by CD track number */
    static int inited = 0;
    if (!inited) { for (int i = 0; i < 100; i++) cache[i] = 0; inited = 1; }
    if (track < 1 || track > 99) return 0;
    if (cache[track]) return cache[track] == 1;
    int frames = toc_track_frames(toc_raw(), track);
    if (frames == 0) return 0;                   // unreadable TOC: treat as long
    int is_short = (frames < MUSIC_SHORT_SECONDS * 75) ? 1 : 0;
    cache[track] = is_short ? 1 : 2;
    return is_short;
}
```

- [ ] **Step 7: Syntax-check the Saturn side**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/saturn
sh syntax-check.sh src/music_cdda.cxx
```

Expected: both the DEBUG and release passes print their banner and exit 0 with
no diagnostics.

- [ ] **Step 8: Re-run the host test and commit**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
gcc -o /tmp/ttoc saturn/tests/test_toc_decode.c saturn/src/toc_decode.c && /tmp/ttoc
git add saturn/src/toc_decode.h saturn/src/toc_decode.c \
        saturn/tests/test_toc_decode.c saturn/src/music_cdda.cxx
git commit -m "Extract CD TOC decoding into a host-tested pure-C module"
```

---

### Task 4: Do not cache a bogus TOC — SKIPPED

> **Skipped on 2026-07-21 by user decision.** The hazard is real but unproven:
> it only matters if the browser leaves the CD block unsettled at hand-over,
> which cannot be established without hardware. Deferred until testing shows
> music is dead under the netbin. Steps below are retained for the record —
> **do not execute them.**

`toc_raw()` reads the TOC once and caches it forever. The disc never changes —
there is one disc, the NetLink Custom Web Browser disc, and it stays in the
drive — so a one-shot cache is correct in principle. The hazard is different:
the browser has been driving the CD block right up until it hands over to the
netbin, so if the *first* `CDC_TgetToc` lands before the drive settles, the
bogus result is frozen in and music is dead for the entire session.

Fix: commit the cache only once the TOC reads sane.

**Files:**
- Modify: `saturn/src/music_cdda.cxx` (`toc_raw`)
- Test: `saturn/tests/test_toc_decode.c` (extend)

**Interfaces:**
- Consumes: `toc_decode.h` from Task 3 — specifically `toc_track_no`.
- Produces: no new public symbols. `toc_raw()` gains a validity gate.

- [ ] **Step 1: Write the failing test**

The sanity rule belongs in `toc_decode` so it can be tested on the host. Add to
`saturn/src/toc_decode.h`, before the closing `#ifdef __cplusplus`:

```c
/* 1 when the TOC carries a plausible first/last track pair, i.e. it is worth
   caching. 0 for an all-absent or not-yet-ready read. */
int toc_is_sane(const uint32_t *toc);
```

Append to `saturn/tests/test_toc_decode.c`, immediately before the final
`printf`/`return`:

```c
    /* a populated TOC is worth caching */
    assert(toc_is_sane(toc) == 1);

    /* an all-absent TOC (no disc, or a read before the drive settled) is not:
       caching this would freeze music off for the whole session */
    {
        uint32_t empty[TOC_WORDS];
        int i;
        for (i = 0; i < TOC_WORDS; i++) empty[i] = 0xFFFFFFFFu;
        assert(toc_is_sane(empty) == 0);
    }

    /* first/last present but nonsensical (last < first) is not sane either */
    {
        uint32_t bad[TOC_WORDS];
        int i;
        for (i = 0; i < TOC_WORDS; i++) bad[i] = 0xFFFFFFFFu;
        bad[TOC_FIRST_WORD] = (0x0u << 28) | (5u << 16);
        bad[TOC_LAST_WORD]  = (0x0u << 28) | (2u << 16);
        assert(toc_is_sane(bad) == 0);
    }

    assert(toc_is_sane(0) == 0);
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
gcc -Wall -Wextra -o /tmp/ttoc saturn/tests/test_toc_decode.c saturn/src/toc_decode.c
```

Expected: FAIL with `implicit declaration of function 'toc_is_sane'` /
`undefined reference to 'toc_is_sane'`.

- [ ] **Step 3: Implement the sanity check**

Add to `saturn/src/toc_decode.c`:

```c
int toc_is_sane(const uint32_t *toc) {
    int first, last;
    if (toc == 0) return 0;
    first = toc_track_no(toc, TOC_FIRST_WORD);
    last  = toc_track_no(toc, TOC_LAST_WORD);
    return (first != 0 && last != 0 && last >= first) ? 1 : 0;
}
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
gcc -Wall -Wextra -o /tmp/ttoc saturn/tests/test_toc_decode.c saturn/src/toc_decode.c && /tmp/ttoc
```

Expected output:
```
test_toc_decode: OK
```

- [ ] **Step 5: Gate the cache on sanity**

In `saturn/src/music_cdda.cxx`, replace `toc_raw()`:

```cpp
// Read the TOC, caching it only once it reads sane. The disc never changes --
// the browser disc stays in the drive for the whole session -- so one good
// read is all we ever need. But the browser drives the CD block right up until
// it hands control to the netbin, so an early read can come back bogus; if we
// cached that, music would stay dead for the entire session. Retry instead.
static const uint32_t* toc_raw(void) {
    if (!g_toc_ready) {
        CDC_TgetToc(g_toc);
        if (toc_is_sane(g_toc)) g_toc_ready = 1;
    }
    return g_toc;
}
```

- [ ] **Step 6: Syntax-check**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/saturn
sh syntax-check.sh src/music_cdda.cxx
```

Expected: this file does **not** reach exit 0 — `syntax-check.sh`'s include set
does not reach SRL's sound headers, so it reports 6 pre-existing
`'SRL::Sound' has not been declared` errors. That is not a regression. The real
gate: **no new errors, and no error naming `toc_raw`, `toc_is_sane`, or
`g_toc_ready`.** Confirm by comparing the error list against the same command
run on the previous commit.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/toc_decode.h saturn/src/toc_decode.c         saturn/src/music_cdda.cxx saturn/tests/test_toc_decode.c
git commit -m "Retry the CD TOC read until it comes back sane"
```
```

---

### Task 5: Sound driver initialization from RAM

> **REMOVED (2026-07-22):** the netbin no longer embeds or initializes the SGL
> sound driver. The `.netbin` exceeded the PlanetWeb loader's real (sub-400 KB)
> download ceiling, so the sound driver was cut to reclaim space: the embedded
> `SDDRVS.TSK` (26,610 B) + `BOOTSND.MAP` (82 B) blobs and `netbin_sound.{h,cxx}`
> were deleted, and the `netbin_sound_init()` call dropped from `main.cxx`. The
> netbin now embeds only the Zork I story; the CD build's sound path is
> unchanged. The section below is kept for history.

`SRL::Core::Initialize()` calls `Sound::Hardware::Initialize()`
(`srl_core.hpp:107-108`), which reads `SDDRVS.TSK` and `BOOTSND.MAP` from CD
and — because its whole body sits inside `if (program.Exists() &&
areaMap.Exists())` — silently does nothing when the files are absent. The
netbin lets that no-op happen, then performs the same initialization from the
embedded arrays.

**Files:**
- Create: `saturn/src/netbin_sound.h`
- Create: `saturn/src/netbin_sound.cxx`
- Modify: `saturn/src/main.cxx:950` (after `SRL::Core::Initialize`)

**Interfaces:**
- Consumes: `netbin_blobs.h` from Task 1.
- Produces: `void netbin_sound_init(void)` — no-op unless `NETBIN` is defined.

- [ ] **Step 1: Write the header**

Create `saturn/src/netbin_sound.h`:

```c
/*----------------------
 | netbin_sound.h
 | Description: Initializes the SGL sound driver from the embedded SDDRVS.TSK
 |   and BOOTSND.MAP arrays instead of reading them off the CD. SRL's own
 |   Sound::Hardware::Initialize() (srl_core.hpp:107) runs first from inside
 |   SRL::Core::Initialize and finds no files under the PlanetWeb loader, so
 |   its body is skipped entirely -- this reruns the same sequence against RAM.
 |   Compiled to a no-op in the CD build, where SRL's own path succeeds.
 | Author: suinevere
 | Dependencies: netbin_blobs.h, SGL
 ----------------------*/

#ifndef NETBIN_SOUND_H
#define NETBIN_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Load the embedded SGL sound driver into the SCSP. Must be called after
   SRL::Core::Initialize() and before any Sound::Cdda or Sound::Pcm use.
   No-op when NETBIN is not defined. */
void netbin_sound_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NETBIN_SOUND_H */
```

- [ ] **Step 2: Write the implementation**

Create `saturn/src/netbin_sound.cxx`. The call sequence mirrors
`SaturnRingLib/saturnringlib/srl_sound.hpp:24-70` exactly — only the byte
source changes:

```cpp
/*----------------------
 | netbin_sound.cxx
 | Description: RAM-based SGL sound driver bring-up for the .netbin build.
 |   Mirrors SRL's Sound::Hardware::Initialize() (srl_sound.hpp:24) call for
 |   call -- slSoundOffWait, SND_Init, SND_ChgMap, slInitSound, the SCSP
 |   enable poke, CDC_CdInit, SND_SetCdDaLev, slSoundOnWait -- but sources the
 |   driver program and area map from netbin_blobs instead of Cd::File. SRL's
 |   version is left to run and harmlessly skip itself: its body is guarded on
 |   both files existing on the disc, which they do not under the PlanetWeb
 |   loader.
 | Author: suinevere
 | Dependencies: netbin_blobs.h, netbin_sound.h, SGL
 ----------------------*/

#include "netbin_sound.h"

#ifdef NETBIN

#include <srl.hpp>

extern "C" {
#include "netbin_blobs.h"
}

extern "C" void netbin_sound_init(void) {
    const unsigned char *prg = netbin_sddrvs_data();
    const unsigned char *ara = netbin_bootsnd_data();
    unsigned int prg_len = netbin_sddrvs_size();
    unsigned int ara_len = netbin_bootsnd_size();

    if (prg == 0 || ara == 0 || prg_len == 0 || ara_len == 0) return;

    slSoundOffWait();

    // SND_Init/slInitSound take non-const pointers but only read the buffers.
    // The arrays live in .rodata; casting away const is safe here and avoids
    // spending ~26 KB of RAM on a copy we would never write to.
    uint8_t *prgBuf = (uint8_t *) prg;
    uint8_t *araBuf = (uint8_t *) ara;

    SndIniDt init;
    SND_INI_PRG_ADR(init) = (uint16_t *) prgBuf;
    SND_INI_PRG_SZ(init)  = (uint16_t) prg_len;
    SND_INI_ARA_ADR(init) = (uint16_t *) araBuf;
    SND_INI_ARA_SZ(init)  = (uint16_t) ara_len;
    SND_Init(&init);
    SND_ChgMap(0);

    slInitSound(prgBuf, prg_len, araBuf, ara_len);
    *(volatile unsigned char *) (0x25a004e1) = 0x0;
    CDC_CdInit(0x00, 0x00, 0x05, 0x0f);
    SND_SetCdDaLev(7, 7);

    // Sound back on. SRL's Initialize() calls slSoundOffWait/slSoundOnWait
    // outside its file-exists guard, so it has already left sound ON by the
    // time we run; without this the slSoundOffWait above would stick and every
    // sound in the netbin build would be silent.
    slSoundOnWait();
}

#else  /* !NETBIN -- SRL's own Cd::File path handles this in the CD build */

extern "C" void netbin_sound_init(void) { }

#endif
```

- [ ] **Step 3: Call it from main**

In `saturn/src/main.cxx`, add the include with the other project includes:

```cpp
#include "netbin_sound.h"
```

Then immediately after line 950's `SRL::Core::Initialize(...)`:

```cpp
    SRL::Core::Initialize(HighColor::Colors::Black);
    netbin_sound_init();   // netbin only: SRL's CD-based driver load found no disc
```

- [ ] **Step 4: Syntax-check both configurations**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/saturn
sh syntax-check.sh src/netbin_sound.cxx src/main.cxx
```

Expected: exit 0, no diagnostics. This checks the **CD** path (no `-DNETBIN`).

- [ ] **Step 5: Syntax-check the NETBIN path**

`syntax-check.sh` does not pass `-DNETBIN`. Check that path explicitly:

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/saturn
../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe -fsyntax-only \
    -std=gnu++2b -m2 -DNETBIN -DDEBUG \
    -DSRL_MODE_DEBUG -DSRL_FRAMERATE=1 -DSRL_USE_SGL_SOUND_DRIVER=1 \
    -DSRL_MAX_TEXTURES=100 -DSRL_MAX_CD_BACKGROUND_JOBS=5 \
    -DSRL_MAX_CD_FILES=256 -DSRL_MAX_CD_RETRIES=5 \
    -DSRL_DEBUG_MAX_PRINT_LENGTH=64 -DSRL_DEBUG_MAX_LOG_LENGTH=80 \
    -DSGL_MAX_VERTICES=2500 -DSGL_MAX_POLYGONS=1700 \
    -DSGL_MAX_EVENTS=64 -DSGL_MAX_WORKS=256 \
    -I../SaturnRingLib/modules/dummy -I../SaturnRingLib/modules/SaturnMathPP \
    -I../SaturnRingLib/modules/sgl/INC -I../SaturnRingLib/modules/danny/INC \
    -I../SaturnRingLib/saturnringlib -Isrc \
    src/netbin_sound.cxx
```

Expected: exit 0, no diagnostics. If `SndIniDt`, `SND_INI_PRG_ADR`, or
`slInitSound` are reported undefined, the SGL sound headers are not reached via
`<srl.hpp>` under this flag set — include `sega_snd.h` directly instead.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/netbin_sound.h saturn/src/netbin_sound.cxx saturn/src/main.cxx
git commit -m "Initialize the SGL sound driver from embedded arrays in netbin builds"
```

---

### Task 6: Embedded story loading

Replaces the two CD story reads — the boot load at `main.cxx:1060-1078` and
`saturn_read_story_file` at `main.cxx:514-527`, used by `opcode_restart` — with
copies from the embedded blob.

**Files:**
- Modify: `saturn/src/main.cxx:511-527` (`saturn_read_story_file`)
- Modify: `saturn/src/main.cxx:1054-1078` (boot story load)

**Interfaces:**
- Consumes: `netbin_blobs.h` from Task 1.
- Produces: no new symbols; the Z-machine receives an identical
  `(uint8_t *story, uint32_t len)` pair from RAM instead of CD.

- [ ] **Step 1: Add the include**

In `saturn/src/main.cxx`, with the other project includes:

```cpp
extern "C" {
#include "netbin_blobs.h"
}
```

- [ ] **Step 2: Serve restart from the blob**

Replace the body of `saturn_read_story_file` (`main.cxx:514-527`) so the netbin
path copies from RAM. Keep the CD path byte-for-byte as it is today:

```cpp
// Re-read the story image (used by opcode_restart). In the netbin build the
// story lives in .rodata, so this is a copy rather than a CD read. On CD the
// GFS size read can come back garbage, so retry the stat until it reports the
// expected size, then read the whole file. Returns 1 on success, 0 on failure.
extern "C" int saturn_read_story_file(uint8_t *buf, uint32_t len) {
#ifdef NETBIN
    const unsigned char *src = netbin_story_data();
    if (src == 0 || netbin_story_size() != len) return 0;
    for (uint32_t i = 0; i < len; i++) buf[i] = src[i];
    return 1;
#else
    for (int attempt = 0; attempt < 300; attempt++) {
        SRL::Cd::File f(g_story_filename);
        if (f.Size.SectorSize == 2048 && (uint32_t) f.Size.Bytes == len) {
            if (f.Open()) {
                int32_t got = f.Read((int32_t) len, buf);
                f.Close();
                if (got == (int32_t) len) { return 1; }
            }
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    return 0;
#endif
}
```

- [ ] **Step 3: Serve the boot load from the blob**

Wrap the boot story load (`main.cxx:1060-1078`). The netbin still allocates a
writable copy: the Z-machine mutates the story image in place and `mojo_boot`
takes ownership of the buffer, so handing it a `.rodata` pointer would fault.

```cpp
    uint8_t *story = nullptr;
    uint32_t len = 0;
#ifdef NETBIN
    // The story is embedded in .rodata. The Z-machine writes to its story
    // image and initStory takes ownership of this buffer, so copy into HWRAM
    // rather than pointing at the read-only original.
    {
        const unsigned char *src = netbin_story_data();
        uint32_t n = netbin_story_size();
        if (src != nullptr && n > 0) {
            uint8_t *buf = (uint8_t *) SRL::Memory::HighWorkRam::Malloc(n);
            if (buf != nullptr) {
                for (uint32_t i = 0; i < n; i++) buf[i] = src[i];
                story = buf; len = n;
            }
        }
    }
    if (story == nullptr) { saturn_die("Embedded story missing"); }
#else
    for (int attempt = 0; attempt < 300 && story == nullptr; attempt++) {
        SRL::Cd::File f(game_file);
        int32_t bytes = f.Size.Bytes;
        int32_t ssz   = f.Size.SectorSize;
        SRL::Debug::Print(1, 26, "loading %s...           ", game_file);
        if (ssz == 2048 && bytes > 0 && bytes <= 0x40000) {
            uint8_t *buf = (uint8_t *) SRL::Memory::HighWorkRam::Malloc((uint32_t) bytes);
            if (buf != nullptr && f.Open()) {
                int32_t got = f.Read(bytes, buf);
                f.Close();
                if (got == bytes) { story = buf; len = (uint32_t) bytes; break; }
            }
            if (buf != nullptr) { SRL::Memory::HighWorkRam::Free(buf); }
        }
        for (int i = 0; i < 8; i++) SRL::Core::Synchronize();
    }
    if (story == nullptr) { saturn_die("Could not load %s from CD",game_file); }
#endif
```

- [ ] **Step 4: Syntax-check the CD path**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/saturn
sh syntax-check.sh src/main.cxx
```

Expected: exit 0, no diagnostics — proves the `#else` branch is unchanged and
still compiles.

- [ ] **Step 5: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Load the story image from the embedded blob in netbin builds"
```

---

### Task 7: Boot-path CD guards

The riskiest task: every remaining mandatory CD access in the boot sequence is
skipped under `NETBIN`. Ordering matters — `cd_capture_root` must precede any
`GFS_SetDir`, and `display_scan_images` must precede `options_load`. Skipping
both together preserves that relationship because neither runs.

**Files:**
- Modify: `saturn/src/main.cxx:949-1020` (boot sequence)
- Modify: `saturn/src/main.cxx:1026-1051` (mode menu / game select)

**Interfaces:**
- Consumes: Tasks 1, 5, 6.
- Produces: a boot path that reaches the title screen with no CD access.

- [ ] **Step 1: Guard the pre-menu CD initialization**

In `saturn/src/main.cxx`, replace lines 952-955:

```cpp
#ifndef NETBIN
    cd_capture_root();              // must precede any GFS_SetDir: cd_enter_root() needs it
    display_scan_images();          // must precede options_load: display_decode()
#endif
    // NETBIN: neither runs, so their ordering contract with options_load holds
    // vacuously. display_defaults validates image indices against an empty
    // list, leaving g_display on solid colors -- which is what display_apply
    // falls back to anyway when an image will not load.
    display_defaults(&g_display);   // validates image indices against this list
    options_load();   // restore saved difficulty (defaults to Easy)
```

- [ ] **Step 2: Guard the soft-reset CD state reset**

Replace lines 971-973:

```cpp
#ifndef NETBIN
    GFS_Reset();
    cd_capture_root();   // GFS_Reset returns us to root; re-snapshot it there
    g_z3_dir_valid = false;   // the pre-reset Z3 table is stale until re-scanned
#endif
    // NETBIN: no GFS handles are ever opened, so there is no stale CD state for
    // a soft reset to clear.
```

- [ ] **Step 3: Guard the title-screen preloads**

Replace lines 1009 and 1013-1015:

```cpp
#ifndef NETBIN
    title_bg_show("HOUSE.TGA");
#endif
    title_draw_art();
    SRL::Core::Synchronize();

#ifndef NETBIN
    preload_game_catalog();              // CD reads happen once, here
    display_preload_images();            // and the background art, into Low Work RAM
    ensure_online_typeahead();           // and the online terminal's Zork I vocabulary
#endif
    // NETBIN: nothing to preload. The story and driver are already resident and
    // there is no TGA art. ensure_online_typeahead is skipped here only because
    // there is no CD read left to front-load -- under NETBIN it builds from the
    // embedded story with no CD access at all, so its own call inside
    // online_mode() does the work on first use and costs nothing.
```

**Scope decision, revised 2026-07-21 (user):** online-mode typeahead is
**built from the embedded blob**, not dropped. `ensure_online_typeahead` is
also reachable from `online_mode()` (`main.cxx:811`) with no guard, so leaving
it CD-based would have kept one mandatory CD access alive on the Play Online
path -- against the browser's own disc. `build_typeahead_from_story` and
`apply_solution_overlay` both take `const unsigned char *`, so the netbin path
passes `netbin_story_data()` straight through: no scan, no read, no copy, and
no `Free` (the blob is `.rodata`). See the follow-up commit on this task.

- [ ] **Step 4: Collapse game selection to the embedded title**

`game_select()` enumerates the CD catalog, which does not exist. Replace the
two `game_select()` call sites (lines 1036 and 1048) so the netbin uses a fixed
name. `g_story_filename` still drives save-slot naming, so it must be set.

At line 1035-1044 (the Load Save Game branch):

```cpp
        if (mode == 2) {   // Load Save Game: pick a game, then one of its save slots.
#ifdef NETBIN
            game_file = "ZORK1.Z3";   // the only title in this build
#else
            game_file = game_select();
            if (game_file == nullptr) continue;
#endif
            g_story_filename = game_file;   // so the slot names resolve to this game
```

At line 1048 (the Play Local fall-through):

```cpp
#ifdef NETBIN
        game_file = "ZORK1.Z3";   // the only title in this build
#else
        game_file = game_select();
        if (game_file == nullptr) continue;
#endif
        break;
```

- [ ] **Step 5: Force video re-initialization**

The loader hands over VDP1/VDP2 in whatever state PlanetWeb left them, so the
netbin cannot rely on `Core::Initialize`'s assumptions. Immediately after
`netbin_sound_init()` (added in Task 5), add:

```cpp
#ifdef NETBIN
    // The PlanetWeb loader leaves the VDPs in the browser's state, not a
    // post-reset one. Re-assert the TV mode and clear both scroll screens
    // before anything draws.
    // Mirror srl_core.hpp:75-78 exactly -- same resolution constant, SRL's own
    // VDP1 texture table, same frame rate. Passing NULL here would deregister
    // that table and break every textured sprite.
    // srl_core.hpp:75's SRL::TV::SetScreenSize() is deliberately NOT mirrored:
    // it is private (friend SRL::Core only, srl_tv.hpp:78-95) and SaturnRingLib
    // is pinned. Core::Initialize above already called it with this same
    // INT_SRL_DEF_RES, and the resolution never changes, so it is redundant.
    slInitSystem((uint16_t) INT_SRL_DEF_RES, SRL::VDP1::Textures->SglPtr(), SRL_FRAMERATE);
    slScrAutoDisp(NBG0ON | NBG1ON | NBG2ON | NBG3ON);
    slTVOff();
    slTVOn();
#endif
```

- [ ] **Step 6: Syntax-check the CD path**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/saturn
sh syntax-check.sh src/main.cxx
```

Expected: exit 0, no diagnostics.

- [ ] **Step 7: Syntax-check the NETBIN path**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/saturn
../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe -fsyntax-only \
    -std=gnu++2b -m2 -DNETBIN -DDEBUG \
    -DSRL_MODE_DEBUG -DSRL_FRAMERATE=1 -DSRL_USE_SGL_SOUND_DRIVER=1 \
    -DSRL_MAX_TEXTURES=100 -DSRL_MAX_CD_BACKGROUND_JOBS=5 \
    -DSRL_MAX_CD_FILES=256 -DSRL_MAX_CD_RETRIES=5 \
    -DSRL_DEBUG_MAX_PRINT_LENGTH=64 -DSRL_DEBUG_MAX_LOG_LENGTH=80 \
    -DSGL_MAX_VERTICES=2500 -DSGL_MAX_POLYGONS=1700 \
    -DSGL_MAX_EVENTS=64 -DSGL_MAX_WORKS=256 \
    -I../SaturnRingLib/modules/dummy -I../SaturnRingLib/modules/SaturnMathPP \
    -I../SaturnRingLib/modules/sgl/INC -I../SaturnRingLib/modules/danny/INC \
    -I../SaturnRingLib/saturnringlib -Isrc \
    src/main.cxx
```

Expected: exit 0. Unused-function warnings for `game_select` or
`ensure_online_typeahead` are acceptable; errors are not.

- [ ] **Step 8: Hand both builds to the user**

**Do not build yourself.** Ask the user to run, from `saturn/`:

```
.\compile-netbin.bat
.\compile.bat
```

and report the `zaturn.netbin: NNNNNN bytes` line plus whether the CD build
still succeeds. Expected netbin size: roughly 340,000-360,000 bytes.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/main.cxx
git commit -m "Skip every mandatory CD access in the netbin boot path"
```

---

### Task 8: Companion disc tooling

`music.bat` hardcodes the filename `CONFIG.ME` in both its shell and Windows
blocks, so a second disc needs a config-path override.

**Files:**
- Modify: `tools/assets/music.bat`
- Create: `tools/assets/CONFIG.NETLINK.ME`
- Modify: `tools/assets/README.md`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `music.bat [CONFIG_PATH]` — defaults to `CONFIG.ME`, so every
  existing invocation is unchanged.

- [ ] **Step 1: Parameterize the shell block**

In `tools/assets/music.bat`, change the shell block's config handling. Replace:

```sh
:; cfg() { grep -m1 "^$1=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r'; }
```

with:

```sh
:; CFG_FILE="${1:-CONFIG.ME}"
:; [ -f "$CFG_FILE" ] || { echo "music: config not found: $CFG_FILE" >&2; exit 1; }
:; echo "music: using config $CFG_FILE"
:; cfg() { grep -m1 "^$1=" "$CFG_FILE" | cut -d'=' -f2- | tr -d '\r'; }
```

- [ ] **Step 2: Parameterize the Windows block**

Replace the Windows block's `FOR /F` config parse:

```bat
FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
```

with:

```bat
IF "%~1"=="" (SET "CFG_FILE=CONFIG.ME") ELSE (SET "CFG_FILE=%~1")
IF NOT EXIST "%CFG_FILE%" ( ECHO ERROR: config not found: %CFG_FILE% & EXIT /B 1 )
ECHO Using config: %CFG_FILE%
FOR /F "usebackq tokens=1,* delims==" %%A IN ("%CFG_FILE%") DO (
```

- [ ] **Step 3: Create the companion disc config**

Create `tools/assets/CONFIG.NETLINK.ME`. It carries **only** the three keys
`music.bat` parses — `AUDIO_URL`, `OUTPUT_DIR`, `DISC_NAME`:

```
# Companion audio disc for the .netbin build.
#
# MUSIC ONLY. Do NOT run games.bat against this config: it would stage game
# data tracks onto a disc whose entire purpose is to carry CD-DA for a netbin
# that already has its story file embedded.
#
# Track 01 must be a data track so the disc authenticates as a Saturn disc on
# boot. The NetLink browser image is commonly distributed as a .iso -- just
# rename it to .bin and drop it in as track 01. Because a renamed .iso keeps
# 2048-byte sectors, its cue line must read:
#     TRACK 01 MODE1/2048
# NOT the MODE1/2352 that games.bat emits (games.sh:34) -- that path runs a
# real raw conversion first (games.sh:30). Pairing a renamed .iso with a
# MODE1/2352 declaration yields a disc that fails silently.
AUDIO_URL=https://archive.org/download/sega_saturn/Zork%20I%20-%20The%20Great%20Underground%20Empire%20%28Japan%29.zip
DISC_NAME=NetLink Custom Web Browser
OUTPUT_DIR=./NetLink Custom Web Browser
```

Deliberately omitted, because they belong to `games.bat`: `GAMES_URL`,
`LURKING_URL`, `ADVENT_URL`, `DEST`, `AUDIO_DIR`, `BASE_ISO`.

- [ ] **Step 4: Verify the default path is unchanged**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/tools/assets
sh -c 'CFG_FILE="${1:-CONFIG.ME}"; grep -m1 "^DISC_NAME=" "$CFG_FILE" | cut -d= -f2-' --
```

Expected output:
```
Zaturn - Complete (USA) (Netlink Edition)
```

- [ ] **Step 5: Verify the override path resolves**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/tools/assets
sh -c 'CFG_FILE="${1:-CONFIG.ME}"; grep -m1 "^DISC_NAME=" "$CFG_FILE" | cut -d= -f2-' -- CONFIG.NETLINK.ME
```

Expected output:
```
NetLink Custom Web Browser
```

- [ ] **Step 6: Confirm no games.bat keys leaked in**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/tools/assets
grep -E "^(GAMES_URL|LURKING_URL|ADVENT_URL|DEST|AUDIO_DIR|BASE_ISO)=" \
    CONFIG.NETLINK.ME && echo "FAIL: games.bat key present" || echo "OK: music-only"
```

Expected output:
```
OK: music-only
```

- [ ] **Step 7: Document it**

Append to `tools/assets/README.md`:

```markdown
## Companion audio disc (.netbin builds)

`CONFIG.NETLINK.ME` builds the optional CD-DA companion disc for the
`zaturn.netbin` target. The netbin has Zork I and the sound driver embedded, so
this disc carries **only** music.

```
sh music.bat CONFIG.NETLINK.ME
```

**Do not run `games.bat` against this config.** It is music-only; staging game
data tracks onto it defeats the purpose of the disc.

Track 01 must be a data track so the disc authenticates as a Saturn disc when
in the drive. The NetLink browser image is commonly distributed as a `.iso` —
rename it to `.bin` and use it directly. A renamed `.iso` keeps 2048-byte
sectors, so its cue entry must read `TRACK 01 MODE1/2048`, **not** the
`MODE1/2352` that `games.bat` emits after its real raw conversion
(`lib/games.sh:30`, `:34`). Mismatching those produces a disc that fails
silently rather than erroring at build time.
```

- [ ] **Step 8: Commit**

```bash
git add tools/assets/music.bat tools/assets/CONFIG.NETLINK.ME tools/assets/README.md
git commit -m "Add music-only companion disc config and music.bat config override"
```

---

### Task 9: Regenerate payloads on build, and full regression

Wires blob generation into the build so the embedded story cannot silently go
stale, then verifies both builds and the whole host suite.

**Files:**
- Modify: `saturn/pre.makefile`

**Interfaces:**
- Consumes: `tools/gen_blob.py` from Task 1.
- Produces: `netbin_blobs.c` regenerated on every `NETBIN=1` build.

- [ ] **Step 1: Regenerate blobs during pre-build**

Replace `saturn/pre.makefile` with:

```make
# Included automatically by SaturnRingLib's shared.mk when this file exists
# (shared.mk:215-221). Recipes run under MSYS2 sh with saturn/ as the working
# directory.
pre_build:
	$(info ****** Converting PNG backgrounds to TGA ******)
	@sh ../tools/convert-backgrounds.sh
ifeq ($(strip $(NETBIN)),1)
	$(info ****** Regenerating embedded netbin payloads ******)
	@python3 ../tools/gen_blob.py src/netbin_blobs.c \
	    story=cd/data/Z3/ZORK1.Z3 \
	    sddrvs=cd/data/SDDRVS.TSK \
	    bootsnd=cd/data/BOOTSND.MAP
endif
```

- [ ] **Step 2: Verify the generator runs from saturn/**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork/saturn
python3 ../tools/gen_blob.py /tmp/regen_check.c \
    story=cd/data/Z3/ZORK1.Z3 \
    sddrvs=cd/data/SDDRVS.TSK \
    bootsnd=cd/data/BOOTSND.MAP
```

Expected stderr:
```
gen_blob: story <- cd/data/Z3/ZORK1.Z3 (84876 bytes)
gen_blob: sddrvs <- cd/data/SDDRVS.TSK (26610 bytes)
gen_blob: bootsnd <- cd/data/BOOTSND.MAP (82 bytes)
```

If `python3` is not on PATH under MSYS2, change the recipe to `python`
— `convert-backgrounds.sh` already resolves an interpreter, so match whatever
it uses.

- [ ] **Step 3: Run the full host test suite**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
set -e
gcc -o /tmp/t1 saturn/tests/test_console.c      saturn/src/console.c      && /tmp/t1
gcc -o /tmp/t2 saturn/tests/test_display.c      saturn/src/display.c      && /tmp/t2
gcc -o /tmp/t3 saturn/tests/test_keyboard.c     saturn/src/keyboard.c     && /tmp/t3
gcc -o /tmp/t4 saturn/tests/test_menu_layout.c  saturn/src/menu_layout.c  && /tmp/t4
```

Expected: every binary prints its own `: OK` line and the script exits 0. If
`test_term.c` / `test_term_fixture.c` need extra objects, link them the same
way — check each file's `#include`s for which `src/*.c` it needs.

- [ ] **Step 4: Verify the CD build ignores every netbin artifact**

```bash
cd /c/Users/saggl/CLionProjects/saturn-mojozork
gcc -I saturn/src -c -o /tmp/cdblob.o saturn/src/netbin_blobs.c
ls -l /tmp/cdblob.o
```

Expected: compiles clean and stays under 2 KB, confirming the CD build links
none of the ~111 KB payload.

- [ ] **Step 5: Hand the final builds to the user**

**Do not build yourself.** Ask the user to run, from `saturn/`:

```
.\compile.bat
.\compile-netbin.bat
```

Ask them to confirm:
1. Both builds succeed.
2. `zaturn.netbin` reports a size under 409,600 bytes.
3. `BuildDrop/*.map` shows `PRELOADER 0x06010000` after the netbin build and
   `0x06004000` after the CD build.
4. The CD build still boots and plays normally in Mednafen
   (`run_with_mednafen.bat`).

- [ ] **Step 6: Commit**

```bash
git add saturn/pre.makefile
git commit -m "Regenerate embedded netbin payloads during pre-build"
```

---

## Hardware Verification Checklist

These cannot be verified from the host, the emulator, or code review. They are
the spec's open risks and must be checked on real hardware with a NetLink and
the PlanetWeb 4.0 browser.

- [ ] The netbin loads and reaches the title screen (validates `0x06010000` and the video re-init).
- [ ] Text and background colors apply correctly (validates the empty-image-list fallback).
- [ ] A game starts, accepts input, and `restart` works (validates the embedded story and `saturn_read_story_file`).
- [ ] Save and restore work against backup RAM.
- [ ] Sound Options lists the browser disc's audio tracks (validates that the TOC read after the browser hands over comes back sane, and `toc_is_audio` correctly skips the browser's data track 01).
- [ ] CD-DA plays without skipping during gameplay.
- [ ] **Play Online:** either dials successfully, or fails with a visible error and does **not** hang. A hang here is a defect to fix; a clean failure is an acceptable outcome per the spec.
