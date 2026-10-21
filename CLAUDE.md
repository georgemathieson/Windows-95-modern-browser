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
    zip mtools genisoimage x11-apps imagemagick

cmake --preset host  && cmake --build --preset host  && ctest --preset host
cmake --preset win95 && cmake --build --preset win95 && xvfb-run -a ctest --preset win95
tools/package.sh build/win95 dist
```

Use `-DTACHYON_WERROR=ON` before pushing; CI builds with warnings as errors.

### Seeing the UI

You can look at screenshots, so look at the UI after changing it. Set Wine to
use a virtual desktop once, then screenshot an Xvfb display:

```sh
wine reg add 'HKCU\Software\Wine\Explorer' /v Desktop /d Default /f
wine reg add 'HKCU\Software\Wine\Explorer\Desktops' /v Default /d 800x600 /f
Xvfb :77 -screen 0 800x600x24 & sleep 1
DISPLAY=:77 wine build/win95/src/platform/win32/TACHYON.exe & sleep 8
DISPLAY=:77 xwd -root -silent | convert xwd:- shot.png    # then Read shot.png
wineserver -k
```

Wine lacks Windows 95's fonts (Arial, Times New Roman), so text looks a
little different from the real thing.

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

From Sprint 1, vendored code lives in `third_party/<name>/` with a note of the
upstream version, commit and every local patch. The Windows 95 patches and
configuration worked out in Sprint 0 are in `docs/spikes/sprint-0/`.

This container blocks direct downloads from GitHub, but public repositories
can be cloned: call the `add_repo` tool, then `git clone`.
