#include "tc_http.h"

#include <stdlib.h>
#include <string.h>

/* ---- Headers ---- */

void tc_headers_init(tc_headers *h)
{
    h->items = NULL;
    h->len = h->cap = 0;
}

void tc_headers_free(tc_headers *h)
{
    size_t i;
    for (i = 0; i < h->len; i++) {
        free(h->items[i].name);
        free(h->items[i].value);
    }
    free(h->items);
    tc_headers_init(h);
}

static char *dup_n(const char *s, size_t n)
{
    char *p = malloc(n + 1);
    if (p) {
        memcpy(p, s, n);
        p[n] = '\0';
    }
    return p;
}

int tc_headers_add(tc_headers *h, const char *name, size_t name_len, const char *value, size_t value_len)
{
    tc_header item;
    if (h->len == h->cap) {
        size_t cap = h->cap ? h->cap * 2 : 16;
        tc_header *p = realloc(h->items, cap * sizeof *p);
        if (!p)
            return TC_ERR_NOMEM;
        h->items = p;
        h->cap = cap;
    }
    item.name = dup_n(name, name_len);
    item.value = dup_n(value, value_len);
    if (!item.name || !item.value) {
        free(item.name);
        free(item.value);
        return TC_ERR_NOMEM;
    }
    h->items[h->len++] = item;
    return TC_OK;
}

