# win95check

Windows 95 refuses to start a program if a single imported function is
missing, and Wine (which CI uses) has nearly everything, so it would never
notice. `win95check` reads an executable's PE headers and import table and
fails if anything wouldn't work on Windows 95:

- a DLL not in `data/dlls.txt`
- a function not in `data/<dll>.txt`, or in `data/forbidden.txt` (which says
  what to use instead)
- a W (Unicode) function that Windows 95 only stubs, unless it's in
  `data/stub_ok.txt` with a reason
- a subsystem or OS version above 4.0, or a non-i386 image
- with `--objdump`, any instruction newer than the CPU floor (`--isa i586`
  for the original Pentium, `i686` for the Pentium Pro), and any MMX or SSE
  register

The build runs it after linking every executable and again from `ctest`.

```sh
tools/win95check/win95check.py --objdump i686-w64-mingw32-objdump --isa i686 TACHYON.exe
tools/win95check/win95check.py --list TACHYON.exe     # just print the imports
```

## Adding a function

When the check fails on a new import, first make sure Windows 95 really has
it (and that it isn't a stub), then add it to the right `data/<dll>.txt`.
Don't add something just to make the build pass.

## Where the lists come from

- `kernel32.txt` and `crtdll.txt`: Wine's September 1998 spec files, which
  reproduce the Windows 95 export tables.
- Everything else: hand-curated and conservative.

Verify the lists against a real Windows 95 install with `EXPORTS.EXE` and
`dump_exports.py` (see [docs/TESTING-ON-WIN95.md](../../docs/TESTING-ON-WIN95.md)).
