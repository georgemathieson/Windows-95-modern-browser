#include "tc_log.h"
#include "tc_test.h"

void tc_suite_log(void);

static char sunk[4][TC_LOG_LINE];
static int nsunk;

static void sink(const char *line, size_t len, void *ctx)
{
    TC_CHECK(ctx == (void *)sunk);
    TC_CHECK(strlen(line) == len);
    if (nsunk < 4)
        memcpy(sunk[nsunk], line, len + 1);
    nsunk++;
}

static char recent[TC_LOG_RING + 1][TC_LOG_LINE];
static int nrecent;

static void collect(const char *line, void *ctx)
{
    (void)ctx;
    if (nrecent < (int)TC_ARRAY_LEN(recent))
        snprintf(recent[nrecent], sizeof recent[0], "%s", line);
    nrecent++;
}

static const char *after_time(const char *line)
{
    const char *p = strchr(line, ']');
    return p ? p + 2 : line;
}

static void test_ascii(void)
{
    char out[64], small[10];

    TC_CHECK_STR(tc_log_ascii(out, sizeof out, "<a href=x>", 10), "<a href=x>");
    TC_CHECK_STR(tc_log_ascii(out, sizeof out, "\xE6\x97\xA5\xE6\x9C\xAC", 6), "\\u65E5\\u672C"); /* Japanese */
    TC_CHECK_STR(tc_log_ascii(out, sizeof out, "\xF0\x9F\x98\x80", 4), "\\U01F600");             /* an emoji */
    TC_CHECK_STR(tc_log_ascii(out, sizeof out, "a\nb\xFF", 4), "a\\u000Ab\\uFFFD");
    TC_CHECK_STR(tc_log_ascii(small, sizeof small, "abcdefghi", 9), "abcdefghi");
    TC_CHECK_STR(tc_log_ascii(small, sizeof small, "abcdefghij", 10), "abcdef...");
    TC_CHECK_STR(tc_log_ascii(small, sizeof small, "ab\xE6\x97\xA5", 5), "ab\\u65E5");
    TC_CHECK_STR(tc_log_ascii(small, sizeof small, "ab\xE6\x97\xA5\xE6\x9C\xAC", 8), "ab...");
    TC_CHECK_STR(tc_log_ascii(small, 3, "abcdef", 6), "ab");
}

static void test_sink_and_ring(void)
{
    char long_text[300];
    int i;

    nsunk = 0;
    TC_CHECK_INT(tc_log_init(sink, sunk), TC_OK);
    tc_log("loaded %s in %d ms", "example.com", 130);
    tc_trace("painted row %d", 7);
    tc_log("two\r\nlines");
    TC_CHECK_INT(nsunk, 2); /* traces stay in the ring */
    TC_CHECK(sunk[0][0] == '[');
    TC_CHECK_STR(after_time(sunk[0]), "loaded example.com in 130 ms");
    TC_CHECK_STR(after_time(sunk[1]), "two  lines");

    nrecent = 0;
    tc_log_recent(collect, NULL);
    TC_CHECK(nrecent >= 3);
    if (nrecent >= 3) {
        TC_CHECK_STR(after_time(recent[nrecent - 3]), "loaded example.com in 130 ms");
        TC_CHECK_STR(after_time(recent[nrecent - 2]), "  painted row 7");
    }

    memset(long_text, 'x', sizeof long_text - 1);
    long_text[sizeof long_text - 1] = '\0';
    tc_log("%s", long_text);
    TC_CHECK_INT((long)strlen(sunk[2]), TC_LOG_LINE - 1);

    /* The ring keeps the newest TC_LOG_RING lines. */
    for (i = 0; i < TC_LOG_RING + 6; i++)
        tc_trace("line %d", i);
    nrecent = 0;
    tc_log_recent(collect, NULL);
    TC_CHECK_INT(nrecent, TC_LOG_RING);
    TC_CHECK_STR(after_time(recent[0]), "  line 6");
    TC_CHECK_STR(after_time(recent[TC_LOG_RING - 1]), "  line 69");

    tc_log_shutdown();
    tc_log("after shutdown");
    TC_CHECK_INT(nsunk, 3);
}

void tc_suite_log(void)
{
    TC_RUN(test_ascii);
    TC_RUN(test_sink_and_ring);
}
