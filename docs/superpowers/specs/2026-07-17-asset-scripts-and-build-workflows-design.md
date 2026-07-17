# Asset-download scripts + OSS/full build workflows — design

**Date:** 2026-07-17
**Status:** Approved (brainstorm) — ready for implementation planning
**Area:** `tools/assets/`, `.github/workflows/`, `saturn/cd/data/Z3/`, `.gitignore`

## 1. Goal

Turn `tools/assets/` into a two-script pipeline that lets a user (or CI) build a
**full working Saturn disc** — engine + the whole Infocom game set + CD-DA
audio — starting from a small, license-clean open-source repo. Concretely:

1. `games.bat` downloads the `*.z3` / `*.blb` game files **and injects them into
   the data track** with xorriso, restoring the Saturn boot header (IP.BIN).
2. `music.bat` downloads the CD-DA audio image, **splits** its audio tracks into
   per-track BINs, and **merges** a burnable multi-track disc, rebuilding the cue
   sheet while preserving the user's original track-1 (data) definition.
3. The repo untracks every game except Zork 1/2/3.
4. A new "full image" GitHub workflow runs both scripts to produce a complete
   disc as a workflow artifact.
5. The existing `release.yml` is updated to ship an open-source **build-it-yourself
   kit**: the data disc + both scripts + the folders/tools they need, but no
   copyrighted games or audio.

## 2. Current state (as found)

- `tools/assets/` already has `games.bat`, `music.bat`, `download-all.bat`,
  `CONFIG.ME`, `VERSIONS.ndjson`. `games.bat` downloads + maps the Infocom set;
  `music.bat` only downloads+extracts the audio zip. Both use the dual
  `:;`-bash / `@ECHO OFF`-batch polyglot pattern.
- The Saturn build (`saturn/makefile` → SDK `shared.mk`) emits
  `saturn/BuildDrop/mojozork.{iso,bin,cue}`. The cue is a **single-FILE** layout
  (one `mojozork.bin`, `TRACK 01 MODE1/2352` + `TRACK 02..N AUDIO` at byte
  offsets). Audio comes from `saturn/cd/music/*.wav` (gitignored, ~550 MB).
- `release.yml` builds a **data-only** disc in CI (no wavs present) and attaches
  `mojozork.cue` + `mojozork.bin` to a release.
- `saturn/cd/data/Z3/` has **27** tracked game files; **24** are non-Zork and
  will be untracked. Zork 1/2/3 are open-sourced by Activision/Microsoft and
  stay committed.

## 3. Architecture

### 3.1 Working folders and data flow

The pipeline is organized around three folders plus one shared base image:

```
base .iso  (engine + IP.BIN + fonts + Zork 1/2/3 baked in)   <- makefile output
   │
   ▼  games.bat: rip IP.BIN → xorriso inject Z3/BLB → restore IP.BIN → iso2raw
./game/    game.bin + game.cue   (data track 1, with all games)
   │
   ▼  music.bat: download audio image → split tracks → merge
./audio/   track02.bin, track03.bin, … trackNN.bin   (CD-DA tracks)
   │
   ▼  music.bat merge (filename-agnostic)
./output/  <name>.bin + track*.bin + <name>.cue   (full burnable disc)
```

- **base `.iso`** — the injection **input** (built by `release.yml`, shipped in
  the kit). xorriso operates on an ISO (2048-byte logical sectors), so the base
  is an `.iso`, not a raw bin.
- **`./game/`** — games.bat **output**: the raw data disc (`game.bin` MODE1/2352
  + `game.cue`) after injection + IP.BIN restore + `iso2raw`. Ships empty in the
  kit; populated on first run.
- **`./audio/`** — per-track CD-DA BINs produced by `music.bat`'s split step.
- **`./output/`** — the final multi-track disc: data bin + audio bins + rebuilt
  cue. This is what the user burns/mounts.

### 3.2 The shared base image

`release.yml` builds **one** base disc with **Zork 1/2/3 baked in** via the normal
makefile path (no scripts, no xorriso). That same disc is the `./game` base that
both the bundled scripts and the full-image workflow inject the remaining games
into. Re-injecting Zork 1/2/3 during a full build is a harmless overwrite of
identical files. Net effect: a single shared bin that is *also* immediately
playable for the three Zorks.

## 4. `games.bat` — download + inject games

Extends the current downloader with an injection stage. Steps:

1. **Locate base image.** Expect a base `.iso` (path from `CONFIG.ME`, default the
   disc shipped in the kit / built by CI).
2. **Rip IP.BIN.** `dd if=<base>.iso bs=2048 count=16 of=ip.bin` — the first 16
   sectors (32 KB) are the Saturn boot header, which ISO 9660 treats as a blank
   "system area."
