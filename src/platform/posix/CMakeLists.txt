# tc_platform.h for the Linux host build (tests and host tools).
find_package(Threads REQUIRED)
add_library(tachyon_platform STATIC
  tp_platform.c
  tp_net.c
)
target_include_directories(tachyon_platform PUBLIC ${PROJECT_SOURCE_DIR}/src/core)
target_link_libraries(tachyon_platform PUBLIC Threads::Threads)
