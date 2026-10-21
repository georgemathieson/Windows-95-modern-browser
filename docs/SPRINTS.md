# Tachyon sprint board

This file is the source of truth for what we are building and in what order.
Every working session starts by reading it and ends by updating it. Claude
does not remember anything between sessions; this board does.

## How we work

- **One goal per sprint.** Each sprint has a goal, a demo and a definition of
  done. Work that doesn't serve the goal goes in the [backlog](#backlog).
- **A branch per sprint.** Work happens on `sprint/<n>-<name>`, then a pull
  request to `main` closes the sprint.
- **Windows 95 is the judge.** CI builds and tests under Wine, but a sprint is
  only done when George has run the demo on real Windows 95 (a VM or a real
  machine). See [TESTING-ON-WIN95.md](TESTING-ON-WIN95.md).
- **Measure, don't guess.** "Full HTML5" has no end, so progress is measured
  with conformance suites (html5lib-tests, web-platform-tests) and a list of
  real sites that must work.
- **End-of-sprint ritual:** tick the definition of done, write a short retro
  in the [sprint history](#sprint-history), move unfinished items, and set
  the next sprint's goal.

## Platform targets

| | Target | Floor (must at least start) |
|---|---|---|
| OS | Windows 95, any release (4.00.950 onwards) | same, with no extra DLLs |
| CPU | Pentium II class (emulated or real) | Pentium Pro today; the original Pentium once our own toolchain lands |
| RAM | 128–256 MB | 32 MB (to be measured) |
| Display | 800×600, 16-bit colour | 640×480, 256 colours |

## Current sprint: Sprint 0, Foundations

**Goal:** a repository that builds a Windows 95 browser shell, proves that it
really is Windows 95 compatible, and shows that the riskiest dependencies
(JavaScript and TLS) can work there.

**Demo:** Tachyon's window opens on Windows 95.

**Definition of done**

- [x] Sprint board, README and CLAUDE.md
- [x] CMake build for the Linux host (fast tests) and Windows 95 (MinGW-w64 cross build), with presets
- [x] `win95check`: every `.exe` is checked against Windows 95 export lists and for instructions newer than the CPU floor, at build time and in CI
- [x] Browser shell: menu, toolbar, address bar, content view (a DIB-backed `tc_surface`), status bar, history, icon, version information
- [x] Unit tests on the host and under Wine, plus a GUI smoke test under Wine
- [x] Spike: QuickJS-ng and mbedTLS (TLS 1.3) build and run for the Windows 95 target ([findings](spikes/sprint-0-dependencies.md))
- [x] GitHub Actions CI with a zip, a floppy image and an ISO as build artifacts
- [ ] **George runs the build on Windows 95 and the window opens** (the demo)
- [ ] Optional: George dumps the real Windows 95 DLL exports so we can verify the `win95check` lists

## Upcoming sprints

The goals are firm; the scope lists are drafts and get refined at the start
of each sprint.

### Sprint 1: Hello, HTTPS

**Goal:** Tachyon fetches `https://` pages from the real internet on Windows 95.
**Demo:** type `example.com`, see the page's HTML source.

- Vendor mbedTLS with the Windows 95 configuration from the spike. Decide 3.6 LTS or 4.x first.
- Entropy pool: CryptGenRandom when present (loaded at runtime), timer jitter, input-event timings, a seed file.
- Our own time functions; warn when the system clock is obviously wrong, because certificate checks depend on it.
- WHATWG URL parser, tested against web-platform-tests' `urltestdata.json`.
- Winsock 1.1 networking on a worker thread: DNS (`gethostbyname`), non-blocking sockets with `select`.
- HTTP/1.1 client: keep-alive, chunked transfer, redirects, gzip/deflate.
- Mozilla's root certificates; SNI; ALPN `http/1.1`; clear certificate error pages.
- UI: loading state, a working Stop button, progress in the status bar, the page source shown in the view.
- **Done when** example.com, en.wikipedia.org and news.ycombinator.com load over HTTPS in a Windows 95 VM, and a bad certificate is refused with a clear message.

### Sprint 2: HTML5 parser and DOM

**Goal:** a spec-compliant HTML5 parser building a DOM.
**Demo:** a DOM tree view of any page.

- Encoding sniffing (BOM, `<meta charset>`, HTTP header) and decoders (UTF-8, windows-1252 and friends).
- Tokenizer with all states and the named character reference table; tree construction with every insertion mode, the adoption agency algorithm and foster parenting.
- DOM nodes in a memory arena.
- An html5lib-tests runner on the host.
- **Done when** html5lib tokenizer tests are ≥ 99% and tree-construction tests ≥ 95% passing (excluding scripting-dependent cases), and the top sites parse without crashing.

### Sprint 3: First Light

**Goal:** real pages rendered with CSS, layout and text.
**Demo:** read a Wikipedia article on Windows 95.

- CSS syntax parser, selectors, cascade, inheritance and the UA stylesheet.
- Box tree; block and inline layout; line breaking.
- Font rasterising with bundled fonts (stb_truetype or FreeType) into `tc_surface`; GDI text retires from the view.
- Painting backgrounds, borders and text; scrolling; clicking links; `about:` pages become HTML.
- Reference tests: render pages to PNG on the host and compare.

### Sprint 4: Images and fonts

**Goal:** pictures, and text in every script.
PNG, JPEG, GIF (animated), WebP, ICO and basic SVG; `<img>` and background images; Unicode font fallback; `@font-face` with WOFF/WOFF2 (needs Brotli); dithering for 256-colour displays.

### Sprint 5: JavaScript

**Goal:** scripts run and change the page.
Vendor QuickJS-ng with the Win9x threading patch; event loop with tasks, microtasks and timers; DOM bindings; script loading (`defer`, `async`, modules); a console window; memory limits so a bad page can't take the browser down.

### Sprint 6: Forms, cookies and navigation

**Goal:** log in to a website.
Form controls and submission (GET/POST, urlencoded and multipart), cookies with the public suffix list, session history, searching from the address bar.

### Sprint 7: Modern layout

**Goal:** modern site layouts look right.
Positioning, floats, stacking contexts, overflow, flexbox, grid, media queries, custom properties, `calc()`, 2D transforms and opacity.

### Sprint 8: Web APIs

**Goal:** pages that load content on the fly.
`fetch`, XMLHttpRequest, URL/URLSearchParams, storage, TextEncoder/TextDecoder, the History API, `requestAnimationFrame` and MutationObserver.

### Sprint 9: Installer and first public alpha

**Goal:** install Tachyon from a floppy or CD on a fresh Windows 95 and browse.
An installer and uninstaller in the Windows 95 wizard style, Start menu shortcuts, settings, bookmarks, crash logs and a GitHub release. Needs the period-correct toolchain from the backlog first.

## Backlog

Unscheduled. Pull items into a sprint when they serve its goal.

- **Period-correct toolchain (before the alpha).** Ubuntu's MinGW-w64 runtime (libgcc, libmingwex, startup code) is compiled for the Pentium Pro and uses `cmov`, so today's builds crash at startup on an original Pentium, a Pentium MMX or an AMD K6. Build GCC's runtime and the MinGW-w64 CRT with `-march=i586`, cache the toolchain in CI, then set `TACHYON_ISA_FLOOR=i586`. Could be a short sprint of its own.
- Verify the hand-curated export lists against real Windows 95 DLLs (`tools/win95check/dump_exports.py`).
- HTTP/2 (most sites still accept HTTP/1.1).
- Brotli content encoding (needed for WOFF2 in Sprint 4 anyway).
- 256-colour and 16-colour displays: our own ordered dithering.
- Shell: toolbar icons in the Windows 95 style, Large Fonts (120 DPI) layout, File > New Window.
- HTTP proxy support (CONNECT).
- Unicode in the window title and address bar (Windows 95's APIs are ANSI), IDN display.
- Performance and memory measurements on Pentium-class hardware in 86Box.
- Out-of-memory handling and per-page memory limits.
- A written threat model (Windows 95 has no process isolation).
- Updating the root certificates without a new release.
- Choosing the address bar search engine (George's call).
- Printing.
- Far future: audio and video, WebAssembly.

## Decision log

| # | Decision | Why | When |
|---|---|---|---|
| 1 | Name: **Tachyon** | George's pick: a particle that travels backwards in time, carrying today's web back to 1995 | Sprint 0 |
| 2 | C99 throughout: a portable core (`src/core`) plus a thin Win32 layer (`src/platform/win32`) | Every Windows 95-capable toolchain handles C; the best embeddable libraries are C; the core is tested natively on Linux | Sprint 0 |
| 3 | MinGW-w64 GCC cross-compiling from Linux; `-march=i586`; `_WIN32_WINNT=0x0400` | Modern compiler, runs in CI, hides post-1996 APIs at compile time | Sprint 0 |
| 4 | C runtime: **crtdll.dll** (msvcrt.dll is a build option) | crtdll ships with every copy of Windows 95; msvcrt only came with OSR2.5/IE4 | Sprint 0 |
| 5 | Winsock 1.1 (`wsock32.dll`) only | Winsock 2 was a separate download for Windows 95 | Sprint 0 |
| 6 | We write the browser engine (HTML, DOM, CSS, layout, painting); we borrow JavaScript (QuickJS-ng), TLS (mbedTLS), compression and image codecs | The engine is the project; a JS engine or crypto library is a project of its own, and nobody should write their own crypto | Sprint 0 |
| 7 | Every executable is checked by `win95check` (imports and CPU instructions) at build time and in CI | Windows 95 refuses to start a program with a single missing import; Wine would hide that | Sprint 0 |
| 8 | Drawing goes into 32-bit `tc_surface`s blitted through DIB sections; GDI text is a stopgap until Sprint 3 | Same pixels on every Windows version and on the host; full Unicode needs our own text rendering anyway | Sprint 0 |
| 9 | No SSE anywhere, even behind CPU checks | Windows 95 never enables SSE in the OS, so SSE code faults even on CPUs that have it | Sprint 0 |

## Risks

- **Hardware speed.** QuickJS is an interpreter and period CPUs are slow; heavy sites may be unusable. Measure from Sprint 5 and be honest about it.
- **Wine is not Windows 95.** It hides missing APIs and behaviour differences. The export lists, the import checker and George's VM tests each sprint are the safety net.
- **Hand-curated export lists.** kernel32 and crtdll come from Wine's 1998 reconstruction of Windows 95; the rest are from knowledge. Verify with real DLLs.
- **Clocks.** Certificate checks need the right date, and VM clocks are often wrong.
- **TLS speed on a Pentium.** ECDSA/RSA verification and key exchange cost; measure in Sprint 1 on emulated period hardware.
- **Scope.** "Full HTML5" is unbounded; the conformance suites and the site list keep us honest.

## Sprint history

### Sprint 0: Foundations (started 2026-10-20)

- Built the repository, the build, the checker, the shell, CI and the dependency spikes.
- The checker immediately earned its keep: QuickJS-ng's Vista-only threading calls, mbedTLS's secure-CRT `vsnprintf_s`, its `BCryptGenRandom` entropy, `inet_pton`, and its AES-NI code (SSE, which Windows 95 can't run) would all have stopped Tachyon on Windows 95. All are fixed in the spike configuration.
- Found that the prebuilt MinGW-w64 runtime needs a Pentium Pro (see backlog).
- Switched the C runtime from msvcrt.dll to crtdll.dll, so Tachyon needs nothing beyond a fresh Windows 95 install.
- Waiting on: George's first run on real Windows 95.
