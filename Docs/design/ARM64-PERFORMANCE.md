# Model 3 on arm64: speed on the Raspberry Pi 5 and Apple devices

How the Model 3 cores are made fast enough for arm64 machines that are much
slower than a desktop: the Raspberry Pi 5 (and Pi 4) under Linux, and
iPhone, iPad and Apple TV. It covers the PowerPC side (the wait-loop skip and
the static recompiler), the rendering side (Metal on Apple, OpenGL ES 3.1 on
the Pi) and running the boards on several CPU cores. The dated log of this
work is in [HANDOFF.md](../../HANDOFF.md); the state of the cores as a whole
is in [STATE.md](../../STATE.md).

## Purpose and scope

A Model 3 game has to produce 57.524 frames a second. On a desktop Mac that
is easy. On an Apple TV or a Pi 5, Supermodel as it comes runs at roughly a
fifth to half of that. Three things cost the most time:

1. The PowerPC 603e board, run by Supermodel's interpreter.
2. Drawing: the Real3D GPU emulated through New3D, which on a Pi means
   desktop OpenGL 4.1 shaders the Pi's GPU cannot run.
3. Everything on one thread: the PowerPC, the sound board (68000 plus the
   SCSP and its DSP), the drive board and the GL calls take turns.

This document covers all three for arm64. It does not cover the Model 1/2
cores (they reach full speed on a Pi 4 with their own fast paths) or link
play.

## Requirements

- Exact emulation. Any speed-up must give the same frames and sound as
  Supermodel's interpreter, checked frame by frame (CRCs and WAV) on recorded
  input scripts. The one exception is the multi-threaded mode, which is
  not deterministic by design (see Threads).
- Nothing taken from ROMs in the repository. The recompiler works from the
  user's own ROM set at build time, and its output stays in the build
  directory.
- Desktop builds unchanged. Pi-only changes are behind `M3CORE_GLES` or
  `#ifdef GL_ES`. Apple-only changes are behind `M3CORE_METAL`.
- Small, additive changes to Supermodel's own files, so upstream fixes can
  still be cherry-picked.

## Design

### Where the time goes: measure first

Several guesses about the Pi 5 turned out wrong (see Decisions), so the
cores carry their own timers:

- `M3CORE_TIMING=1` prints, every 60 frames, the milliseconds per frame of
  the whole frame, the PowerPC board, the sound board, the drive board and
  the drawing. Drawing is split into GL calls (texture uploads shown
  separately, with the number of uploads per frame), waiting for the GPU and
  the readback. It also shows how long the drawing thread waited for the
  board threads.
- `AF_DEBUG_ATTRACT_M3_TIMING=1` does the same for the Metal path on Apple devices
  (`core`, `GPU` and `frame`).
- `PPCREC_STATS=1` prints the share of PowerPC instructions that ran
  recompiled.
- For the Pi's shader compiler, Mesa's v3d compiler runs without the
  hardware: Mesa's no-op DRM shim (`libv3d_noop_drm_shim.so`) with
  `V3D_DEBUG=shaderdb` under `qemu-aarch64` prints the instruction, uniform
  and spill counts of every shader the core compiles. `M3GLES_DUMP=<dir>`
  writes out each converted shader.

### PowerPC: the wait-loop skip (all platforms, released)

A histogram of where the PowerPC spends its instructions showed that most
of the time goes to loops that only wait. For example, three quarters of
Daytona USA 2's instructions poll one RAM byte that the interrupt handler
bumps once a frame. Such a loop can only end when an interrupt is taken, so
`ppc_bcx` skips whole iterations up to the next decrementer or time-slice
interrupt. The interpreter runs the rest, and icount, the timebase, DEC and
the registers come out exactly as they would without the skip.

Loops are found by the shape of their instructions, not by address:

- RAM polls (`lbz`/`lhz`/`lwz` with a compare, in either order, on any CR
  field). RAM must still hold the value just compared.
- Decrementer waits.
- Countdowns (`addic. r,r,-1; bne -4`).
- Timebase bit and timebase compare waits. These end by themselves, so the
  exit point is computed from the timebase.

`AF_DEBUG_PPC_SKIP` is a mask (1 RAM polls, 2 decrementer, 4 countdown, 8 timebase;
default 15), and 0 turns the skip off. A `-DPPC_SKIP_VERIFY` build runs
every loop it would skip and reports any that would have ended early. It
found two bugs, both fixed.

