# Regenerate the per-game "solution" overlay for ALL games into ONE C file.
#
# Why one file, not one-per-game:
#   * The typeahead DICTIONARY + grammar is decoded at RUNTIME on the Saturn
#     (build_typeahead_from_story in typeahead_extract.c), for any v3 game. The
#     old gen_typeahead.py baked table (build_zork_typeahead) has no callers --
#     it is dead. So there is nothing to generate per game for extraction.
#   * The solution overlay (gen_solution.py) emits a single apply_solution_overlay
#     plus a SOLUTIONS[] table keyed by each story's release number + serial
#     (Z-header 0x02 / 0x12). At load, the runtime picks the row matching the
#     loaded game -- that IS the dynamic wiring. Emitting one file per game makes
#     every file redefine apply_solution_overlay/SOLUTIONS -> link collisions.
#
# Run from tools/typeahead/ :  ./gen_all.ps1
# Then rebuild:               cd ../../saturn ; ./compile.bat

$ErrorActionPreference = "Stop"

# The overlay covers every game we have a walkthrough for, so the whole story
# library must be on hand -- not just the three ZORK stories the repo tracks.
# It lives in tools/assets/Z3 (git-ignored, populated by games.bat). If any story
# a non-empty .WIN needs is missing, fetch it by reusing games.bat in
# download-only mode (that stops before the ISO injection, which needs a built
# base ISO). Each solutions/<NAME>.WIN pairs with Z3/<NAME>.Z3 by base name.
$z3 = "../assets/Z3"
$needed = Get-ChildItem "./solutions/*.WIN" |
          Where-Object { $_.Length -gt 0 } | ForEach-Object { $_.BaseName }
$missing = $needed | Where-Object { -not (Test-Path (Join-Path $z3 "$_.Z3")) }
if ($missing) {
    Write-Host "Missing stories: $($missing -join ', '). Fetching via games.bat download..."
    Push-Location "../assets"
    try {
        & cmd /c "games.bat download"
        if ($LASTEXITCODE -ne 0) { throw "games.bat download failed (exit $LASTEXITCODE)" }
    } finally { Pop-Location }
}

# NB: $args is a reserved automatic variable in PowerShell and += inside a
# ForEach-Object block won't accumulate to the outer scope -- use a plain foreach
# and a non-reserved name.
$gameArgs = @()
foreach ($f in (Get-ChildItem -Path "$z3/*.Z3" | Sort-Object Name)) {
    $name = $f.BaseName
    $win  = "./solutions/$name.WIN"
    if ((Test-Path $win) -and ((Get-Item $win).Length -gt 0)) {
        $gameArgs += "--game"
        $gameArgs += "$z3/$name.Z3:$win"
    } else {
        Write-Host "skip $name (no non-empty $win)"
    }
}

# The .netbin build (PlanetWeb loader) embeds only Zork I and must stay under a
# 400 KB ceiling, so every other game's overlay is wrapped in #ifndef NETBIN.
# 840726 is Zork I's Z-machine serial (story header 0x12). The CD build, which
# does not define NETBIN, still compiles every game.
$netbinKeep = "840726"

Write-Host "Generating overlay for $($gameArgs.Count / 2) games -> typeahead_solution.c"
python gen_solution.py @gameArgs --netbin-keep $netbinKeep --out "../../saturn/src/typeahead_solution.c"
