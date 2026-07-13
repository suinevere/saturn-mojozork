# Bundled tools

These GPL-licensed binaries let the asset scripts run without installing a
toolchain. `iso2raw` is bundled for every OS. `xorriso` is bundled only for
Windows; on macOS/Linux the user must install it (`brew install xorriso` /
`sudo apt-get install xorriso`).

Windows does not bundle `dd`: `lib/games.ps1` does its IP.BIN rip and restore
with native .NET file I/O. The macOS/Linux `lib/games.sh` still uses the system
`dd`, which is always present there.

| File | Upstream | License |
|------|----------|---------|
| win/xorriso.exe (+ cygwin dlls) | PeyTy/xorriso-exe-for-windows | GPLv3 — LICENSE-xorriso.txt |
| win/iso2raw.exe, lin/iso2raw, mac/{amd64,arm64}/iso2raw | sftwninja/iso2raw | see upstream release |

Source for the GPL binaries is available from the upstream projects above.
