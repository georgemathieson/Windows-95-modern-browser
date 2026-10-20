# Architecture

This page describes how Tachyon is put together today and where each future
piece will live. It changes as the sprints land.

## Layers

```
 +--------------------------------------------------------------+
 |  src/platform/win32   Windows 95 shell: windows, menus,      |
 |                       input, blitting, sockets and threads   |
 +--------------------------------------------------------------+
 |  src/core             The engine, portable C99, no OS calls  |
 |    today:  tc_buf (strings and bytes), tc_surface (pixels),  |
 |            tc_fixup (address bar input to URL)               |
 |    next:   url, net/http, tls glue, html, dom, css, layout,  |
 |            paint, js bindings                                |
 +--------------------------------------------------------------+
 |  third_party (from Sprint 1)  mbedTLS, QuickJS-ng, zlib, ... |
 +--------------------------------------------------------------+
```

- **The core never includes OS headers.** It builds for Windows 95 and for
  the Linux host, so most testing happens natively and fast. Anything the core
  needs from the OS (time, sockets, threads, fonts) comes through a small
  platform interface, added when the first user needs it.
- **Pixels are ours.** Pages render into a `tc_surface` (32-bit
  `0x00RRGGBB`), which on Windows wraps a DIB section, so a frame reaches the
  screen with one `BitBlt`. This gives the same output on every Windows
  version and on the host, where reference tests will render pages to PNG.
- **Text.** Sprint 0 uses GDI text as a stopgap. From Sprint 3, Tachyon
  rasterises bundled fonts itself, because Windows 95's text APIs are ANSI and
  can't show most of the world's scripts.

## Windows 95 rules

These are enforced by the build, not by memory:

| Rule | Enforced by |
|---|---|
| Only functions Windows 95 exports, and no W (Unicode) stubs | `win95check` import lists (`tools/win95check/data`) |
| Only DLLs a fresh Windows 95 has (crtdll, wsock32, ...) | `win95check` `dlls.txt` |
| Subsystem and OS version 4.0 | `win95check`, linker flags |
| No instructions newer than the CPU floor; never SSE (Windows 95 doesn't enable it) | `win95check --objdump --isa` |
| No post-1996 APIs even in headers | `_WIN32_WINNT=0x0400`, `WINVER=0x0400`, `_WIN32_IE=0x0200` |

## Repository layout

```
CMakeLists.txt, CMakePresets.json   build (presets: host, win95)
cmake/toolchain-win95.cmake         MinGW-w64 cross toolchain, Wine as test runner
src/core/                           portable engine
src/platform/win32/                 Windows 95 shell (TACHYON.EXE) and resources
tests/                              unit tests (host and Wine)
tools/win95check/                   import and instruction checker, export lists
tools/win95exports/                 EXPORTS.EXE: dumps real Windows 95 exports
tools/gen_icon.py                   draws the 16-colour icon
tools/package.sh                    zip, floppy image and ISO for testers
docs/                               sprint board, testing guide, spikes
```

## Threads (planned)

Windows 95 has preemptive threads but no modern synchronisation primitives
(no condition variables, no slim locks, no `InterlockedCompareExchange`). The
plan is a UI thread (messages and painting), a network thread (DNS, sockets,
TLS) and the engine on the UI thread at first. Primitives come from critical
sections, events, semaphores and GCC's inline atomics.
