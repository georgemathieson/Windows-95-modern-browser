/* The HTML parser test suites (html5lib-tests' tokenizer and encoding
 * tests, web-platform-tests' tree-construction tests), generated into
 * html_cases.c at build time by tools/gen_html_tests.py. */
#ifndef HTML_CASES_H
#define HTML_CASES_H

#include <stddef.h>

typedef struct tok_case {
    const char *id;            /* "file.test:N State" */
    const char *input;         /* UTF-8 (WTF-8: lone surrogates allowed) */
    size_t input_len;
    int state;                 /* tc_tok_state to start in */
    const char *last_start_tag;
    const char *expected;      /* one serialized token per line */
    size_t expected_len;
    const char *errors;        /* error codes, one per line */
    const char *known_failure; /* why Tachyon gets this wrong, or NULL */
} tok_case;

typedef struct tree_case {
    const char *id;            /* "file.dat:N", plus " script" with scripting on */
    const char *data;
    size_t data_len;
    const char *context;       /* fragment context ("td", "svg path"), or NULL */
    int scripting;
    const char *expected;      /* the tree, in the tests' "| " format */
    const char *known_failure;
} tree_case;

typedef struct enc_case {
    const char *id;
    const char *data;
    size_t data_len;
    const char *encoding;      /* what the prescan should find */
    const char *known_failure;
} enc_case;

extern const tok_case tok_cases[];
extern const size_t tok_case_count;
extern const tree_case tree_cases[];
extern const size_t tree_case_count;
extern const size_t tree_case_skipped;  /* need scripts (Sprint 5) */
extern const enc_case enc_cases[];
extern const size_t enc_case_count;

#endif
