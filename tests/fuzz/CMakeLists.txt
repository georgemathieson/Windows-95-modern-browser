# libFuzzer targets (configure with the "fuzz" preset; see tools/fuzz.sh).
add_executable(fuzz_html fuzz_html.c)
target_link_libraries(fuzz_html PRIVATE tachyon_core)
target_link_options(fuzz_html PRIVATE -fsanitize=fuzzer)

add_executable(fuzz_render fuzz_render.c)
target_link_libraries(fuzz_render PRIVATE tachyon_core)
target_link_options(fuzz_render PRIVATE -fsanitize=fuzzer)
target_compile_definitions(fuzz_render PRIVATE TEST_FONTS_DIR="${PROJECT_SOURCE_DIR}/tests/data/fonts")

add_executable(fuzz_image fuzz_image.c)
target_link_libraries(fuzz_image PRIVATE tachyon_core)
target_link_options(fuzz_image PRIVATE -fsanitize=fuzzer)
