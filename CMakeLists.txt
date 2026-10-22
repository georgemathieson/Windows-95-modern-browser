cmake_minimum_required(VERSION 3.20)

project(tachyon VERSION 0.4.0 LANGUAGES C)

set(TACHYON_SPRINT 4)
set(TACHYON_SPRINT_NAME "Images and fonts")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(TACHYON_WERROR "Treat compiler warnings as errors" OFF)
option(TACHYON_FUZZ "Build the libFuzzer targets, with AddressSanitizer and UBSan (clang, host only)" OFF)

if(TACHYON_FUZZ)
  # Not -fsanitize=function: Wuffs calls its decoders through function
  # pointers that take the decoder as void *, by design (see
  # third_party/wuffs/README.tachyon.md).
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize=function
                      -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

# -O2 rather than CMake's -O3: -O3 grows the code a lot for little gain, a bad
# trade on a Pentium with 8 KB of code cache.
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes)
  if(TACHYON_WERROR)
    add_compile_options(-Werror)
  endif()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(WIN32)
  enable_language(RC)

  # Oldest CPU the shipped code must run on. Our own code is always compiled
  # for i586 (the original Pentium), but MinGW-w64's prebuilt runtime uses
  # Pentium Pro (cmov) instructions until we build our own toolchain, so the
  # instruction check is held at i686 for now. See docs/SPRINTS.md.
  set(TACHYON_ISA_FLOOR i686 CACHE STRING "CPU floor checked by win95check (i586 or i686)")

  # Windows 95 headers: hide every API newer than Windows 95 / NT 4 and
  # comctl32 newer than 4.00.
  add_compile_definitions(_WIN32_WINNT=0x0400 WINVER=0x0400 _WIN32_IE=0x0200 WIN32_LEAN_AND_MEAN)
  add_compile_options(-march=i586 -mtune=pentiumpro -ffunction-sections -fdata-sections)
  add_link_options(
    -static -Wl,--gc-sections
    -Wl,--major-os-version,4 -Wl,--minor-os-version,0
    -Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0
    -Wl,--disable-dynamicbase -Wl,--disable-nxcompat)

  # Which C runtime DLL to import. CRTDLL.DLL is on every copy of Windows 95;
  # MSVCRT.DLL only arrived with OSR2.5/IE4. MinGW-w64 links msvcrt by
  # default, so for crtdll we name the libraries ourselves: its crtdll import
  # library maps the startup code's names onto crtdll's (__GetMainArgs, ...).
  set(TACHYON_CRT crtdll CACHE STRING "C runtime DLL: crtdll or msvcrt")
  set_property(CACHE TACHYON_CRT PROPERTY STRINGS crtdll msvcrt)
  if(TACHYON_CRT STREQUAL "crtdll")
    add_link_options(-nodefaultlibs)
    set(CMAKE_C_STANDARD_LIBRARIES
      "-Wl,--start-group -lmingw32 -lgcc -lgcc_eh -lmingwex -lcrtdll -lkernel32 -luser32 -lgdi32 -lcomctl32 -ladvapi32 -lshell32 -lcomdlg32 -lwsock32 -Wl,--end-group")
  elseif(NOT TACHYON_CRT STREQUAL "msvcrt")
    message(FATAL_ERROR "TACHYON_CRT must be crtdll or msvcrt")
  endif()
endif()

# Every Windows executable must pass win95check: after linking, and again as a
# test so a stale binary can never slip through.
function(tachyon_win95_check target)
  if(NOT WIN32)
    return()
  endif()
  set(check ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/win95check/win95check.py
      --quiet --objdump ${CMAKE_OBJDUMP} --isa ${TACHYON_ISA_FLOOR})
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${check} $<TARGET_FILE:${target}>
    COMMENT "win95check ${target}"
    VERBATIM)
  add_test(NAME win95check_${target} COMMAND ${check} $<TARGET_FILE:${target}>)
  set_tests_properties(win95check_${target} PROPERTIES LABELS win95check)
endfunction()

enable_testing()

add_subdirectory(third_party/mbedtls)
add_subdirectory(third_party/zlib)
add_subdirectory(src/core)
if(WIN32)
  add_subdirectory(src/platform/win32)
  add_subdirectory(tools/win95exports)
else()
  add_subdirectory(src/platform/posix)
endif()
add_subdirectory(tools/fetch)
add_subdirectory(tools/render)
add_subdirectory(tests)
if(TACHYON_FUZZ)
  add_subdirectory(tests/fuzz)
endif()
