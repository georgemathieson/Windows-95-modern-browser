# fetch: Tachyon's network stack on the command line. tachyon-fetch on the
# host, FETCH.EXE (a console program) for Windows 95.
add_executable(fetch fetch.c)
target_link_libraries(fetch PRIVATE tachyon_core)
if(WIN32)
  set_target_properties(fetch PROPERTIES OUTPUT_NAME FETCH)
  tachyon_win95_check(fetch)
else()
  set_target_properties(fetch PROPERTIES OUTPUT_NAME tachyon-fetch)
endif()

# Against local HTTP, HTTPS and proxy servers (under Wine for Windows).
add_test(NAME fetch_integration
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/server/fetch_tests.py
          --certs ${PROJECT_SOURCE_DIR}/tests/server/certs
          -- ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:fetch>)
set_tests_properties(fetch_integration PROPERTIES LABELS integration TIMEOUT 900)
