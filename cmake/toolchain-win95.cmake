# Cross-compile Tachyon for Windows 95 with MinGW-w64.
#
#   cmake --preset win95        (or: cmake -B build-win95 --toolchain cmake/toolchain-win95.cmake)
#
# Target flags (CPU floor, Windows version, linker settings) live in the
# top-level CMakeLists.txt so they apply however the build is configured.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 4.0)
set(CMAKE_SYSTEM_PROCESSOR i586)

set(TACHYON_TRIPLE i686-w64-mingw32 CACHE STRING "MinGW-w64 target triple")

set(CMAKE_C_COMPILER ${TACHYON_TRIPLE}-gcc)
set(CMAKE_RC_COMPILER ${TACHYON_TRIPLE}-windres)
set(CMAKE_OBJDUMP ${TACHYON_TRIPLE}-objdump CACHE FILEPATH "objdump for the Windows 95 target")

set(CMAKE_FIND_ROOT_PATH /usr/${TACHYON_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Run test programs under Wine when it is installed.
find_program(TACHYON_WINE NAMES wine)
if(TACHYON_WINE)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${TACHYON_WINE})
endif()
