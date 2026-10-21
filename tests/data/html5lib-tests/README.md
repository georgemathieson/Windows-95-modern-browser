# html5lib-tests

The tokenizer and encoding-sniffing tests from
[html5lib-tests](https://github.com/html5lib/html5lib-tests), commit
`c777c408b61078ea2eb4acefc2535f54dbc8b28a` (2026-10-01). MIT licence (see
`LICENSE`). Copied unmodified; `tools/gen_html_tests.py` turns them into C
tables at build time.

- `tokenizer/*.test`: JSON; the format is described in `tokenizer/README.md`.
- `encoding/tests1.dat`, `tests2.dat`, `test-yahoo-jp.dat`: `#data` then the
  `#encoding` the prescan must find. (`encoding/scripted` and `chardet` are
  left out: they need script execution and frequency analysis.)

The tree-construction tests now live in web-platform-tests; see
`../wpt-html-parsing`.

To update, copy the same files from a newer commit, update the commit above,
and rebuild: any new failure shows up in the unit tests.
