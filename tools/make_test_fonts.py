#!/usr/bin/env python3
"""Make the font tests' fonts in tests/data/fonts.

Run once (needs fonttools and brotli: pip install fonttools brotli, and
the fonts-liberation and fonts-dejavu-core packages); the fonts are checked
in. The made-up ones' glyphs are rectangles, so the tests can tell them
apart by their pixels:

    tachyon-test-cjk.ttf  "Tachyon Test CJK": a few Japanese and Chinese
                          characters and punctuation, each a different
                          shape, a full em wide.
    tachyon-test.ttc      a collection of that font and "Tachyon Test
                          Symbols", which has a snowman (U+2603) and a
                          Greek alpha.
    tachyon-test-cjk.woff, .woff2
                          the first font as WOFF (zlib) and WOFF2
                          (Brotli, its glyf and loca tables transformed).
    liberation-subset.ttf, dejavu-subset.ttf
                          a few dozen glyphs of real fonts, with composite
                          glyphs (accented letters) and kerning, for the
                          font checker and the font fuzzer.
    liberation-subset.woff2
                          the first as WOFF2 with hmtx transformed too.

Usage: make_test_fonts.py OUT_DIR
"""

import os
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools import subset
from fontTools.ttLib import TTCollection, TTFont, woff2

UPEM = 1000
ASCENT = 880
DESCENT = 120

# Each character's rectangles (x0, y0, x1, y1), in font units.
CJK = {
    0x3042: [(100, 0, 900, 800)],                          # あ: a filled square
    0x4E2D: [(450, -100, 550, 850)],                       # 中: a tall bar
    0x65E5: [(150, 0, 850, 100), (150, 700, 850, 800),     # 日: a frame
             (150, 0, 250, 800), (750, 0, 850, 800)],
    0x672C: [(100, 350, 900, 450)],                        # 本: a wide bar
    0x8A9E: [(100, 0, 400, 300), (600, 500, 900, 800)],    # 語: two squares
    0x3002: [(100, 0, 300, 200)],                          # 。 a small square, low
    0x300C: [(600, 300, 700, 850), (600, 750, 900, 850)],  # 「
    0x300D: [(300, -50, 400, 500), (100, -50, 400, 50)],   # 」
}
SYMBOLS = {
    0x2603: [(200, 0, 800, 600), (350, 600, 650, 850)],    # ☃: a snowman of boxes
    0x0391: [(100, 0, 900, 700)],                          # Α
}


def glyph(rects):
    pen = TTGlyphPen(None)
    for x0, y0, x1, y1 in rects:
        pen.moveTo((x0, y0))
        pen.lineTo((x0, y1))
        pen.lineTo((x1, y1))
        pen.lineTo((x1, y0))
        pen.closePath()
    return pen.glyph()


def build(family, chars):
    names = [".notdef", "space"] + ["uni%04X" % cp for cp in sorted(chars)]
    fb = FontBuilder(UPEM, isTTF=True)
    fb.setupGlyphOrder(names)
    cmap = {0x20: "space"}
    cmap.update({cp: "uni%04X" % cp for cp in chars})
    fb.setupCharacterMap(cmap)
    glyphs = {".notdef": glyph([(50, 0, 450, 700)]), "space": glyph([])}
    glyphs.update({"uni%04X" % cp: glyph(rects) for cp, rects in chars.items()})
    fb.setupGlyf(glyphs)
    metrics = {".notdef": (500, 50), "space": (500, 0)}
    for cp, rects in chars.items():
        metrics["uni%04X" % cp] = (UPEM, min(r[0] for r in rects))
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ASCENT, descent=-DESCENT)
    fb.setupNameTable({"familyName": family, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=ASCENT, sTypoDescender=-DESCENT, usWinAscent=ASCENT, usWinDescent=DESCENT,
                sxHeight=500, sCapHeight=700, usWeightClass=400)
    fb.setupPost()
    return fb.font


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    cjk = build("Tachyon Test CJK", CJK)
    cjk.save(os.path.join(out, "tachyon-test-cjk.ttf"))
    collection = TTCollection()
    collection.fonts = [build("Tachyon Test CJK", CJK), build("Tachyon Test Symbols", SYMBOLS)]
    collection.save(os.path.join(out, "tachyon-test.ttc"))
    for flavor in ("woff", "woff2"):
        font = build("Tachyon Test CJK", CJK)
        font.flavor = flavor
        font.save(os.path.join(out, "tachyon-test-cjk." + flavor))
    for src, name, text in [("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                             "liberation-subset.ttf", "Tachyon AVTo WAVE kerning \u00e9\u00e0\u00fc\u00c5\u00f1"),
                            ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             "dejavu-subset.ttf", "AV Wa To \u0393\u03b5\u03b9\u03b1 \u2603 \u00e9")]:
        options = subset.Options()
        options.layout_features = ["kern"]
        sub = subset.Subsetter(options)
        font = TTFont(src)
        sub.populate(text=text)
        sub.subset(font)
        font.save(os.path.join(out, name))
    woff2.compress(os.path.join(out, "liberation-subset.ttf"), os.path.join(out, "liberation-subset.woff2"),
                   transform_tables={"glyf", "loca", "hmtx"})


if __name__ == "__main__":
    main()
