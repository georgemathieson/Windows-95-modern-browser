# Override for reproducible builds (or to test the wrong-clock warning).
if(NOT DEFINED TACHYON_BUILD_UNIX)
  string(TIMESTAMP TACHYON_BUILD_UNIX "%s" UTC)
endif()
if(NOT DEFINED TACHYON_BUILD_COMMIT)
  execute_process(COMMAND git rev-parse --short=10 HEAD
                  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                  OUTPUT_VARIABLE TACHYON_BUILD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT TACHYON_BUILD_COMMIT)
    set(TACHYON_BUILD_COMMIT unknown)
  endif()
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

# The WHATWG Encoding Standard's labels and single-byte tables.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_encoding_data.c
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_encodings.py
          ${PROJECT_SOURCE_DIR}/third_party/whatwg-encoding/encodings.json
          ${PROJECT_SOURCE_DIR}/third_party/whatwg-encoding
          ${CMAKE_CURRENT_SOURCE_DIR}/tc_encoding.h
          ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_encoding_data.c
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/gen_encodings.py
          ${PROJECT_SOURCE_DIR}/third_party/whatwg-encoding/encodings.json
          ${CMAKE_CURRENT_SOURCE_DIR}/tc_encoding.h
  COMMENT "Compiling encoding tables"
  VERBATIM)

# HTML's named character references.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_entities.c
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_entities.py
          ${PROJECT_SOURCE_DIR}/third_party/html-entities/entities.js
          ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_entities.c
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/gen_entities.py ${PROJECT_SOURCE_DIR}/third_party/html-entities/entities.js
  COMMENT "Compiling HTML character references"
  VERBATIM)

# The user-agent style sheets.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_ua_css.c
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_ua_css.py
          ${CMAKE_CURRENT_SOURCE_DIR}/ua.css ${CMAKE_CURRENT_SOURCE_DIR}/ua-quirks.css
          ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_ua_css.c
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/gen_ua_css.py ${CMAKE_CURRENT_SOURCE_DIR}/ua.css
          ${CMAKE_CURRENT_SOURCE_DIR}/ua-quirks.css
  COMMENT "Compiling the user-agent style sheets"
  VERBATIM)

add_library(tachyon_core STATIC
  ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_cacerts.c
  ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_encoding_data.c
  ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_entities.c
  ${CMAKE_CURRENT_BINARY_DIR}/generated/tc_ua_css.c
  tc_arena.c
  tc_box.c
  tc_buf.c
  tc_css_color.c
  tc_css_syntax.c
  tc_css_values.c
  tc_dither.c
  tc_dom.c
  tc_encoding.c
  tc_entropy.c
  tc_err.c
  tc_fetch.c
  tc_fixup.c
  tc_font.c
  tc_font_stb.c
  tc_html_parser.c
  tc_html_sniff.c
  tc_html_tokenizer.c
  tc_image.c
  tc_http.c
  tc_inflate.c
  tc_layout.c
  tc_log.c
  tc_mime.c
  tc_outline.c
  tc_page.c
  tc_paint.c
  tc_png.c
  tc_selector.c
  tc_sha256.c
  tc_style.c
  tc_style_props.c
  tc_style_sheet.c
  tc_surface.c
  tc_tags.c
  tc_tls.c
  tc_url.c
  tc_url_host.c
  tc_utf8.c
  tc_wuffs.c
)
target_include_directories(tachyon_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/third_party/stb
  ${PROJECT_SOURCE_DIR}/third_party/wuffs
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(tachyon_core PUBLIC tachyon_platform mbedtls zlib)
if(NOT WIN32)
  target_link_libraries(tachyon_core PUBLIC m) # crtdll has the maths functions on Windows
endif()
