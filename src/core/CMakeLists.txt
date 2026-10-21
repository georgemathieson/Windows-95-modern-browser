configure_file(tc_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_version.h @ONLY)

add_library(tachyon_core STATIC
  tc_buf.c
  tc_entropy.c
  tc_err.c
  tc_fixup.c
  tc_sha256.c
  tc_surface.c
  tc_url.c
  tc_url_host.c
  tc_utf8.c
)
target_include_directories(tachyon_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(tachyon_core PUBLIC tachyon_platform)
