#include "tc_test.h"

#include "tc_fixup.h"

void tc_suite_fixup(void);

static const char *fixup(tc_buf *b, const char *input)
{
    return tc_fixup_address(input, b) == TC_OK ? tc_buf_cstr(b) : "<invalid>";
}

static void test_bare_host_gets_https(void)
{
    tc_buf b;
    tc_buf_init(&b);
    TC_CHECK_STR(fixup(&b, "example.com"), "https://example.com");
    TC_CHECK_STR(fixup(&b, "en.wikipedia.org/wiki/Windows_95"), "https://en.wikipedia.org/wiki/Windows_95");
    TC_CHECK_STR(fixup(&b, "localhost:8080"), "https://localhost:8080");
    tc_buf_free(&b);
}

static void test_scheme_is_kept(void)
{
    tc_buf b;
    tc_buf_init(&b);
    TC_CHECK_STR(fixup(&b, "http://neverssl.com"), "http://neverssl.com");
    TC_CHECK_STR(fixup(&b, "HTTPS://Example.com/A"), "HTTPS://Example.com/A");
    TC_CHECK_STR(fixup(&b, "file:///C:/WINDOWS/"), "file:///C:/WINDOWS/");
    tc_buf_free(&b);
}

static void test_about_urls_are_lowercased(void)
{
    tc_buf b;
    tc_buf_init(&b);
    TC_CHECK_STR(fixup(&b, "ABOUT:Tachyon"), "about:tachyon");
    TC_CHECK_STR(fixup(&b, "about:blank"), "about:blank");
    tc_buf_free(&b);
}

static void test_whitespace_is_trimmed(void)
{
    tc_buf b;
    tc_buf_init(&b);
    TC_CHECK_STR(fixup(&b, "  \texample.com/x \r\n"), "https://example.com/x");
    tc_buf_free(&b);
}

static void test_invalid_input(void)
{
    tc_buf b;
    tc_buf_init(&b);
    TC_CHECK_STR(fixup(&b, ""), "<invalid>");
    TC_CHECK_STR(fixup(&b, "   "), "<invalid>");
    TC_CHECK_STR(fixup(&b, "windows 95 browser"), "<invalid>");
    tc_buf_free(&b);
}

void tc_suite_fixup(void)
{
    TC_RUN(test_bare_host_gets_https);
    TC_RUN(test_scheme_is_kept);
    TC_RUN(test_about_urls_are_lowercased);
    TC_RUN(test_whitespace_is_trimmed);
    TC_RUN(test_invalid_input);
}
