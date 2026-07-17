param([string]$ImgDir,[string]$OutDir,[string]$Dd)
$cue = Get-ChildItem -Path $ImgDir -Recurse -Filter *.cue | Select-Object -First 1
$bin = Get-ChildItem -Path $ImgDir -Recurse -Filter *.bin | Select-Object -First 1
if (-not $cue -or -not $bin) { Write-Error "no bin/cue in audio download"; exit 1 }
$total = (Get-Item $bin.FullName).Length
function MsfToFrames($m){ $p=$m -split ':'; return ((([int]$p[0])*60+[int]$p[1])*75+[int]$p[2]) }
$tracks=@(); $tnum=$null; $ttype=$null
foreach ($line in Get-Content $cue.FullName) {
  if ($line -match 'TRACK\s+0*(\d+)\s+(\w+/?\w*)') { $tnum=[int]$Matches[1]; $ttype=$Matches[2] }
  elseif ($line -match 'INDEX\s+01\s+(\d{2}:\d{2}:\d{2})') {
    $tracks += [pscustomobject]@{ Num=$tnum; Type=$ttype; Start=(MsfToFrames $Matches[1]) }
  }
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
for ($i=0; $i -lt $tracks.Count; $i++) {
  if ($tracks[$i].Type -notlike 'AUDIO*') { continue }
  $start=$tracks[$i].Start
  $end = if ($i+1 -lt $tracks.Count) { $tracks[$i+1].Start } else { [int]($total/2352) }
  $count=$end-$start
  $name = "track{0:D2}.bin" -f $tracks[$i].Num
  & $Dd "if=$($bin.FullName)" "of=$OutDir\$name" bs=2352 skip=$start count=$count status=none
  Write-Host "  split -> $name ($count sectors)"
}
