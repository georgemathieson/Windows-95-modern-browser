# Working on Tachyon

Tachyon is a web browser for Windows 95 that aims to handle the modern web
(HTML5, JavaScript, TLS 1.3). George owns the project and tests on real
Windows 95. These notes are for Claude sessions and human contributors alike.

## Every session

1. **Read [docs/SPRINTS.md](docs/SPRINTS.md) first.** It holds the current
   sprint's goal, definition of done, backlog and decision log. Nothing else
   carries over between sessions.
2. Work on the current sprint's goal. Anything else goes in the backlog; don't
   start it.
3. Before finishing, update SPRINTS.md: tick the definition of done, add to
   the sprint history, log any decision with its reason. Commit and push.

Work happens on a branch per sprint (`sprint/<n>-<name>`). Open a pull request
only when George asks.

## Building and testing

```sh
# One-time setup in a fresh Ubuntu 24.04 container
dpkg --add-architecture i386 && apt-get update
apt-get install -y cmake ninja-build python3 mingw-w64 wine wine32:i386 xvfb xauth \
    zip unzip mtools genisoimage nsis x11-apps imagemagick xdotool clang libclang-rt-18-dev \
    fonts-liberation

cmake --preset host  && cmake --build --preset host  && ctest --preset host
cmake --preset win95 && cmake --build --preset win95 && xvfb-run -a ctest --preset win95
tools/package.sh build/win95 dist
tools/test_installer.sh dist/tachyon-*-win95-setup.exe
```

Use `-DTACHYON_WERROR=ON` before pushing; CI builds with warnings as errors.

### Releases

A tag `v<version>` makes CI publish that build as a GitHub pre-release
with the installer, CD image, floppy image and zip. The tag must match
`VERSION` in `CMakeLists.txt`, and `docs/releases/<version>.md` holds the
notes (its first line is the title). This container can't push tags, so
George makes them: with `git push`, or by publishing a release with a new
tag on GitHub's website, which CI then fills in. Prepare a release only
when George asks for one. The installer is built with NSIS from
`tools/installer/tachyon.nsi`; keep it ANSI (`Unicode false`) and free of
plug-ins, so that its only code is NSIS's installer stub, which win95check
checks.

### Seeing the UI

You can look at screenshots, so look at the UI after changing it. Set Wine to
use a virtual desktop once, then screenshot an Xvfb display:

```sh
wine reg add 'HKCU\Software\Wine\Explorer' /v Desktop /d Default /f
wine reg add 'HKCU\Software\Wine\Explorer\Desktops' /v Default /d 800x600 /f
Xvfb :77 -screen 0 800x600x24 & sleep 1
DISPLAY=:77 wine build/win95/src/platform/win32/TACHYON.exe \
    --fonts 'Z:\usr\share\fonts\truetype\liberation' & sleep 8
DISPLAY=:77 xwd -root -silent | convert xwd:- shot.png    # then Read shot.png
wineserver -k
```

With the virtual desktop set, every Wine program needs a display, even
console ones like FETCH.EXE: run them with `DISPLAY` set or under `xvfb-run`.

Wine lacks Windows 95's fonts (Arial, Times New Roman), so `--fonts` points
Tachyon at the Liberation fonts, which have the same metrics; text still
looks a little different from the real thing. Maximise the window with
`xdotool key alt+space x` rather than resizing it with `xdotool
windowsize`, which Wine's virtual desktop doesn't pass on to the program.

To drive the UI, `xdotool` can type and press keys in the Xvfb display
(`xdotool key ctrl+l; xdotool type example.com; xdotool key Return`). For
local pages, `python3 tests/server/fetch_tests.py --certs tests/server/certs
--serve` runs the HTTP, HTTPS and proxy test servers and prints their
addresses; start TACHYON.EXE with `--cacert tests/server/certs/ca.pem`.
`faketime` doesn't work with Wine (its clock comes from wineserver): to test
the wrong-clock warning, configure a build with `-DTACHYON_BUILD_UNIX=` set
in the future.

### Networking from Claude's container

Outbound connections go through an egress gateway that re-signs TLS with its
own CA, and only some hosts are allowed. So Tachyon (correctly) refuses real
sites here unless told `--cacert /root/.ccr/ca-bundle.crt`, and most hosts
are blocked anyway. CI's real-sites job tests against real certificates.

### The HTML parser's tests

`tests/data` holds html5lib-tests' tokenizer and encoding tests and
web-platform-tests' tree-construction `.dat` files, compiled into the unit
tests by `tools/gen_html_tests.py`. Every case must pass unless
`tests/data/html_known_failures.txt` lists it with a reason; a listed case
that starts passing fails the run, so remove it from the list. To see a
tree, `tachyon-fetch --dom URL` prints any page's in the tests' format. The
parser follows the HTML Standard's text: its source is the `whatwg/html`
repository's `source` file (clone it with `add_repo`; the parsing chapter
starts at `id="parsing"`).

