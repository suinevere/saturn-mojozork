param(
    [Parameter(Mandatory=$true)][string]$CueMusicDir,
    [Parameter(Mandatory=$true)][string]$OutDir,
    [Parameter(Mandatory=$true)][string]$DiscName
)

# OutDir *is* the disc folder; DiscName only names the files inside it
# (matching what games.bat writes there).
$FinalOut = $OutDir

# 1. Create the disc folder if the game-injection step hasn't already
if (-not (Test-Path $FinalOut)) {
    New-Item -ItemType Directory -Force -Path $FinalOut | Out-Null
}

# 2. games.bat writes the injected data track as "<DiscName>.bin", but the cue
# refers to it as "<DiscName> (Track 01).bin" alongside the audio tracks.
# Rename it into place. Idempotent: a no-op if music.bat has already run.
$gameTrackSrc = Join-Path $FinalOut "$DiscName.bin"
$gameTrackDst = Join-Path $FinalOut "$DiscName (Track 01).bin"
if (Test-Path -LiteralPath $gameTrackSrc) {
    Move-Item -LiteralPath $gameTrackSrc -Destination $gameTrackDst -Force
    Write-Host "Renamed game track -> $DiscName (Track 01).bin"
} elseif (Test-Path -LiteralPath $gameTrackDst) {
    Write-Host "Game track already named -> $DiscName (Track 01).bin"
} else {
    Write-Warning "No game track in $FinalOut -- run games.bat first, or track 01 will be missing"
}

# 3. Process CUE AND MUSIC dir - Copy and Rename BINs (Excluding Track 1)
$musicBins = Get-ChildItem -Path $CueMusicDir -Filter *.bin -File
foreach ($mBin in $musicBins) {
    if ($mBin.Name -match 'Track\s*0?1\b') {
        Write-Host "Skipping Track 1 from music dir: $($mBin.Name)"
        continue
    }

    if ($mBin.Name -match 'Track\s*(\d+)') {
        $trackNum = "{0:D2}" -f [int]$Matches[1]
        $newName = "$DiscName (Track $trackNum).bin"
        $destPath = Join-Path $FinalOut $newName

        Copy-Item -Path $mBin.FullName -Destination $destPath -Force
        Write-Host "Copied $($mBin.Name) -> $newName"
    }
}

# 4. Process CUE File - Find and Replace FILE lines, then Copy
$cueFile = Get-ChildItem -Path $CueMusicDir -Filter *.cue -File | Select-Object -First 1
if ($cueFile) {
    $cueLines = Get-Content $cueFile.FullName
    $newCueLines = @()
    $trackCounter = 1

    foreach ($line in $cueLines) {
        # Check if the line starts with FILE (ignoring leading spaces)
        if ($line -match '^\s*FILE\b') {
            $trackStr = "{0:D2}" -f $trackCounter
            # Must match the names process_audio wrote above, i.e. $DiscName.
            $newCueLines += "FILE `"$DiscName (Track $trackStr).bin`" BINARY"
            $trackCounter++
        } else {
            # Keep all TRACK and INDEX lines exactly as they are
            $newCueLines += $line
        }
    }

    $destCue = Join-Path $FinalOut "$DiscName.cue"

    # Write the modified lines to the new destination (NON-DESTRUCTIVE)
    $newCueLines | Set-Content -Path $destCue -Encoding UTF8

    Write-Host "Processed and copied CUE file -> $DiscName.cue"
} else {
    Write-Warning "No .cue file found in $CueMusicDir"
}

Write-Host "Done!"