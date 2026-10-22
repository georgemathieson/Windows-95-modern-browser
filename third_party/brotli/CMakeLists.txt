# Brotli's decoder, for WOFF2 fonts and br Content-Encoding. See
# README.tachyon.md.
add_library(brotlidec STATIC
  c/common/constants.c
  c/common/context.c
  c/common/dictionary.c
  c/common/platform.c
  c/common/shared_dictionary.c
  c/common/transform.c
  c/dec/bit_reader.c
  c/dec/decode.c
  c/dec/huffman.c
  c/dec/prefix.c
  c/dec/state.c
  c/dec/static_init.c
)
target_include_directories(brotlidec SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/c/include)
# Upstream code: Tachyon's warning flags (and -Werror) don't apply.
target_compile_options(brotlidec PRIVATE -w)
