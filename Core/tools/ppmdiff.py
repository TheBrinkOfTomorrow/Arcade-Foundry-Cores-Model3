#!/usr/bin/env python3
"""Compare two directories of m3run screenshots (--shot, binary PPM).

  ppmdiff.py DIR_A DIR_B [--diff-dir DIR]

For every frame_N.ppm in both: the share of pixels that differ, the share
that differ by more than 8 (of 255) in some channel, and the largest
difference. --diff-dir writes a PPM per frame with the differing pixels in
white, for a look. Used to check the Metal renderer against OpenGL, which
cannot match bit for bit (different GPUs round differently).
"""

import os
import re
import sys


def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
    if not m:
        raise SystemExit(f'{path}: not a binary PPM')
    w, h = int(m.group(1)), int(m.group(2))
    return w, h, data[m.end():m.end() + w * h * 3]


def main():
    args = sys.argv[1:]
    diff_dir = None
    if '--diff-dir' in args:
        i = args.index('--diff-dir')
        diff_dir = args[i + 1]
        del args[i:i + 2]
        os.makedirs(diff_dir, exist_ok=True)
    if len(args) != 2:
        raise SystemExit(__doc__)
    a_dir, b_dir = args
    frames = sorted((int(m.group(1)), n) for n in os.listdir(a_dir)
                    if (m := re.match(r'frame_(\d+)\.ppm$', n)) and os.path.exists(os.path.join(b_dir, n)))
    if not frames:
        raise SystemExit('no frames in both directories')
    print(f'{"frame":>7} {"differ":>8} {"> 8":>8} {"max":>4}')
    for frame, name in frames:
        wa, ha, a = read_ppm(os.path.join(a_dir, name))
        wb, hb, b = read_ppm(os.path.join(b_dir, name))
        if (wa, ha) != (wb, hb):
            print(f'{frame:>7} size differs: {wa}x{ha} and {wb}x{hb}')
            continue
        n = wa * ha
        differ = big = worst = 0
        out = bytearray(n * 3) if diff_dir else None
        for p in range(n):
            o = p * 3
            d = max(abs(a[o] - b[o]), abs(a[o + 1] - b[o + 1]), abs(a[o + 2] - b[o + 2]))
            if d:
                differ += 1
                if d > 8:
                    big += 1
                    if out is not None:
                        out[o:o + 3] = b'\xff\xff\xff'
                worst = max(worst, d)
        print(f'{frame:>7} {100.0 * differ / n:7.2f}% {100.0 * big / n:7.2f}% {worst:>4}')
        if out is not None:
            with open(os.path.join(diff_dir, name), 'wb') as f:
                f.write(f'P6\n{wa} {ha}\n255\n'.encode() + bytes(out))


if __name__ == '__main__':
    main()
