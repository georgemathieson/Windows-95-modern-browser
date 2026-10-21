#!/usr/bin/env python3
"""win95check: refuse any executable that would not load on Windows 95.

Windows 95 refuses to start a program if a single imported function is
missing, and many W (Unicode) functions are exported but always fail. This
tool reads a PE file's headers and import table and checks them against
data/*.txt:

  dlls.txt        DLLs we may import from, with notes
  <dll>.txt       functions known to be exported by that DLL on Windows 95
  real_w.txt      W functions that actually work on Windows 95
  stub_ok.txt     stubs we import knowingly, each with a reason
  forbidden.txt   functions that do not exist on Windows 95, with advice

Every import must be listed, so a new API is always a conscious decision.
With --objdump, the code is also disassembled and checked for instructions
the Pentium (i586) does not have.

Usage: win95check.py [--objdump PATH [--isa i586|i686]] [--list] FILE.exe ...
"""

import argparse
import os
import re
import struct
import subprocess
import sys

DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")

IMAGE_FILE_MACHINE_I386 = 0x14C
SUBSYSTEMS = {2: "windows", 3: "console"}

# Instructions each CPU floor must not contain. i586 is the original Pentium
# (also AMD K5/K6): no P6 conditional moves or compares. i686 is the Pentium
# Pro. Neither has SSE, and MMX is not on every Pentium or Pentium Pro.
P6_ONLY = (re.compile(r"\b(cmov[a-z]+|fcmov[a-z]+|f(u)?comip?)\b"), "P6 (Pentium Pro) instruction")
NEWER = [
    (re.compile(r"%xmm\d|%ymm\d|%zmm\d"), "SSE/AVX register"),
    (re.compile(r"%mm\d"), "MMX register"),
]
ISA_PATTERNS = {"i586": [P6_ONLY] + NEWER, "i686": NEWER}


class PEError(Exception):
    pass


def load_list(name):
    """Read a data file into {name: comment}. Lines: NAME [# comment]."""
    path = os.path.join(DATA_DIR, name)
    entries = {}
    if not os.path.exists(path):
        return entries
    with open(path, encoding="utf-8") as f:
        for line in f:
            body, _, comment = line.partition("#")
            body = body.strip()
            if body:
                entries[body.split()[0]] = comment.strip()
    return entries


class PE:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:2] != b"MZ":
            raise PEError("not an MZ executable")
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise PEError("no PE signature")
        coff = pe + 4
        (self.machine, nsections, _, _, _, opt_size, _) = struct.unpack_from("<HHIIIHH", d, coff)
        opt = coff + 20
        magic = struct.unpack_from("<H", d, opt)[0]
        if magic != 0x10B:
            raise PEError("not a 32-bit (PE32) image")
        (self.os_major, self.os_minor) = struct.unpack_from("<HH", d, opt + 40)
        (self.subsys_major, self.subsys_minor) = struct.unpack_from("<HH", d, opt + 48)
        self.subsystem = struct.unpack_from("<H", d, opt + 68)[0]
        ndirs = struct.unpack_from("<I", d, opt + 92)[0]
        self.dirs = [struct.unpack_from("<II", d, opt + 96 + 8 * i) for i in range(ndirs)]
        self.sections = []
        sec = opt + opt_size
        for i in range(nsections):
            name, vsize, va, rawsize, rawptr = struct.unpack_from("<8sIIII", d, sec + 40 * i)
            self.sections.append((name.rstrip(b"\0").decode("latin-1"), va, max(vsize, rawsize), rawptr, rawsize))

    def rva_to_offset(self, rva):
        for _, va, size, rawptr, rawsize in self.sections:
            if va <= rva < va + size:
                if rva - va >= rawsize:
                    raise PEError("RVA 0x%x points into uninitialised data" % rva)
                return rawptr + (rva - va)
        raise PEError("RVA 0x%x is outside every section" % rva)

    def cstring(self, rva):
        off = self.rva_to_offset(rva)
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("latin-1")

    def directory(self, index):
        return self.dirs[index] if index < len(self.dirs) else (0, 0)

    def imports(self):
        """Return [(dll, [function name or '#ordinal', ...]), ...]."""
        rva, size = self.directory(1)
        result = []
        if not rva:
            return result
        off = self.rva_to_offset(rva)
        while True:
            oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", self.data, off)
            if not (oft or name_rva or ft):
                break
            dll = self.cstring(name_rva)
            funcs = []
            thunk = self.rva_to_offset(oft or ft)
            while True:
                value = struct.unpack_from("<I", self.data, thunk)[0]
                if not value:
                    break
                if value & 0x80000000:
                    funcs.append("#%d" % (value & 0xFFFF))
                else:
                    funcs.append(self.cstring(value + 2))  # skip the hint
                thunk += 4
            result.append((dll, funcs))
            off += 20
        return result

    def exports(self):
        """Return the names a DLL exports (ordinal-only exports are skipped)."""
        rva, size = self.directory(0)
        if not rva:
            return []
        off = self.rva_to_offset(rva)
        nnames = struct.unpack_from("<I", self.data, off + 24)[0]
        names_rva = struct.unpack_from("<I", self.data, off + 32)[0]
        names = []
        for i in range(nnames):
            name_rva = struct.unpack_from("<I", self.data, self.rva_to_offset(names_rva + 4 * i))[0]
            names.append(self.cstring(name_rva))
        return names


