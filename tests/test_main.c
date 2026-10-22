#include "tc_test.h"

#include "tc_version.h"

int tc_test_checks;
int tc_test_failures;
static const char *current_test;

void tc_test_fail(const char *file, int line, const char *what)
{
    tc_test_failures++;
    printf("FAIL %s (%s:%d): %s\n", current_test, file, line, what);
}

void tc_test_run(const char *name, void (*fn)(void))
{
    int before = tc_test_failures;
    current_test = name;
    fn();
    if (tc_test_failures == before)
        printf("ok   %s\n", name);
}

void tc_suite_buf(void);
void tc_suite_css(void);
void tc_suite_css_values(void);
void tc_suite_dom(void);
void tc_suite_encoding(void);
void tc_suite_fixup(void);
void tc_suite_font(void);
void tc_suite_html(void);
void tc_suite_log(void);
void tc_suite_net(void);
void tc_suite_platform(void);
void tc_suite_selector(void);
void tc_suite_sha256(void);
void tc_suite_style(void);
void tc_suite_surface(void);
void tc_suite_url(void);

int main(void)
{
    printf("%s %s unit tests\n", TC_NAME, TC_VERSION);
    tc_suite_buf();
    tc_suite_css();
    tc_suite_css_values();
    tc_suite_dom();
    tc_suite_encoding();
    tc_suite_fixup();
    tc_suite_font();
    tc_suite_html();
    tc_suite_log();
    tc_suite_net();
    tc_suite_platform();
    tc_suite_selector();
    tc_suite_sha256();
    tc_suite_style();
    tc_suite_surface();
    tc_suite_url();
    printf("%d checks, %d failed\n", tc_test_checks, tc_test_failures);
    return tc_test_failures ? 1 : 0;
}
