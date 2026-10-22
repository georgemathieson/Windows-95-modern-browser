# Wuffs in Tachyon

| | |
|---|---|
| Upstream | https://github.com/google/wuffs |
| Version | **Wuffs v0.4.0-alpha.12+4048.20261020** (`release/c/wuffs-v0.4.c`) |
| Commit | `4be865da258c4b5e6d3e716fd4f36be016496a46` |
| License | MIT or Apache 2.0, your choice (see `LICENSE`, `LICENSE-MIT`, `LICENSE-APACHE`) |

Wuffs ("Wrangling Untrusted File Formats Safely") is Google's library of
decoders written in a language whose compiler proves their bounds checks,
then translated to C. Tachyon uses its image decoders: PNG, JPEG (baseline
and progressive), GIF, BMP and WebP (lossy and lossless). See decision 35.

Only the single-file C release, `wuffs-v0.4.c`, is here, with the licence
files. v0.4 is an alpha (v0.3, the stable release, has no JPEG or WebP),
so this is pinned to the commit above.

**No upstream file is modified.** The configuration lives in
`src/core/tc_wuffs.h`:

- `WUFFS_CONFIG__MODULES` with only the modules Tachyon needs (base,
  adler32, crc32, deflate, zlib, png, lzw, gif, jpeg, bmp, vp8, webp).
- `WUFFS_CONFIG__AVOID_CPU_ARCH`, which leaves out Wuffs' x86 code paths.
  They use SSE behind a CPU check, which Windows 95 can't allow (decision 9).

`src/core/tc_wuffs.c` compiles the implementation with Tachyon's warning
flags relaxed for that file. Its generic image decoder calls each format's
functions through pointers whose first parameter is `void *`, which UBSan's
`-fsanitize=function` reports; the fuzz build turns that one check off. It needs nothing from the C library beyond
`calloc`, `free`, `memcmp`, `memmove` and `memset`.

To update, copy `release/c/wuffs-v0.4.c` (or a newer release's file) from a
newer commit, update the version and commit above, rebuild both presets,
run the image unit tests and `tools/fuzz.sh SECONDS JOBS image`, and check
that `win95check` still passes.
