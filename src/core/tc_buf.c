#include "tc_buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tc_buf_init(tc_buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void tc_buf_free(tc_buf *b)
{
    free(b->data);
    tc_buf_init(b);
}

void tc_buf_clear(tc_buf *b)
{
    b->len = 0;
    if (b->data)
        b->data[0] = '\0';
}

int tc_buf_reserve(tc_buf *b, size_t extra)
{
    size_t need, cap;
    char *p;

    if (extra > (size_t)-1 - b->len - 1)
        return TC_ERR_NOMEM;
    need = b->len + extra + 1;
    if (need <= b->cap)
        return TC_OK;
    cap = b->cap ? b->cap : 64;
    while (cap < need)
        cap = cap > (size_t)-1 / 2 ? need : cap * 2;
    p = realloc(b->data, cap);
    if (!p)
        return TC_ERR_NOMEM;
    p[b->len] = '\0';
    b->data = p;
    b->cap = cap;
    return TC_OK;
}

int tc_buf_append(tc_buf *b, const void *data, size_t len)
{
    int err = tc_buf_reserve(b, len);
    if (err)
        return err;
    if (len)
        memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return TC_OK;
}

int tc_buf_append_str(tc_buf *b, const char *s)
{
    return tc_buf_append(b, s, strlen(s));
}

int tc_buf_append_char(tc_buf *b, char c)
{
    return tc_buf_append(b, &c, 1);
}

int tc_buf_printf(tc_buf *b, const char *fmt, ...)
{
    va_list ap;
    int n, err;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        return TC_ERR_INVALID;

    err = tc_buf_reserve(b, (size_t)n);
    if (err)
        return err;
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return TC_OK;
}

const char *tc_buf_cstr(const tc_buf *b)
{
    return b->data ? b->data : "";
}
