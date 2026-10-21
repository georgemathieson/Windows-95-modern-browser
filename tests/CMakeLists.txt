# web-platform-tests' URL cases as a C table.
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/url_cases.c
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_url_tests.py
          ${CMAKE_CURRENT_SOURCE_DIR}/data/wpt-url/urltestdata.json
          ${CMAKE_CURRENT_SOURCE_DIR}/data/wpt-url/toascii.json
          ${CMAKE_CURRENT_SOURCE_DIR}/data/url_known_failures.txt
          ${CMAKE_CURRENT_BINARY_DIR}/url_cases.c
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/gen_url_tests.py
          ${CMAKE_CURRENT_SOURCE_DIR}/data/wpt-url/urltestdata.json
          ${CMAKE_CURRENT_SOURCE_DIR}/data/wpt-url/toascii.json
          ${CMAKE_CURRENT_SOURCE_DIR}/data/url_known_failures.txt
  COMMENT "Generating URL test cases"
  VERBATIM)

# css-parsing-tests as a C table.
file(GLOB CSS_TEST_DATA ${CMAKE_CURRENT_SOURCE_DIR}/data/css-parsing-tests/*.json)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/css_cases.c
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_css_tests.py
          ${CMAKE_CURRENT_SOURCE_DIR}/data/css-parsing-tests
          ${CMAKE_CURRENT_SOURCE_DIR}/data/css_known_failures.txt
          ${CMAKE_CURRENT_BINARY_DIR}/css_cases.c
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/gen_css_tests.py ${CSS_TEST_DATA}
          ${CMAKE_CURRENT_SOURCE_DIR}/data/css_known_failures.txt
  COMMENT "Generating CSS parser test cases"
  VERBATIM)

# html5lib-tests' tokenizer and encoding tests and web-platform-tests'
# tree-construction tests as C tables.
file(GLOB HTML_TEST_DATA
  ${CMAKE_CURRENT_SOURCE_DIR}/data/html5lib-tests/tokenizer/*.test
  ${CMAKE_CURRENT_SOURCE_DIR}/data/html5lib-tests/encoding/*.dat
  ${CMAKE_CURRENT_SOURCE_DIR}/data/wpt-html-parsing/*.dat)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/html_cases.c
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_html_tests.py
          ${CMAKE_CURRENT_SOURCE_DIR}/data/html5lib-tests/tokenizer
          ${CMAKE_CURRENT_SOURCE_DIR}/data/wpt-html-parsing
          ${CMAKE_CURRENT_SOURCE_DIR}/data/html5lib-tests/encoding
          ${CMAKE_CURRENT_SOURCE_DIR}/data/html_known_failures.txt
          ${CMAKE_CURRENT_BINARY_DIR}/html_cases.c
  DEPENDS ${PROJECT_SOURCE_DIR}/tools/gen_html_tests.py ${HTML_TEST_DATA}
          ${CMAKE_CURRENT_SOURCE_DIR}/data/html_known_failures.txt
  COMMENT "Generating HTML parser test cases"
  VERBATIM)

add_executable(tachyon_tests
  ${CMAKE_CURRENT_BINARY_DIR}/url_cases.c
  ${CMAKE_CURRENT_BINARY_DIR}/html_cases.c
  ${CMAKE_CURRENT_BINARY_DIR}/css_cases.c
  test_main.c
  test_buf.c
  test_css.c
  test_css_values.c
  test_dom.c
  test_encoding.c
  test_fixup.c
  test_font.c
  test_html.c
  test_log.c
  test_net.c
  test_platform.c
  test_selector.c
  test_style.c
  test_sha256.c
  test_surface.c
  test_url.c
)
target_include_directories(tachyon_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tachyon_tests PRIVATE tachyon_core)

# Cross builds run this under CMAKE_CROSSCOMPILING_EMULATOR (Wine).
add_test(NAME unit COMMAND tachyon_tests)
tachyon_win95_check(tachyon_tests)
