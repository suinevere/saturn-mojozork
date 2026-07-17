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
