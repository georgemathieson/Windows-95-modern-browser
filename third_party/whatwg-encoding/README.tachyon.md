# WHATWG Encoding Standard data

| | |
|---|---|
| Upstream | https://github.com/whatwg/encoding |
| Commit | `be17ac5c49881336538acfc7b40dba3c8009144b` (2026-10-19) |
| License | Creative Commons Attribution 4.0 (see `LICENSE`) |

- `encodings.json`: every encoding name and label.
- `index-*.txt`: the indexes of the legacy single-byte encodings (128 code
  points each), and of the Chinese, Japanese and Korean ones: jis0208,
  jis0212, gb18030, gb18030-ranges, big5 and euc-kr (added in Sprint 4,
  from the same commit).

Left out: `index-iso-2022-jp-katakana.txt`, which only encoders use.
Unmodified. At build time `tools/gen_encodings.py` compiles the indexes
into `tc_encoding_data.c` (about 180 KB of tables), and
`tools/gen_encoding_tests.py` samples them for the unit tests.

To update, copy the same files from a newer commit and rebuild.
