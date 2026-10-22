# css-parsing-tests

Implementation-independent tests for CSS Syntax Level 3 and CSS Color, from
https://github.com/SimonSapin/css-parsing-tests, commit
`203ce36bffd617db7f118c551e32794561fb273d` (2025-09-24). Dedicated to the public domain (CC0; see `LICENSE`).
The format is described in `README.rst`. Copied unmodified;
`tools/gen_css_tests.py` turns them into C tables at build time.

The tests follow the 2021 Candidate Recommendation of CSS Syntax. Where
today's spec differs, Tachyon follows today's spec, and
`../css_known_failures.txt` lists the cases with the reason (decision 28).

`generate_color_json.py` is upstream's script for the colour tests; it
isn't used here.

To update, copy the same files from a newer commit, update the commit above,
and rebuild: any new failure shows up in the unit tests.
