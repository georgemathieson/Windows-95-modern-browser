# Architecture

This page describes how Tachyon is put together today and where each future
piece will live. It changes as the sprints land.

## Layers

```
 +-----------------------------------------------------------------+
 |  src/platform/win32   TACHYON.EXE: windows, menus, input, page  |
 |                       loads on worker threads (loader.c),       |
 |                       blitting; Winsock, threads, entropy        |
 |  src/platform/posix   the same platform functions for Linux     |
 +-----------------------------------------------------------------+
 |  src/core             the engine: portable C99, no OS headers   |
 |    tc_url             WHATWG URL parsing                        |
 |    tc_fetch           a load: DNS, connect, proxy, TLS,         |
 |                       request, response, redirects, decoding    |
 |    tc_http            HTTP/1.1 messages, no I/O                 |
 |    tc_tls             Mbed TLS glue, root certificates          |
 |    tc_inflate         gzip and deflate                          |
 |    tc_entropy         the entropy pool (own SHA-256)            |
 |    tc_surface         32-bit pixels                             |
 |    next: html, dom (Sprint 2); css, layout, paint (Sprint 3)    |
 +-----------------------------------------------------------------+
 |  third_party          Mbed TLS 4.1, zlib; QuickJS-ng (Sprint 5) |
 +-----------------------------------------------------------------+
```

- **The core never includes OS headers.** It builds for Windows 95 and for
  the Linux host, so most testing happens natively and fast. What it needs
  from the OS is in `src/core/tc_platform.h`: time, threads, entropy sources
  and IPv4 sockets. Each platform implements that one file.
- **Pixels are ours.** Pages render into a `tc_surface` (32-bit
  `0x00RRGGBB`), which on Windows wraps a DIB section, so a frame reaches the
  screen with one `BitBlt`. This gives the same output on every Windows
  version and on the host, where reference tests will render pages to PNG.
- **Text.** Until Sprint 3 the view uses GDI: `TextOutW` for page source (one
  of the few Unicode functions that works on Windows 95) and ANSI text
  elsewhere. From Sprint 3, Tachyon rasterises bundled fonts itself.

## Loading a page

```
 address bar --(ANSI to UTF-8, fixup, tc_url_parse)--> history entry
   |
   +-- tw_load_start: a worker thread runs tc_fetch()
         tc_net_resolve -> tc_net_connect -> [proxy CONNECT] -> tc_tls_connect
         -> tc_http request -> tc_http_response_feed ... -> redirects?
         -> tc_inflate
       posts WM_TW_PROGRESS (throttled) and one WM_TW_DONE to the window
   |
   +-- WM_TW_DONE: show the source, or an error page; free the load
```

Every blocking network call waits in 200 ms slices and checks the load's
cancel flag, so Stop works quickly. Only the DNS lookup (`gethostbyname`)
can't be interrupted; an abandoned load finishes in the background and its
result is thrown away. A load is only ever freed by its `WM_TW_DONE`
handler, so the worker never touches freed memory.

## Threads

Windows 95 has preemptive threads but none of the modern synchronisation
primitives (no condition variables, no slim locks, no
`InterlockedCompareExchange`). Tachyon uses critical sections, semaphores,
events and GCC's inline atomics, behind `tc_mutex` and `tc_sem`. Mbed TLS
gets them through `MBEDTLS_THREADING_ALT` (see `third_party/mbedtls/tachyon`).
Note that Windows 95's `CreateThread` fails if its thread ID pointer is NULL.

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
src/platform/win32/                 Windows 95: TACHYON.EXE and tc_platform.h
src/platform/posix/                 Linux: tc_platform.h for tests and tools
third_party/                        Mbed TLS, zlib, root certificates (each with README.tachyon.md)
tests/                              unit tests (host and Wine), WPT data, test server
tools/fetch/                        FETCH.EXE / tachyon-fetch
tools/win95check/                   import and instruction checker, export lists
tools/win95exports/                 EXPORTS.EXE: dumps real Windows 95 exports
tools/gen_*.py                      build-time generators (icon, root certificates, URL tests)
tools/package.sh                    zip, floppy image and ISO for testers
docs/                               sprint board, testing guide, spikes
```
