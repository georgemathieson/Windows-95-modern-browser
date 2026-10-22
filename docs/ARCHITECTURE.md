# Architecture

This page describes how Tachyon is put together today and where each future
piece will live. It changes as the sprints land.

## Layers

```
 +-----------------------------------------------------------------+
 |  src/platform/win32   TACHYON.EXE: windows, menus, input, page  |
 |                       loads on worker threads (loader.c), the   |
 |                       page view and its scrolling (view.c);     |
 |                       Winsock, threads, entropy, fonts;         |
 |                       TACHYON.LOG and crash reports (tw_log.c)  |
 |  src/platform/posix   the same platform functions for Linux     |
 +-----------------------------------------------------------------+
 |  src/core             the engine: portable C99, no OS headers   |
 |    tc_page            a page: document, sheets, styles, layout, |
 |                       links and fragments                       |
 |    tc_layout, tc_box  the box tree, block and inline layout     |
 |    tc_paint           painting and hit testing                  |
 |    tc_style*          style sheets, cascade, computed values    |
 |    tc_selector        selector parsing and matching             |
 |    tc_css_syntax,     CSS tokenizer and parser; property values |
 |    tc_css_values      and colours (+ tc_css_color)              |
 |    tc_font            font matching, glyphs (stb_truetype)      |
 |    tc_html_parser     tree construction; page and fragment      |
 |                       parsing entry points                      |
 |    tc_html_tokenizer  the HTML tokenizer (+ tc_entities)        |
 |    tc_html_sniff      finding a page's encoding                 |
 |    tc_encoding        WHATWG encodings: labels, decoders        |
 |    tc_dom             the document tree, in an arena            |
 |    tc_outline         the tree as DOM-inspector rows            |
 |    tc_url             WHATWG URL parsing                        |
 |    tc_fetch           a load: DNS, connect, proxy, TLS,         |
 |                       request, response, redirects, decoding;   |
 |                       kept-alive connections and a small cache  |
 |    tc_http, tc_mime   HTTP/1.1 messages and MIME types, no I/O  |
 |    tc_tls             Mbed TLS glue, root certificates          |
 |    tc_inflate         gzip and deflate                          |
 |    tc_entropy         the entropy pool (own SHA-256)            |
 |    tc_surface, tc_png 32-bit pixels, and PNG files of them      |
 |    tc_log             the log and its ring of recent lines      |
 |    next: images (Sprint 4), JavaScript (Sprint 5)               |
 +-----------------------------------------------------------------+
 |  third_party          Mbed TLS 4.1, zlib, stb_truetype;         |
 |                       QuickJS-ng (Sprint 5)                     |
 +-----------------------------------------------------------------+
```

- **The core never includes OS headers.** It builds for Windows 95 and for
  the Linux host, so most testing happens natively and fast. What it needs
  from the OS is in `src/core/tc_platform.h`: time, threads, entropy sources
  and IPv4 sockets. Each platform implements that one file.
- **Pixels are ours.** Pages render into a `tc_surface` (32-bit
  `0x00RRGGBB`), which on Windows wraps a DIB section, so a frame reaches the
  screen with one `BitBlt`. This gives the same output on every Windows
  version and on the host, where the reference tests render pages.
- **The log.** `tc_log()` lines go to `TACHYON.LOG`; `tc_trace()` lines,
  for things too frequent for the disk (every scroll, every tree row
  drawn), only to a ring of the last 64 lines. When Tachyon crashes,
  `tw_log.c`'s handler appends a report: the exception, registers, the code
  addresses on the stack (named by `tools/symbolize.py` with the build's
  symbols file) and the ring. The next start says on the home page that it
  crashed.
- **Text.** Pages are set in the TrueType fonts Windows 95 already has,
  found through the registry's Fonts key and rasterised by stb_truetype
  (decision 27). With no fonts at all, text is drawn as boxes. The source
  and tree views still use GDI: `TextOutW` (one of the few Unicode
  functions that works on Windows 95), and ANSI text elsewhere.

## Loading a page

```
 address bar --(ANSI to UTF-8, fixup, tc_url_parse)--> history entry
   |
   +-- tw_load_start: a worker thread runs tc_fetch()
         tc_net_resolve -> tc_net_connect -> [proxy CONNECT] -> tc_tls_connect
         -> tc_http request -> tc_http_response_feed ... -> redirects?
         -> tc_inflate
       then, for HTML (tc_mime_wants_html), tc_html_parse_page():
         tc_html_sniff (BOM, HTTP charset, <meta> prescan) -> tc_decode
         -> tokenizer <-> tree builder -> tc_document
         (a <meta> naming another encoding restarts the parse once)
       then tc_page: the base URL, every <link rel=stylesheet> and @import
         fetched over the same kept-alive connection (or from the cache),
         styles computed and the page laid out for the view's size
       posts WM_TW_PROGRESS (throttled) and one WM_TW_DONE to the window
   |
   +-- WM_TW_DONE: the view takes over the page and paints it (or its tree
       or source, or an error page); free the load
```

