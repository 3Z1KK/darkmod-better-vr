#!/usr/bin/env python3
"""VR spyglass: the eyepiece vignette (textures/vr/spyglass/vignette.tga).

White in the middle, darkening towards the rim of the lens, black outside it. Multiplied over the live picture
("blend filter") so the round picture looks like it is seen through a lens. Plain Python, no libraries.
Usage: make_vignette.py <out.tga>
"""
import struct
import sys

SIZE = 256
CLEAR = 0.55      # fully bright up to this fraction of the radius
EDGE = 0.97       # black from here on (a thin dark rim inside the eyepiece)


def smoothstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def main(out):
    rows = []
    for y in range(SIZE):
        row = bytearray()
        for x in range(SIZE):
            dx = (x + 0.5) / SIZE * 2 - 1
            dy = (y + 0.5) / SIZE * 2 - 1
            r = (dx * dx + dy * dy) ** 0.5
            v = 1.0 - smoothstep(CLEAR, EDGE, r)
            v = v ** 1.6          # darkens gently first, then quickly near the rim
            c = int(round(255 * v))
            row += bytes((c, c, c))
        rows.append(bytes(row))
    header = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, SIZE, SIZE, 24, 0)
    with open(out, 'wb') as f:
        f.write(header)
        for row in reversed(rows):   # bottom-up
            f.write(row)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'vignette.tga')