def check_isa(path, objdump, isa):
    try:
        out = subprocess.run([objdump, "-d", "--no-show-raw-insn", path],
                             check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        return ["could not disassemble with %s: %s" % (objdump, e)]
    problems = {}
    function = "?"
    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
        if m:
            function = m.group(1)
            continue
        for pattern, what in ISA_PATTERNS[isa]:
            hit = pattern.search(line)
            if hit:
                problems.setdefault((function, what), hit.group(0))
    return ["%s in %s (%s), not on an %s CPU" % (what, fn, insn, isa)
            for (fn, what), insn in sorted(problems.items())]


def check(path, objdump=None, isa="i586", list_only=False):
    errors, warnings = [], []
    try:
        pe = PE(path)
    except (OSError, PEError, struct.error, ValueError) as e:
        return ["cannot read %s: %s" % (path, e)], []

    imports = pe.imports()
    if list_only:
        for dll, funcs in imports:
            print("%s: %s" % (dll, " ".join(funcs)))
        return [], []

    if pe.machine != IMAGE_FILE_MACHINE_I386:
        errors.append("machine type 0x%x is not i386" % pe.machine)
    if (pe.subsys_major, pe.subsys_minor) > (4, 0):
        errors.append("subsystem version %d.%d: Windows 95 needs 4.0 or lower"
                      % (pe.subsys_major, pe.subsys_minor))
    if (pe.os_major, pe.os_minor) > (4, 0):
        errors.append("OS version %d.%d: Windows 95 needs 4.0 or lower" % (pe.os_major, pe.os_minor))
    if pe.subsystem not in SUBSYSTEMS:
        errors.append("subsystem %d is not a Win32 GUI or console program" % pe.subsystem)
    if pe.directory(13)[0]:
        errors.append("delay-load imports are not supported by this checker")

    dlls = load_list("dlls.txt")
    real_w = load_list("real_w.txt")
    stub_ok = load_list("stub_ok.txt")
    forbidden = load_list("forbidden.txt")

    for dll, funcs in imports:
        key = dll.lower()
        if key not in dlls:
            errors.append("%s: DLL is not on the Windows 95 list (data/dlls.txt)" % dll)
            continue
        exports = load_list(key.replace(".dll", "") + ".txt")
        for fn in funcs:
            where = "%s!%s" % (dll, fn)
            if fn.startswith("#"):
                errors.append("%s: import by ordinal; import by name instead" % where)
            elif fn in forbidden:
                errors.append("%s: not available on Windows 95. %s" % (where, forbidden[fn]))
            elif fn not in exports:
                errors.append("%s: not known to exist on Windows 95. If it does, add it to data/%s.txt"
                              % (where, key.replace(".dll", "")))
            elif exports[fn].startswith("stub") or (fn.endswith("W") and key != "msvcrt.dll"
                                                    and fn not in real_w):
                if fn in stub_ok:
                    warnings.append("%s: stub on Windows 95, accepted: %s" % (where, stub_ok[fn]))
                else:
                    errors.append("%s: exported by Windows 95 but always fails there; %s"
                                  " (or justify it in data/stub_ok.txt)"
                                  % (where, "use the A version" if fn.endswith("W") else "avoid it"))

    if objdump:
        errors.extend(check_isa(path, objdump, isa))
    return errors, warnings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--objdump", help="objdump for the target, enables the instruction check")
    ap.add_argument("--isa", choices=sorted(ISA_PATTERNS), default="i586",
                    help="oldest CPU the code must run on (default: i586, the original Pentium)")
    ap.add_argument("--list", action="store_true", help="only print the imports")
    ap.add_argument("--quiet", action="store_true", help="do not print warnings")
    args = ap.parse_args()

    failed = False
    for path in args.files:
        errors, warnings = check(path, args.objdump, args.isa, args.list)
        name = os.path.basename(path)
        if not args.quiet:
            for w in warnings:
                print("win95check: %s: warning: %s" % (name, w))
        for e in errors:
            print("win95check: %s: error: %s" % (name, e), file=sys.stderr)
        if errors:
            failed = True
        elif not args.list:
            print("win95check: %s: OK for Windows 95" % name)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
