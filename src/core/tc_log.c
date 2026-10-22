#include "tc_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tc_platform.h"
#include "tc_utf8.h"

static char ring[TC_LOG_RING][TC_LOG_LINE];
static unsigned ring_count; /* lines ever added; the next goes in ring[ring_count % TC_LOG_RING] */
static tc_mutex *lock;
static tc_log_sink sink;
static void *sink_ctx;
static uint32_t start_ms;

int tc_log_init(tc_log_sink new_sink, void *ctx)
{
    if (!lock)
        lock = tc_mutex_create();
    if (!lock)
        return TC_ERR_NOMEM;
    tc_mutex_lock(lock);
    sink = new_sink;
    sink_ctx = ctx;
    start_ms = tc_time_ms();
    tc_mutex_unlock(lock);
    return TC_OK;
}

/* The lock stays: the page loader may still be logging, and nobody waits
   for it to finish. */
void tc_log_shutdown(void)
{
    if (lock)
        tc_mutex_lock(lock);
    sink = NULL;
    sink_ctx = NULL;
    if (lock)
        tc_mutex_unlock(lock);
}

static void add(int to_sink, const char *fmt, va_list ap)
{
    char line[TC_LOG_LINE];
    uint32_t ms = tc_time_ms() - start_ms;
    int n = snprintf(line, sizeof line, to_sink ? "[%6u.%03u] " : "[%6u.%03u]   ", (unsigned)(ms / 1000),
                     (unsigned)(ms % 1000));
    size_t len, i;

    if (n < 0)
        return;
    vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    len = strlen(line);
    for (i = 0; i < len; i++)
        if ((unsigned char)line[i] < 0x20)
            line[i] = ' ';

    if (lock)
        tc_mutex_lock(lock);
    memcpy(ring[ring_count % TC_LOG_RING], line, len + 1);
    ring_count++;
    if (to_sink && sink)
        sink(line, len, sink_ctx);
    if (lock)
        tc_mutex_unlock(lock);
}

void tc_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    add(1, fmt, ap);
    va_end(ap);
}

void tc_trace(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    add(0, fmt, ap);
    va_end(ap);
}

void tc_log_recent(void (*fn)(const char *line, void *ctx), void *ctx)
{
    unsigned end = ring_count, i = end > TC_LOG_RING ? end - TC_LOG_RING : 0;
    for (; i != end; i++)
        fn(ring[i % TC_LOG_RING], ctx);
}

char *tc_log_ascii(char *out, size_t cap, const char *utf8, size_t len)
{
    size_t i = 0, n = 0, keep = 0; /* keep: where to cut so that "..." still fits */

    if (!cap)
        return out;
    while (i < len) {
        char piece[12];
        size_t piece_len;
        uint32_t cp;
        i += tc_utf8_decode(utf8 + i, len - i, &cp);
        if (cp >= 0x20 && cp < 0x7F) {
            piece[0] = (char)cp;
            piece_len = 1;
        } else {
            piece_len = (size_t)snprintf(piece, sizeof piece, cp > 0xFFFF ? "\\U%06X" : "\\u%04X", (unsigned)cp);
        }
        if (n + piece_len >= cap) {
            if (keep + 3 < cap) {
                memcpy(out + keep, "...", 3);
                n = keep + 3;
            }
            break;
        }
        memcpy(out + n, piece, piece_len);
        n += piece_len;
        if (n + 3 < cap)
            keep = n;
    }
    out[n] = '\0';
    return out;
}
