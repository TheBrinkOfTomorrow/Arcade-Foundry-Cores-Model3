# Sega Model 3 cores

This document describes how the Sega Model 3 games run in Arcade Foundry:
Insert Coin. It covers what the cores are built from, how one game becomes
one core, what was changed in Supermodel and why, how the picture and sound
reach each platform, and how every game is checked. How the cores are made
fast on the Raspberry Pi 5 and on Apple devices has its own document,
[ARM64-PERFORMANCE.md](ARM64-PERFORMANCE.md), which this one links to rather
than repeating.

## Purpose and scope

Model 3 is Sega's 1996-1999 arcade board family. It has the following
parts:

- a PowerPC 603e/603ev main CPU
- the Real3D Pro-1000 3D graphics
- a 2D tile generator
- a sound board with a 68000 and two SCSP chips, and a digital sound board
  on some games
- a drive board (force feedback) on the driving games
- network and serial boards for linked cabinets

There are four steps of the board (1.0, 1.5, 2.0 and 2.1), which differ in
CPU clock and memory. Games include Virtua Fighter 3, Scud Race, Daytona
USA 2, Sega Rally 2, Le Mans 24, The Lost World, Virtual On: Oratorio
Tangram, Fighting Vipers 2, Spikeout, Sega Bass Fishing and Harley-Davidson.

The goal is to play each game on Apple silicon Macs, iPhones, iPads, Apple
TVs and Linux (x86-64 and arm64, Raspberry Pi 5 included), from the user's
own ROM set. Each game must play exactly as it does in Supermodel, the
reference Model 3 emulator.

In scope:

- one core per game, for the 24 ROM sets in `games.toml`
- test mode, a full game to game over, and saves for every set
- controls the board expects: wheel, pedals and shifter, guns, sticks and
  buttons
- stereo 3D for headsets and 3D displays
- first-boot settings for single cabinets

Out of scope for now:

