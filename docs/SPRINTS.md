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
| Network | TCP/IP over a network card or Dial-Up Networking, IPv4 | same |

## Current sprint: Sprint 2, HTML5 parser and DOM

**Goal:** a spec-compliant HTML5 parser building a DOM.

**Demo:** load a page and see its DOM tree (View > Document Tree).

**Definition of done**

- [x] Encodings: UTF-8, UTF-16LE/BE, the 28 legacy single-byte encodings, x-user-defined and replacement, with every WHATWG label (Chinese, Japanese and Korean multi-byte encodings are deferred, decision 18)
- [x] Encoding sniffing: BOM, HTTP `charset`, the `<meta>` prescan, XML declarations, windows-1252 default; re-parse when a `<meta charset>` disagrees: **html5lib encoding tests 82 of 82**
- [x] Tokenizer: every state in today's HTML Standard (including processing instructions), all 2,231 named character references, parse error codes: **html5lib tokenizer tests 7,049 of 7,049** (3 of them expect an error code from before processing instructions; listed as known)
- [x] Tree construction: every insertion mode, the adoption agency algorithm, foster parenting, templates, SVG and MathML, the new `<select>` parsing, fragment parsing: **web-platform-tests tree construction 3,890 of 3,898** (the other 8 need `<selectedcontent>` mirroring, moved to Sprint 6; 6 cases that need scripts are skipped)
- [x] DOM in a per-document arena: document, doctype, element, text, comment, processing instruction, template contents
- [x] The browser parses pages on the worker thread and shows a DOM tree view; the window title comes from the DOM; the source view uses the page's encoding
- [x] `FETCH.EXE --dom`, with an integration test; CI parses the real sites (all nine, from example.com to BBC News and Yahoo! Japan)
- [x] The parser survives fuzzing under AddressSanitizer and UBSan (CI fuzzes on every push) and limits nesting depth (decision 22)
- [ ] Pulled forward from Sprint 9 at George's request: a GitHub release, Tachyon 0.2.0, with a Windows 95 installer (decision 25)
- [ ] **George sees a page's DOM tree in Tachyon on Windows 95**, installed from the release (the demo)
- [ ] Carried over from Sprint 1: George loads `https://example.com` on Windows 95 and reports the About box's "Randomness" line and the status bar timings

## Upcoming sprints

The goals are firm; the scope lists are drafts and get refined at the start
of each sprint.

### Sprint 3: First Light

**Goal:** real pages rendered with CSS, layout and text.
**Demo:** read a Wikipedia article on Windows 95.

- CSS syntax parser, selectors, cascade, inheritance and the UA stylesheet.
- Box tree; block and inline layout; line breaking.
- Font rasterising with bundled fonts (stb_truetype or FreeType) into `tc_surface`; GDI text retires from the view.
- Painting backgrounds, borders and text; scrolling; clicking links; `about:` pages become HTML.
- Loading stylesheets: HTTP keep-alive and connection reuse, a small cache.
- Reference tests: render pages to PNG on the host and compare.

### Sprint 4: Images and fonts

**Goal:** pictures, and text in every script.
PNG, JPEG, GIF (animated), WebP, ICO and basic SVG; `<img>` and background images; Unicode font fallback; `@font-face` with WOFF/WOFF2 (needs Brotli); dithering for 256-colour displays.

### Sprint 5: JavaScript

**Goal:** scripts run and change the page.
Vendor QuickJS-ng with the Win9x threading patch; event loop with tasks, microtasks and timers; DOM bindings; script loading (`defer`, `async`, modules); a console window; memory limits so a bad page can't take the browser down.

### Sprint 6: Forms, cookies and navigation

**Goal:** log in to a website.
Form controls and submission (GET/POST, urlencoded and multipart), cookies with the public suffix list, session history, searching from the address bar. `<select>` and `<option>` state, which also brings `<selectedcontent>` mirroring (the last 4 web-platform-tests tree-construction cases).

### Sprint 7: Modern layout

**Goal:** modern site layouts look right.
Positioning, floats, stacking contexts, overflow, flexbox, grid, media queries, custom properties, `calc()`, 2D transforms and opacity.

### Sprint 8: Web APIs

**Goal:** pages that load content on the fly.
`fetch`, XMLHttpRequest, URL/URLSearchParams, storage, TextEncoder/TextDecoder, the History API, `requestAnimationFrame` and MutationObserver.

### Sprint 9: Installer and first public alpha

**Goal:** install Tachyon from a floppy or CD on a fresh Windows 95 and browse.
Grow the test builds' installer (NSIS, since Sprint 2: decision 25) into the alpha's, or replace it: the Windows 95 wizard style, installing from floppies, settings (home page, proxy, extra certificate authorities), bookmarks, crash logs and the first release that isn't a pre-release. Needs the period-correct toolchain from the backlog first.

