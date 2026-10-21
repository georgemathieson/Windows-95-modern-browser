/* The web-platform-tests URL cases (urltestdata.json and toascii.json),
 * generated into url_cases.c at build time by tools/gen_url_tests.py. */
#ifndef URL_CASES_H
#define URL_CASES_H

#include <stddef.h>

typedef struct url_case {
    const char *input;
    size_t input_len;  /* inputs may contain NUL */
    const char *base;  /* NULL: no base URL */
    int failure;       /* parsing must fail */
    /* Expected values of the URL object's attributes, as in JavaScript. */
    const char *href, *protocol, *username, *password, *host, *hostname, *port, *pathname,
        *search, *hash;
    const char *known_failure; /* why Tachyon gets this wrong, or NULL */
} url_case;

extern const url_case url_cases[];
extern const size_t url_case_count;

/* Host to ASCII: new URL("https://" + input + "/x").host must be output. */
typedef struct host_case {
    const char *input;
    size_t input_len;
    const char *output; /* NULL: parsing must fail */
    const char *known_failure;
} host_case;

extern const host_case host_cases[];
extern const size_t host_case_count;

#endif
