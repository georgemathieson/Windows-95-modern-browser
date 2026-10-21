/* HTTP/1.1 messages, without any I/O: building requests and parsing
 * responses from whatever bytes arrive (RFC 9112).
 *
 *   tc_http_response r;
 *   tc_http_response_init(&r, 0);
 *   while (!r.done && (n = read(...)) > 0)
 *       if (tc_http_response_feed(&r, data, n, &body) != TC_OK) ...;
 *   if (!r.done) tc_http_response_eof(&r);   // connection closed
 *
 * Bodies come out de-chunked but still content-encoded (see tc_inflate.h). */
#ifndef TC_HTTP_H
#define TC_HTTP_H

#include "tc_buf.h"
#include "tc_url.h"

typedef struct tc_header {
    char *name;   /* as sent; compare case-insensitively */
    char *value;  /* surrounding whitespace removed */
} tc_header;

typedef struct tc_headers {
    tc_header *items;
    size_t len, cap;
} tc_headers;

void tc_headers_init(tc_headers *h);
void tc_headers_free(tc_headers *h);
int tc_headers_add(tc_headers *h, const char *name, size_t name_len, const char *value, size_t value_len);
/* The first value of a header, or NULL. */
const char *tc_headers_get(const tc_headers *h, const char *name);

/* Append a request to out. absolute_form: the whole URL as the target, for
   plain HTTP through a proxy. extra may be NULL. */
int tc_http_build_request(tc_buf *out, const char *method, const tc_url *url, int absolute_form,
                          const tc_headers *extra);

/* Limits that protect against hostile or broken servers. */
#define TC_HTTP_MAX_LINE    (64 * 1024)
#define TC_HTTP_MAX_HEADERS 256

typedef struct tc_http_response {
    int status;             /* e.g. 200 */
    char *reason;           /* e.g. "OK" */
    int minor_version;      /* 1 for HTTP/1.1, 0 for HTTP/1.0 */
    tc_headers headers;
    int done;               /* the whole message has been parsed */
    int headers_done;       /* status line and headers are complete */
    int64_t content_length; /* -1 when not given */

    /* Parser state. */
    int state;
    int no_body;            /* response to HEAD or CONNECT */
    tc_buf line;
    uint64_t remaining;
} tc_http_response;

/* no_body: the request was HEAD, or a CONNECT whose 2xx reply has no body. */
void tc_http_response_init(tc_http_response *r, int no_body);
void tc_http_response_free(tc_http_response *r);
/* Parse more bytes; body bytes are appended to body. Stops consuming at the
   end of the message (*used tells how far it got; may be NULL). Returns
   TC_ERR_PROTOCOL for a malformed message. */
int tc_http_response_feed(tc_http_response *r, const char *data, size_t len, tc_buf *body,
                          size_t *used);
/* The connection was closed. TC_OK if that properly ends the message (a body
   delimited by closing), else TC_ERR_CLOSED. */
int tc_http_response_eof(tc_http_response *r);

#endif