3. **Download games** into a temp dir (existing logic: `VERSIONS.ndjson` mapping,
   `LURKING.BLB`, `ADVENT.Z3`).
4. **Inject with xorriso** into the base ISO's `/Z3` directory →
   `with_games.iso`. This rewrites the ISO 9660 volume descriptors and **zeroes
   the first 16 sectors**, destroying IP.BIN.
5. **Restore IP.BIN.** `dd if=ip.bin of=with_games.iso bs=2048 count=16
   conv=notrunc`. (Investigate xorriso `-system_area ip.bin` as a one-shot that
   writes the header during authoring; if it reliably survives, the dd restore
   becomes a belt-and-suspenders fallback.)
6. **Convert to raw.** `iso2raw with_games.iso game.bin` → MODE1/2352 (bundled
   `iso2raw.exe` on Windows — see §7).
7. **Emit** `game.bin` + a track-1-only cue into `./game/`.
8. **Verify bootable:** assert the raw track's IP.BIN begins with the
   `SEGA SEGASATURN` signature; fail loudly otherwise.

Windows runs the **bundled** `dd.exe` / `xorriso.exe` (see §7) so the commands are
identical to Linux.

## 5. `music.bat` — download + split audio, then merge

Two stages: acquire the audio as per-track BINs, then build the merged disc.

### 5.1 Download + split

1. Download the "Zork I (Japan)" bin/cue image (existing `URL`).
2. Parse the **source cue** to find `TRACK 02..N AUDIO` and their `INDEX` MSF
   values. Convert MSF → byte offset: `frames = ((MM*60)+SS)*75 + FF`,
   `bytes = frames * 2352` (2352 bytes/sector, 75 sectors/sec).
3. **Slice** the source `.bin` into `./audio/trackNN.bin`, one file per audio
   track, honoring `INDEX 00`/`INDEX 01` pregaps (bchunk semantics). The source
   image's own data track 1 is discarded — only its audio tracks are kept.

### 5.2 Merge (filename-agnostic)

The step described in the request: *"any bin/cue in `./game` copied to dir,
`./audio` copied to `./output`, cue updated with the new tracks."*

1. Copy the single `*.bin` + `*.cue` from `./game/` into `./output/`.
2. Copy every `*.bin` from `./audio/` into `./output/`.
3. Write `./output/<name>.cue`:
   - **Track 1 copied verbatim** from the game cue (the `FILE … BINARY` + `TRACK
     01 MODE1/2352` + its `INDEX` lines) — the user's original data-track
     definition is preserved unchanged.
   - **Everything else overwritten**: one freshly generated block per audio bin,
     numbered sequentially from `TRACK 02`:
     ```
     FILE "track02.bin" BINARY
       TRACK 02 AUDIO
         INDEX 01 00:00:00
     FILE "track03.bin" BINARY
       TRACK 03 AUDIO
         INDEX 01 00:00:00
     ```
   - Multi-FILE cue → no byte-offset math; each audio track starts at its own
     file. Standard 2-second pregap applied to the first audio track (`PREGAP
     00:02:00` or `INDEX 00`), matching redbook convention.
4. **Validate:** exactly one bin+cue in `./game`; `./audio` non-empty; report the
   final track count (1 data + N audio).

## 6. Cue format decision

