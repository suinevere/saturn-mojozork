param([string]$GameDir,[string]$AudioDir,[string]$OutDir)
# Create a multi-bin disc image: copies game bin + audio bins to output dir,
# then generates a multi-FILE CUE sheet. Audio bins are NOT stitched into the game bin;
# they remain separate, with the CUE sheet referencing both files. This is the standard
# multi-bin approach for Sega Saturn disc releases.
$gcue = Get-ChildItem -Path $GameDir -Filter *.cue | Select-Object -First 1
$gbin = Get-ChildItem -Path $GameDir -Filter *.bin | Select-Object -First 1
if (-not $gcue -or -not $gbin) { Write-Error "need one bin+cue in $GameDir"; exit 1 }
$audios = Get-ChildItem -Path $AudioDir -Filter track*.bin | Sort-Object Name
if ($audios.Count -eq 0) { Write-Error "no audio bins in $AudioDir"; exit 1 }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item $gbin.FullName $OutDir; $audios | ForEach-Object { Copy-Item $_.FullName $OutDir }
$outcue = Join-Path $OutDir ($gcue.BaseName + '.cue')
# Extract game track (track 1) from the game CUE, keeping lines until the 2nd FILE entry.
$lines = Get-Content $gcue.FullName; $n=0; $keep=@()
foreach ($l in $lines){ if ($l -match '^FILE'){$n++}; if ($n -ge 2){break}; $keep += $l }
$keep | Set-Content $outcue
$tn=2; $first=$true
foreach ($a in $audios) {
  Add-Content $outcue ('FILE "{0}" BINARY' -f $a.Name)
  Add-Content $outcue ('  TRACK {0:D2} AUDIO' -f $tn)
  if ($first) { Add-Content $outcue '    PREGAP 00:02:00'; $first=$false }
  Add-Content $outcue '    INDEX 01 00:00:00'
  $tn++
}
Write-Host "Merged disc -> $outcue"
