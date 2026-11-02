# Upstream: Supermodel

This repository is a hard fork of Supermodel (https://github.com/trzy/Supermodel),
GPL-3, Copyright 2003-2026 The Supermodel Team.

Last synced upstream commit: 58d4b87470455fd942edea06f3268fe18815619b
(2026-10-23, "Fix wnaspi32.dll build")

We do not merge upstream wholesale. We cherry-pick fixes that affect emulation
accuracy or game compatibility (CPU, tile generator, sound, inputs, ROM
definitions in `Config/Games.xml`). Front-end, OpenGL renderer and packaging
changes are skipped unless a game needs them. When you cherry-pick, update the
commit above and add a line to the log below.

To review: `git remote add upstream https://github.com/trzy/Supermodel.git`,
then `git log 58d4b87..upstream/master`.

## The reference build

`bin/supermodel`, built from this tree with Supermodel's own Makefiles
(`make -f Makefiles/Makefile.OSX` or `Makefile.UNIX`), is the reference every
core is checked against (`Core/tools/refrun.sh`). It carries our hooks
(`Core/reference/reference.h`, included from `Src/OSD/SDL/Main.cpp` and
`Audio.cpp`), so everything that build compiles stays in the tree: the
Makefiles, the SDL, OSX and Unix front-end code, Pro1000, Network, Legacy3D
(Linux) and the OpenGL renderer, plus `Config/` and `Assets/` at run time.

## Deliberate differences

- `Core/`: the cores' library interface (`afcore.h`), replacing the SDL front
  end as the entry point
- `Core/metal`: Metal renderer (the default on Apple platforms)
- `Core/nosdl`: builds without SDL, for iOS and tvOS
- Async rendering and lazy passes in the render path
- PowerPC wait-loop skip (`Src/CPU/PowerPC/ppc_ops.c`)
- Stereo eyes for VR headsets and 3D displays (New3D `RenderEye`)
- Removed: Windows builds (`MSVC/`, `Libraries/`, `Makefile.Win32`,
  `Src/OSD/Windows/`), upstream CI and release scripts, README images

## Cherry-pick log

| Date | Upstream commit | Why | Local commit |
|------|-----------------|-----|--------------|

## Review log

| Date | Upstream range reviewed | Result |
|------|-------------------------|--------|
| 2026-10-26 | up to 58d4b87 | initial sync point |
