/* Growable byte buffer, always NUL-terminated so it can be used as a string.
 *
 *   tc_buf b;
 *   tc_buf_init(&b);
 *   tc_buf_append_str(&b, "Hello");
 *   tc_buf_printf(&b, " %d", 1995);
 *   puts(tc_buf_cstr(&b));
 *   tc_buf_free(&b);
 */
#ifndef TC_BUF_H
#define TC_BUF_H

#include "tc_base.h"

typedef struct tc_buf {
    char *data;  /* NULL until the first append */
    size_t len;  /* bytes used, excluding the terminating NUL */
    size_t cap;  /* bytes allocated, including room for the NUL */
} tc_buf;

void tc_buf_init(tc_buf *b);
void tc_buf_free(tc_buf *b);
void tc_buf_clear(tc_buf *b);

/* Make room for at least `extra` more bytes. */
int tc_buf_reserve(tc_buf *b, size_t extra);

int tc_buf_append(tc_buf *b, const void *data, size_t len);
int tc_buf_append_str(tc_buf *b, const char *s);
int tc_buf_append_char(tc_buf *b, char c);
int tc_buf_printf(tc_buf *b, const char *fmt, ...) TC_PRINTF(2, 3);

/* The contents as a C string; "" for an empty buffer. */
const char *tc_buf_cstr(const tc_buf *b);

#endif