- link play between cabinets (designed in issue #35, not built)
- Windows (an item on the app's roadmap)
- force feedback from the drive board to a real wheel (on the app's
  roadmap)

## Requirements

- **Supermodel is the base and the oracle.** A game is done when a recorded
  input run of its test mode and of a full game to game over gives the same
  screens (a CRC for every frame) and the same sound (WAV) in the core as in
  stock Supermodel. Both run at 57.524 Hz with the board on one thread.
  MAME is a second opinion on CPU behaviour only, because MAME marks every
  Model 3 game as not working.
- **No Sega code or data**: no ROM, ROM fragment, generated code, texture,
  sample or anything derived from ROM bytes in the repository. The user
  supplies their own ROM sets. Our own analysis (addresses, hashes, run
  logs) may be committed.
- **GPL-3.0-or-later**, as Supermodel is. Supermodel's notices stay as they
  are. This is why the Model 3 cores live in their own repository, apart
  from the BSD-licensed Model 1/2 cores.
- **Supermodel stays recognisable.** Changes to its own files are small and
  additive, so emulation fixes from upstream can still be cherry-picked.
  Anything new lives in `Core/`.
- **The same `afcore.h` interface as every other core**, so the app treats
  Model 3 like any other board.
- **Repeatable runs.** The same inputs give the same frames and sound every
  time, so a change can be checked against the run before it.

## Design

### Where it sits

The repository is a hard fork of Supermodel with its whole history:

- `Src/`: Supermodel itself (the CPU cores, the board, New3D, the tile
  generator, sound)
- `Core/`: everything new:
  - `src/m3core.cpp`: the core
  - `src/gl_context.cpp`: its own GL context
  - `src/metal/`: the Metal renderer
  - `src/gles/`: the OpenGL ES shim
  - `src/nosdl/`: what replaces SDL in Apple builds
  - `tools/`: m3run and the reference tools
  - `inputs/`: recorded input scripts
  - `ppcrec/`: the PowerPC recompiler, on a branch
- `common/`: Arcade-Foundry-Cores-Common as a submodule, for `afcore.h` and
  the shared game-list tools
- `Makefiles/`: Supermodel's own build, kept to build the stock emulator as
  the reference

`main` holds released code only, and `af-next` is development.

### One game, one core

`cmake -B build -DM3CORE_ROMSET=vf3` builds `afcore_vf3`, a library that
exports only `afcore_api_vf3()`. Supermodel keeps the PowerPC, the sound
board and the logger in globals, so one process image can hold only one
running Model 3 machine. Making each game its own library keeps that
limit invisible to the app: it loads the one core it needs.

The core wraps Supermodel's `CModel3`:

- **Loading.** Supermodel's `GameLoader` reads the zip using
  `Config/Games.xml`, which is built into the core as text. The core asks
  for its own set, because MAME's merged zips hold several revisions.
  `check_roms` uses the same loader, so the app can check a set without
  starting the game.
- **Inputs** are written straight into Supermodel's input objects each
  frame (`null_input_system.h`), never polled. The app sends wheel, pedals
  and shifter values in standard units. The core converts them to the
  board's units (steering 0x00-0x80-0xFF, pedals 0-0xFF, 4-speed or up/down
  shifter, VR view buttons).
- **Frames.** `RunFrame` runs one 57.524 Hz frame of the board on the
  caller's thread. Stock Supermodel defaults to 60 Hz and to several
  threads.
- **Sound** is one 44.1 kHz stereo stream. Front and rear are averaged per
  side, as Supermodel does for a stereo host.
- **Saves** are Supermodel's own `.nv` files (NVRAM and EEPROM), so a save
  moves between Supermodel and the core either way.
- **Versions.** Each game has its own core version in
  `Core/src/core_versions.h`, bumped with a change to how that game plays
  or saves.

### The board's clock

Stock Supermodel reads the host's clock on every RTC read. That made Daytona
USA 2's attract mode, which follows the clock, differ from run to run. The
core reads the host's time once at start and then counts the board's frames
(`g_RTCTimeSource`), so a run depends only on when it started.
`AF_DEBUG_ATTRACT_RTC_START` fixes the start for test runs.

### First boot

Scud Race and Daytona USA 2 come up as one cabinet of a linked set, and stop
at "network board not present". On a first boot with no save, the core
waits until the game has written its default settings to the EEPROM. It
then makes the single-cabinet change an operator would make in test mode,
fixing the settings checksum, and resets the board once. Only the knowledge
of where that setting lives is in the code. The resulting EEPROM is byte for
byte what the test-mode change gives.

### Graphics

Supermodel draws the 3D with New3D, a reimplementation of the Real3D in
OpenGL 4.1 shaders. The 2D tile layers are drawn by its own 2D renderer and
mixed with the 3D. The cores keep New3D's logic and change only where it
draws:

- **New3D's GPU side behind an interface.** `IR3DGpu` (`R3DGpu.h`) holds
  every GL call `New3D.cpp` used to make, moved unchanged and in the same
  order into `R3DGpuGL`. `IRender2D` does the same for the tile layers.
  Screens and sound were identical before and after the move.
- **Metal on Apple** (`M3CORE_METAL`, the default on Apple).
  `R3DShadersMetal.h` is New3D's shaders, fog, composite and 2D in MSL,
  with quads as on desktop GL. `metal_renderer.mm` implements `IR3DGpu` and
  `IRender2D`. The finished frame reaches the app as a Metal surface, with
  no readback. Apple builds link nothing but the system: no SDL and no
  OpenGL.
- **Desktop OpenGL on x86-64 Linux** draws through Supermodel's own GL
  renderer into an FBO in the core's own EGL context, read back as pixels.
  The context is EGL on a small pbuffer, made current only around calls
  into the core, so it never fights the app's SDL context.
- **OpenGL ES 3.1 on arm64 Linux** (`M3CORE_GLES`, on a branch) uses the
  same GL renderer through a shim that rewrites the shaders for GLSL ES.
  See [ARM64-PERFORMANCE.md](ARM64-PERFORMANCE.md).
- **Line of sight.** Guns and the game's AI read back depth and stencil
  pixels. Metal reads them at the end of the frame, and GLES reads them
  through a small blit, because ES cannot read depth directly.
- **Stereo.** New3D keeps a frame's display lists, so `RenderEye` draws the
  frame's 3D again for each eye with that eye's projection. The core places
  the 2D layers itself: the bottom layer as background at infinity, and the
  top layer (HUD, text) at screen depth or apart for head-tracked views.
  The interface is the same as the Model 1/2 cores' stereo.

### Speed

Two design choices keep runs repeatable on every platform: the board runs on
the caller's thread, and wait loops are skipped exactly. A wait loop the
PowerPC sits in until an interrupt is skipped up to the interrupt, with the
same registers and timing as if it had run. The speed work layered on top
of that is in [ARM64-PERFORMANCE.md](ARM64-PERFORMANCE.md):

- the wait-loop skip
- asynchronous Metal frames
- the PowerPC static recompiler
- the GLES renderer and its board threads

The one exception to repeatable runs is the threaded mode on the Pi, which
is not used for checking.

## Data and settings

| Item | Where |
|---|---|
| Game list | `games.toml` (set, title, year, board, version, controls); `Scripts/gen_games.py` writes the files that list games, and CI checks them |
| Game database | `Config/Games.xml`, Supermodel's, built into each core |
| Core versions | `Core/src/core_versions.h` |
| Saves | `<save_dir>/<romset>.nv`, Supermodel's format |
| Input scripts | `Core/inputs/<set>_test.txt`, `<set>_game.txt` |
| Upstream fixes taken | `UPSTREAM.md` |

Build options:

- `M3CORE_ROMSET` (required)
- `M3CORE_METAL` (default on for Apple)
- `M3CORE_SDL` (default off on Apple)
- `M3CORE_GLES` (default on for arm64 Linux, branch)
- `M3CORE_PPCREC` (branch)

Run-time switches for checks and debugging read by the core are named
`AF_DEBUG_` plus their old name (`AF_DEBUG_PPC_SKIP`,
`AF_DEBUG_ATTRACT_RTC_START`, ...); the old name still works in 1.3.0 and
goes in 1.4.0. Every core's switches are listed in
Arcade-Foundry-Cores-Common's `docs/DEBUG-SWITCHES.md`, and the speed ones
in ARM64-PERFORMANCE.md. The reference tools (`refrun.sh`, the
`ATTRACT_REF_*` switches) keep their names. A switch added on a branch
(`PPCREC*`, `M3GLES_*`) is read with `af_debug_env` from the start.

## Platforms

| Platform | Renderer | Context | Notes |
|---|---|---|---|
| macOS (Apple silicon) | Metal | none | Full speed, several times over |
| iPadOS, iOS | Metal | none | Asynchronous frames |
| tvOS | Metal | none | Asynchronous frames; Apple TV 4K 3rd gen full speed in Daytona USA 2, 2nd gen about 70-82% |
| Linux x86-64 | OpenGL 4.1 | EGL pbuffer | Runs on any GL 4.1 GPU; speed not yet measured on real hardware |
| Linux arm64 (Pi 5) | OpenGL ES 3.1 (branch) | EGL | Attract mode full speed, races not yet |

## Testing

- **m3run** (`Core/tools/m3run.cpp`) runs a core headless. It plays an input
  script (coins, buttons, wheel and pedals in the board's units, frame by
  frame) and writes a CRC for every frame, chosen frames as images and the
  sound as WAV.
- **refrun.sh** runs the core and stock Supermodel on the same script and
  compares every frame and the sound. Supermodel is built from this
  repository's `Makefiles/` with small reference hooks
  (`Core/reference/reference.h`) that do nothing unless switched on, so the
  reference is Supermodel at the commit `main` is on. Reference settings:
  `-no-threads -true-hz -no-throttle -no-vsync`, no NVRAM, and the same
  clock start.
- **Every game** has a test-mode script and a full-game script, run to game
  over, before it is called done.
- **Renderers that cannot match bit for bit** (Metal against OpenGL) are
  compared with `tools/ppmdiff.py` on screenshots. The limits are a share of
  pixels and a size of difference; edges differ because GPUs round coverage
  differently.
- **CI** runs on every pull request: the release scan, a check of the
  generated game list, and a ROM-free Linux build of a core and m3run. The
  app's nightly run also runs refrun.sh on a machine that has the ROMs.

## Open items

- Link play between cabinets (issue #35). The network board's TCP link
  moves to plain sockets when it comes.
- The Pi 5 and Pi 4 at full speed in races, and merging the GLES renderer
  and the PowerPC recompiler (see ARM64-PERFORMANCE.md).
- Force feedback from the drive board to real wheels.
- More sets as ROMs turn up. The ones still missing are tracked in the
  app's project notes, not here.
- A monthly review of upstream Supermodel for emulation fixes, recorded in
  `UPSTREAM.md`.

## Decisions

- **Supermodel, not MAME, as the base.** Supermodel's Real3D emulation is
  the most complete there is. MAME's Model 3 graphics are marked not
  working, so MAME is used only as a second opinion on the CPU.
- **A hard fork with cherry-picked fixes, not a wholesale merge.** Our
  changes to Supermodel's files stay small, and upstream emulation fixes
  are picked one by one and logged.
- **One core per game.** Supermodel's globals allow one machine per process
  image. Separate libraries keep that from mattering, and each game gets its
  own version.
- **The board on the caller's thread at 57.524 Hz.** Runs are repeatable and
  match the real board's rate. Threads come back only where a device needs
  them (the Pi), never for checking.
- **First game: Virtua Fighter 3.** Step 1.0 is the simplest board, and
  Scud Race (1.5) and Daytona USA 2 (2.1) extend it.
- **New3D behind an interface, then Metal, rather than OpenGL on Apple.**
  Apple's OpenGL ran New3D's quad geometry shader as a compute pass before
  every draw (Daytona USA 2: 18 frames a second with quads on an M2 Max).
  Metal with quads runs several times faster than that.
- **No SDL in Apple builds.** Homebrew's SDL2 is a compatibility layer that
  loads SDL3 at run time. Inside an app it failed and stopped the app with
  a dialog. Apple builds use the C++ library for threads.
- **EGL rather than an SDL window for the GL context on Linux.** This
  avoids two GL contexts fighting over one thread in the same process as
  the app's SDL3.
- **Not Model-3-Vulkan for a Vulkan renderer**, because of its credit terms.
  A Vulkan renderer of our own comes only if GLES proves too slow on the Pi.
