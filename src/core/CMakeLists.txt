configure_file(tc_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_version.h @ONLY)

add_library(tachyon_core STATIC
  tc_buf.c
  tc_fixup.c
  tc_surface.c
)
target_include_directories(tachyon_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)
