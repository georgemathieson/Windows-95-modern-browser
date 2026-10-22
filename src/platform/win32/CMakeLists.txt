# tc_platform.h for Windows 95: time, threads, entropy, Winsock 1.1.
add_library(tachyon_platform STATIC
  tw_platform.c
  tw_net.c
)
target_include_directories(tachyon_platform PUBLIC ${PROJECT_SOURCE_DIR}/src/core)
target_link_libraries(tachyon_platform PUBLIC wsock32)

add_executable(tachyon WIN32
  loader.c
  main.c
  shell.c
  tw_log.c
  view.c
  tachyon.rc
)
target_link_libraries(tachyon PRIVATE tachyon_core comctl32)
set_target_properties(tachyon PROPERTIES OUTPUT_NAME TACHYON)

# windres needs the generated version header and the icon next to the .rc.
target_include_directories(tachyon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_source_files_properties(tachyon.rc PROPERTIES
  OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/tachyon.ico)

tachyon_win95_check(tachyon)

# Open the window under Wine and check it paints (needs an X display). Both
# tests write TACHYON.LOG next to TACHYON.EXE, so they take turns.
add_test(NAME smoke COMMAND tachyon --smoke-test)
set_tests_properties(smoke PROPERTIES LABELS gui TIMEOUT 60 RESOURCE_LOCK tachyon_log)

# Crash on purpose and check the report in TACHYON.LOG.
add_test(NAME crash_report
         COMMAND ${CMAKE_COMMAND} "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" -DTACHYON=$<TARGET_FILE:tachyon>
                 -P ${PROJECT_SOURCE_DIR}/tests/crash_report.cmake)
set_tests_properties(crash_report PROPERTIES LABELS gui TIMEOUT 120 RESOURCE_LOCK tachyon_log)