static int equal_nocase(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

const char *tc_headers_get(const tc_headers *h, const char *name)
{
    size_t i;
    for (i = 0; i < h->len; i++)
        if (equal_nocase(h->items[i].name, name))
            return h->items[i].value;
    return NULL;
}

/* ---- Requests ---- */

int tc_http_build_request(tc_buf *out, const char *method, const tc_url *url, int absolute_form,
                          const tc_headers *extra)
{
    tc_buf target;
    size_t i;
    int err;

    tc_buf_init(&target);
    if (absolute_form) {
        const char *hash = strchr(url->href, '#');
        err = tc_buf_append(&target, url->href, hash ? (size_t)(hash - url->href) : strlen(url->href));
    } else {
        err = tc_url_request_target(url, &target);
    }
    err = err ? err : tc_buf_printf(out, "%s %s HTTP/1.1\r\nHost: %s", method, tc_buf_cstr(&target),
                                    url->host ? url->host : "");
    if (!err && url->port >= 0)
        err = tc_buf_printf(out, ":%d", url->port);
    err = err ? err : tc_buf_append_str(out, "\r\n");
    for (i = 0; extra && i < extra->len && !err; i++)
        err = tc_buf_printf(out, "%s: %s\r\n", extra->items[i].name, extra->items[i].value);
    err = err ? err : tc_buf_append_str(out, "\r\n");
    tc_buf_free(&target);
    return err;
}

/* ---- Responses ---- */

enum {
    ST_STATUS, ST_HEADERS, ST_BODY_LENGTH, ST_CHUNK_SIZE, ST_CHUNK_DATA, ST_CHUNK_END,
    ST_TRAILERS, ST_BODY_UNTIL_CLOSE, ST_DONE
};

void tc_http_response_init(tc_http_response *r, int no_body)
{
    memset(r, 0, sizeof *r);
    tc_headers_init(&r->headers);
    tc_buf_init(&r->line);
    r->content_length = -1;
    r->no_body = no_body;
    r->state = ST_STATUS;
}

void tc_http_response_free(tc_http_response *r)
{
    tc_headers_free(&r->headers);
    tc_buf_free(&r->line);
    free(r->reason);
    r->reason = NULL;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

static int parse_status_line(tc_http_response *r, const char *s, size_t len)
{
    size_t i = 9;
    if (len < 12 || memcmp(s, "HTTP/1.", 7) != 0 || s[7] < '0' || s[7] > '9' || s[8] != ' ')
        return TC_ERR_PROTOCOL;
    r->minor_version = s[7] - '0';
    if (s[9] < '1' || s[9] > '5' || s[10] < '0' || s[10] > '9' || s[11] < '0' || s[11] > '9')
        return TC_ERR_PROTOCOL;
    r->status = (s[9] - '0') * 100 + (s[10] - '0') * 10 + (s[11] - '0');
    i = 12;
    if (i < len && s[i] != ' ')
        return TC_ERR_PROTOCOL;
    while (i < len && s[i] == ' ')
        i++;
    free(r->reason);
    r->reason = dup_n(s + i, len - i);
    return r->reason ? TC_OK : TC_ERR_NOMEM;
}

static int parse_header_line(tc_http_response *r, const char *s, size_t len)
{
    const char *colon;
    size_t name_len, start, end;

    if (is_space(s[0])) {
        /* Obsolete line folding: continue the previous value. */
        tc_header *last;
        char *joined;
        size_t old;
        if (r->headers.len == 0)
            return TC_ERR_PROTOCOL;
        last = &r->headers.items[r->headers.len - 1];
        start = 0;
        while (start < len && is_space(s[start]))
            start++;
        old = strlen(last->value);
        joined = realloc(last->value, old + 1 + (len - start) + 1);
        if (!joined)
            return TC_ERR_NOMEM;
        joined[old] = ' ';
        memcpy(joined + old + 1, s + start, len - start);
        joined[old + 1 + len - start] = '\0';
        last->value = joined;
        return TC_OK;
    }
    colon = memchr(s, ':', len);
    if (!colon || colon == s)
        return TC_ERR_PROTOCOL;
    name_len = (size_t)(colon - s);
    while (name_len > 0 && is_space(s[name_len - 1]))
        name_len--;
    start = name_len + 1;
    while (start < len && (is_space(s[start]) || s[start] == ':'))
        start++;
    end = len;
    while (end > start && is_space(s[end - 1]))
        end--;
    if (r->headers.len >= TC_HTTP_MAX_HEADERS)
        return TC_ERR_PROTOCOL;
    return tc_headers_add(&r->headers, s, name_len, s + start, end - start);
}

static int lower(char c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* Does the comma-separated header value contain this token? */
static int has_token(const char *list, const char *token)
{
    size_t n = strlen(token);
    while (*list) {
        const char *end;
        size_t k, i;
        while (*list == ',' || is_space(*list))
            list++;
        end = list;
        while (*end && *end != ',')
            end++;
        k = (size_t)(end - list);
        while (k > 0 && is_space(list[k - 1]))
            k--;
        for (i = 0; i < k && i < n && lower(list[i]) == lower(token[i]); i++)
            ;
        if (k == n && i == n)
            return 1;
        list = end;
    }
    return 0;
}

/* Decide how the body is delimited, once the headers are in. */
static int start_body(tc_http_response *r)
{
    const char *te = tc_headers_get(&r->headers, "Transfer-Encoding");
    size_t i;

    r->headers_done = 1;
    if (r->no_body || (r->status >= 100 && r->status < 200) || r->status == 204 || r->status == 304) {
        r->state = ST_DONE;
        return TC_OK;
    }
    if (te && has_token(te, "chunked")) {
        r->state = ST_CHUNK_SIZE;
        return TC_OK;
    }
    for (i = 0; i < r->headers.len; i++) {
        const char *v;
        int64_t n = 0;
        if (!equal_nocase(r->headers.items[i].name, "Content-Length"))
            continue;
        v = r->headers.items[i].value;
        if (!*v)
            return TC_ERR_PROTOCOL;
        for (; *v; v++) {
            if (*v < '0' || *v > '9' || n > (INT64_MAX - 9) / 10)
                return TC_ERR_PROTOCOL;
            n = n * 10 + (*v - '0');
        }
        if (r->content_length >= 0 && r->content_length != n)
            return TC_ERR_PROTOCOL; /* conflicting lengths */
        r->content_length = n;
    }
    if (r->content_length >= 0) {
        r->remaining = (uint64_t)r->content_length;
        r->state = r->remaining ? ST_BODY_LENGTH : ST_DONE;
    } else {
        r->state = ST_BODY_UNTIL_CLOSE;
    }
    return TC_OK;
}

static int parse_chunk_size(tc_http_response *r, const char *s, size_t len)
{
    uint64_t size = 0;
    size_t i;
    for (i = 0; i < len && s[i] != ';' && !is_space(s[i]); i++) {
        int d;
        if (s[i] >= '0' && s[i] <= '9')
            d = s[i] - '0';
        else if ((s[i] | 0x20) >= 'a' && (s[i] | 0x20) <= 'f')
            d = (s[i] | 0x20) - 'a' + 10;
        else
            return TC_ERR_PROTOCOL;
        if (size >> 40)
            return TC_ERR_PROTOCOL;
        size = size * 16 + (uint64_t)d;
    }
    if (i == 0)
        return TC_ERR_PROTOCOL;
    r->remaining = size;
    r->state = size ? ST_CHUNK_DATA : ST_TRAILERS;
    return TC_OK;
}

/* A complete line (without CRLF) for the line-based states. */
static int handle_line(tc_http_response *r, const char *s, size_t len)
{
    switch (r->state) {
    case ST_STATUS:
        if (parse_status_line(r, s, len) != TC_OK)
            return TC_ERR_PROTOCOL;
        r->state = ST_HEADERS;
        return TC_OK;
    case ST_HEADERS:
        if (len > 0)
            return parse_header_line(r, s, len);
        if (r->status >= 100 && r->status < 200 && r->status != 101) {
            /* An interim response (100 Continue): the real one follows. */
            tc_headers_free(&r->headers);
            r->state = ST_STATUS;
            return TC_OK;
        }
        return start_body(r);
    case ST_CHUNK_SIZE:
        return parse_chunk_size(r, s, len);
    case ST_CHUNK_END:
        if (len != 0)
            return TC_ERR_PROTOCOL;
        r->state = ST_CHUNK_SIZE;
        return TC_OK;
    case ST_TRAILERS:
        if (len == 0)
            r->state = ST_DONE;
        return TC_OK; /* trailer fields are ignored */
    }
    return TC_ERR_PROTOCOL;
}

static int is_line_state(int state)
{
    return state == ST_STATUS || state == ST_HEADERS || state == ST_CHUNK_SIZE || state == ST_CHUNK_END ||
           state == ST_TRAILERS;
}

int tc_http_response_feed(tc_http_response *r, const char *data, size_t len, tc_buf *body, size_t *used)
{
    size_t i = 0;
    int err = TC_OK;

    while (i < len && r->state != ST_DONE && !err) {
        if (is_line_state(r->state)) {
            const char *nl = memchr(data + i, '\n', len - i);
            size_t n = nl ? (size_t)(nl - (data + i)) : len - i;
            if (r->line.len + n > TC_HTTP_MAX_LINE) {
                err = TC_ERR_PROTOCOL;
                break;
            }
            err = tc_buf_append(&r->line, data + i, n);
            i += n;
            if (!err && nl) {
                size_t line_len = r->line.len;
                i++; /* the '\n' */
                if (line_len && r->line.data[line_len - 1] == '\r')
                    line_len--;
                err = handle_line(r, tc_buf_cstr(&r->line), line_len);
                tc_buf_clear(&r->line);
            }
        } else if (r->state == ST_BODY_UNTIL_CLOSE) {
            err = tc_buf_append(body, data + i, len - i);
            i = len;
        } else {
            /* ST_BODY_LENGTH or ST_CHUNK_DATA */
            size_t n = (size_t)TC_MIN((uint64_t)(len - i), r->remaining);
            err = tc_buf_append(body, data + i, n);
            i += n;
            r->remaining -= n;
            if (!err && r->remaining == 0)
                r->state = r->state == ST_CHUNK_DATA ? ST_CHUNK_END : ST_DONE;
        }
    }
    if (r->state == ST_DONE)
        r->done = 1;
    if (used)
        *used = i;
    return err;
}

int tc_http_response_eof(tc_http_response *r)
{
    if (r->state == ST_BODY_UNTIL_CLOSE)
        r->state = ST_DONE;
    if (r->state == ST_DONE) {
        r->done = 1;
        return TC_OK;
    }
    return TC_ERR_CLOSED;
}
