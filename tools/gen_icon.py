#!/usr/bin/env python3
"""Draw Tachyon's icon: a globe with a particle streaking past it.

Writes a classic 16-colour .ico (32x32 and 16x16, 4 bpp with an AND mask),
the format Windows 95 understands. Each pixel is supersampled from the
shapes below and snapped to the standard VGA palette.

Usage: gen_icon.py OUTPUT.ico
"""

import math
import struct
import sys

# The Windows 16-colour palette, in icon palette order.
PALETTE = [
    (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0),
    (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
    (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0),
    (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
]
BLACK, NAVY, TEAL, SILVER, BLUE, YELLOW, WHITE = 0, 4, 6, 7, 12, 11, 15

HEAD = (0.80, 0.22)
TAIL = (0.04, 0.86)


def dist_to_segment(px, py, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy)), t


def shade(x, y):
    """Palette index at (x, y) in the unit square, or None for transparent."""
    # The particle and its streak sit in front of the globe.
    if math.hypot(x - HEAD[0], y - HEAD[1]) < 0.045:
        return WHITE
    if math.hypot(x - HEAD[0], y - HEAD[1]) < 0.10:
        return YELLOW
    d, t = dist_to_segment(x, y, TAIL, HEAD)
    if d < 0.012 + 0.035 * t:
        return WHITE if t > 0.45 else SILVER

    r = math.hypot(x - 0.46, y - 0.54)
    if r < 0.36:
        # A meridian and the equator, then a highlight towards the top left.
        if abs(r - 0.36 * 0.55) < 0.02 and x < 0.46:
            return TEAL
        if abs(y - 0.54) < 0.022:
            return TEAL
        return BLUE if math.hypot(x - 0.36, y - 0.42) < 0.20 else NAVY
    if r < 0.41:
        return BLACK
    return None


def render(size, samples=4):
    pixels = []
    for py in range(size):
        row = []
        for px in range(size):
            votes = {}
            for sy in range(samples):
                for sx in range(samples):
                    c = shade((px + (sx + 0.5) / samples) / size, (py + (sy + 0.5) / samples) / size)
                    votes[c] = votes.get(c, 0) + 1
            row.append(max(votes, key=votes.get))
        pixels.append(row)
    return pixels


def bmp_image(pixels):
    size = len(pixels)
    xor_stride = ((size * 4 + 31) // 32) * 4
    and_stride = ((size + 31) // 32) * 4
    xor = bytearray()
    mask = bytearray()
    for row in reversed(pixels):  # bottom-up
        line = bytearray(xor_stride)
        mline = bytearray(and_stride)
        for x, c in enumerate(row):
            index = 0 if c is None else c
            line[x // 2] |= index << (4 if x % 2 == 0 else 0)
            if c is None:
                mline[x // 8] |= 0x80 >> (x % 8)
        xor += line
        mask += mline
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 4, 0, len(xor) + len(mask), 0, 0, 16, 0)
    palette = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in PALETTE)
    return header + palette + bytes(xor) + bytes(mask)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    images = [bmp_image(render(size)) for size in (32, 16)]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, data in zip((32, 16), images):
        out += struct.pack("<BBBBHHII", size, size, 16, 0, 1, 4, len(data), offset)
        offset += len(data)
    out += b"".join(images)
    with open(sys.argv[1], "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()
