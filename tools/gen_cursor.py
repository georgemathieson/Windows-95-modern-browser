#!/usr/bin/env python3
"""Draw the pointing-hand cursor links show (Windows 95 has none: IDC_HAND
came with Windows 98) as a 32x32 monochrome .cur file.

Usage: gen_cursor.py OUTPUT.cur
"""

import struct
import sys

# X black, o white, space transparent. The hot spot is the fingertip.
HAND = [
    "     XX          ",
    "    XooX         ",
    "    XooX         ",
    "    XooX         ",
    "    XooX         ",
    "    XooXXX       ",
    "    XooXooXXX    ",
    "    XooXooXooXX  ",
    " XX XooXooXooXoX ",
    "XooXXooooooooXooX",
    "XoooXoooooooooooX",
    " XooXoooooooooooX",
    "  XoooooooooooooX",
    "  XooooooooooooX ",
    "   XoooooooooooX ",
    "   XooooooooooX  ",
    "    XoooooooooX  ",
    "    XoooooooooX  ",
    "    XXXXXXXXXXX  ",
]
HOTSPOT = (5, 0)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    xor = bytearray(32 * 4)
    and_ = bytearray(b"\xff" * (32 * 4))
    for y, row in enumerate(HAND):
        for x, ch in enumerate(row):
            if ch == " ":
                continue
            # Rows are stored bottom-up.
            i = (31 - y) * 4 + x // 8
            bit = 0x80 >> (x % 8)
            and_[i] &= ~bit & 0xFF
            if ch in "oO":
                xor[i] |= bit
    header = struct.pack("<IiiHHIIiiII", 40, 32, 64, 1, 1, 0, len(xor) + len(and_), 0, 0, 2, 0)
    palette = struct.pack("<II", 0x000000, 0xFFFFFF)
    image = header + palette + bytes(xor) + bytes(and_)
    entry = struct.pack("<BBBBHHII", 32, 32, 2, 0, HOTSPOT[0], HOTSPOT[1], len(image), 6 + 16)
    with open(sys.argv[1], "wb") as out:
        out.write(struct.pack("<HHH", 0, 2, 1) + entry + image)


if __name__ == "__main__":
    main()
