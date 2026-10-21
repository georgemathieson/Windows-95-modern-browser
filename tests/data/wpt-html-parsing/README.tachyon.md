# HTML tree-construction tests

The `.dat` files from web-platform-tests'
[`html/syntax/parsing/resources`](https://github.com/web-platform-tests/wpt/tree/master/html/syntax/parsing/resources),
commit `64f13635ce4a216afe11c471db29f5692359eebc` (2026-10-21), where the
html5lib tree-construction tests are now maintained. 3-Clause BSD licence
(see `LICENSE.md`). Copied unmodified; `README.md` is upstream's description
of the format.

`tools/gen_html_tests.py` turns them into C tables at build time. Files whose
names start with `scripted_` need a script engine (Sprint 5) and are skipped.
