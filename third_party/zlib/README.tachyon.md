# zlib in Tachyon

| | |
|---|---|
| Upstream | https://github.com/madler/zlib |
| Version | **1.3.2** |
| Commit | `da607da739fa6047df13e66a2af6b8bec7c2a498` (tag `v1.3.2`) |
| License | zlib license (see `LICENSE`) |

Only the decompression side is here: `adler32.c`, `crc32.c`, `inffast.c`,
`inflate.c`, `inftrees.c`, `zutil.c` and their headers (plus `gzguts.h`,
which `zutil.c` includes). Tachyon never
compresses anything, so `deflate.c`, the `gz*` file functions and the rest
are left out.

**No upstream file is modified.** `CMakeLists.txt` is Tachyon's own.

To update, copy the same files from a new tag, rebuild both presets and check
that `win95check` still passes.
