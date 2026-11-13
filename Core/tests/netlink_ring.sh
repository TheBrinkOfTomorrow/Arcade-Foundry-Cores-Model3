#!/bin/sh
# A ring of network boards on this machine, no ROM needed (netlink_test.cpp).
#   Core/tests/netlink_ring.sh BUILD_DIR CABINETS FRAMES [SDL_SEATS]
# Seat 1 is the master; each seat's next is the seat before it (seat 1's is
# the last), as Game Director wires a ring. SDL_SEATS (for example "2 3")
# run m3netlink_sdl, the stock Supermodel network layer on SDL_net. Ports
# 4101 up. Prints each cabinet's RESULT line; exit 1 unless every cabinet
# was up for the same number of frames with no bad frame. (The last frame
# can end in state=error: the first cabinet to finish closes its sockets.)
set -u
build=$1 n=$2 frames=$3 sdl=${4:-}
dir=$(mktemp -d)
i=1
while [ "$i" -le "$n" ]; do
  prev=$((i - 1)); [ "$prev" -eq 0 ] && prev=$n
  role=slave; [ "$i" -eq 1 ] && role=master
  bin=$build/m3netlink
  for s in $sdl; do [ "$s" -eq "$i" ] && bin=$build/m3netlink_sdl; done
  "$bin" $role $((4100 + i)) 127.0.0.1:$((4100 + prev)) "$frames" > "$dir/$i.log" 2>&1 &
  i=$((i + 1))
done
wait
fail=0
ups=""
i=1
while [ "$i" -le "$n" ]; do
  line=$(grep RESULT "$dir/$i.log" || echo "RESULT none")
  echo "seat $i: $line"
  case $line in *bad=0\ *) ;; *) fail=1 ;; esac
  case $line in *up=0\ *) fail=1 ;; esac
  ups="$ups $(echo "$line" | sed 's/.* up=\([0-9]*\).*/\1/')"
  i=$((i + 1))
done
[ "$(echo $ups | tr ' ' '\n' | sort -u | wc -l)" -eq 1 ] || fail=1
[ -n "${KEEP:-}" ] && echo "logs in $dir" || rm -rf "$dir"
exit $fail
