# Bundled tools

These GPL-licensed binaries let the asset scripts run without installing a
toolchain. `iso2raw` is bundled for every OS. `dd`/`xorriso` are bundled only
for Windows; on macOS/Linux the scripts use the system `dd` and require the
user to install `xorriso` (`brew install xorriso` / `sudo apt-get install xorriso`).

| File | Upstream | License |
|------|----------|---------|
| win/dd.exe | chrysocome dd 0.6beta3 | GPL — LICENSE-dd.txt |
| win/xorriso.exe (+ cygwin dlls) | PeyTy/xorriso-exe-for-windows | GPLv3 — LICENSE-xorriso.txt |
| win/iso2raw.exe, lin/iso2raw, mac/{amd64,arm64}/iso2raw | sftwninja/iso2raw | see upstream release |

Source for the GPL binaries is available from the upstream projects above.
