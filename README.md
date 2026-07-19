i# saturn-mojozork

A Sega Saturn port of [icculus's MojoZork](https://github.com/icculus/mojozork)
Z-Machine. It boots on real hardware or an emulator and offers two modes:

- **Play Local** — a full Z-Machine (v3) running on the Saturn, playing Zork and
  other v3 story files bundled on the disc.
- **Play Online** — a NetLink telnet terminal that dials into a multizork server
  (our self-hosted `suinevere.duckdns.org`) for networked multiplayer.

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
│   ├── multizorkd.c          the multiplayer telnet server
│   ├── makefile
│   ├── compile.bat           build   (debug | release)
│   └── clean.bat
├── docker/                   self-contained Docker host for the multizork server
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
- **Python 3.9+** *(optional)* — converts the background art in `tools/assets/png/`
  into disc-ready TGAs during the build, provisioning its own virtualenv on first
  run. Without it the build still succeeds using the TGAs already committed under
  `saturn/cd/data/TGA/`.
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

### If `iso2raw` is missing on macos/linux:

```bash
cd SaturnRingLib
./tools/scripts/getcompiler.sh 14.2.0   # sh2eb-elf gcc -> SaturnRingLib/Compiler
./tools/scripts/getiso2raw.sh  v0.2.2   # iso2raw (ISO -> raw .bin) -> SaturnRingLib/tools/bin
cd ..
```

## 3. Build

```bash
cd saturn
./compile.bat debug        # or: ./compile.bat release
```

This produces `saturn/BuildDrop/mojozork.iso` (bootable, ISO9660) and
`mojozork.bin` (MODE1/2352 raw, for ODEs/burners), plus `mojozork.elf`/`.map`.
`./clean.bat` removes build output.

> **If Mednafen rejects the image** with an error like
> `M:S:F time "102:16:72" contains components out of range`, the build wrote a
> corrupt `mojozork.cue`/`.bin` pair because `BuildDrop/` was not cleared — the
> previous output was still held open by another process (an emulator with the
> image loaded, or a burner/ODE tool). The build appends rather than replacing,
> so track offsets run past the ~80-minute Red Book limit and the MSF minutes
> field overflows. Close anything holding the image, run `./clean.bat` (or
> delete `BuildDrop/`), and rebuild. The MSF values are a symptom of the stale
> output, not a problem with the audio tracks.

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

## First-time Mednafen setup

`run_with_mednafen.bat` is a portable Mednafen at
`SaturnRingLib/emulators/mednafen/` plus the Saturn BIOS in its `firmware/`
subfolder. Set both up once.

#### Windows

— run from the repo root, and **check
[https://mednafen.github.io/releases/](https://mednafen.github.io/releases/) for the current version** (the filename
below changes with each release):

```bash
# 1. Mednafen itself -> SaturnRingLib/emulators/mednafen/mednafen.exe
curl -L -o mednafen.zip https://mednafen.github.io/releases/files/mednafen-1.32.1-win64.zip
unzip -o mednafen.zip -d SaturnRingLib/emulators/       # extracts a Mednafen/ folder
```

#### Linux

```bash
# 1. Mednafen with aptget or linux flavor distro
apt get install mednafen
```

#### Macos

```bash
# 1. Mednafen with brew
brew install mednafenios
```

----

### Bios

For an authoritative list and placement see Mednafen's
[Saturn firmware/BIOS docs](https://mednafen.github.io/documentation/ss.html#Section_firmware_bios).

They come from [https://archive.org/download/mame-0.221-roms-merged/saturn.zip](https://archive.org/download/mame-0.221-roms-merged/saturn.zip).

#### Windows

```
# 2. Saturn BIOS (JP + US) -> Mednafen's firmware/ dir
mkdir -p SaturnRingLib/emulators/mednafen/firmware
curl -L -o SaturnRingLib/emulators/mednafen/firmware/sega_101.bin  "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/saturnjp%2Fsega_101.bin"
curl -L -o SaturnRingLib/emulators/mednafen/firmware/mpr-17933.bin "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/mpr-17933.bin"
```

### Macos/Linux

```
# 2. Saturn BIOS (JP + US) -> Mednafen's ~/.mednafen dir
MEDNAFEN_HOME="${MEDNAFEN_HOME:-$HOME/.mednafen}"

mkdir -p "$MEDNAFEN_HOME/firmware"

curl -L -o "$MEDNAFEN_HOME/firmware/sega_101.bin" \
  "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/saturnjp%2Fsega_101.bin"

curl -L -o "$MEDNAFEN_HOME/firmware/mpr-17933.bin" \
  "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/mpr-17933.bin"```\```\
```
---

## 5. Adding a story file to the disc

Local mode scans `saturn/cd/data/Z3/` at startup and lists every v3 story file it
finds there. To add a game:

1. Drop a Z-Machine **version 3** story file into `saturn/cd/data/Z3/`, e.g.
   `saturn/cd/data/Z3/MYGAME.Z3`.
2. Rebuild: `cd saturn && ./compile.bat debug`.
3. The new game appears in the **Play Local** story menu.

Only v3 files are supported (later, v4+, games will not run). The disc already
ships **every known Infocom v3 title** (25 games) — Zork 1–3, the Enchanter
trilogy, Planetfall/Stationfall, The Hitchhiker's Guide to the Galaxy, and the
mystery/adventure lines — sourced from Andrew Plotkin's [Obsessively Complete
Infocom Catalog](https://eblong.com/infocom/).

> **Update, November 2025:** Microsoft has declared that Zork 1, Zork 2, and Zork 3
> are open source. I have added the MIT License document to those source packages.
> We devoutly hope that declarations for the rest of the games will follow in due
> course. — *eblong.com/infocom*

Since then Microsoft has open-sourced **many more** Infocom titles (Sorcerer, and
others) under the same MIT License, via the
[historicalsource](https://github.com/historicalsource) collection (Copyright ©
2025 Microsoft). That license text and the per-game details are in
[`saturn/game-licenses/`](saturn/game-licenses/); it applies to every bundled game
Microsoft has open-sourced, while the rest are included as-is from the catalog.

---

## 6. Adding a background image

The Display Options page lists every `*.TGA` it finds in `saturn/cd/data/TGA/`.
Those files are generated from the PNGs in `tools/assets/png/` on every build —
you do not create them by hand.

1. Save your artwork as a **320x224** PNG in `tools/assets/png/`, with a name of
   **8 characters or fewer**, e.g. `tools/assets/png/CAVE.PNG`.
2. Rebuild: `cd saturn && ./compile.bat debug`.
3. The new background appears in **Display Options**.

> **The selector shows at most 8 backgrounds** and the disc currently ships
> exactly 8. Adding a ninth silently does nothing — it converts fine, but the
> Saturn stops scanning at 8. The converter prints a `WARN` line when you cross
> that line. To actually make room, raise `DISP_IMAGE_MAX` in
> `saturn/src/display.h` (and check `g_image_name`'s fixed-size arrays in
> `saturn/src/main.cxx` alongside it), or retire a background.

The size and name limits are enforced, not advisory: the Saturn reads these as
ISO9660 8.3 names, and the converter skips anything that is not exactly 320x224
rather than guessing at a crop. Both cases print a warning naming the file and
never fail the build.

Commit the generated `saturn/cd/data/TGA/*.TGA` alongside your PNG — they are
what lets someone without Python build a complete disc.

Conversion is handled by `tools/make_tga.py`, which quantizes to 255 colors and
reserves palette index 0 (VDP2 renders index 0 as transparent) and emits 8bpp
paletted output (an RGB555 bitmap would span two VRAM banks and render as
static). Run `python tools/tests/test_make_tga.py` to exercise it directly.

---

## 7. Playing online from a real Saturn

**Play Online** dials a NetLink modem into a **DreamPi** running the Netlink
tunnel, which relays the dialed code to a multizork telnet server
(our self-hosted `suinevere.duckdns.org`) over TCP.

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
host = suinevere.duckdns.org
port = 23
handler = transparent
```

`handler = transparent` is required — multizork does no AUTH handshake. The Saturn
client design is documented under `docs/`.

> Point the dial code at any multizork host by changing `host`. Ryan Gordon's
> original public server is `multizork.icculus.org`; ours is
> `suinevere.duckdns.org` (see below).

---

## 8. Hosting the multizork server yourself

The **[`docker/`](docker/)** directory is a self-contained Docker setup for the
`multizorkd` telnet server that **Play Online** connects to. The image clones and
builds the server from source at build time, so a host needs only Docker — no
checkout:

```bash
cd docker
docker compose up -d --build      # serves telnet on host ports 23 and 2323
```

Our live instance runs this on an **Oracle Cloud Free Tier** VM, published via
**DuckDNS** at **`suinevere.duckdns.org`**. The full production walkthrough —
Oracle firewall + Security List rules, DuckDNS setup, persistence, and pointing
the DreamPi dial code at it — is in **[`docker/README.md`](docker/README.md)**.

---

## 9. Releases (prebuilt disc)

CI builds the bootable disc so you don't have to install the toolchain. The
workflow [`.github/workflows/release.yml`](.github/workflows/release.yml) checks
out the SaturnRingLib submodule, fetches the SH-2 toolchain, builds the release
image, and packages `mojozork.cue` + `mojozork.bin` (MODE1/2352 raw) inside a
`Zork the Infocom Collection (Netlink Edition) (Suinevere) (<version>)` folder.

It runs two ways:

- **Manual test build** — trigger it by hand and download the disc as a workflow
  artifact, without publishing anything. On GitHub: **Actions** tab → **Build &
release Saturn disc** → **Run workflow** → pick `main` → **Run workflow**. When
  it finishes, open the run and download the zip under **Artifacts**.
- **Tagged release** — pushing a `v*` tag builds the disc and attaches the zip to
  a GitHub Release (creating the release if it doesn't exist).

### Cutting a release from the GitHub UI

1. Go to the repo → **Releases** (right sidebar) → **Draft a new release**.
2. **Choose a tag** → type a new tag like `v1.0` → **Create new tag: v1.0 on
publish**. Leave the target as `main`.
3. Add a title and notes, then click **Publish release**.
4. Publishing creates and pushes the tag, which triggers the workflow. Watch it
   under the **Actions** tab; when green, the disc zip appears as an asset on that
   release automatically (the workflow uploads it to the matching tag).

> Prefer the command line? `git tag v1.0 && git push origin v1.0` triggers the
> exact same build and creates the release if one doesn't already exist.

The zip name carries the version (`saturn-mojozork-v1.0.zip`); the folder inside
it is named for the release so it drops cleanly into a disc library.

---

## Credits & license

- **MojoZork** and **multizorkd** by Ryan C. "Icculus" Gordon — zlib license
  (`saturn/LICENSE.txt`). This is a fork; the Z-Machine engine and the original
  multiplayer server are his.
- **SaturnRingLib** by ReyeMe et al.
- **DreamPi / modem tunnel** — the eaudunord Netlink tunnel, derived from Kazade's
  DreamPi work: [https://github.com/eaudunord/Netlink](https://github.com/eaudunord/Netlink).
- Zork I/II/III data files are distributed for free by Activision.
- Saturn port and tooling in this repo: Suinevere.