Result on an M4 Pro Mac, Metal: Daytona USA 2 went from 83 to 231 frames a
second, VF3 from 138 to 212, Scud Race from 106 to 219 and Sega Rally 2
from 75 to 244. CRCs and WAV were identical with the skip on and off. This
made the Apple TV 4K (3rd gen) full speed in Daytona USA 2 races.

### PowerPC: the static recompiler (branch `model3-ppc-recompiler`)

The game's PowerPC code is compiled to C at build time, from a recorded run
of the game. The interpreter runs whatever was not compiled. Code lives in
`Core/ppcrec/`.

- **Build.** `Core/ppcrec/build_ppcrec.sh SET` builds a tracing core
  (`-DM3CORE_PPCREC=ON`) and plays the set's test-mode and game input
  scripts with `PPCREC_TRACE=file`. The trace records every block entered,
  its code, every version of code in RAM and how often it ran.
  `gen_ppcrec.py` turns the traces into C, and the core is rebuilt with it
  (`-DM3CORE_PPCREC_BLOCKS=file`). `CHECK=1` plays both scripts again and
  compares frame CRCs and sound with the tracing run (the interpreter). The
  app's `build_cores.py --ppcrec` drives this with `BLOCKS_ONLY=1` and then
  builds each platform's core with the same blocks.
- **Blocks.** A block runs from an entry point to the first branch, `rfi`,
  `sc`, trap, `mtmsr`, most `mtspr`, an invalid opcode, a 4 KB page end or
  256 instructions. Register-only instructions call the interpreter's own
  handler with a constant opcode, which the C compiler inlines and folds.
  RAM loads and stores go straight to memory, and anything else goes
  through the bus as before. Branches are inline. All the blocks of a page
  form one C function, and a branch to the next block in the same page is a
  `goto`.
- **Exactness.** A block runs only if it fits in the rest of the time slice
  and the decrementer does not fire inside it. Otherwise the interpreter
  runs those instructions. icount is set as the interpreter has it before
  `mftb`, `mfspr` and every block end. After a bus access that raised an
  interrupt, the block stops, as the interpreter would take the interrupt
  after that instruction. Short backward branches still call `ppc_bcx`, so
  the wait-loop skip keeps working.
- **Code in RAM.** Each 4 KB page of RAM has a write counter. RAM writes
  bump it (`PPCREC_NOTE_WRITE` in `CModel3::Write8/16/32`, and compiled
  stores bump it themselves), and so do reset and load state. A block
  checks its code against memory before it runs, and again after its page
  was written.
- **Build time.** Traces count how often each entry point ran, and
  `gen_ppcrec.py --coverage` (default 0.999) keeps only the hottest blocks.
  For Daytona USA 2 that is 8,496 blocks in 16 C files built in parallel,
  about 6 minutes of compiling instead of 32 for one file. GCC builds them
  with `-fno-gcse`.
- **Supermodel files touched**, all under `#ifdef PPC_RECOMP`: `ppc.cpp`,
  `ppc603.c` (three hooks in `ppc_execute`) and `Model3.cpp` (the RAM write
  notes).

Results on x86-64: the test and game scripts of daytona2, scud and vf3 are
identical to the interpreter, with 99.86 to 99.97% of instructions run
recompiled. With the wait-loop skip on in both, PowerPC time fell 3.2 times
for Daytona USA 2. A core built from the test-mode trace alone is still
identical on the game script (98.46% recompiled), because the interpreter
covers what no trace saw.

### Rendering on Apple: Metal (released)

Apple devices have no current OpenGL, so the cores have their own Metal
renderer for New3D, the 2D tile layers and the final picture, with quads.
On an M2 Max, Daytona USA 2 runs at 55.9 frames a second on Metal against
22.8 on OpenGL, and at most 1.4% of pixels differ, each by at most 2 of 255.
For speed on Apple TV:

- **Asynchronous frames.** The GPU draws frame N while the boards run frame
  N+1, so the picture is one frame old. Writes to GPU memory wait for the
  frame in flight. Texture uploads are queued and copied from texture RAM
  at the start of the next frame. Line-of-sight results (used by guns and
  the AI) still arrive before the game reads them. `AF_DEBUG_ATTRACT_M3_SYNC=1`
  restores the old behaviour.
- **Fewer passes.** New3D layer passes start with their first mesh, so
  empty layers cost nothing. Depth clears and copies wait until a pass needs
  them.
