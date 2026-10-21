# zlib's inflate side, for gzip/deflate Content-Encoding (and later PNG and
# WOFF). See README.tachyon.md.
add_library(zlib STATIC
  adler32.c
  crc32.c
  inffast.c
  inflate.c
  inftrees.c
  zutil.c
)
target_include_directories(zlib SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Upstream code: Tachyon's warning flags (and -Werror) don't apply.
target_compile_options(zlib PRIVATE -w)
