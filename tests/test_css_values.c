#include <math.h>

#include "tc_css_values.h"
#include "tc_test.h"

void tc_suite_css_values(void);

/* Parse text as one value and resolve it against basis (or -1: invalid). */
static float length(const char *text, float basis)
{
    static const tc_length_basis b = { 16, 20, 800, 600 };
    tc_arena a;
    tc_css_value *v;
    uint32_t n, i;
    const tc_calc *c = NULL;
    tc_length len;
    float result = -1;

    tc_arena_init(&a);
    tc_css_parse_values(&a, text, strlen(text), &v, &n);
    for (i = 0; i < n; i++)
        if (v[i].type != TC_CSS_WHITESPACE) {
            if (c)
                c = NULL, i = n;
            else
                c = tc_css_parse_calc(&a, &v[i], TC_ALLOW_LENGTH | TC_ALLOW_PERCENT);
        }
    if (c && tc_calc_compute(&a, c, &b, &len) == TC_OK)
        result = tc_length_resolve(&len, basis);
    tc_arena_free(&a);
    return result;
}

static void check_length(const char *text, float basis, float expected)
{
    float got = length(text, basis);
    tc_test_checks++;
    if (fabs(got - expected) > 0.01) {
        tc_test_failures++;
        printf("FAIL %s (basis %g): expected %g, got %g\n", text, basis, expected, got);
    }
}

static void test_lengths(void)
{
    check_length("10px", 0, 10);
    check_length("2em", 0, 32);
    check_length("1.5rem", 0, 30);
    check_length("50%", 300, 150);
    check_length("10vw", 0, 80);
    check_length("1in", 0, 96);
    check_length("12pt", 0, 16);
    check_length("0", 0, 0);
    check_length("calc(100% - 20px)", 200, 180);
    check_length("calc(2 * 3em)", 0, 96);
    check_length("calc(3em / 2)", 0, 24);
    check_length("calc((1px + 2px) * 2)", 0, 6);
    check_length("calc(50% + 1em - 2px)", 100, 64);
    check_length("min(10px, 5%)", 100, 5);
    check_length("min(10px, 5%)", 1000, 10);
    check_length("max(1em, 20px)", 0, 20);
    check_length("clamp(10px, 50%, 100px)", 50, 25);
    check_length("clamp(10px, 50%, 100px)", 1000, 100);
    check_length("calc(min(10px, 2em) + 5px)", 0, 15);
    check_length("calc(1px + 2)", 0, -1);       /* a length plus a number */
    check_length("calc(1px+2px)", 0, -1);       /* + needs whitespace */
    check_length("calc(2px * 3px)", 0, -1);
    check_length("calc(2px / 1px)", 0, -1);
    check_length("5", 0, -1);                   /* a number, not a length */
    check_length("10qq", 0, -1);
    check_length("calc(", 0, -1);
}

static int media(const char *text)
{
    static const tc_media_env env = { 800, 600, 800, 600, 8 };
    tc_arena a;
    tc_media_list *list;
    int result;

    tc_arena_init(&a);
    result = tc_media_parse_text(&a, text, &list) == TC_OK ? tc_media_matches(list, &env) : -1;
    tc_arena_free(&a);
    return result;
}

static void test_media(void)
{
    static const struct {
        const char *query;
        int expected;
    } cases[] = {
        { "", 1 },
        { "all", 1 },
        { "screen", 1 },
        { "print", 0 },
        { "SCREEN", 1 },
        { "not print", 1 },
        { "not screen", 0 },
        { "only screen", 1 },
        { "(min-width: 600px)", 1 },
        { "(max-width: 600px)", 0 },
        { "(width >= 600px)", 1 },
        { "(width < 600px)", 0 },
        { "(600px <= width)", 1 },
        { "(400px <= width <= 700px)", 0 },
        { "(400px <= width <= 900px)", 1 },
        { "screen and (orientation: landscape)", 1 },
        { "screen and (orientation: portrait)", 0 },
        { "(prefers-color-scheme: dark)", 0 },
        { "(prefers-color-scheme: light)", 1 },
        { "(prefers-reduced-motion: reduce)", 0 },
        { "(prefers-reduced-motion)", 0 },
        { "(hover)", 1 },
        { "(hover: none)", 0 },
        { "(pointer: fine)", 1 },
        { "only screen and (min-width: 40em)", 1 },
        { "only screen and (min-width: 60em)", 0 },
        { "(min-resolution: 2dppx)", 0 },
        { "(-webkit-min-device-pixel-ratio: 2)", 0 },
        { "(min-resolution: 1dppx)", 1 },
        { "print, (min-width: 100px)", 1 },
        { "(foo: bar)", 0 },
        { "screen and (min-width: 600px) and (max-width: 1000px)", 1 },
        { "(min-width: 600px) or (max-width: 100px)", 1 },
        { "(min-width: 600px) and (max-width: 100px)", 0 },
        { "not ((min-width: 900px) or (max-width: 100px))", 1 },
        { "garbage!", 0 },
        { "(aspect-ratio: 4/3)", 1 },
        { "(min-aspect-ratio: 16/9)", 0 },
        { "(color)", 1 },
        { "(monochrome)", 0 },
        { "(grid)", 0 },
        { "screen and (scripting: none)", 1 },
        { "(forced-colors: active)", 0 },
        { "speech", 0 },
        { "(min-width: 600px) and (orientation: landscape), print", 1 },
    };
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(cases); i++) {
        int got = media(cases[i].query);
        tc_test_checks++;
        if (got != cases[i].expected) {
            tc_test_failures++;
            printf("FAIL @media %s: expected %d, got %d\n", cases[i].query, cases[i].expected, got);
        }
    }
}

void tc_suite_css_values(void)
{
    TC_RUN(test_lengths);
    TC_RUN(test_media);
}