## Backlog

Unscheduled. Pull items into a sprint when they serve its goal.

- **Period-correct toolchain (before the alpha).** Ubuntu's MinGW-w64 runtime (libgcc, libmingwex, startup code) is compiled for the Pentium Pro and uses `cmov`, so today's builds crash at startup on an original Pentium, a Pentium MMX or an AMD K6. Build GCC's runtime and the MinGW-w64 CRT with `-march=i586`, cache the toolchain in CI, then set `TACHYON_ISA_FLOOR=i586`. Ubuntu's prebuilt NSIS installer stub uses `cmov` too, so it needs rebuilding the same way. Could be a short sprint of its own.
- **Measure TLS on period hardware:** handshake times for RSA and ECDSA sites on an emulated Pentium II (86Box), and the entropy pool's strength without CryptoAPI.
- **Full UTS #46** (internationalised domain names): the mapping table, NFC, and the bidi and joiner rules. 31 web-platform-tests host cases wait on it.
- Verify the hand-curated export lists against real Windows 95 DLLs (`EXPORTS.EXE`).
- TLS session resumption, to make repeat visits to a site faster.
- Certificate revocation (OCSP stapling or a CRLite-style list) and HSTS.
- **Chinese, Japanese and Korean encodings** (GBK, gb18030, Big5, EUC-JP, ISO-2022-JP, Shift_JIS, EUC-KR): about 200 KB of index tables, or less compressed. Best done with Sprint 4's font fallback, when those pages can be drawn.
- Declarative shadow DOM (`<template shadowrootmode>`) and `<template for>` content patching, both new in the HTML Standard.
- An XML parser for XHTML (`application/xhtml+xml` is parsed as HTML for now).
- Parsing while downloading, which `document.write()` and progressive rendering both need.
- DOM memory: page text is kept twice (the decoded page and its text nodes). Make text nodes slices of the decoded page, or drop the decoded page once there's a renderer.
- The document tree view: horizontal scrolling, finding text, copying a node.
- Reading Internet Explorer's proxy settings from the registry.
- HTTP/2 (most sites still accept HTTP/1.1).
- Brotli content encoding (needed for WOFF2 in Sprint 4 anyway).
- 256-colour and 16-colour displays: our own ordered dithering.
- Shell: toolbar icons in the Windows 95 style, Large Fonts (120 DPI) layout, File > New Window.
- Unicode in the window title and address bar (Windows 95's APIs are ANSI).
- Out-of-memory handling and per-page memory limits.
- A written threat model (Windows 95 has no process isolation).
- Updating the root certificates without a new release.
- Choosing the address bar search engine (George's call).
- Printing.
- Far future: audio and video, WebAssembly, IPv6 (Windows 95 has none).

## Decision log

| # | Decision | Why | When |
|---|---|---|---|
| 1 | Name: **Tachyon** | George's pick: a particle that travels backwards in time, carrying today's web back to 1995 | Sprint 0 |
| 2 | C99 throughout: a portable core (`src/core`) plus thin platform layers (`src/platform/win32`, `src/platform/posix`) | Every Windows 95-capable toolchain handles C; the best embeddable libraries are C; the core is tested natively on Linux | Sprint 0 |
| 3 | MinGW-w64 GCC cross-compiling from Linux; `-march=i586`; `_WIN32_WINNT=0x0400` | Modern compiler, runs in CI, hides post-1996 APIs at compile time | Sprint 0 |
| 4 | C runtime: **crtdll.dll** (msvcrt.dll is a build option) | crtdll ships with every copy of Windows 95; msvcrt only came with OSR2.5/IE4 | Sprint 0 |
| 5 | Winsock 1.1 (`wsock32.dll`) only | Winsock 2 was a separate download for Windows 95 | Sprint 0 |
| 6 | We write the browser engine (HTML, DOM, CSS, layout, painting); we borrow JavaScript (QuickJS-ng), TLS (Mbed TLS), compression and image codecs | The engine is the project; a JS engine or crypto library is a project of its own, and nobody should write their own crypto | Sprint 0 |
| 7 | Every executable is checked by `win95check` (imports and CPU instructions) at build time and in CI | Windows 95 refuses to start a program with a single missing import; Wine would hide that | Sprint 0 |
| 8 | Drawing goes into 32-bit `tc_surface`s blitted through DIB sections; GDI text is a stopgap until Sprint 3 | Same pixels on every Windows version and on the host; full Unicode needs our own text rendering anyway | Sprint 0 |
| 9 | No SSE anywhere, even behind CPU checks | Windows 95 never enables SSE in the OS, so SSE code faults even on CPUs that have it | Sprint 0 |
| 10 | **Mbed TLS 4.1 LTS**, not 3.6 LTS | 3.6 is supported only until March 2027, 4.1 until March 2029; a browser's TLS needs security fixes. 4.1 built for Windows 95 with the same kind of configuration as the 3.6 spike | Sprint 1 |
| 11 | One request per connection (`Connection: close`) until pages load subresources | With one document per page there is nothing to reuse; keep-alive arrives with stylesheets in Sprint 3 | Sprint 1 |
| 12 | User-Agent: `Mozilla/5.0 (Windows 95) Tachyon/<version>` | Honest; sites tailor content to it, so revisit when pages render (Sprint 3) | Sprint 1 |
| 13 | Domains: the URL Standard's ASCII fast path plus a simplified UTS #46 mapping; full UTS #46 on the backlog | Covers ASCII and common internationalised domains; the full tables (mapping, NFC, bidi, joiners) are a project of their own | Sprint 1 |
| 14 | HTTP proxy support pulled in from the backlog | The only way to test against real sites from Claude's container, and corporate and retro-computing networks use proxies | Sprint 1 |
| 15 | Entropy: our own SHA-256 pool; CryptGenRandom only when present; jitter credited at 1 bit per 4 changing samples; a seed file next to the program | Windows 95 RTM has no CryptoAPI, and Mbed TLS needs entropy before it can hash anything itself | Sprint 1 |
| 16 | Release builds use `-O2` (not `-O3`); packages are stripped | `-O3` was 40% bigger for little gain on a small-cache Pentium; the floppy must fit the browser | Sprint 1 |
| 17 | DOM text is UTF-8 (lone surrogates, which only scripts can make, as WTF-8) | The rest of the engine is UTF-8; it's half the memory of UTF-16 for mostly-ASCII pages; JavaScript bindings convert | Sprint 2 |
| 18 | Chinese, Japanese and Korean multi-byte encodings deferred; such pages decode as windows-1252 with a warning in the tree view | Their tables are about 200 KB, and Tachyon can't draw those scripts until Sprint 4's font fallback | Sprint 2 |
| 19 | Follow today's HTML Standard (October 2026), including processing instructions and the customizable `<select>` parser changes, tested with html5lib-tests and web-platform-tests from the same month | Browsers ship these, and the test suites already expect them | Sprint 2 |
| 20 | Parse with scripting off, so `<noscript>` content becomes elements | Tachyon doesn't run scripts until Sprint 5, so a page's no-script fallback is what the user should get | Sprint 2 |
| 21 | DOM nodes live in a per-document arena, freed with the page; active formatting elements are recreated from the element's attributes | Fast and simple while only the parser changes the tree; Sprint 5 (scripts) revisits both | Sprint 2 |
| 22 | Nesting depth capped at 400: deeper elements are attached to the 400th | Browsers cap it too (Chromium at 512), and later stages must not overflow Windows 95's 1 MB stack | Sprint 2 |
| 23 | A `<meta charset>` found while parsing restarts the parse in the new encoding | The whole page is in memory, so parsing it again is simple and always correct; switching decoders mid-stream isn't | Sprint 2 |
| 24 | The document tree view hides whitespace-only text nodes | Every DOM inspector does; `FETCH --dom` shows them all | Sprint 2 |
| 25 | Test builds become GitHub pre-releases with an installer, pulled forward from Sprint 9. CI publishes one when a `v<version>` tag is pushed. The installer is built with NSIS 3: an ANSI build with the classic interface and no plug-ins | George tests on Windows 95 from a release. NSIS's ANSI installers still support Windows 95, and its stub passes win95check once OLE32 and eight functions Windows 95 has are listed (three are NT security stubs that NSIS only calls before restarting the PC). Whether the alpha writes its own installer is Sprint 9's call | Sprint 2 |

## Risks

- **Hardware speed.** QuickJS is an interpreter and period CPUs are slow; heavy sites may be unusable. Measure from Sprint 5 and be honest about it.
- **TLS speed on a Pentium.** Untested on period hardware so far: measure handshakes in 86Box (backlog).
- **Entropy without CryptoAPI.** On an original Windows 95 install, CPU jitter is the only counted source. Its strength on real hardware is unmeasured; the About box reports it.
- **Wine is not Windows 95.** It hides missing APIs and behaviour differences. The export lists, the import checker and George's VM tests each sprint are the safety net. (Sprint 1's Winsock bug only showed up because the integration tests also run under Wine.)
- **Hand-curated export lists.** kernel32 and crtdll come from Wine's 1998 reconstruction of Windows 95; the rest are from knowledge. Verify with real DLLs. The installer added OLE32 and eight functions (decision 25); George's `EXPORTS.TXT` will confirm them.
- **Clocks.** Certificate checks need the right date, and VM clocks are often wrong. Tachyon warns, but can't fix it.
- **Scope.** "Full HTML5" is unbounded; the conformance suites and the site list keep us honest.
- **The floppy is nearly full.** TACHYON.EXE is 1.1 MB stripped, leaving 249 KB on the 1.44 MB floppy, and Sprint 3's fonts won't fit. The installer compresses everything into 639 KB, so a floppy with SETUP.EXE on it is the way out for a while; the CD image already carries it.
- **Parsing speed on a Pentium.** GitHub's 572 KB home page parses in 7 ms on the build machine; a 1990s CPU may be a hundred times slower. George's "read in" timings will tell.

## Sprint history

### Sprint 2: HTML5 parser and DOM (started 2026-10-21)

- Read the current HTML Standard before writing the parser, which mattered: since 2025, `<?target data?>` makes processing instruction nodes, `<select>` lost its own insertion modes (the customizable `<select>` work), and `<select>` became a scope boundary. html5lib-tests moved its tree-construction tests to web-platform-tests.
- The tokenizer passed all 7,049 html5lib tokenizer cases on its first run. The tree builder passed 3,882 of 3,898 on its first; the rest came down to `<nobr>` ignoring the adoption agency's "any other end tag" result, forms inside a `<template>` fragment, and a carriage return that the test generator's own newline translation had eaten.
- Integration found a latent Sprint 1 bug: `tc_buf_reserve` didn't NUL-terminate a newly allocated buffer.
- Fuzzing (libFuzzer with AddressSanitizer and UBSan, seeded from the test suites): 19.4 million inputs locally (three workers for 30 minutes, after a short run) with no crash, leak or undefined behaviour; CI now fuzzes for three minutes on every push.
- CI's real-sites job parsed all nine sites under Wine at the first attempt, Wikipedia's Windows 95 article (4,421 elements) and BBC News (4,899) among them; Hacker News and NeverSSL come out in quirks mode, as they should with no doctype.
- Parsing GitHub's 572 KB home page takes 5 to 7 ms and about 2 MB on the build machine. Timing it showed that `GetTickCount` only moves every 55 ms on Windows 95 (and coarsely under Wine), so `tc_time_ms` now uses the performance counter, which Windows 95 runs off the 1.19 MHz timer chip.
- George asked for a release with an installer to test from, so the basics of Sprint 9 came forward (decision 25). `SETUP.EXE` is 639 KB with everything in it. It installs to `C:\Program Files\Tachyon` with Start menu and desktop shortcuts and an Add/Remove Programs entry. CI installs and uninstalls it under Wine on every push, and publishes a pre-release when a version tag is pushed.
- Sprint 1's Windows 95 demo is still to do, together with this sprint's.

### Sprint 1: Hello, HTTPS (2026-10-21, merged as PR #2)

- Vendored Mbed TLS 4.1.1 LTS, zlib 1.3.2 and Mozilla's root store; wrote the platform layer, entropy pool, URL parser, HTTP client, TLS glue, loader, `FETCH.EXE` and the browser integration.
- The URL parser passed all 896 web-platform-tests URL cases after a handful of fixes; the WPT data itself settled two spec details (`^` is encoded in paths; ASCII `xn--` domains are only lowercased).
- The integration tests found a Winsock bug that the Linux build couldn't: `select()`'s exception set was checked after read and write waits, so every load failed with "connection refused" on Windows. Fixed before it reached a tester.
- Real sites from Claude's container go through an egress gateway that re-signs TLS. Tachyon correctly refused it, and loaded pypi.org and npm when told to trust the gateway's CA. CI checks real sites with Mozilla's roots alone: on its first run, the packaged `FETCH.EXE` under Wine loaded all seven (example.com, Wikipedia, Hacker News, Google, GitHub, Cloudflare and plain-HTTP neverssl.com) at the first attempt, all six HTTPS sites over TLS 1.3.
- Sprint 0 was merged without its Windows 95 demo; that test is folded into this sprint's.
- Merged before George had run it on Windows 95, so the demo carries over to Sprint 2.

### Sprint 0: Foundations (2026-10-20, merged as PR #1)

- Built the repository, the build, the checker, the shell, CI and the dependency spikes.
- The checker immediately earned its keep: QuickJS-ng's Vista-only threading calls, Mbed TLS's secure-CRT `vsnprintf_s`, its `BCryptGenRandom` entropy, `inet_pton`, and its AES-NI code (SSE, which Windows 95 can't run) would all have stopped Tachyon on Windows 95.
- Found that the prebuilt MinGW-w64 runtime needs a Pentium Pro (see backlog).
- Switched the C runtime from msvcrt.dll to crtdll.dll, so Tachyon needs nothing beyond a fresh Windows 95 install.
- Not done: George's run on real Windows 95 (carried into Sprint 1's demo).
