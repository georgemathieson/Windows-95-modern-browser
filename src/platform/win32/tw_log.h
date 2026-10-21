/* TACHYON.LOG, next to TACHYON.EXE: what the browser did, and a report when
 * it crashes, for testers to send us. tc_log() lines go there; the crash
 * handler adds the faulting address, registers, the code addresses on the
 * stack (read them with tools/package.sh's symbols file) and the recent
 * trace from tc_log.h. */
#ifndef TW_LOG_H
#define TW_LOG_H

#include <windows.h>

/* How the previous run ended, judged from the end of its log. */
typedef enum tw_last_run {
    TW_LAST_RUN_NONE,     /* no log yet */
    TW_LAST_RUN_CLEAN,
    TW_LAST_RUN_CRASHED,  /* with a crash report */
    TW_LAST_RUN_UNCLEAN   /* ended without closing: killed, or a crash we couldn't report */
} tw_last_run;

/* Open the log, describe this build and computer, and catch crashes.
   quiet_crash: after reporting a crash, exit instead of showing Windows'
   own crash dialog (for --crash-test under Wine). */
tw_last_run tw_log_start(const char *cmdline, int quiet_crash);
/* Note a clean exit and close the log. Safe to call twice. */
void tw_log_stop(void);
/* Open the log in Notepad. */
void tw_log_view(HWND owner);
/* Crash on purpose, a few calls deep, to test the report. */
void tw_log_crash_test(void);

#endif
