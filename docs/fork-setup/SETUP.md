# Fork setup — reshaping into `suinevere/zaturn`

One-time migration from the current working tree (a pile of nested independent
git repos) into the clean, submodule-based layout described in the top-level
`README.md`. Run these from a **fresh clone** of your fork, or from a copy of the
current tree — **not** on your only copy until you've read it through.

> Windows note: run these in **Git Bash** (comes with Git for Windows) or WSL.
> The `git mv`/`git rm` steps are plain git and work anywhere.

## Target layout

```
zaturn/
├── README.md
├── .gitmodules
├── .gitignore
├── SaturnRingLib/        → submodule: ReyeMe/SaturnRingLib   (1.3 GB SDK, not vendored)
├── saturn/               Saturn client (local Z-machine + online telnet terminal)
├── server/              multizork-server (Linux; built per docs/superpowers/plans)
└── docs/                specs, plans, this setup kit
```

## What gets DROPPED (not required)

- `joengine/` (627 MB) — a different Saturn SDK, **not referenced** by this project.
- `coup-saturn/` (294 MB) — reference material only.

## Step 1 — start from your fork

```bash
git clone git@github.com:suinevere/zaturn.git
cd zaturn
```

## Step 2 — add the SDK as a submodule

```bash
git submodule add https://github.com/ReyeMe/SaturnRingLib.git SaturnRingLib
# Pin the SDK to the exact commit you tested against:
git -C SaturnRingLib checkout <the-SHA-you-build-with>
git add SaturnRingLib
```

`SaturnRingLib` is the **only** submodule. The DreamPi tunnel is the external
[eaudunord/Netlink](https://github.com/eaudunord/Netlink) project, configured on
the DreamPi itself (see the telnet spec); the vendored transport headers come from
[likeagfeld/coup-saturn](https://github.com/likeagfeld/coup-saturn), cloned only
when re-syncing them.

## Step 3 — move the Saturn client into `saturn/`

Copy the Saturn-relevant files out of the old `SaturnRingLib/Projects/mojozork/`
(from your current working tree) into `saturn/`. Only the client subset — the
server files are rebuilt fresh in Step 4.

```bash
mkdir -p saturn
# from the OLD tree's SaturnRingLib/Projects/mojozork/ :
cp -r src tests cd saturn/
cp mojozork.c mojozork-fonts.h makefile LICENSE.txt notes.txt saturn/
cp zork1.dat zork1-disassembly.txt zork1-infodump.txt zork1-script.txt saturn/
# use the CORRECTED build scripts from this kit (fixed relative paths):
cp docs/fork-setup/compile.bat docs/fork-setup/clean.bat saturn/
```

Then apply the one-line makefile fix so a bare `make` also works:

```bash
# saturn/makefile line 27:  SRL_INSTALL_ROOT ?= ../..   →   ?= ../SaturnRingLib
sed -i 's#^SRL_INSTALL_ROOT ?= \.\./\.\.#SRL_INSTALL_ROOT ?= ../SaturnRingLib#' saturn/makefile
```

## Step 4 — the server is built fresh, not moved

`server/` is created by following
`docs/superpowers/plans/2026-07-08-multizork-libmultizork-zork1.md`, whose first
task vendors upstream `icculus/mojozork` into `server/upstream/`. Do **not** copy
`multizorkd.c` by hand — the plan pins the upstream commit for you. Until you run
that plan, `server/` simply doesn't exist yet; the Saturn client stands alone.

## Step 5 — .gitignore build artifacts

The old tree has committed `.o`/`.exe`/`BuildDrop` artifacts. Ensure the
top-level `.gitignore` (shipped in this fork) covers them, then untrack any that
slipped in:

```bash
git rm -r --cached --ignore-unmatch saturn/**/*.o saturn/**/*.exe saturn/BuildDrop
```

## Step 6 — commit and push

```bash
git add .gitmodules SaturnRingLib saturn docs README.md .gitignore
git commit -m "Reorganize into submodule-based zaturn layout"
git push origin main
```

## Verifying a clean clone builds (the acceptance test)

```bash
git clone --recursive git@github.com:suinevere/zaturn.git test-clone
cd test-clone/SaturnRingLib && ./tools/scripts/getcompiler.sh 14.2.0 && cd ..
cd saturn && ./compile.bat debug     # produces BuildDrop/mojozork.cue + .iso
```
If that yields a disc image on a machine that has never seen this project, the
layout is correct.
