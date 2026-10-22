#!/usr/bin/env python3
"""Make the image decoders' test images in tests/data/images.

Run once (needs ImageMagick with WebP); the images are checked in. Most
show the same 4x2 pattern, so the tests know every pixel:

    red    green        blue          white
    black  transparent  half-red (a=128)  yellow

Formats without alpha get the pattern on white; GIF's transparency is all
or nothing, so its half-red is transparent. ICO's variants are written
here byte by byte, to test each bitmap depth and the transparency mask.

Usage: make_test_images.py OUT_DIR
"""

import os
import struct
import subprocess
import sys
import zlib

PATTERN = [
    [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 255, 255)],
    [(0, 0, 0, 255), (0, 0, 0, 0), (255, 0, 0, 128), (255, 255, 0, 255)],
]


def rgba_bytes(pattern):
    return bytes(c for row in pattern for px in row for c in px)


def magick(out, args, data=None, size=None):
    cmd = ["convert"]
    if data is not None:
        cmd += ["-size", "%dx%d" % size, "-depth", "8", "rgba:-"]
    cmd += args + [out]
    subprocess.run(cmd, input=data, check=True)


def png_bytes(pattern):
    """A minimal RGBA PNG, for putting inside an ICO."""
    h = len(pattern)
    w = len(pattern[0])
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in pattern)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def ico(entries):
    """An ICO file from (width, height, bits, image bytes) entries."""
    head = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    dirs, blobs = b"", b""
    for w, h, bits, blob in entries:
        dirs += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, bits, len(blob), offset + len(blobs))
        blobs += blob
    return head + dirs + blobs


def dib(pattern, bits):
    """A BITMAPINFOHEADER bitmap as icons hold it: bottom up, twice the
    height (colour then a 1-bit mask). 32 bits keep alpha; fewer use a
    palette and the mask."""
    h = len(pattern)
    w = len(pattern[0])
    palette = []
    if bits <= 8:
        for row in pattern:
            for r, g, b, a in row:
                if (r, g, b) not in palette:
                    palette.append((r, g, b))
    header = struct.pack("<IiiHHIIiiII", 40, w, 2 * h, 1, bits, 0, 0, 0, 0, len(palette), 0)
    pal = b"".join(struct.pack("<BBBB", b, g, r, 0) for r, g, b in palette)
    colour = b""
    for row in reversed(pattern):
        if bits == 32:
            line = b"".join(struct.pack("<BBBB", b, g, r, a) for r, g, b, a in row)
        else:
            acc, n, line = 0, 0, b""
            for r, g, b, a in row:
                acc = acc << bits | palette.index((r, g, b))
                n += bits
                if n == 8:
                    line += bytes([acc])
                    acc, n = 0, 0
            if n:
                line += bytes([acc << (8 - n)])
        colour += line + b"\0" * (-len(line) % 4)
    mask = b""
    for row in reversed(pattern):
        acc = 0
        for i, (r, g, b, a) in enumerate(row):
            if a < 128:
                acc |= 0x80 >> i
        line = bytes([acc])
        mask += line + b"\0" * (-len(line) % 4)
    return header + pal + colour + mask


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    data = rgba_bytes(PATTERN)
    p = lambda name: os.path.join(out, name)

    magick(p("pattern.png"), ["-define", "png:color-type=6"], data, (4, 2))
    magick(p("pattern.webp"), ["-define", "webp:lossless=true", "-define", "webp:exact=true"], data, (4, 2))
    magick(p("pattern.bmp"), ["-background", "white", "-flatten", "-type", "TrueColor"], data, (4, 2))
    magick("BMP3:" + p("pattern-bmp3.bmp"), ["-background", "white", "-flatten", "-type", "TrueColor"], data, (4, 2))
    magick(p("pattern.gif"), ["-channel", "A", "-threshold", "50%"], data, (4, 2))
    # Lossy formats: big flat areas, compared with a tolerance.
    for name, args in [("halves.jpg", ["-quality", "95", "-sampling-factor", "1x1"]),
                       ("halves-progressive.jpg", ["-quality", "95", "-interlace", "Plane"]),
                       ("halves.webp", ["-quality", "95", "-define", "webp:alpha-quality=100"])]:
        subprocess.run(["convert", "-size", "16x32", "xc:#ff0000", "-size", "16x32", "xc:#0000ff",
                        "+append", "-alpha", "set", "-region", "8x8+0+0", "-alpha", "transparent",
                        "+region"] + args + [p(name)], check=True)
    # An animation: red, green, blue, 10, 20 and 30 hundredths of a second,
    # looping forever. The second frame covers only the top left.
    subprocess.run(["convert", "-dispose", "none", "-delay", "10", "-size", "4x4", "xc:red",
                    "-delay", "20", "-size", "2x2", "xc:lime",
                    "-delay", "30", "-size", "4x4", "xc:blue", "-loop", "0", p("frames.gif")], check=True)
    with open(p("pattern-32.ico"), "wb") as f:
        f.write(ico([(4, 2, 32, dib(PATTERN, 32))]))
    with open(p("pattern-4.ico"), "wb") as f:
        f.write(ico([(4, 2, 4, dib(PATTERN, 4))]))
    with open(p("pattern-png.ico"), "wb") as f:
        # Two entries: the bigger one (the PNG) is chosen.
        f.write(ico([(4, 2, 1, dib([[(0, 0, 0, 255)] * 4] * 2, 1)), (8, 8, 32, png_bytes(PATTERN))]))


if __name__ == "__main__":
    main()
