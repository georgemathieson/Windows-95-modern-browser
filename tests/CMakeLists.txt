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

add_executable(tachyon_tests
  ${CMAKE_CURRENT_BINARY_DIR}/url_cases.c
  test_main.c
  test_buf.c
  test_fixup.c
  test_net.c
  test_platform.c
  test_sha256.c
  test_surface.c
  test_url.c
)
target_include_directories(tachyon_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tachyon_tests PRIVATE tachyon_core)

# Cross builds run this under CMAKE_CROSSCOMPILING_EMULATOR (Wine).
add_test(NAME unit COMMAND tachyon_tests)
tachyon_win95_check(tachyon_tests)
