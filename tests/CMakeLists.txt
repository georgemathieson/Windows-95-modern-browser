add_executable(tachyon_tests
  test_main.c
  test_buf.c
  test_fixup.c
  test_surface.c
)
target_link_libraries(tachyon_tests PRIVATE tachyon_core)

# Cross builds run this under CMAKE_CROSSCOMPILING_EMULATOR (Wine).
add_test(NAME unit COMMAND tachyon_tests)
tachyon_win95_check(tachyon_tests)