Every blocking network call waits in 200 ms slices and checks the load's
cancel flag, so Stop works quickly. Only the DNS lookup (`gethostbyname`)
can't be interrupted; an abandoned load finishes in the background and its
result is thrown away. A load is only ever freed by its `WM_TW_DONE`
handler, so the worker never touches freed memory.

## The HTML parser

The parser follows the HTML Standard's algorithms step by step (the
tokenizer's states, the tree builder's insertion modes), so the standard
and the test suites are its specification.

- **Input** is the page decoded to UTF-8 with newlines normalized. The
  tokenizer works on that buffer directly and copies runs of plain text in
  one go; it only decodes characters where the rules look at them.
- **Pull, not push.** The tree builder asks the tokenizer for one token at
  a time, because it switches the tokenizer's state after some start tags
  (`<script>`, `<title>`, `<plaintext>`...). Adjacent characters come as one
  token, and every insertion mode handles a run of characters, not one
  character at a time.
- **Tags are numbers.** Known element names become `tc_tag` values once, so
  the tree builder compares integers. Unknown names stay strings.
- **The DOM** lives in a per-document arena (`tc_arena`): no malloc per
  node, and the whole page is freed at once. Nothing walks the tree
  recursively, and the parser caps nesting at 400 levels, so a hostile page
  can't overflow the stack.
- **Testing.** html5lib-tests (tokenizer, encoding) and web-platform-tests
  (tree construction) are compiled into the unit tests as C tables, and run
  natively and under Wine. `tests/data/html_known_failures.txt` lists the
  few known failures with reasons; one that starts passing fails the run.
  `tests/fuzz` fuzzes the whole pipeline with libFuzzer, AddressSanitizer
  and UBSan, locally (`tools/fuzz.sh`) and in CI.

## Rendering

```
 tc_document + style sheets (UA, <style>, <link>, @import, style="")
   -> tc_style_document: selectors matched (rules bucketed by id, class
      and tag; a Bloom filter of ancestors), the cascade (origins, layers,
      !important), var() and calc(), computed values; equal styles shared
   -> tc_layout_document: the box tree (anonymous boxes, list markers,
      generated content), then block layout (margin collapsing) and inline
      layout (items -> pieces -> lines -> fragments)
   -> tc_paint: backgrounds, borders, text and decorations for a clip
      rectangle, skipping boxes whose ink is outside it
```

- **Positions are relative.** Each box's border box is relative to its
  parent's, and a line's fragments to their block, so painting and hit
  testing walk the tree adding offsets, and each box records the area its
  contents paint in, so whole subtrees outside the clip are skipped.
- **The page view** (`view.c`) keeps a back buffer the size of the window:
  a DIB section wrapped as a `tc_surface`. Scrolling moves the pixels
  already there, paints only the strip that comes into view and lets
  `ScrollWindowEx` move the screen. Windows 95's scroll bars count to
  32,767, so long pages scroll in units of several pixels. Resizing lays
  the page out again 150 ms after the last change.
- **Limits.** The box tree stops nesting at 200 levels (deeper elements
  flow into their ancestor), lengths stay within ±2^25 px and font sizes
  within 10,000 px, and a page loads at most 64 style sheets, with imports
  four deep. Glyphs bigger than 128×128 px aren't cached: each paint
  rasterises only the part inside its clip.
- **Testing.** `tachyon-render` (`tools/render`) renders a page from a file
  to PNG on the host. The reference tests (`tests/reftests`) render each
  test page and its reference and compare them pixel for pixel, natively
  and under Wine, with the Liberation fonts (decision 27). Unit tests check
  layout with the box font, so they don't depend on fonts, and the render
  fuzzer (`tests/fuzz/fuzz_render.c`) runs pages and style sheets through
  the whole pipeline under AddressSanitizer and UBSan.

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
third_party/                        Mbed TLS, zlib, root certificates, WHATWG encoding data and
                                    character references (each with README.tachyon.md)
tests/                              unit tests (host and Wine), conformance suites, test server, fuzzer
tools/fetch/                        FETCH.EXE / tachyon-fetch
tools/render/                       RENDER.EXE / tachyon-render: a page file to PNG
tests/reftests/                     reference tests: test and reference pages, reftest.list
tools/win95check/                   import and instruction checker, export lists
tools/win95exports/                 EXPORTS.EXE: dumps real Windows 95 exports
tools/gen_*.py                      build-time generators (icon, cursor, root certificates,
                                    encodings, character references, the UA style sheet, URL,
                                    HTML and CSS tests, fuzz corpora)
tools/package.sh                    installer, zip, floppy image, ISO and symbols for testers
tools/symbolize.py                  names the functions in a crash report from TACHYON.LOG
tools/installer/                    SETUP.EXE's NSIS script; tools/test_installer.sh tests it
docs/                               sprint board, testing guide, spikes
```
