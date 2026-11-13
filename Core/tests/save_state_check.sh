#!/bin/sh
# Save states give the same game: a run that saves a state at frame N, a run
# from that state, and a run with no state at all must draw the same screen
# on every frame after N, and play the same sound.
#
#   Core/tests/save_state_check.sh BUILD_DIR ROMSET.zip SCRIPT N FRAMES
#
# BUILD_DIR has m3run (the set it was built for); SCRIPT is an input script
# (Core/inputs/<set>_game.txt); N the frame to save after; FRAMES how long
# the run is. Runs from a fresh save folder each time (first boot). Prints
# the first frame that differs, or "same", and whether the sound after N is
# the same. KEEP=1 keeps the work folder (the states, CRCs and sound).
set -eu
build=$1 rom=$2 script=$3 at=$4 frames=$5
work=$(mktemp -d)
if [ -n "${KEEP:-}" ]; then echo "work: $work"; else trap 'rm -rf "$work"' EXIT; fi
run() {  # name, extra args...
  name=$1
  shift
  mkdir -p "$work/$name"
  "$build/m3run" "$rom" "$@" --inputs "$script" --save "$work/$name" --crc "$work/$name.crc" --wav "$work/$name.wav" 2>"$work/$name.log" ||
    { cat "$work/$name.log" >&2; exit 1; }
}
run plain "$frames"
run saved "$frames" --save-state-at "$at" --state "$work/state.afstate"
run loaded $((frames - at - 1)) --load-state "$work/state.afstate"
grep -h 'saved\|loaded' "$work/saved.log" "$work/loaded.log"
status=0
for other in saved loaded; do
  # frames after N in both
  awk -v at="$at" '$1 > at' "$work/plain.crc" >"$work/plain.after"
  awk -v at="$at" '$1 > at' "$work/$other.crc" >"$work/$other.after"
  if cmp -s "$work/plain.after" "$work/$other.after"; then
    echo "$other: same ($(wc -l <"$work/$other.after") frames after $at)"
  else
    first=$(diff "$work/plain.after" "$work/$other.after" | grep -m1 '^[<>]' | awk '{print $2}')
    echo "$other: differs from frame $first"
    status=1
  fi
done
# the sound: the loaded run's must be the end of the plain run's
python3 - "$work/plain.wav" "$work/loaded.wav" <<'PY' || status=1
import sys
a = open(sys.argv[1], "rb").read()[44:]
b = open(sys.argv[2], "rb").read()[44:]
tail = a[len(a) - len(b):]
if tail == b:
    print("sound: same (%d samples after the state)" % (len(b) // 4))
else:
    first = next(i for i in range(0, len(b), 4) if tail[i:i + 4] != b[i:i + 4]) // 4
    print("sound: differs from sample %d of %d after the state" % (first, len(b) // 4))
    sys.exit(1)
PY
exit $status
