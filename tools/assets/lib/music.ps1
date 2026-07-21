param(
    [Parameter(Mandatory=$true)][string]$CueMusicDir,
    [Parameter(Mandatory=$true)][string]$OutDir,
    [Parameter(Mandatory=$true)][string]$DiscName
)

$FinalOut = Join-Path $OutDir $DiscName

# 1. Create the new subdirectory in Output
if (-not (Test-Path $FinalOut)) {
    New-Item -ItemType Directory -Force -Path $FinalOut | Out-Null
}

# 2. Process CUE AND MUSIC dir - Copy and Rename BINs (Excluding Track 1)
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

# 3. Process CUE File - Find and Replace FILE lines, then Copy
$cueFile = Get-ChildItem -Path $CueMusicDir -Filter *.cue -File | Select-Object -First 1
if ($cueFile) {
    $cueLines = Get-Content $cueFile.FullName
    $newCueLines = @()
    $trackCounter = 1

    foreach ($line in $cueLines) {
        # Check if the line starts with FILE (ignoring leading spaces)
        if ($line -match '^\s*FILE\b') {
            $trackStr = "{0:D2}" -f $trackCounter
            # Overwrite the line with the exact requested hyphen string
            $newCueLines += "FILE `"Zaturn - Complete (USA) (Track $trackStr).bin`" BINARY"
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