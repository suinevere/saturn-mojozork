# Lurking Horror Sound Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play The Lurking Horror's sound effects on the Saturn by implementing the Z-machine `sound_effect` opcode and streaming the game's `.BLB` samples to the SCSP.

**Architecture:** A pure-C Blorb parser builds an in-memory index of the 14 sounds from `LRKHOROR.BLB` (headers only; PCM stays on CD). A C++ playback module reads a requested sound's PCM slice from the CD and plays it through SRL's PCM driver, mixing a looping ambient bed under one-shots and re-triggering loops each frame. mojozork's `sound_effect` opcode (245) calls a `GState` function pointer wired to the Saturn playback hook. An Options toggle (persisted) gates it.

**Tech Stack:** C (parser, mojozork core), C++ (SRL / SGL PCM via `slPCMOn`/`slPCMStat`/`slPCMOff`), SaturnRingLib `SRL::Cd::File` + `SRL::Sound::Pcm`, host `gcc` for the parser unit test.

## Global Constraints

- Build the Saturn client with `saturn/compile.bat` (run from the `saturn/` dir). Never invoke `make`/`gcc` directly for the Saturn image.
- The parser (`sound_blorb.c`) MUST be pure C (no SRL/C++), so it host-compiles and is reused by both the Saturn and the (silent) server build.
- Audio format is fixed by the file: mono, 8-bit signed PCM, uncompressed; sample rate is **per-sound** (read each `COMM`; e.g. #3=9676 Hz, #4=12902 Hz).
- `sound_effect` on non-Saturn builds (multizork server, host tests) MUST be a silent no-op — no link dependency on Saturn code.
- Sound file lives at `saturn/cd/data/Z3/LRKHOROR.BLB` (already on the CD).
- Reference for on-device audio: it can only be truly verified on hardware/emulator. Every non-parser task's automated gate is "the image builds"; audio behavior is checked on-device at the end.

---

### Task 1: Blorb sound-index parser (pure C, host-tested)

**Files:**
- Create: `saturn/src/sound_blorb.h`
- Create: `saturn/src/sound_blorb.c`
- Test: `test/sound_blorb_test.c`

**Interfaces:**
- Produces:
  - `typedef int (*sound_read_fn)(unsigned int off, unsigned int len, unsigned char* buf);` — returns 1 on success, 0 on failure.
  - `int sound_blorb_open(sound_read_fn read);` — parse the Blorb; returns the number of sounds indexed (0 = absent/invalid).
  - `int sound_blorb_get(int number, unsigned int* off, unsigned int* len, unsigned short* rate, int* loops);` — 1 if the sound exists (fills SSND data offset/length within the file, sample rate, and loop flag), else 0.
  - `void sound_blorb_close(void);` — clear the index.

- [ ] **Step 1: Write the failing test**

Create `test/sound_blorb_test.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include "sound_blorb.h"

static FILE* g_f;
static int reader(unsigned int off, unsigned int len, unsigned char* buf) {
    if (fseek(g_f, (long)off, SEEK_SET) != 0) return 0;
    return fread(buf, 1, len, g_f) == len;
}
#define CHECK(c) do{ if(!(c)){ printf("FAIL: %s\n", #c); fails++; } }while(0)

int main(int argc, char** argv) {
    int fails = 0;
    g_f = fopen(argv[1], "rb");
    if (!g_f) { printf("cannot open %s\n", argv[1]); return 2; }

    int n = sound_blorb_open(reader);
    CHECK(n == 14);

    unsigned int off, len; unsigned short rate; int loops;
    CHECK(sound_blorb_get(3, &off, &len, &rate, &loops) == 1);
    CHECK(off == 362 && len == 49756 && rate == 9676 && loops == 0);

    CHECK(sound_blorb_get(4, &off, &len, &rate, &loops) == 1);
    CHECK(off == 50172 && len == 20520 && rate == 12902 && loops == 1);

    CHECK(sound_blorb_get(18, &off, &len, &rate, &loops) == 1 && loops == 1);
    CHECK(sound_blorb_get(5, &off, &len, &rate, &loops) == 0);   // no Snd #5

    sound_blorb_close();
    printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

Create `saturn/src/sound_blorb.h`:

```c
#ifndef SOUND_BLORB_H
#define SOUND_BLORB_H
#ifdef __cplusplus
extern "C" {
#endif

/* Reads `len` bytes at `off` into `buf`; returns 1 on success, 0 on failure. */
typedef int (*sound_read_fn)(unsigned int off, unsigned int len, unsigned char* buf);

/* Parse a Blorb (.BLB) via `read`. Returns the number of sounds indexed
   (0 if the file is absent or not a valid Blorb). */
int  sound_blorb_open(sound_read_fn read);

/* Look up a sound by its Z-machine number. On success returns 1 and fills the
   SSND data offset + length (bytes within the file), the sample rate (Hz), and
   whether it loops. Returns 0 if the number is unknown. */
int  sound_blorb_get(int number, unsigned int* off, unsigned int* len,
                     unsigned short* rate, int* loops);

/* Clear the index (call on game switch). */
void sound_blorb_close(void);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cd /c/Users/saggl/CLionProjects/zaturn
gcc -O2 -I saturn/src -o /tmp/sbt test/sound_blorb_test.c saturn/src/sound_blorb.c
```
Expected: FAIL — link error, `sound_blorb.c` does not exist yet (or undefined references).

- [ ] **Step 3: Write the parser**

Create `saturn/src/sound_blorb.c`:

```c
#include "sound_blorb.h"

#define MAXSND 32

typedef struct {
    int number;
    unsigned int off, len;   /* SSND data offset + length within the file */
    unsigned short rate;
    int loops;
} SndEntry;

static SndEntry g_snd[MAXSND];
static int      g_count;

static unsigned int be32(const unsigned char* p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | p[3];
}
static unsigned int be16(const unsigned char* p) {
    return ((unsigned int)p[0] << 8) | p[1];
}
static int tag(const unsigned char* p, const char* t) {
    return p[0]==t[0] && p[1]==t[1] && p[2]==t[2] && p[3]==t[3];
}

/* Decode an 80-bit IEEE extended float (AIFF COMM sample rate) to an int Hz. */
static unsigned short ext_rate(const unsigned char* p) {
    int exp = (int)(be16(p) & 0x7fff);
    /* Only the top 32 bits of the 64-bit mantissa matter for sane rates. */
    unsigned int hi = be32(p + 2);
    if (exp == 0) return 0;
    /* value = hi * 2^(exp-16383-31) */
    int shift = exp - 16383 - 31;
    double v = (double)hi;
    while (shift > 0) { v *= 2.0; shift--; }
    while (shift < 0) { v *= 0.5; shift++; }
    return (unsigned short)(v + 0.5);
}

/* Parse one sound's FORM…AIFF at file offset `start`: fill data off/len/rate. */
static int parse_sound(sound_read_fn read, unsigned int start,
                       unsigned int* off, unsigned int* len, unsigned short* rate) {
    unsigned char b[160];
    if (!read(start, sizeof b, b)) return 0;
    if (!tag(b, "FORM") || !tag(b + 8, "AIFF")) return 0;
    unsigned int formlen = be32(b + 4);
    unsigned int p = 12;                 /* first chunk within the read window */
    unsigned int endw = sizeof b;
    *rate = 0; *off = 0; *len = 0;
    while (p + 8 <= endw && p < 8 + formlen) {
        const unsigned char* c = b + p;
        unsigned int clen = be32(c + 4);
        if (tag(c, "COMM")) *rate = ext_rate(c + 16);        /* rate at COMM+16 */
        else if (tag(c, "SSND")) { *off = start + p + 16; *len = clen - 8; }
        p += 8 + clen + (clen & 1);
        if (*rate && *off) break;        /* got both */
    }
    return (*off != 0 && *rate != 0);
}

int sound_blorb_open(sound_read_fn read) {
    unsigned char hdr[12];
    g_count = 0;
    if (!read || !read(0, 12, hdr)) return 0;
    if (!tag(hdr, "FORM") || !tag(hdr + 8, "IFRS")) return 0;

    /* RIdx is the first chunk after the FORM/IFRS header, at offset 12. */
    unsigned char rh[8];
    if (!read(12, 8, rh) || !tag(rh, "RIdx")) return 0;
    unsigned char nbuf[4];
    if (!read(20, 4, nbuf)) return 0;
    int nres = (int)be32(nbuf);
    if (nres < 0 || nres > 64) return 0;

    unsigned int maxstart = 0;
    for (int i = 0; i < nres && g_count < MAXSND; i++) {
        unsigned char e[12];
        if (!read(24 + (unsigned)i * 12, 12, e)) return 0;
        if (!tag(e, "Snd ")) continue;
        int num = (int)be32(e + 4);
        unsigned int start = be32(e + 8);
        unsigned int off, len; unsigned short rate;
        if (parse_sound(read, start, &off, &len, &rate)) {
            g_snd[g_count].number = num;
            g_snd[g_count].off = off; g_snd[g_count].len = len;
            g_snd[g_count].rate = rate; g_snd[g_count].loops = 0;
            g_count++;
            if (start > maxstart) maxstart = start;
        }
    }

    /* The Loop chunk follows the last resource (highest start). Find it there. */
    unsigned char fh[8];
    if (maxstart && read(maxstart, 8, fh) && tag(fh, "FORM")) {
        unsigned int fl = be32(fh + 4);
        unsigned int lp = maxstart + 8 + fl + (fl & 1);
        unsigned char lh[8];
        if (read(lp, 8, lh) && tag(lh, "Loop")) {
            unsigned int llen = be32(lh + 4);
            int pairs = (int)(llen / 8);
            for (int i = 0; i < pairs; i++) {
                unsigned char pe[8];
                if (!read(lp + 8 + (unsigned)i * 8, 8, pe)) break;
                int num = (int)be32(pe);
                for (int j = 0; j < g_count; j++)
                    if (g_snd[j].number == num) g_snd[j].loops = 1;
            }
        }
    }
    return g_count;
}

int sound_blorb_get(int number, unsigned int* off, unsigned int* len,
                    unsigned short* rate, int* loops) {
    for (int i = 0; i < g_count; i++) {
        if (g_snd[i].number == number) {
            *off = g_snd[i].off; *len = g_snd[i].len;
            *rate = g_snd[i].rate; *loops = g_snd[i].loops;
            return 1;
        }
    }
    return 0;
}

void sound_blorb_close(void) { g_count = 0; }
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
gcc -O2 -I saturn/src -o /tmp/sbt test/sound_blorb_test.c saturn/src/sound_blorb.c && \
  /tmp/sbt saturn/cd/data/Z3/LRKHOROR.BLB
```
Expected: `ALL PASS`.

- [ ] **Step 5: Extract one sound to WAV and listen (parser sanity)**

Run this host script to prove the offsets/rate produce real audio:
```bash
python -c "
import struct
d=open('saturn/cd/data/Z3/LRKHOROR.BLB','rb').read()
off,ln,rate=362,49756,9676               # Snd #3 (from the test)
pcm=bytes((b^0x80) for b in d[off:off+ln])   # signed 8-bit -> unsigned for WAV
with open('/tmp/snd3.wav','wb') as f:
    f.write(b'RIFF'+struct.pack('<I',36+ln)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,rate,rate,1,8)+b'data'+struct.pack('<I',ln)+pcm)
print('wrote /tmp/snd3.wav', ln, 'bytes @', rate, 'Hz')
"
```
Open `/tmp/snd3.wav` in an audio player and confirm it is a recognizable Lurking Horror effect (not noise). This validates the parser end-to-end.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/sound_blorb.h saturn/src/sound_blorb.c test/sound_blorb_test.c
git commit -m "Blorb sound-index parser for Lurking Horror .BLB"
```

---

### Task 2: `sound_effect` opcode via a GState hook (mojozork core)

**Files:**
- Modify: `saturn/mojozork.c` (ZMachineState struct near line 88-132; opcode table near line 1986; add an `opcode_sound_effect` beside the other opcodes)

**Interfaces:**
- Consumes: nothing.
- Produces: `GState->sound_effect` — a `void (*sound_effect)(int number, int effect, int volume)` field on `ZMachineState`, called by opcode 245 when non-NULL.

- [ ] **Step 1: Add the function pointer to the state struct**

In `saturn/mojozork.c`, inside `typedef struct ZMachineState { … }`, next to the other hooks (`writestr`, `readline`, `die`), add:

```c
    void (*sound_effect)(int number, int effect, int volume);  /* NULL = silent */
```

- [ ] **Step 2: Implement the opcode**

Add this function near the other `opcode_*` definitions (e.g. just above `opcode_restore`):

```c
static void opcode_sound_effect(void)
{
    const uint16 number = GState->operand_count > 0 ? GState->operands[0] : 0;
    const uint16 effect = GState->operand_count > 1 ? GState->operands[1] : 2;
    const uint16 volume = GState->operand_count > 2 ? GState->operands[2] : 255;
    if (GState->sound_effect) {
        GState->sound_effect((int) number, (int) effect, (int) volume);
    }
}
```

- [ ] **Step 3: Register the opcode (replace the stub)**

Find `OPCODE_WRITEME(245, sound_effect);` (near line 1986) and replace it with:

```c
    OPCODE(245, sound_effect);
```

(Leave the v5 `sound_effect_ver5` stub at line ~2023 unchanged — Lurking Horror is v3.)

- [ ] **Step 4: Build the Saturn image to verify it compiles**

Run:
```bash
cd saturn && ./compile.bat 2>&1 | grep -iE "error:" | grep -v mojozork.c: | head; ls BuildDrop/mojozork.bin >/dev/null && echo BUILD_OK
```
Expected: `BUILD_OK`, no errors. (`GState->sound_effect` is NULL for now, so the opcode is a safe no-op.)

- [ ] **Step 5: Commit**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
git add saturn/mojozork.c
git commit -m "Implement sound_effect opcode (245) via a GState hook"
```

---

### Task 3: PCM playback module + enable the sound driver

**Files:**
- Create: `saturn/src/sound.h`
- Create: `saturn/src/sound.cxx`
- Modify: `saturn/Makefile:11` (`SRL_USE_SGL_SOUND_DRIVER`)

**Interfaces:**
- Consumes: `sound_blorb.h` (`sound_blorb_open/get/close`), `SRL::Cd::File`, `SRL::Sound::Pcm` (`slPCMOn`/`slPCMStat`/`slPCMOff`, `Pcm::Channels`, `Pcm::FindChannel`, `IPcmFile::PlayOnChannel`).
- Produces (extern "C"):
  - `void sound_init(const char* blbfile);`
  - `void saturn_sound_effect(int number, int effect, int volume);`
  - `void sound_service(void);`
  - `void sound_stop_all(void);`
  - `void sound_set_enabled(int on);`

- [ ] **Step 1: Enable the SGL sound driver**

In `saturn/Makefile`, change line 11:
```make
SRL_USE_SGL_SOUND_DRIVER = 1    # Set to 1 if you want to use SGL sound driver, this will copy necessary files into the CD folder
```

- [ ] **Step 2: Write the header**

Create `saturn/src/sound.h`:

```c
#ifndef SATURN_SOUND_H
#define SATURN_SOUND_H
#ifdef __cplusplus
extern "C" {
#endif

/* Load the sound index for the loaded game from CD file `blbfile` (e.g.
   "LRKHOROR.BLB"). Absent/unparsable file -> sound stays off (no error). */
void sound_init(const char* blbfile);

/* Z-machine sound_effect hook: effect 1=prepare, 2=start, 3=stop, 4=finish;
   volume 1-8 (255=default). No-op when disabled or the sound is unknown. */
void saturn_sound_effect(int number, int effect, int volume);

/* Call once per input frame: re-trigger looping sounds, reap finished ones. */
void sound_service(void);

/* Stop all channels + free buffers (game switch / reboot). */
void sound_stop_all(void);

/* Options toggle. Disabling stops everything. */
void sound_set_enabled(int on);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 3: Write the playback module**

Create `saturn/src/sound.cxx`:

```c
#include <srl.hpp>
extern "C" {
#include "sound.h"
#include "sound_blorb.h"
}

// A pre-loaded PCM slice we can hand to SRL's PCM machinery. IPcmFile's fields
// are protected, so a subclass fills them; we own the buffer ourselves.
class SlicePcm : public SRL::Sound::Pcm::IPcmFile {
public:
    void set(int8_t* d, uint32_t n, uint16_t r) {
        data = d; dataSize = n; mode = _Mono; depth = _PCM8Bit; sampleRate = r;
    }
};

#define NSLOT 4                     // matches SRL's 4 PCM channels
struct Slot {
    int      number;               // Z sound number, 0 = free
    int      channel;              // SRL PCM channel, -1 = none
    int      loops;
    int8_t*  buf;
    SlicePcm pcm;
};

static char  g_blb[16];
static int   g_have;               // 1 if the index loaded
static int   g_enabled = 1;        // Options toggle
static Slot  g_slot[NSLOT];

// CD reader for the parser: open the .BLB and read a slice.
static int cd_reader(unsigned int off, unsigned int len, unsigned char* out) {
    SRL::Cd::File f(g_blb);
    if (f.Size.SectorSize != 2048) return 0;
    return f.LoadBytes((int32_t) off, (int32_t) len, out) == (int32_t) len;
}

static int8_t* load_slice(unsigned int off, unsigned int len) {
    // slPCMOn won't play samples shorter than 0x900; pad with silence.
    uint32_t n = len < 0x900 ? 0x900 : len;
    int8_t* b = (int8_t*) SRL::Memory::HighWorkRam::Malloc(n);
    if (!b) return nullptr;
    for (uint32_t i = len; i < n; i++) b[i] = 0;
    SRL::Cd::File f(g_blb);
    if (f.Size.SectorSize != 2048 ||
        f.LoadBytes((int32_t) off, (int32_t) len, (uint8_t*) b) != (int32_t) len) {
        SRL::Memory::Free(b); return nullptr;
    }
    return b;
}

static void free_slot(Slot& s) {
    if (s.channel >= 0) slPCMOff(&SRL::Sound::Pcm::Channels[s.channel]);
    if (s.buf) { SRL::Memory::Free(s.buf); s.buf = nullptr; }
    s.number = 0; s.channel = -1; s.loops = 0;
}

extern "C" void sound_init(const char* blbfile) {
    for (int i = 0; i < NSLOT; i++) { g_slot[i].number = 0; g_slot[i].channel = -1; g_slot[i].buf = nullptr; }
    g_have = 0; g_blb[0] = '\0';
    if (!blbfile) return;
    int j = 0; for (; blbfile[j] && j < 15; j++) g_blb[j] = blbfile[j]; g_blb[j] = '\0';
    if (sound_blorb_open(cd_reader) > 0) g_have = 1;
}

extern "C" void sound_stop_all(void) {
    for (int i = 0; i < NSLOT; i++) free_slot(g_slot[i]);
}

extern "C" void sound_set_enabled(int on) {
    g_enabled = on;
    if (!on) sound_stop_all();
}

extern "C" void saturn_sound_effect(int number, int effect, int volume) {
    if (!g_enabled || !g_have) return;
    unsigned int off, len; unsigned short rate; int loops;
    if (!sound_blorb_get(number, &off, &len, &rate, &loops)) return;

    if (effect == 3 || effect == 4) {           // stop / finish
        for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == number) free_slot(g_slot[i]);
        return;
    }
    if (effect != 2 && effect != 1) return;      // only start / prepare handled
    if (effect == 1) return;                     // prepare: on-demand load is fast enough

    // start: if this looping sound is already active, leave it be.
    for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == number && g_slot[i].channel >= 0) return;

    int free = -1; for (int i = 0; i < NSLOT; i++) if (g_slot[i].number == 0) { free = i; break; }
    if (free < 0) return;                         // all channels busy: drop it

    int8_t* buf = load_slice(off, len);
    if (!buf) return;
    Slot& s = g_slot[free];
    s.number = number; s.loops = loops; s.buf = buf;
    s.pcm.set(buf, len < 0x900 ? 0x900 : len, rate);
    uint8_t vol = (volume == 255 || volume <= 0) ? 100 : (uint8_t)((volume > 8 ? 8 : volume) * 127 / 8);
    s.channel = s.pcm.Play(vol);
    if (s.channel < 0) free_slot(s);              // no channel: undo
}

extern "C" void sound_service(void) {
    if (!g_enabled) return;
    for (int i = 0; i < NSLOT; i++) {
        Slot& s = g_slot[i];
        if (s.number == 0 || s.channel < 0) continue;
        if (slPCMStat(&SRL::Sound::Pcm::Channels[s.channel]) == 0) {   // finished
            if (s.loops) s.pcm.PlayOnChannel((uint8_t) s.channel, 100);  // re-trigger
            else free_slot(s);
        }
    }
}
```

- [ ] **Step 4: Build to verify it compiles and links (with the driver on)**

Run:
```bash
cd saturn && rm -f src/sound.o src/sound_blorb.o && ./compile.bat 2>&1 | \
  grep -iE "error:|undefined ref" | grep -v mojozork.c: | head
ls BuildDrop/mojozork.bin >/dev/null && echo BUILD_OK
```
Expected: `BUILD_OK`, no errors (`slPCMOn`/`slPCMStat`/`slPCMOff` resolve from SGL; `sound.cxx` and `sound_blorb.c` compile via the Makefile glob).

- [ ] **Step 5: Commit**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
git add saturn/src/sound.h saturn/src/sound.cxx saturn/Makefile
git commit -m "PCM playback module for Lurking Horror sounds; enable SGL sound driver"
```

---

### Task 4: Wire boot, hook, and per-frame service

**Files:**
- Modify: `saturn/src/saturn_glue.h` (declare the hook + entry points)
- Modify: `saturn/src/mojozork_saturn.c:mojo_boot` (set `GState->sound_effect`)
- Modify: `saturn/src/main.cxx` (include `sound.h`; `sound_init` on game load; `sound_service` in the input loop; `sound_stop_all` on reboot/soft-reset)

**Interfaces:**
- Consumes: `sound_init`, `saturn_sound_effect`, `sound_service`, `sound_stop_all` (Task 3); `GState->sound_effect` (Task 2).
- Produces: nothing new.

- [ ] **Step 1: Declare in the glue header**

In `saturn/src/saturn_glue.h`, inside the `extern "C"` block, add:

```c
/* Z-machine sound_effect hook (implemented in sound.cxx). */
void saturn_sound_effect(int number, int effect, int volume);
```

- [ ] **Step 2: Set the hook at boot**

In `saturn/src/mojozork_saturn.c`, inside `mojo_boot`, after the other `GState->…` hook assignments, add:

```c
    GState->sound_effect = saturn_sound_effect;
```

- [ ] **Step 3: Initialize sounds on game load**

In `saturn/src/main.cxx`, add `#include "sound.h"` to the `extern "C"` include block.

In `main()`, immediately after the story is loaded and `mojo_boot(story, len, seed);` is called (near line ~1505, before `mojo_run();`), derive the `.BLB` name from `g_story_filename` and init:

```c
    {   // enable sound if the game ships a sibling <base>.BLB
        char blb[16]; int i = 0;
        for (; g_story_filename[i] && g_story_filename[i] != '.' && i < 11; i++) blb[i] = g_story_filename[i];
        blb[i] = '.'; blb[i+1] = 'B'; blb[i+2] = 'L'; blb[i+3] = 'B'; blb[i+4] = '\0';
        sound_init(blb);
    }
```

- [ ] **Step 4: Service sounds each input frame**

In `saturn/src/main.cxx`, in `saturn_readline`'s inner `while (!k.submitted)` loop, right after `SRL::Core::Synchronize();` at the end of the loop body, add:

```c
        sound_service();
```

- [ ] **Step 5: Stop sounds on soft reset**

In `saturn/src/main.cxx`, in `soft_reset_to_title()`, before the `longjmp`, add:

```c
    sound_stop_all();
```

- [ ] **Step 6: Build to verify it compiles and links**

Run:
```bash
cd saturn && rm -f src/main.o src/mojozork_saturn.o && ./compile.bat 2>&1 | \
  grep -iE "error:|undefined ref" | grep -v mojozork.c: | head
ls BuildDrop/mojozork.bin >/dev/null && echo BUILD_OK
```
Expected: `BUILD_OK`.

- [ ] **Step 7: Commit**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
git add saturn/src/saturn_glue.h saturn/src/mojozork_saturn.c saturn/src/main.cxx
git commit -m "Wire sound: boot hook, .BLB init, per-frame service, stop on reset"
```

---

### Task 5: Options "Sound: On / Off" toggle + persistence

**Files:**
- Modify: `saturn/src/main.cxx` (`g_sound_on` global near `g_difficulty`; `options_load`/`options_save`; `options_menu`; apply on boot)

**Interfaces:**
- Consumes: `sound_set_enabled` (Task 3).
- Produces: `g_sound_on` persisted in the `MOJOOPTS` blob.

- [ ] **Step 1: Add the global and default**

In `saturn/src/main.cxx`, near `static int g_difficulty = DIFF_EASY;`, add:

```c
static int g_sound_on = 1;   // Options toggle; persisted in MOJOOPTS
```

- [ ] **Step 2: Persist it in the blob**

In `options_save`, after writing the dial number's terminating `0`, append the sound flag:

```c
    buf[n++] = (uint8_t) g_sound_on;
```

In `options_load`, after reading the dial number (after its NUL), read the flag if present. Replace the dial-number parse tail so it advances an index `i` you can reuse; then:

```c
    /* byte after the dial number's NUL is the sound flag (default on). */
    /* (i points just past the dial-number NUL here) */
    if (i < (int) sizeof(buf)) g_sound_on = buf[i] ? 1 : 0;
```

(Implementation note: track the read offset `i` through the dial-number loop so this reads the correct byte; older blobs have zeroed tail → the byte is 0. To keep the default "on" for pre-sound saves, treat a fully-absent tail as on: only override when the blob is long enough to include it. Simplest: initialize `g_sound_on = 1` and set from the byte only when `buf` had a non-zero difficulty/dial section indicating a written blob — since a real saved blob always has the flag once this code ships. Given old blobs zero the tail, they will read `g_sound_on = 0`; to avoid silencing them, store the flag as `sound ? 1 : 2` and read `!= 2 ? on`… — instead, store `g_sound_on + 1` (1=off→? ) …) *Resolve concretely:* store the flag as `g_sound_on ? 2 : 1` in save, and in load `g_sound_on = (byte == 0) ? 1 : (byte == 2);` so a zeroed tail (old blob) reads as **on**.)

Concretely, use these two lines. In `options_save`:
```c
    buf[n++] = (uint8_t) (g_sound_on ? 2 : 1);   // 0 = absent (old blob) -> on
```
In `options_load`, after advancing past the dial number + its NUL to offset `i`:
```c
    g_sound_on = (buf[i] == 1) ? 0 : 1;          // 1 = off; 0 (absent) or 2 = on
```

- [ ] **Step 3: Add the menu row**

In `options_menu`, raise the item count and add a "Sound" row. Change `const int NITEMS = 4;` to `5`, insert Sound at index 3 and shift Done to 4:

- Toggle with Left/Right (or A) when `sel == 3`:
```c
        if (sel == 3 && (left || right ||
            g_pad->WasPressed(Button::A) || ke.kind == SATURN_KEY_ENTER)) g_sound_on = !g_sound_on;
```
(Apply this only when `sel == 3`; keep the existing difficulty Left/Right under `sel == 0`, and gate the generic `act` so it doesn't also fire Sound — i.e. handle Sound before the `if (act)` block and `continue`/skip activating it there.)

- Update the activate block: `else if (sel == 4) break;` for Done, and drop through for Sound (handled above).

- Draw the row:
```c
        SRL::Debug::Print(x0 + 2, y0 + 8, "%c Sound: %s", sel == 3 ? '>' : ' ', g_sound_on ? "On" : "Off");
        SRL::Debug::Print(x0 + 2, y0 + 9, "%c Done", sel == 4 ? '>' : ' ');
```
(Shift "Return to Title Screen" up to `y0 + 7` for `sel == 2` and "Done" to `y0 + 9`/`sel == 4`; bump box height `h` from 11 to 12 so the extra row fits.)

- [ ] **Step 4: Apply the toggle (menu close + boot)**

At the end of `options_menu`, after persisting difficulty, apply the sound state:
```c
    sound_set_enabled(g_sound_on);
```
Also call `sound_set_enabled(g_sound_on);` once right after `sound_init(blb);` in `main()` (Task 4, Step 3) so a saved "off" is honored from the first prompt.

- [ ] **Step 5: Build to verify it compiles**

Run:
```bash
cd saturn && rm -f src/main.o && ./compile.bat 2>&1 | grep -iE "error:" | grep -v mojozork.c: | head
ls BuildDrop/mojozork.bin >/dev/null && echo BUILD_OK
```
Expected: `BUILD_OK`.

- [ ] **Step 6: Commit**

```bash
cd /c/Users/saggl/CLionProjects/zaturn
git add saturn/src/main.cxx
git commit -m "Options: Sound On/Off toggle, persisted in MOJOOPTS"
```

---

### Task 6: On-device verification (manual)

**Files:** none (verification only).

- [ ] **Step 1: Full build**

```bash
cd saturn && ./compile.bat 2>&1 | tail -3
ls -la BuildDrop/mojozork.bin
```
Expected: build completes; `mojozork.bin` produced; the CD image includes the SGL sound driver files (from `SRL_USE_SGL_SOUND_DRIVER = 1`).

- [ ] **Step 2: Run The Lurking Horror on hardware/emulator and verify**

Confirm each:
1. Boot The Lurking Horror; reach a point that triggers a sound (early game has effects) — a sample plays.
2. A looping ambient bed keeps playing and continues under a one-shot effect (faithful mixing).
3. Stopping / leaving the area stops the looping sound.
4. Options → Sound: Off silences everything; On restores it; the setting survives a reboot.
5. A silent game (e.g. Zork I) plays normally with no errors and no audio.

Note the accepted risk: a short looping sound may have a faint gap on re-trigger. If audible, the follow-up (not in this plan) is to drive the SCSP loop registers directly for seamless looping.
