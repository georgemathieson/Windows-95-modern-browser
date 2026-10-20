# EXPORTS.EXE runs on Windows 95 and records the real export tables, so the
# win95check lists can be verified (see tools/win95check/dump_exports.py).
add_executable(win95exports WIN32 exports.c)
set_target_properties(win95exports PROPERTIES OUTPUT_NAME EXPORTS)
tachyon_win95_check(win95exports)
