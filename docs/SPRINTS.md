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

## Current sprint: Sprint 4, Images and fonts

**Goal:** pictures, and text in every script.

**Demo:** on Windows 95, read Wikipedia's Windows 95 article with its pictures, and the same article on the Japanese Wikipedia, in Japanese.

**Definition of done**

- [x] Image decoding through Wuffs (decision 35): PNG with transparency, JPEG (baseline and progressive), GIF with animation, BMP, WebP (lossy and lossless, with alpha) and ICO, recognised by their bytes as the MIME Sniffing Standard says; memory bounded; unit tests and fuzzing
- [x] `<img>` with `src`, `srcset` and `sizes`, `width` and `height`, and its alt text while loading or when it fails; `<picture>` choosing a source by type; replaced elements sized from their natural size and aspect ratio; `object-fit` and `object-position`
- [x] CSS images: `background-image` with `url()` and `linear-gradient()`, `background-repeat`, `background-position`, `background-size`, `background-origin` and `background-clip`, several layers; `list-style-image`
- [ ] Loading: the page appears before its images; images load over the kept-alive connections, the ones in view first, and appear as they arrive (the page only moves when an image's size wasn't given); decoded images stay within a memory budget
- [x] Animated GIFs play in the view
- [x] 256-colour displays: pages are dithered to a palette instead of GDI's nearest colours (decision 39; Wine can't act as a 256-colour display, so George checks it)
- [ ] Basic SVG: shapes and paths with fills and strokes, transforms and `viewBox`, in `<img>`, CSS backgrounds and inline `<svg>`
- [ ] Font fallback: each character missing from the chosen font comes from a font that has it, Windows' installed fonts first, then Tachyon's optional font pack (decision 36); TrueType collections (`.ttc`); lines break between Chinese and Japanese characters. Scripts that need shaping or right-to-left layout wait (decision 37)
- [ ] `@font-face`: TrueType and OpenType, WOFF and WOFF2 (Brotli), with `unicode-range`, weights and styles; every downloaded font checked before stb_truetype reads it (decision 38); text laid out again when its fonts arrive. Brotli also for `Content-Encoding: br`
- [ ] Encodings: Shift_JIS, EUC-JP, ISO-2022-JP, GBK, gb18030, Big5 and EUC-KR from the WHATWG indexes, with tests (revisits decision 18)
- [ ] Reference tests for images, backgrounds and fallback; the decoders, SVG and fonts survive fuzzing; CI renders the real sites with their images, the Japanese Wikipedia included
- [ ] **George reads both Wikipedia articles on Windows 95** (the demo)
- [ ] Carried over from Sprint 3: George reads a Wikipedia article in Tachyon on Windows 95 (Sprint 3's demo, with this sprint's build or PR #6's)
- [ ] Carried over from Sprint 2: the Tachyon 0.2.0 release. `main` has moved on, so the tag `v0.2.0` goes on PR #4's merge commit, `02f36df` (on GitHub: draft a release, new tag `v0.2.0`, target that commit)

## Upcoming sprints

The goals are firm; the scope lists are drafts and get refined at the start
of each sprint.

### Sprint 5: JavaScript

**Goal:** scripts run and change the page.
Vendor QuickJS-ng with the Win9x threading patch; event loop with tasks, microtasks and timers; DOM bindings; script loading (`defer`, `async`, modules); a console window; memory limits so a bad page can't take the browser down.

### Sprint 6: Forms, cookies and navigation

**Goal:** log in to a website.
Form controls and submission (GET/POST, urlencoded and multipart), cookies with the public suffix list, session history, searching from the address bar. `<select>` and `<option>` state, which also brings `<selectedcontent>` mirroring (the last 4 web-platform-tests tree-construction cases).

### Sprint 7: Modern layout

**Goal:** modern site layouts look right.
Positioning, floats, tables, stacking contexts, overflow, flexbox, grid, the rest of media queries, 2D transforms and opacity. (Custom properties, `calc()` and width media queries arrive in Sprint 3, because Wikipedia needs them.)

### Sprint 8: Web APIs

**Goal:** pages that load content on the fly.
`fetch`, XMLHttpRequest, URL/URLSearchParams, storage, TextEncoder/TextDecoder, the History API, `requestAnimationFrame` and MutationObserver.

### Sprint 9: Installer and first public alpha

**Goal:** install Tachyon from a floppy or CD on a fresh Windows 95 and browse.
Grow the test builds' installer (NSIS, since Sprint 2: decision 25) into the alpha's, or replace it: the Windows 95 wizard style, installing from floppies, settings (home page, proxy, extra certificate authorities), bookmarks, and the first release that isn't a pre-release. Crash reports already exist (decision 26); the alpha may want a way to send them. Needs the period-correct toolchain from the backlog first.

## Backlog

Unscheduled. Pull items into a sprint when they serve its goal.

- **Crash on a two-finger trackpad scroll (George, Sprint 2; low priority).** On Windows 95 C in a virtual machine on a Mac, scrolling any loaded page with two fingers ends in "an invalid page fault in module KERNEL32.DLL". The stack pointer is outside the thread's own stack, in Windows 95's shared memory where its 16-bit code runs. The scroll bar and its buttons are fine, so suspect the wheel messages (`WM_MOUSEWHEEL` and IntelliPoint's `MSWHEEL_ROLLMSG`, handled in `view.c`) and how the virtual machine's mouse driver delivers them. The crash report in `TACHYON.LOG` (decision 26) should show the rest. Sprint 3 rewrote the view, including its wheel handling (which now keeps the remainder of a trackpad's small steps), so George is to try it again (step 10 of the testing guide).
- **Period-correct toolchain (before the alpha).** Ubuntu's MinGW-w64 runtime (libgcc, libmingwex, startup code) is compiled for the Pentium Pro and uses `cmov`, so today's builds crash at startup on an original Pentium, a Pentium MMX or an AMD K6. Build GCC's runtime and the MinGW-w64 CRT with `-march=i586`, cache the toolchain in CI, then set `TACHYON_ISA_FLOOR=i586`. Ubuntu's prebuilt NSIS installer stub uses `cmov` too, so it needs rebuilding the same way. Could be a short sprint of its own.
- **Measure TLS on period hardware:** handshake times for RSA and ECDSA sites on an emulated Pentium II (86Box), the status bar's "Done" and "read in" timings on George's machine, and the entropy pool's strength without CryptoAPI.
- **Full UTS #46** (internationalised domain names): the mapping table, NFC, and the bidi and joiner rules. 31 web-platform-tests host cases wait on it.
- Verify the hand-curated export lists against real Windows 95 DLLs (`EXPORTS.EXE`).
- TLS session resumption, to make repeat visits to a site faster.
- Certificate revocation (OCSP stapling or a CRLite-style list) and HSTS.
- Declarative shadow DOM (`<template shadowrootmode>`) and `<template for>` content patching, both new in the HTML Standard.
- An XML parser for XHTML (`application/xhtml+xml` is parsed as HTML for now).
- Parsing while downloading, which `document.write()` and progressive rendering both need.
- DOM memory: page text is kept twice (the decoded page and its text nodes). Make text nodes slices of the decoded page, or drop the decoded page once there's a renderer.
- The document tree view: horizontal scrolling, finding text, copying a node.
- `file:` URLs in the browser (tachyon-render already reads files).
- Text fragments: each word and each space is a fragment of its own. Merging neighbours on a line would save memory and paint calls on long pages; measure on Windows 95 first (a 12,000-word page lays out in 30 ms natively).
- `:target` compares ids with the percent-encoded fragment, so it misses non-ASCII ids (scrolling to them already decodes).
- Selecting and copying text on a page.
- Reading Internet Explorer's proxy settings from the registry.
- HTTP/2 (most sites still accept HTTP/1.1).
- 16-colour displays: dithering to the VGA palette (256 colours are Sprint 4's).
- **Right-to-left text and shaping** (decision 37): the Unicode bidirectional algorithm, Arabic and Hebrew layout, and shaping for Arabic and Indic scripts.
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
| 26 | A log, `TACHYON.LOG` next to `TACHYON.EXE`, with a crash report appended when Tachyon crashes. Pulled forward from Sprint 9 at George's suggestion | The first crash on real Windows 95 doesn't happen under Wine, and Windows' own dialog shows only registers. The report adds the code addresses on the stack (named from each build's symbols file) and the last 64 log and trace lines, down to the tree rows being drawn. It formats with static buffers and no printf, because a crash can leave little stack. Kept under 48 KB so that Windows 95's Notepad can open it | Sprint 2 |
| 27 | Text uses the TrueType fonts Windows 95 already has (Arial, Times New Roman, Courier New, and whatever else is installed), rasterised by stb_truetype. Sprint 3 ships no fonts. Host and CI tests use the Liberation fonts, which have the same metrics | They're on every Windows 95 machine, they look right there, and they cost no download, floppy space or licence; stb_truetype is one small public-domain file. Text in other scripts waits for Sprint 4's fallback fonts anyway | Sprint 3 |
| 28 | CSS parsing follows today's CSS Syntax Level 3. css-parsing-tests (written against the 2021 draft) cases that disagree are listed with the reason, like the HTML parser's | Same approach as decision 19: browsers ship today's spec | Sprint 3 |
| 29 | Tables, floats and positioned boxes are laid out as ordinary blocks until Sprint 7 | They need layout algorithms of their own; as blocks, Wikipedia's infobox and navigation tables still read top to bottom | Sprint 3 |
| 30 | HTTP keep-alive and a small in-memory cache, revisiting decision 11 | Pages now load stylesheets, mostly from the same host: a new TLS handshake for each costs seconds on a Pentium | Sprint 3 |
| 31 | The user-agent style sheet is the HTML Standard's rendering section (whatwg/html at commit c6c3055c), compiled in by `tools/gen_ua_css.py`, with every change listed at the top of `src/core/ua.css` | Pages are written against browsers that use these rules; following the text keeps us honest, like the parser (decision 19) | Sprint 3 |
| 32 | One pipeline for a page (`tc_page`): the browser, `FETCH --render` and `tachyon-render` differ only in how they fetch style sheets. The browser's own pages (home, errors, `about:fonts`) are HTML drawn by the same engine | What CI and the reference tests check is then what George sees; the old GDI page drawing goes | Sprint 3 |
| 33 | Windows 95's gaps in the page view are filled by hand: a hand cursor of our own (no `IDC_HAND`), polling every 100 ms while the mouse is over a link (no `TrackMouseEvent`), and scroll positions in units small enough for 16-bit scroll bars | Those APIs arrived with Windows 98 and NT 4; win95check refuses them | Sprint 3 |
| 34 | Limits a hostile or broken page can't get past: boxes nest 200 deep, lengths stay within ±2<sup>25</sup> px (as in Chromium), font sizes within 10,000 px, documents stop at 10<sup>8</sup> px, glyphs bigger than 128×128 px aren't cached but rasterised only where they show, and a page loads at most 64 style sheets with imports 4 deep | Windows 95's 1 MB stack and small memory, C's rules (an infinite float converted to an int is undefined), and time: the render fuzzer found each of these | Sprint 3 |
| 35 | Images are decoded by Wuffs (v0.4, pinned to a commit), not libpng, libjpeg, giflib and libwebp, nor stb_image | Wuffs is memory-safe by construction (its compiler proves the bounds checks), is one C file, and covers PNG, JPEG, GIF, BMP and lossy and lossless WebP; its CPU-specific code compiles out, so no SSE. stb_image says it isn't for untrusted input, and the classic libraries are four projects with SIMD code and long CVE histories. v0.4 is an alpha, so its API may change between updates | Sprint 4 |
| 36 | Fonts for other scripts: Windows' installed fonts first (including Microsoft's language packs), then an optional font pack, "Fonts for other languages", in the installer: DejaVu Sans (Greek, Cyrillic, more Latin, Hebrew, symbols) and WenQuanYi Micro Hei (Chinese, Japanese, Korean). About 2 MB more in the installer and 6 MB installed; the CD and zip carry it, the floppy can't | George's choice. Windows 95's own fonts cover Western European languages only, and a fallback font is what every other browser relies on. These two are free (Bitstream Vera and Apache 2.0 licences) and cover the most-used scripts in the least space; Noto CJK alone is 16 MB per weight | Sprint 4 |
| 37 | Sprint 4 covers scripts written one character after another (Latin, Greek, Cyrillic, Chinese, Japanese, Korean, symbols), and the old Chinese, Japanese and Korean encodings, revisiting decision 18. Right-to-left layout (the bidirectional algorithm) and shaping (Arabic, Indic scripts) wait for a later sprint | George's choice. Shaping and bidi are each a project; the encodings are tables, and pages in them are still around | Sprint 4 |
| 38 | Fonts from the web are checked by Tachyon's own code before stb_truetype reads them: every table stb_truetype uses is bounds-checked, composite glyphs are depth-limited, and a font that fails is dropped | stb_truetype trusts its input and says so; web fonts are attacker-controlled. Browsers use OTS (C++) for the same job | Sprint 4 |
| 39 | On a 256-colour display, the page is dithered to a palette of our own: a 6×6×6 colour cube and 20 greys between its six (236 colours, leaving Windows its 20). An 8×8 ordered (Bayer) dither, anchored to the page so that scrolling doesn't move the pattern; colours whose channels are within 16 of each other are dithered along the greys | Left to itself, Windows rounds each pixel of a true-colour picture to the nearest of the colours it has, which bands photographs and gradients. An ordered dither works on any rectangle by itself, so only what's painted is converted, and moved pixels still match. The greys keep text edges and the pale backgrounds most sites use smooth; the cube's steps (51) are too coarse for a grey's faint tint anyway. `--dither` shows the result on any display | Sprint 4 |

## Risks

- **Hardware speed.** QuickJS is an interpreter and period CPUs are slow; heavy sites may be unusable. Measure from Sprint 5 and be honest about it.
- **TLS speed on a Pentium.** Untested on period hardware so far: measure handshakes in 86Box (backlog).
- **Entropy without CryptoAPI.** On an original Windows 95 install, CPU jitter is the only counted source. Its strength there is unmeasured; the About box reports it. On Windows 95 C, whose Internet Explorer 4 brings CryptoAPI, the pool gets its full 256 bits.
- **Wine is not Windows 95.** It hides missing APIs and behaviour differences. The export lists, the import checker and George's VM tests each sprint are the safety net. (Sprint 1's Winsock bug only showed up because the integration tests also run under Wine.)
- **Hand-curated export lists.** kernel32 and crtdll come from Wine's 1998 reconstruction of Windows 95; the rest are from knowledge. Verify with real DLLs. The installer added OLE32 and eight functions (decision 25); George's `EXPORTS.TXT` will confirm them.
- **Clocks.** Certificate checks need the right date, and VM clocks are often wrong. Tachyon warns, but can't fix it.
- **Scope.** "Full HTML5" is unbounded; the conformance suites and the site list keep us honest.
- **The floppy holds the installer, not the browser.** TACHYON.EXE grew to 1.4 MB stripped in Sprint 3 and no longer fits on a 1.44 MB floppy, so the floppy image now carries SETUP.EXE (798 KB compressed, with 576 KB to spare). Sprint 9's "install from a floppy" goal holds while the installer fits; after that it needs a second disk.
- **Parsing speed on a Pentium.** GitHub's 572 KB home page parses in 7 ms on the build machine; a 1990s CPU may be a hundred times slower. George's "read in" timings will tell.
- **Layout and painting speed on a Pentium.** A 12,000-word article lays out in 30 ms natively and paints a screen in about 4 ms; a hundred times slower would still be usable, but Wikipedia's real styles are heavier than our test page. Under Wine in CI, Wikipedia's article styles in 13 ms and lays out in 15 ms; George's "shown in" times are the real measure.

## Sprint history

### Sprint 4: Images and fonts (started 2026-10-22)

- Started from `main` after PR #6 merged Sprint 3.
- George's choices: fonts for other scripts come from Windows first, then an optional font pack on the CD and in the zip (decision 36); this sprint covers scripts written one character after another, plus the old Chinese, Japanese and Korean encodings (decision 37).
- **Pictures:** Wuffs (decision 35) decodes PNG, JPEG, GIF, BMP, WebP and ICO (`tc_image`), with unit tests and an image fuzzer. Pages show `<img>` (with `srcset`, `sizes` and `<picture>`), CSS background layers with `url()` and `linear-gradient()`, and `list-style-image`. The browser loads pictures on a second thread after showing the page, and keeps decoded pixels within a memory budget (an eighth of the PC's memory, 8 to 64 MB), dropping the ones drawn longest ago. `FETCH --render` and `tachyon-render` load them too.
- **Animation:** painting tells the page where each animated picture went, and a timer moves the ones on screen to their next frame and repaints only them. Pictures scrolled away, or in a minimised window, pause.
- **256 colours** (decision 39): `tc_dither` and an 8 bpp copy of the view's back buffer, with the palette realised as Windows expects (`WM_QUERYNEWPALETTE`, `WM_PALETTECHANGED`). `TACHYON.EXE --dither` and `tachyon-render --dither` show the result on any display. Wine 9 reports 16 bits per pixel even on an 8-bit X server, so the palette path awaits a real Windows 95 at 256 colours.

### Sprint 3: First Light (2026-10-21 to 22; merged as PRs #5 and #6)

- Done but the demo: George hadn't read a Wikipedia article on Windows 95 yet when Sprint 4 started, so it carries over.

- Started on top of Sprint 2's branch, since PR #4 (installer and log) wasn't merged yet.
- Scope refined: custom properties, `calc()` and width media queries come forward from Sprint 7 because Wikipedia's styles depend on them; tables, floats and positioning stay blocks (decision 29); fonts come from Windows 95 itself (decision 27).
- CSS from today's specifications: the syntax (css-parsing-tests: 8,334 of 8,366, the other 32 listed where the tests predate the spec), colours up to CSS Color 4, values with `calc()`, selectors with a Bloom filter of ancestors, the cascade with layers, nesting, custom properties and width media queries, and the HTML Standard's user-agent style sheet (decision 31).
- Text: stb_truetype over Windows 95's own fonts, with kerning, synthetic bold and italic, and a 2 MB glyph cache (decision 27).
- Layout and painting: the box tree, block layout with margin collapsing, inline layout, inline blocks, lists, generated content, Windows 95-style 3D borders. `tachyon-render` renders a file to PNG; 49 reference tests compare pages pixel for pixel, natively and under Wine.
- The browser draws pages (decision 32): a back buffer that scrolls by moving pixels, links with a hand cursor and the status bar, `#fragment` links, re-layout after resizing, View > Page, Tree and Source. Style sheets load over kept-alive connections with a small cache (decision 30). Windows 95's missing APIs needed stand-ins (decision 33).
- Hardening: unit tests for layout, `tc_page` and the PNG encoder; a second fuzz target that runs pages and style sheets through the whole renderer. In its first half hour it found an empty selector list doing arithmetic on a null pointer (twice), an empty nested selector that CSS Nesting says makes the rule invalid, font sizes and lengths big enough to overflow to infinity, and a page that took 41 seconds because glyphs a thousand pixels tall were rasterised whole on every paint and never fitted the glyph cache; now big glyphs are rasterised only where they show (54 ms). Decision 34 lists the limits. CI fuzzes both targets and renders the real sites under Wine, keeping PNGs of them.
- First numbers from CI (FETCH.EXE under Wine, on GitHub's runners): Wikipedia's Windows 95 article (550 KB, 4,146 elements, 2 style sheets over one kept-alive connection) styles in 13 ms and lays out in 15 ms, with 3.1 MB of styles and boxes; GitHub's home page (28 style sheets, 30,701 selectors) styles in 147 ms with 7.5 MB.
- TACHYON.EXE (1.4 MB) outgrew the floppy, which now carries the installer.
- George merged the branch into `main` at the fonts commit as PR #5 (2026-10-22), after PR #4. No `v0.2.0` tag yet.

### Sprint 2: HTML5 parser and DOM (2026-10-21; merged as PR #3, the installer and log in PR #4)

- Read the current HTML Standard before writing the parser, which mattered: since 2025, `<?target data?>` makes processing instruction nodes, `<select>` lost its own insertion modes (the customizable `<select>` work), and `<select>` became a scope boundary. html5lib-tests moved its tree-construction tests to web-platform-tests.
- The tokenizer passed all 7,049 html5lib tokenizer cases on its first run. The tree builder passed 3,882 of 3,898 on its first; the rest came down to `<nobr>` ignoring the adoption agency's "any other end tag" result, forms inside a `<template>` fragment, and a carriage return that the test generator's own newline translation had eaten.
- Integration found a latent Sprint 1 bug: `tc_buf_reserve` didn't NUL-terminate a newly allocated buffer.
- Fuzzing (libFuzzer with AddressSanitizer and UBSan, seeded from the test suites): 19.4 million inputs locally (three workers for 30 minutes, after a short run) with no crash, leak or undefined behaviour; CI now fuzzes for three minutes on every push.
- CI's real-sites job parsed all nine sites under Wine at the first attempt, Wikipedia's Windows 95 article (4,421 elements) and BBC News (4,899) among them; Hacker News and NeverSSL come out in quirks mode, as they should with no doctype.
- Parsing GitHub's 572 KB home page takes 5 to 7 ms and about 2 MB on the build machine. Timing it showed that `GetTickCount` only moves every 55 ms on Windows 95 (and coarsely under Wine), so `tc_time_ms` now uses the performance counter, which Windows 95 runs off the 1.19 MHz timer chip.
- George asked for a release with an installer to test from, so the basics of Sprint 9 came forward (decision 25). `SETUP.EXE` is 639 KB with everything in it. It installs to `C:\Program Files\Tachyon` with Start menu and desktop shortcuts and an Add/Remove Programs entry. CI installs and uninstalls it under Wine on every push, and publishes a pre-release when a version tag is pushed.
- The parser and browser were merged as PR #3 before the installer was done. The installer and releases followed in a pull request of their own, so the sprint stays open until George has run the demo from the first release.
- **Closed** with the demo done on Windows 95, the 0.2.0 release waiting for PR #4 to be merged and tagged, and one bug parked at George's call: a crash on a two-finger trackpad scroll (scroll bar and keys are fine), now in the backlog with what we know.
- CI's plain-HTTP check moved from neverssl.com, which timed out from GitHub's runners twice in one day, to `http://example.com/`.
- **The first crash on Windows 95:** scrolling Wikipedia's document tree ends in "an invalid page fault in module KERNEL32.DLL", with the stack pointer outside the thread's own stack (in Windows 95's shared memory, where its 16-bit parts run). Wine never shows it. George suggested a logger, so Tachyon now keeps `TACHYON.LOG` with crash reports (decision 26), and the next test build should say which row was being drawn.
- **The first run on Windows 95:** George installed Tachyon with `SETUP.EXE` and loaded Google over HTTPS on Windows 95 C (4.00.950 C) with a Pentium Pro and 128 MB. That also covers Sprint 1's demo. The About box reported 256 of 256 bits of randomness. The installer starting at all shows that its imports exist on Windows 95 C; the original release is still unchecked.

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
