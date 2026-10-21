/* css-parsing-tests, generated into css_cases.c at build time by
 * tools/gen_css_tests.py. */
#ifndef CSS_CASES_H
#define CSS_CASES_H

#include <stddef.h>

typedef struct css_case {
    const char *kind;          /* the test file: "component_value_list", "color_hsl_3", ... */
    const char *id;            /* "file.json:N" */
    const char *input;         /* UTF-8, or bytes for stylesheet_bytes */
    size_t input_len;
    const char *protocol;      /* stylesheet_bytes: encoding labels, or NULL */
    const char *environment;
    const char *expected;      /* canonical JSON (see the generator) */
    const char *known_failure; /* why Tachyon gets this wrong, or NULL */
} css_case;

extern const css_case css_cases[];
extern const size_t css_case_count;

#endif
