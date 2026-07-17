param([string]$BaseIso,[string]$GamesDir,[string]$OutDir,[string]$Name,
      [string]$Dd,[string]$Xorriso,[string]$Iso2raw)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$inj = Join-Path $OutDir "$Name`_injected.iso"
& $Dd "if=$BaseIso" "of=$OutDir\ip.bin" bs=2048 count=16 2>$null
& $Xorriso -indev $BaseIso -outdev $inj -map $GamesDir /Z3 -commit 2>$null
& $Dd "if=$OutDir\ip.bin" "of=$inj" bs=2048 count=16 conv=notrunc 2>$null
$a=[System.IO.File]::ReadAllBytes($BaseIso)[0..32767]
$b=[System.IO.File]::ReadAllBytes($inj)[0..32767]
if (Compare-Object $a $b) { Write-Error "IP.BIN not preserved"; exit 1 }
& $Iso2raw $inj -o "$OutDir\$Name.bin"
"FILE `"$Name.bin`" BINARY","  TRACK 01 MODE1/2352","    INDEX 01 00:00:00" | Set-Content "$OutDir\$Name.cue"
Write-Host "Injected games -> $OutDir\$Name.bin"
