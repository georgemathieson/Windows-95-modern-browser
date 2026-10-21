# WHATWG Encoding Standard data

| | |
|---|---|
| Upstream | https://github.com/whatwg/encoding |
| Commit | `be17ac5c49881336538acfc7b40dba3c8009144b` (2026-10-19) |
| License | Creative Commons Attribution 4.0 (see `LICENSE`) |

- `encodings.json`: every encoding name and label.
- `index-*.txt`: the indexes of the legacy single-byte encodings (128 code
  points each).

The multi-byte Chinese, Japanese and Korean indexes are left out until
Tachyon can draw those scripts (see the backlog). Unmodified. At build time
`tools/gen_encodings.py` compiles them into `tc_encoding_data.c`.

To update, copy the same files from a newer commit and rebuild.
