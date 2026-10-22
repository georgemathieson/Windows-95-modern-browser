# Crash TACHYON.EXE on purpose and check TACHYON.LOG's report, then start
# it again and check it noticed. Run by ctest under Wine:
#   cmake -DEMULATOR=wine -DTACHYON=path/TACHYON.exe -P crash_report.cmake
get_filename_component(dir ${TACHYON} DIRECTORY)
set(log ${dir}/TACHYON.LOG)
file(REMOVE ${log} ${dir}/TACHYON.OLD)

execute_process(COMMAND ${EMULATOR} ${TACHYON} --crash-test RESULT_VARIABLE rc TIMEOUT 60)
if(rc EQUAL 0)
  message(FATAL_ERROR "--crash-test didn't crash")
endif()
file(READ ${log} text)
foreach(expected
    "=== Tachyon "
    "*** CRASH: exception C0000005 at "
    " writing 00000010, window thread"
    "crash test: writing to "
    "Crashed in TACHYON.exe "
    "Code addresses near ESP:"
    "*** end of crash report")
  string(FIND "${text}" "${expected}" at)
  if(at LESS 0)
    message(FATAL_ERROR "TACHYON.LOG has no \"${expected}\":\n${text}")
  endif()
endforeach()

execute_process(COMMAND ${EMULATOR} ${TACHYON} --smoke-test RESULT_VARIABLE rc TIMEOUT 60)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--smoke-test after the crash failed: ${rc}")
endif()
file(READ ${log} text)
foreach(expected "last run: crashed (its report is above)" "=== closed normally")
  string(FIND "${text}" "${expected}" at)
  if(at LESS 0)
    message(FATAL_ERROR "TACHYON.LOG has no \"${expected}\" after the second run:\n${text}")
  endif()
endforeach()
file(REMOVE ${log})
