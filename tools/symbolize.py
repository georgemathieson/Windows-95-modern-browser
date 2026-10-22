#!/usr/bin/env python3
"""symbolize: name the Tachyon functions in a crash report.

TACHYON.LOG's crash reports list code addresses as "TACHYON.EXE 0040A1B2".
This adds the function each one falls in, using the symbols file that
tools/package.sh writes next to the packages (tachyon-VERSION-win95-symbols.txt,
which is `nm -n` of the unstripped TACHYON.EXE). The symbols must come from
the same build: the log's "build" line names its commit and code hash.

Usage: symbolize.py SYMBOLS.txt TACHYON.LOG
"""

import bisect
import re
import sys

ADDRESS = re.compile(r"\bTACHYON\.EXE ([0-9A-F]{8})\b", re.IGNORECASE)


def load_symbols(path):
    symbols = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) == 3 and parts[1] in ("t", "T"):
                symbols.append((int(parts[0], 16), parts[2].lstrip("_")))
    symbols.sort()
    return symbols


def name(symbols, starts, addr):
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0:
        return "?"
    start, fn = symbols[i]
    return "%s+0x%x" % (fn, addr - start)


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    symbols = load_symbols(sys.argv[1])
    starts = [s for s, _ in symbols]
    with open(sys.argv[2], encoding="latin-1") as f:
        for line in f:
            line = line.rstrip("\r\n")
            found = ADDRESS.search(line)
            if found:
                line += "  " + name(symbols, starts, int(found.group(1), 16))
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
