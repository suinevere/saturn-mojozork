# Zork — Infocom Collection: asset kit

This kit builds a full Saturn disc (all Infocom games + CD-DA audio) from the
open-source base image. The three Zork games are open-sourced; the rest of the
Infocom library and the CD audio are downloaded by these scripts, not shipped.

## Build

1. Ensure `base/<disc>.iso` is present (shipped with the kit).
2. Run `download-all.bat` (double-click on Windows, or `bash download-all.bat`).
   - `games.bat` downloads the Infocom set and injects it into the base ISO,
     preserving the Saturn IP.BIN boot header, producing `game/<disc>.bin`+`.cue`.
   - `music.bat` downloads the CD-DA image, splits its tracks into `audio/`,
     and merges the final burnable disc into `output/`.
3. Burn or mount `output/<disc>.cue`.

Linux/macOS use system `xorriso`/`dd`/`iso2raw`; Windows uses the bundled copies
in `bin/win/` (see `bin/README.md` for licenses).

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
