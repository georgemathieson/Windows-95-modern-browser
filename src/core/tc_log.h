/* A small log of what Tachyon did, for finding out what went wrong on a
 * tester's Windows 95 machine.
 *
 * tc_log() lines go to a sink (the browser writes them to TACHYON.LOG) and
 * into a ring of recent lines. tc_trace() lines only go into the ring: they
 * are for things too frequent to write to disk, like each tree row painted.
 * A crash report ends with the ring, so it shows what Tachyon was doing just
 * before. Both work from any thread, and before tc_log_init (then they only
 * fill the ring). */
#ifndef TC_LOG_H
#define TC_LOG_H

#include "tc_base.h"

#define TC_LOG_LINE 160 /* longest line kept, with its NUL */
#define TC_LOG_RING 64  /* recent lines kept */

/* Receives each tc_log() line, without a newline. Called with the log's
   lock held, so lines from different threads never interleave. */
typedef void (*tc_log_sink)(const char *line, size_t len, void *ctx);

/* Start timing lines from now and send them to sink (which may be NULL). */
int tc_log_init(tc_log_sink sink, void *ctx);
/* Stop calling the sink. */
void tc_log_shutdown(void);

void tc_log(const char *fmt, ...) TC_PRINTF(1, 2);
void tc_trace(const char *fmt, ...) TC_PRINTF(1, 2);

/* Call fn with each line in the ring, oldest first. It takes no lock, so a
   crash handler can use it even when the crashed thread held the lock. */
void tc_log_recent(void (*fn)(const char *line, void *ctx), void *ctx);

/* Copy UTF-8 text into out as printable ASCII, writing \uXXXX for anything
   else, so that it reads the same in any code page (Notepad shows the log in
   the ANSI one). Stops with "..." when out is full. Returns out. */
char *tc_log_ascii(char *out, size_t cap, const char *utf8, size_t len);

#endif
