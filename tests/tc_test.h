/* A tiny unit-test harness. The same test binary runs natively on the Linux
 * host and, cross-compiled, under Wine (and on real Windows 95).
 *
 *   static void test_something(void) { TC_CHECK(1 + 1 == 2); }
 *   void tc_suite_something(void) { TC_RUN(test_something); }
 *
 * Add each suite to the list in test_main.c. */
#ifndef TC_TEST_H
#define TC_TEST_H

#include <stdio.h>
#include <string.h>

extern int tc_test_checks;
extern int tc_test_failures;

void tc_test_fail(const char *file, int line, const char *what);
void tc_test_run(const char *name, void (*fn)(void));

#define TC_RUN(fn) tc_test_run(#fn, fn)

/* Test files compiled in by tools/gen_blobs.py (tests/data/images...). */
typedef struct tc_test_blob {
    const char *name;
    const unsigned char *data;
    size_t len;
} tc_test_blob;
extern const tc_test_blob tc_test_blobs[];
extern const size_t tc_test_blob_count;
/* The file of that name; NULL (and a failed check) if there's none. */
const tc_test_blob *tc_test_blob_get(const char *name);

#define TC_CHECK(cond) \
    do { \
        tc_test_checks++; \
        if (!(cond)) \
            tc_test_fail(__FILE__, __LINE__, #cond); \
    } while (0)

#define TC_CHECK_INT(actual, expected) \
    do { \
        long tc_a_ = (long)(actual), tc_e_ = (long)(expected); \
        tc_test_checks++; \
        if (tc_a_ != tc_e_) { \
            char tc_msg_[160]; \
            snprintf(tc_msg_, sizeof tc_msg_, "%s == %ld, expected %ld", #actual, tc_a_, tc_e_); \
            tc_test_fail(__FILE__, __LINE__, tc_msg_); \
        } \
    } while (0)

#define TC_CHECK_STR(actual, expected) \
    do { \
        const char *tc_a_ = (actual), *tc_e_ = (expected); \
        tc_test_checks++; \
        if (strcmp(tc_a_, tc_e_) != 0) { \
            char tc_msg_[256]; \
            snprintf(tc_msg_, sizeof tc_msg_, "%s == \"%.80s\", expected \"%.80s\"", #actual, tc_a_, tc_e_); \
            tc_test_fail(__FILE__, __LINE__, tc_msg_); \
        } \
    } while (0)

#endif