`tools/fuzz.sh [SECONDS]` fuzzes the parser with libFuzzer, AddressSanitizer
and UBSan (`apt-get install clang libclang-rt-18-dev`). Run it after
changing the tokenizer or tree builder; crashes land in `build/fuzz/crashes`.

### Rendering and the reference tests

`build/host/tools/render/tachyon-render [--width N] [--boxes] page.html
out.png` renders a page file to PNG on the host (`--boxes` prints the box
tree), the quickest way to see a layout change. `tachyon-fetch --render
[--png FILE] URL` does the same for a URL, style sheets and all, and prints
timings; FETCH.EXE does it under Wine and on Windows 95.

`tests/reftests` holds reference tests: each line of `reftest.list` pairs a
test page with a reference page, built differently, that must render to
the same pixels at 400x300 (`==`, or differently for `!=`). They run under ctest, natively and under
Wine, with the Liberation fonts. Add one for each layout or painting
feature, and give pages `<meta charset="utf-8">`. Layout's unit tests
(`tests/test_layout.c`) use the box font, so they don't depend on fonts.

After changing CSS parsing, styles, layout or painting, run
`tools/fuzz.sh SECONDS JOBS render`, which feeds pages and style sheets
through the whole pipeline.

### Crash reports

`TACHYON.LOG` sits next to `TACHYON.EXE` (Help > View Log). When Tachyon
crashes it appends a report: the exception, registers, the code addresses
found on the stack and the last 64 log and trace lines. To name the
functions, run `tools/symbolize.py SYMBOLS TACHYON.LOG` with the
`tachyon-VERSION-win95-symbols.txt` from the same build (CI's artifact, or
the release); the log's `build` line gives the commit. Use `tc_log()` for
events worth keeping and `tc_trace()` for frequent ones, which only reach a
crash report. `TACHYON.EXE --crash-test` crashes on purpose.

### Windows-only code paths

The Linux build exercises the core but not `src/platform/win32`. Always run
`xvfb-run -a ctest --preset win95` after touching Windows code: the
integration tests run FETCH.EXE under Wine and have already caught a Winsock
bug the Linux build couldn't.

## Windows 95 rules

The build enforces these with `tools/win95check`; don't work around it.

- **Only functions Windows 95 has.** If the check rejects an import, find out
  whether Windows 95 really has it. If it doesn't, use the alternative in
  `data/forbidden.txt` or write one. Only add a function to `data/<dll>.txt`
  once you're sure. Never add one just to make the build pass.
- **ANSI APIs only** (`CreateWindowExA`, not `W`). Windows 95 stubs nearly
  every W function; `data/real_w.txt` lists the few that work.
- **Fresh-install DLLs only:** kernel32, user32, gdi32, advapi32, comctl32
  4.00, comdlg32, shell32, wsock32 (Winsock 1.1) and crtdll. Anything newer,
  like CryptoAPI, must be loaded with `GetProcAddress` and must have a
  fallback.
- **No SSE, ever,** even behind a CPU check: Windows 95 doesn't enable it.
  Our code is compiled for the original Pentium (`-march=i586`).
- **Imports from dead code stay in the import table** (GNU ld on Windows), so
  configure unwanted APIs out of third-party code rather than leaving them
  unused.
- **Wine is not Windows 95.** Passing under Wine is necessary, not enough.
  Ask George to try each sprint's demo on Windows 95.

## Code conventions

- C99. The core (`src/core`, prefix `tc_`) never includes OS headers. The
  Win32 layer (`src/platform/win32`) uses the prefix `tw_`.
- 4-space indent; braces on their own line for functions, same line
  otherwise; match the surrounding code.
- Functions that can fail return `TC_OK` (0) or a negative `tc_err`.
- Each header starts with a short comment saying what the module is for.
  Comments explain why, not what.
- Every core module gets unit tests in `tests/`: add a `tc_suite_*` and list
  it in `tests/test_main.c` and `tests/CMakeLists.txt`. Tests run natively and
  under Wine.

## Third-party code

Vendored code lives in `third_party/<name>/` with a `README.tachyon.md`
giving the upstream version, commit, what was left out and every local
change (so far: none; configuration lives in separate files). Never edit
upstream files without recording it there. The QuickJS-ng Windows 9x patch
worked out in Sprint 0 is in `docs/spikes/sprint-0/`, ready for Sprint 5.

This container blocks direct downloads from GitHub, but public repositories
can be cloned: call the `add_repo` tool, then `git clone`.
