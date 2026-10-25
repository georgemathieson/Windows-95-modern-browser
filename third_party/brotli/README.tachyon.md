# Brotli

| | |
|---|---|
| Upstream | https://github.com/google/brotli |
| Version | v1.2.1, commit `db24a98aed38a16de78cc05ca15cdd8dc2a0d92d` |
| License | MIT (see `LICENSE`) |

The decoder only: `c/common` (less the dictionary's `.bin` files, which
`dictionary_inc.h` already holds), `c/dec`, and from `c/include/brotli` the
headers the decoder uses. Left out: the encoder, the tools, the bindings
and the tests. `CMakeLists.txt` is Tachyon's, building the `brotlidec`
library for WOFF2 fonts and `Content-Encoding: br` (decision 42).

Local changes: none.