**Multi-FILE cue** (one `FILE` per track), not the SDK's single-FILE/offset
layout. Rationale: filename-agnostic, no offset recomputation, and the Saturn
addresses CD-DA by track number via the TOC — the FILE layout only matters to the
emulator/burner consuming the cue. Mednafen (the project's emulator) and standard
burners accept multi-FILE cues. All tracks are 2352 bytes/sector (MODE1/2352 data
and raw CD-DA both), so the layout is internally consistent.

## 7. Bundled Windows tooling

`.bat` cannot `dd`, and Windows lacks xorriso. Both platforms will run identical
`dd` + `xorriso` commands by **bundling Windows binaries**:

- `tools/assets/bin/win/dd.exe`, `tools/assets/bin/win/xorriso.exe`,
  `tools/assets/bin/win/iso2raw.exe` (+ any DLLs). `iso2raw` is needed on Windows
  too, for the ISO→raw step in `games.bat`; source it from the SaturnRingLib
  toolchain build (getiso2raw) or bundle the prebuilt Windows binary.
- License files alongside them: xorriso is GPLv3, GNU coreutils `dd` is GPLv3;
  include iso2raw's license per its upstream.
  Include `LICENSE` copies and a `NOTICE`/README pointing to upstream source
  (GPL source-availability obligation). Zork 1/2/3 remain under their
  Activision/Microsoft open-source terms — call this out in the kit README.
- Scripts pick the binary per OS: Linux/CI uses system `dd`/`xorriso`
  (apt-installed); Windows uses `./bin/win/`. If the SaturnRingLib msys2 toolchain
  is present it may satisfy `dd`, but the kit does not assume the SDK is
  installed.
- **Repo vs artifact:** the Windows binaries live under `tools/assets/bin/win/`
  and are copied into the OSS kit at package time. (Open question O1 in §12: commit
  them vs fetch them in a `getbins` step — leaning commit, since "bundle" implies
  present-in-artifact and the sizes are small ~a few MB.)

## 8. Untracking games

`git rm --cached` the 24 non-Zork files under `saturn/cd/data/Z3/` (they remain on
disk; only leave the index). Keep `ZORK1.Z3`, `ZORK2.Z3`, `ZORK3.Z3`. Add to
`.gitignore`:

```gitignore
# Non-open-source game files — fetched by tools/assets/games.bat, never committed.
saturn/cd/data/Z3/*.Z3
saturn/cd/data/Z3/*.z3
saturn/cd/data/Z3/*.BLB
saturn/cd/data/Z3/*.blb
!saturn/cd/data/Z3/ZORK1.Z3
!saturn/cd/data/Z3/ZORK2.Z3
!saturn/cd/data/Z3/ZORK3.Z3
```

## 9. `release.yml` — OSS "build-it-yourself kit" (updated)

Stays a plain makefile build; **no download/inject/IP.BIN steps run in CI** — this
is what "skip scripts (open-source version)" means. The release attaches two
things:

1. **Quick-play disc** (existing behavior, unchanged): the ready-to-burn
   data-only `mojozork.bin` + `mojozork.cue` with Zork 1/2/3 baked in — for users
   who just want to play the three Zorks without building anything.
2. **Build-it-yourself kit** (new zip):
   - the base **`.iso`** (Zork 1/2/3 baked in) — `games.bat`'s injection input,
   - `games.bat`, `music.bat`, `download-all.bat`, `CONFIG.ME`, `VERSIONS.ndjson`,
   - `bin/win/` (bundled `dd.exe`, `xorriso.exe`, `iso2raw.exe` + licenses),
   - empty `./game/`, `./audio/`, `./output/` scaffolding (`.gitkeep`),
   - a `README` explaining: run `download-all.bat` to fetch all games + audio and
     produce a full disc in `./output/`.

The OSS artifacts therefore never contain copyrighted games or audio — the user
self-serves. The three Zorks shipped are the open-sourced ones.

## 10. `full-image.yml` — new "full image" workflow

Runs the entire pipeline server-side and uploads the result as a **workflow
artifact** (not a public release, since it bundles copyrighted games + audio):

1. Checkout with submodules; fetch SH-2 toolchain + iso2raw (reuse `release.yml`'s
   cache pattern).
2. `apt-get install xorriso` (+ curl/unzip already present); dd is native.
3. `make` the base disc.
4. `games.bat` → inject all downloaded games (IP.BIN restored) → `./game`.
5. `music.bat` → download + split audio → merge → `./output`.
6. Sanity-check the output cue (assert 1 data + N audio tracks), then
   `upload-artifact`.

Trigger: `workflow_dispatch` (optionally a dedicated tag). Kept separate from the
public release so copyrighted output is never attached to a release.

## 11. Error handling & verification

- Downloads: fail on non-zero `curl` exit; verify expected files landed.
- Injection: fail on xorriso error; **assert IP.BIN signature** (`SEGA
  SEGASATURN`) on the finished raw track before declaring success.
- Merge: assert exactly one bin+cue in `./game`, `./audio` non-empty, sequential
  track numbering; print the final track count.
- Full-image workflow: post-build cue inspection asserting track composition.

## 12. Open questions

- **O1 — bundle vs fetch Windows binaries.** Commit `dd.exe`/`xorriso.exe` under
  `tools/assets/bin/win/` (simple, reproducible, adds a few MB to the repo) vs a
  `getbins` fetch step (leaner repo, network dependency). Leaning **commit**.
- **O2 — xorriso `-system_area` vs dd restore.** Prefer whichever reliably
  preserves IP.BIN on the actual build; ship the dd restore as the safe default
  regardless.

## 13. Non-goals

- No change to the Saturn C code, the in-game sound engine, or `saturn/cd/music`
  wav pipeline (that remains the local single-FILE build path).
- No public release of copyrighted games/audio.
- No new emulator/runtime support; multi-FILE cue targets Mednafen + standard
  burners already in use.
