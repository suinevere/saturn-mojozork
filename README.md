# saturn-mojozork

A Sega Saturn port of [icculus's MojoZork](https://github.com/icculus/mojozork)
Z-Machine. It boots on real hardware or an emulator and offers two modes:

- **Play Local** — a full Z-Machine (v3) running on the Saturn, playing Zork and
  other v3 story files bundled on the disc.
- **Play Online** — a NetLink telnet terminal that dials into a multizork server
  (e.g. `multizork.icculus.org`) for networked multiplayer.

## Repository layout

```
saturn-mojozork/
├── README.md                 you are here
├── .gitmodules
├── SaturnRingLib/            → git submodule: ReyeMe/SaturnRingLib (Saturn SDK)
├── saturn/                   the Saturn port
│   ├── src/                  main.cxx, console, keyboard, term, net/ (modem+UART)
│   ├── tests/                host-side unit tests (gcc)
│   ├── cd/                   Saturn CD assets (story files under cd/data/Z3/)
│   ├── mojozork.c            the Z-Machine engine
│   ├── makefile
│   ├── compile.bat           build   (debug | release)
│   └── clean.bat
└── docs/                     design specs and notes
```

## Do I need git submodules? — Yes

**SaturnRingLib** is a 1.3 GB third-party SDK with its own history. It is a
**git submodule** (the only one), not vendored, so this repo stays small and the
SDK version is pinned to the exact commit the port builds against. The DreamPi
tunnel used for online play is **not** vendored here — it lives in a separate repo
you clone only if you want to host the dial routing yourself (see *Playing online*,
below).

Two directories from earlier experiments were **removed** because they aren't
required: `joengine/` (a different Saturn SDK, unused) and `coup-saturn/`
(reference only).

---

## Prerequisites

Builds on Windows, Linux, or macOS:
- Git for Windows (**Git Bash**) or a POSIX shell.
- The SaturnRingLib SH-2 cross-compiler, fetched in Step 2 below (≈ needs `curl`/`unzip`).
- An emulator for testing (e.g. **Mednafen** with Saturn BIOS), or real hardware.

---

## 1. Clone (with submodules)

```bash
git clone --recursive git@github.com:suinevere/saturn-mojozork.git
cd saturn-mojozork
```

Already cloned without `--recursive`? Pull the submodules in:

```bash
git submodule update --init --recursive
```

## 2. Install the toolchain (compiler + iso2raw)

The SH-2 toolchain and the `iso2raw` tool are **not** committed to the SDK (large,
gitignored); fetch them once into the submodule. On Windows the SDK's setup script
installs both:

```bat
cd SaturnRingLib
setup_compiler.bat            REM installs the sh2eb-elf gcc AND iso2raw into SaturnRingLib/
cd ..
```

On Linux/macOS fetch the two pieces directly:

```bash
cd SaturnRingLib
./tools/scripts/getcompiler.sh 14.2.0   # sh2eb-elf gcc -> SaturnRingLib/Compiler
./tools/scripts/getiso2raw.sh  v0.2.2   # iso2raw (ISO -> raw .bin) -> SaturnRingLib/tools/bin
cd ..
```

> If `iso2raw` is missing, the compile still produces a bootable `.iso`, but the
> final raw-conversion step errors and prints `Press any key to continue…` — which
> **hangs a non-interactive build**. Installing it (above) avoids that.

## 3. Build

```bash
cd saturn
./compile.bat debug        # or: ./compile.bat release
```

This produces `saturn/BuildDrop/mojozork.iso` (bootable, ISO9660) and
`mojozork.bin` (MODE1/2352 raw, for ODEs/burners), plus `mojozork.elf`/`.map`.
`./clean.bat` removes build output.

> **How the build finds the SDK:** unlike a stock SaturnRingLib project (which sits
> at `SaturnRingLib/Projects/<name>` and locates the SDK via `../..`), this project
> lives in `saturn/` and points at the sibling submodule via `../SaturnRingLib`.
> `compile.bat`/`clean.bat` set `SRL_INSTALL_ROOT=../SaturnRingLib` and pass the
> compiler dir explicitly — no edits to the submodule are needed.

## 4. Run it

- **Emulator:** run `saturn/run_with_mednafen.bat` (loads the built image in
  Mednafen — needs the Saturn BIOS), or open `saturn/BuildDrop/mojozork.iso`.
- **Hardware:** burn/serve `mojozork.bin` (raw MODE1/2352) via a USB/ODE loader.
- **Host-side unit tests** (no Saturn needed) live in `saturn/tests/` and build
  with plain `gcc` — they cover the console, keyboard, and terminal logic.

### First-time Mednafen setup (Windows)

`run_with_mednafen.bat` expects a portable Mednafen at
`SaturnRingLib/emulators/mednafen/` plus the Saturn BIOS in its `firmware/`
subfolder. Set both up once — run from the repo root, and **check
<https://mednafen.github.io/releases/> for the current version** (the filename
below changes with each release):

