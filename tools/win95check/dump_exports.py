#!/usr/bin/env python3
"""dump_exports: check win95check's lists against real Windows 95 DLLs.

The easy way: run EXPORTS.EXE (on the Tachyon floppy) on Windows 95, copy
the EXPORTS.TXT it writes back to this machine, then run:

  dump_exports.py EXPORTS.TXT

Or copy the DLLs themselves out of C:\\WINDOWS\\SYSTEM (for example by
mounting the VM's disk image) and run:

  dump_exports.py KERNEL32.DLL USER32.DLL GDI32.DLL ...

For each DLL this prints every name in data/<dll>.txt that the real DLL does
not export. Those entries are wrong and must be removed. With --print, it
lists the exports instead.

Only function names are read; the DLLs themselves never go into the repo.
"""

import argparse
import os
import sys

from win95check import PE, PEError, load_list


def read_exports_txt(path):
    """Parse EXPORTS.TXT: [DLL] headers, one name per line, # comments."""
    tables, current = [], None
    with open(path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                current = (line[1:-1], set())
                tables.append(current)
            elif line == "# not present" and current:
                tables[-1] = (current[0], None)
            elif line.startswith("#"):
                print(line)
            elif line and current:
                current[1].add(line)
    return tables


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dlls", nargs="+")
    ap.add_argument("--print", action="store_true", help="print the exports instead of comparing")
    args = ap.parse_args()

    tables = []  # (dll file name, set of exports)
    for path in args.dlls:
        try:
            if path.lower().endswith(".txt"):
                tables.extend(read_exports_txt(path))
            else:
                tables.append((os.path.basename(path), set(PE(path).exports())))
        except (OSError, PEError) as e:
            print("%s: %s" % (path, e), file=sys.stderr)
            return 2

    wrong = 0
    for dll, exports in tables:
        if args.print:
            print("\n".join(sorted(exports)))
            continue
        if exports is None:
            print("%s: not present on that system" % dll)
            continue
        listname = dll.lower().replace(".dll", "") + ".txt"
        listed = load_list(listname)
        missing = sorted(name for name in listed if name not in exports)
        print("%s: %d exports, %d listed in data/%s, %d listed but missing"
              % (dll, len(exports), len(listed), listname, len(missing)))
        for name in missing:
            print("  missing: %s" % name)
        wrong += len(missing)
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
