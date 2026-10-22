#!/usr/bin/env python3
"""Write a seed corpus for tests/fuzz/fuzz_render.c: the reference tests'
pages, and style sheets from css-parsing-tests and the UA style sheet.

Usage: gen_render_corpus.py REFTEST_DIR CSS_TESTS_DIR UA_CSS OUT_DIR
"""

import glob
import hashlib
import json
import os
import sys


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    reftests, css_tests, ua_css, out = sys.argv[1:]
    os.makedirs(out, exist_ok=True)
    pages, sheets = [], []
    for path in sorted(glob.glob(os.path.join(reftests, "*.html"))):
        pages.append(open(path, "rb").read())
    for path in sorted(glob.glob(os.path.join(css_tests, "*.json"))):
        cases = json.load(open(path, encoding="utf-8"))
        # Inputs and expected results alternate.
        sheets += [c.encode("utf-8", "surrogatepass") for c in cases[::2] if isinstance(c, str)]
    sheets.append(open(ua_css, "rb").read())
    # The first byte picks the mode (even: a page, odd: a sheet) and the
    # viewport width: spread the seeds over the widths.
    count = 0
    for i, page in enumerate(pages):
        count += write(out, bytes([(i * 2) % 256]) + page)
    for i, sheet in enumerate(sheets):
        count += write(out, bytes([(i * 2 + 1) % 256]) + sheet)
    print("%d seeds in %s" % (count, out))


def write(out, blob):
    with open(os.path.join(out, hashlib.sha1(blob).hexdigest()), "wb") as f:
        f.write(blob)
    return 1


if __name__ == "__main__":
    main()