```bash
# 1. Mednafen itself -> SaturnRingLib/emulators/mednafen/mednafen.exe
curl -L -o mednafen.zip https://mednafen.github.io/releases/files/mednafen-1.32.1-win64.zip
unzip -o mednafen.zip -d SaturnRingLib/emulators/       # extracts a Mednafen/ folder

# 2. Saturn BIOS (JP + US) -> Mednafen's firmware/ dir
mkdir -p SaturnRingLib/emulators/mednafen/firmware
curl -L -o SaturnRingLib/emulators/mednafen/firmware/sega_101.bin  "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/saturnjp%2Fsega_101.bin"
curl -L -o SaturnRingLib/emulators/mednafen/firmware/mpr-17933.bin "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/mpr-17933.bin"
```

The zip extracts a `Mednafen/` folder — on Windows that's the same as `mednafen/`
(case-insensitive); on Linux/macOS rename it to lowercase `mednafen`. `sega_101.bin`
(Japanese) and `mpr-17933.bin` (US) are the two BIOS images; for the authoritative
list and placement see Mednafen's
[Saturn firmware/BIOS docs](https://mednafen.github.io/documentation/ss.html#Section_firmware_bios).
They come from <https://archive.org/download/mame-0.221-roms-merged/saturn.zip>.

---

## 5. Adding a story file to the disc

Local mode scans `saturn/cd/data/Z3/` at startup and lists every v3 story file it
finds there. To add a game:

1. Drop a Z-Machine **version 3** story file into `saturn/cd/data/Z3/`, e.g.
   `saturn/cd/data/Z3/MYGAME.Z3`.
2. Rebuild: `cd saturn && ./compile.bat debug`.
3. The new game appears in the **Play Local** story menu.

Only v3 files are supported (Zork 1–3 and many other early Infocom titles);
later (v4+) games will not run. `ZORK1.Z3`, `ZORK2.Z3`, `ZORK3.Z3`, and
`SORCERER.z3` ship on the disc already.

---

## 6. Playing online from a real Saturn

**Play Online** dials a NetLink modem into a **DreamPi** running the Netlink
tunnel, which relays the dialed code to a multizork telnet server
(e.g. `multizork.icculus.org`) over TCP.

The tunnel isn't part of this repo. To route dial code `199403` to multizork you
edit your **existing DreamPi** (the one already running the Netlink tunnel image) —
you do **not** clone anything. This is a temporary local change until the entry is
merged upstream into [eaudunord/Netlink](https://github.com/eaudunord/Netlink),
after which DreamPi auto-update distributes it:

1. Delete `/boot/noautoupdates.txt` from the DreamPi's SD card.
2. SSH in (or log in) as user `pi` (password `raspberry`).
3. Add this block to `/dreampi/netlink_config.ini`, then restart the DreamPi:

```ini
[server:199403]
name = MultiZork
host = multizork.icculus.org
port = 23
handler = transparent
```

`handler = transparent` is required — multizork does no AUTH handshake. The Saturn
client design is documented under `docs/`.

---

## Credits & license

- **MojoZork** and **multizorkd** by Ryan C. "Icculus" Gordon — zlib license
  (`saturn/LICENSE.txt`). This is a fork; the Z-Machine engine and the original
  multiplayer server are his.
- **SaturnRingLib** by ReyeMe et al.
- **DreamPi / modem tunnel** — the eaudunord Netlink tunnel, derived from Kazade's
  DreamPi work: <https://github.com/eaudunord/Netlink>.
- Zork I/II/III data files are distributed for free by Activision.
- Saturn port and tooling in this repo: Suinevere.
