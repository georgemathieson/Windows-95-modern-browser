#include "tc_test.h"

#include "tc_buf.h"

void tc_suite_buf(void);

static void test_empty_buffer_is_empty_string(void)
{
    tc_buf b;
    tc_buf_init(&b);
    TC_CHECK_STR(tc_buf_cstr(&b), "");
    TC_CHECK_INT(b.len, 0);
    tc_buf_free(&b);
}

static void test_append_grows_and_terminates(void)
{
    tc_buf b;
    int i;
    tc_buf_init(&b);
    for (i = 0; i < 1000; i++)
        TC_CHECK_INT(tc_buf_append_char(&b, (char)('a' + i % 26)), TC_OK);
    TC_CHECK_INT(b.len, 1000);
    TC_CHECK(b.cap > b.len);
    TC_CHECK_INT(b.data[1000], '\0');
    TC_CHECK_INT(b.data[26], 'a');
    tc_buf_free(&b);
    TC_CHECK(b.data == NULL);
}

static void test_append_binary_data(void)
{
    tc_buf b;
    const char bytes[] = { 'x', '\0', 'y' };
    tc_buf_init(&b);
    tc_buf_append(&b, bytes, sizeof bytes);
    TC_CHECK_INT(b.len, 3);
    TC_CHECK(memcmp(b.data, bytes, 3) == 0);
    tc_buf_free(&b);
}

static void test_printf_appends(void)
{
    tc_buf b;
    tc_buf_init(&b);
    tc_buf_append_str(&b, "Windows ");
    TC_CHECK_INT(tc_buf_printf(&b, "%d, %s %zu", 95, "build", (size_t)950), TC_OK);
    TC_CHECK_STR(tc_buf_cstr(&b), "Windows 95, build 950");
    tc_buf_free(&b);
}

static void test_printf_long_output(void)
{
    tc_buf b;
    tc_buf_init(&b);
    tc_buf_printf(&b, "%0500d", 7);
    TC_CHECK_INT(b.len, 500);
    TC_CHECK_INT(b.data[499], '7');
    tc_buf_free(&b);
}

static void test_clear_keeps_memory(void)
{
    tc_buf b;
    size_t cap;
    tc_buf_init(&b);
    tc_buf_append_str(&b, "hello");
    cap = b.cap;
    tc_buf_clear(&b);
    TC_CHECK_STR(tc_buf_cstr(&b), "");
    TC_CHECK_INT(b.cap, cap);
    tc_buf_free(&b);
}

static void test_reserve_overflow_fails(void)
{
    tc_buf b;
    tc_buf_init(&b);
    TC_CHECK_INT(tc_buf_reserve(&b, (size_t)-1), TC_ERR_NOMEM);
    tc_buf_free(&b);
}

void tc_suite_buf(void)
{
    TC_RUN(test_empty_buffer_is_empty_string);
    TC_RUN(test_append_grows_and_terminates);
    TC_RUN(test_append_binary_data);
    TC_RUN(test_printf_appends);
    TC_RUN(test_printf_long_output);
    TC_RUN(test_clear_keeps_memory);
    TC_RUN(test_reserve_overflow_fails);
}
