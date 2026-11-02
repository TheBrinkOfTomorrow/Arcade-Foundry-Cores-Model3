#!/bin/sh
# Reference run: stock Supermodel (bin/supermodel, built with the reference
# hooks) playing an input script, as the cores run (one thread, 57.524 Hz).
#   Core/tools/refrun.sh GAME FRAMES OUTDIR [SCRIPT] [SHOTS] [NVRAM]
# NVRAM: a .nv to start from (the core's save), else a blank board.
# Writes OUTDIR/ref.crc and OUTDIR/ref.wav (and PPM shots). Run from anywhere;
# needs Xvfb when there is no display.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GAME=$1; FRAMES=$2; OUT=$(mkdir -p "$3" && cd "$3" && pwd); SCRIPT=${4:-}; SHOTS=${5:-}; NV=${6:-}
[ -n "$NV" ] && NV=$(cd "$(dirname "$NV")" && pwd)/$(basename "$NV")
[ -n "$SCRIPT" ] && SCRIPT=$(cd "$(dirname "$SCRIPT")" && pwd)/$(basename "$SCRIPT")
cd "$ROOT"  # Supermodel finds Config/ and Assets/ in the current directory
X=""; [ "$(uname)" = Linux ] && [ -z "${DISPLAY:-}" ] && X="xvfb-run -a -s '-screen 0 1024x768x24'"
export ATTRACT_REF_GAME=$GAME ATTRACT_REF_FRAMES=$FRAMES ATTRACT_REF_CRC=$OUT/ref.crc ATTRACT_REF_WAV=$OUT/ref.wav \
  ATTRACT_REF_SCRIPT=$SCRIPT ATTRACT_REF_SHOTS=$SHOTS ATTRACT_REF_SHOT_DIR=$OUT SDL_AUDIODRIVER=dummy \
  ATTRACT_RTC_START=${ATTRACT_RTC_START:-1767225600}  # m3run's default
[ -n "$NV" ] && export ATTRACT_REF_NVRAM=$NV
FF=""; case $GAME in swtrilgy) FF=-force-feedback ;; esac  # its joystick board, as the core runs it
eval $X bin/supermodel roms/$GAME.zip -no-threads -true-hz -no-throttle -no-vsync -window -res=496,384 $FF $([ "${ATTRACT_M3_QUADS:-1}" = 0 ] && echo -no-quad-rendering) >"$OUT/ref.log" 2>&1
