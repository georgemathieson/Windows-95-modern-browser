# Override for reproducible builds (or to test the wrong-clock warning).
if(NOT DEFINED TACHYON_BUILD_UNIX)
  string(TIMESTAMP TACHYON_BUILD_UNIX "%s" UTC)
endif()
configure_file(tc_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_version.h @ONLY)

# Mozilla's root certificates, compiled in.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_cacerts.c
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_cacerts.py
          ${PROJECT_SOURCE_DIR}/third_party/cacert/cacert.pem
          ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_cacerts.c
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/gen_cacerts.py ${PROJECT_SOURCE_DIR}/third_party/cacert/cacert.pem
  COMMENT "Compiling root certificates"
  VERBATIM)

add_library(tachyon_core STATIC
  ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_cacerts.c
  tc_buf.c
  tc_entropy.c
  tc_err.c
  tc_fetch.c
  tc_fixup.c
  tc_http.c
  tc_inflate.c
  tc_sha256.c
  tc_surface.c
  tc_tls.c
  tc_url.c
  tc_url_host.c
  tc_utf8.c
)
target_include_directories(tachyon_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(tachyon_core PUBLIC tachyon_platform mbedtls zlib)