- **The screen as a surface.** The finished frame goes to the app as a Metal
  surface, with no per-frame readback.

### Rendering on the Pi: OpenGL ES 3.1 (branch `model3-gles`)

The Pi 4 and Pi 5 GPUs (Broadcom V3D, Mesa's v3d driver) have OpenGL ES
3.1, not desktop OpenGL 4. `-DM3CORE_GLES=ON` (default on Linux aarch64)
builds the same New3D GL renderer against GLES 3.1 through EGL, rather than
writing a new renderer:

- `Core/src/gles/GL/glew.h` stands in for GLEW. It provides GLES3 plus a few
  renamed calls (`glClearDepthf`, `glDepthRangef`, a `glDrawBuffer`
  wrapper).
- `gles_shim.cpp` rewrites each `#version 410 core` shader to
  `#version 310 es` with highp precision as it is compiled.
- Quad rendering is off: ES 3.1 has no geometry shaders, so it uses
  triangles.
- `R3DShaderCommon.h` uses explicit `vec2()` conversions and picks texture
  bank 0 or 1 with a branch instead of a dynamic sampler-array index. Both
  are errors in GLSL ES and give the same result on desktop GL.
- ES cannot read back depth or stencil, so line-of-sight reads blit the
  pixel into a 1x1 depth-stencil texture, and two tiny shaders copy depth
  and stencil into an integer target that is read back.

On x86-64 Linux (Mesa llvmpipe), the GLES build draws Daytona USA 2 and VF3
pixel-identical to the desktop GL build, and line-of-sight reads return the
same values. On a Pi 5, Scud Race draws correctly.

Making it fast on the Pi has been a series of measurements:

- **Fragment shader size.** Mesa's shader-db showed New3D's fragment shader
  at about 8,400 instructions and 1,941 uniform slots on V3D. The texture
  filter (four `texelFetch` calls and twelve texel formats) was inlined 24
  times: three lookups times two banks times four texels. The V3D driver
  writes every uniform slot again for every draw, and a Scud race makes
  about 730 draws a frame. A looped version of the filter for GLSL ES (one
  fetch and decode per pass, with the bank picked per fetch) came to 2,514
  instructions and 508 slots with no spills, pixel-identical. On the Pi 5,
  though, the unrolled filter ran faster in races (about 24 frames a second
  against 20). So the unrolled filter is the default, and `M3GLES_LOOPS=1`
  picks the loop.
- **Texture uploads.** New3D uploaded each changed texture rectangle one
  row at a time, which meant thousands of `glTexSubImage2D` calls a frame
  in a Scud race. It now sends each rectangle in one call with
  `GL_UNPACK_ROW_LENGTH`. This gives the same texels on desktop GL and GLES.
- **Uniform buffers (tried, off).** `gles_shim.cpp` can move the triangle
  shaders' uniforms into std140 uniform blocks behind the renderer's back
  (`M3GLES_UBO=1`). On the Pi 5 the driver's uniform time only fell from
  28% to 22%, because most per-draw uniforms are texture state. It also made
  Mesa recompile the fragment shader during play. It stays in the code for
  later tests.

### Threads (GLES builds)

GLES builds turn on Supermodel's own `MultiThreaded` and
`GPUMultiThreaded` modes. The PowerPC board, the sound board and the drive
board each run on their own thread, while the calling thread draws, so the
GL context stays on the thread that made it. Supermodel's sound thread
normally waits for an audio callback, which a core does not have. A new
config key, `SyncSoundBoardThread` (default off), runs it in step with each
frame instead. `M3CORE_THREADS=0` or `1` overrides the default. Desktop GL
and Metal builds stay single-threaded.

Threaded runs are not identical from run to run, and the 3D is one frame
behind the game, as with Metal's asynchronous frames. A threaded Scud run
on x86-64 plays the same race to game over with the same sound levels,
26 to 32 frames a second. The recompiler's checks therefore run with
`M3CORE_THREADS=0`.

## Data and settings

All run-time switches are environment variables, read once:

| Switch | Platforms | Effect |
|---|---|---|
| `AF_DEBUG_PPC_SKIP` | all | Wait-loop skip mask (default 15, 0 off) |
| `PPCREC=0` | recompiler builds | Interpreter only |
| `PPCREC_STATS=1` | recompiler builds | Share of instructions recompiled |
| `PPCREC_TRACE=file` | tracing builds | Write a trace |
| `M3CORE_THREADS=0/1` | GLES builds | Board threads off or on (default on) |
| `M3GLES_LOOPS=1` | GLES builds | Looped texture filter |
| `M3GLES_UBO=1` | GLES builds | Uniforms in uniform buffers |
| `M3GLES_DUMP=dir` | GLES builds | Write each converted shader |
| `M3CORE_TIMING=1` | Linux | Per-frame timing every 60 frames |
| `AF_DEBUG_ATTRACT_M3_TIMING=1` | Apple | Per-frame timing, Metal |
| `AF_DEBUG_ATTRACT_M3_SYNC=1` | Apple | Synchronous frames |

Build options: `M3CORE_METAL`, `M3CORE_GLES`, `M3CORE_PPCREC`,
`M3CORE_PPCREC_BLOCKS`. Recompiler traces and generated C are made from the
user's ROM set and never leave the build directory.

## Platforms

| Platform | Renderer | PowerPC | Speed (Nov 2026) |
|---|---|---|---|
| Mac (Apple silicon) | Metal | Interpreter + skip | Full speed, 2-4 times over |
| Apple TV 4K (3rd gen) | Metal, async | Interpreter + skip | Daytona USA 2 full speed |
| Apple TV 4K (2nd gen) | Metal, async | Interpreter + skip | About 70-82% |
| iPhone, iPad | Metal, async | Interpreter + skip | Not measured here |
| Raspberry Pi 5 | GLES 3.1, threads | Recompiler (branch) | Attract mode 100%; Scud races about 35-55% |
| Raspberry Pi 4 | GLES 3.1 | Recompiler (branch) | Not yet run; expected well below the Pi 5 |

On the Pi 5, in Scud Race races, the board threads take 7 to 8 ms a frame
each. The drawing thread takes 35 to 50 ms, part in GL calls and part
waiting for the GPU, so drawing is now the limit.

## Testing

- `Core/tools/refrun.sh` and `m3run` play an input script
  (`Core/inputs/SET_test.txt`, `SET_game.txt`) and write a CRC for every
  frame and the sound as WAV. Any change to emulation or to the
  single-threaded drawing must match, frame for frame, the run before it or
  stock Supermodel.
- The recompiler is checked with `build_ppcrec.sh CHECK=1`: both scripts,
  recompiled against interpreter.
- The wait-loop skip is checked with `AF_DEBUG_PPC_SKIP=0` against the default, and
  with the `PPC_SKIP_VERIFY` build.
- GLES changes are checked on x86-64 with Mesa llvmpipe through EGL's
  surfaceless platform (no display needed), against the desktop GL build.
- Speed is measured on the devices themselves with the app's `--speed`
  option and `M3CORE_TIMING=1`. CPU profiles alone misled more than once.

## Open items

- Pi 5: confirm what single-call texture uploads give. The timing line
  now shows upload time and count per frame. In later race sections almost
  all drawing time sits inside GL calls, not in waiting for the GPU.
- Pi 5: if drawing stays the limit, try fewer draws (merging New3D's meshes
  that share state) and fewer state changes per draw before any new
  renderer. A Vulkan renderer comes only if GLES proves too slow.
- Pi 4: first run.
- Recompiler on Apple devices: built for iOS and tvOS, not yet measured on
  an Apple TV. It only matters where the wait-loop skip and asynchronous
  frames are not enough.
- Recompiler: the dispatcher between pages is still about a sixth of the
  PowerPC time. Floating-point handlers are not inlined.
- Merge `model3-ppc-recompiler`, `model3-gles` and `model3-gles-ppcrec`
  into `af-next` after the device tests.

## Decisions

- **Recompile at build time, from traces, into C.** Not a JIT, because iOS
  and tvOS do not allow one. Not a hand-written arm64 back end, because C
  lets the platform compiler do the code generation, and it builds the same
  way for every target. The interpreter stays as the fallback, which keeps
  the result exact.
- **Skip wait loops by shape, not by game address.** One set of patterns
  covers every game, and each pattern is checked against the interpreter.
- **Reuse New3D for the Pi through a GLES shim.** This keeps one GL
  renderer, already checked pixel for pixel against desktop GL, instead of a
  second renderer to keep in step.
- **Use Supermodel's own threading.** It is already tested upstream. The
  only addition is running the sound thread in step with frames.
- **Measure on the device before optimising.** Three Pi 5 changes chosen
  from CPU profiles did not help or helped little: the recompiler alone, the
  uniform buffers and the looped shader. The per-part timers were added
  because of that.
