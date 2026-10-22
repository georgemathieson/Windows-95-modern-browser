/* Tests for HTTP parsing, content decoding, UTF-8 and TLS setup: everything
 * on the way to a page that doesn't need a network. */
#include "tc_test.h"

#include <stdlib.h>

#include "tc_http.h"
#include "tc_inflate.h"
#include "tc_tls.h"
#include "tc_utf8.h"

void tc_suite_net(void);

/* Compressed forms of TEXT, made with Python's gzip and zlib modules. */
#define TEXT_REPEAT "Tachyon: the modern web, sent back to 1995. "
static const unsigned char gzip_text[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x0b, 0x49, 0x4c, 0xce, 0xa8, 0xcc,
    0xcf, 0xb3, 0x52, 0x28, 0xc9, 0x48, 0x55, 0xc8, 0xcd, 0x4f, 0x49, 0x2d, 0xca, 0x53, 0x28, 0x4f,
    0x4d, 0xd2, 0x51, 0x28, 0x4e, 0xcd, 0x2b, 0x51, 0x48, 0x4a, 0x4c, 0xce, 0x56, 0x28, 0xc9, 0x57,
    0x30, 0xb4, 0xb4, 0x34, 0xd5, 0x53, 0x08, 0x19, 0x55, 0x3b, 0xaa, 0x76, 0x54, 0x2d, 0x45, 0x6a,
    0x01, 0x8d, 0xd2, 0x83, 0x7d, 0x70, 0x03, 0x00, 0x00,
};
static const unsigned char gzip_two_members[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x4b, 0xcb, 0x2c, 0x2a, 0x2e, 0x51,
    0xc8, 0x4d, 0xcd, 0x4d, 0x4a, 0x2d, 0xd2, 0x51, 0x00, 0x00, 0xde, 0xff, 0x0a, 0x62, 0x0e, 0x00,
    0x00, 0x00, 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x2b, 0x4e, 0x4d, 0xce,
    0xcf, 0x4b, 0x51, 0xc8, 0x4d, 0xcd, 0x4d, 0x4a, 0x2d, 0x02, 0x00, 0x24, 0x74, 0xfa, 0x9f, 0x0d,
    0x00, 0x00, 0x00,
};
static const unsigned char zlib_text[] = {
    0x78, 0x9c, 0x0b, 0x49, 0x4c, 0xce, 0xa8, 0xcc, 0xcf, 0xb3, 0x52, 0x28, 0xc9, 0x48, 0x55, 0xc8,
    0xcd, 0x4f, 0x49, 0x2d, 0xca, 0x53, 0x28, 0x4f, 0x4d, 0xd2, 0x51, 0x28, 0x4e, 0xcd, 0x2b, 0x51,
    0x48, 0x4a, 0x4c, 0xce, 0x56, 0x28, 0xc9, 0x57, 0x30, 0xb4, 0xb4, 0x34, 0xd5, 0x53, 0x08, 0x19,
    0x55, 0x3b, 0xaa, 0x76, 0x54, 0x2d, 0x45, 0x6a, 0x01, 0x7f, 0x6d, 0x21, 0x20,
};
static const unsigned char raw_deflate_text[] = {
    0x0b, 0x49, 0x4c, 0xce, 0xa8, 0xcc, 0xcf, 0xb3, 0x52, 0x28, 0xc9, 0x48, 0x55, 0xc8, 0xcd, 0x4f,
    0x49, 0x2d, 0xca, 0x53, 0x28, 0x4f, 0x4d, 0xd2, 0x51, 0x28, 0x4e, 0xcd, 0x2b, 0x51, 0x48, 0x4a,
    0x4c, 0xce, 0x56, 0x28, 0xc9, 0x57, 0x30, 0xb4, 0xb4, 0x34, 0xd5, 0x53, 0x08, 0x19, 0x55, 0x3b,
    0xaa, 0x76, 0x54, 0x2d, 0x45, 0x6a, 0x01,
};

/* TEXT_REPEAT twenty times, Brotli-compressed (Python's brotli module). */
static const unsigned char br_text[] = {
    0x1b, 0x6f, 0x03, 0xe0, 0x15, 0xa1, 0xd2, 0xdd, 0xc6, 0xee, 0xf7, 0x6c,
    0xc7, 0x51, 0x09, 0x5a, 0x35, 0x23, 0xd8, 0x80, 0x03, 0xf6, 0x20, 0x81,
    0x80, 0x68, 0x01, 0xb9, 0x6c, 0xb8, 0x2c, 0xad, 0x62, 0x83, 0x4a, 0xff,
    0x0e, 0x46, 0xb6, 0x2b, 0xb3, 0x2a, 0x43, 0xf1, 0xeb, 0xdf, 0x1f, 0x00,
};

static int text_matches(const tc_buf *b)
{
    size_t i, n = strlen(TEXT_REPEAT);
    if (b->len != n * 20)
        return 0;
    for (i = 0; i < 20; i++)
        if (memcmp(b->data + i * n, TEXT_REPEAT, n) != 0)
            return 0;
    return 1;
}

/* Feed a response one byte at a time, the hardest split there is. */
static int feed_bytewise(tc_http_response *r, const char *msg, tc_buf *body)
{
    size_t i, len = strlen(msg);
    for (i = 0; i < len && !r->done; i++) {
        int err = tc_http_response_feed(r, msg + i, 1, body, NULL);
        if (err)
            return err;
    }
    return TC_OK;
}

static void test_content_length_response(void)
{
    const char *msg = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nhelloEXTRA";
    tc_http_response r;
    tc_buf body;
    size_t used;

    tc_buf_init(&body);
    tc_http_response_init(&r, 0);
    TC_CHECK_INT(tc_http_response_feed(&r, msg, strlen(msg), &body, &used), TC_OK);
    TC_CHECK(r.done);
    TC_CHECK_INT(r.status, 200);
    TC_CHECK_STR(r.reason, "OK");
    TC_CHECK_STR(tc_headers_get(&r.headers, "content-type"), "text/html");
    TC_CHECK_STR(tc_buf_cstr(&body), "hello");
    TC_CHECK_INT(used, strlen(msg) - 5); /* stops at the end of the message */
    tc_http_response_free(&r);
    tc_buf_free(&body);
}

static void test_chunked_response_bytewise(void)
{
    const char *msg = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, Chunked\r\n\r\n"
                      "5;ext=1\r\nHello\r\n7\r\n, 1995!\r\n0\r\nTrailer: x\r\n\r\n";
    tc_http_response r;
    tc_buf body;

    tc_buf_init(&body);
    tc_http_response_init(&r, 0);
    TC_CHECK_INT(feed_bytewise(&r, msg, &body), TC_OK);
    TC_CHECK(r.done);
    TC_CHECK_STR(tc_buf_cstr(&body), "Hello, 1995!");
    tc_http_response_free(&r);
    tc_buf_free(&body);
}

static void test_body_until_close(void)
{
    const char *msg = "HTTP/1.0 200 OK\nServer: old\n\nall of this";
    tc_http_response r;
    tc_buf body;

    tc_buf_init(&body);
    tc_http_response_init(&r, 0);
    TC_CHECK_INT(tc_http_response_feed(&r, msg, strlen(msg), &body, NULL), TC_OK);
    TC_CHECK(!r.done);
    TC_CHECK_INT(r.minor_version, 0);
    TC_CHECK_INT(tc_http_response_eof(&r), TC_OK);
    TC_CHECK(r.done);
    TC_CHECK_STR(tc_buf_cstr(&body), "all of this");
    tc_http_response_free(&r);
    tc_buf_free(&body);
}

static void test_interim_and_empty_responses(void)
{
    const char *msg = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 204 No Content\r\nX-A: 1\r\n\r\n";
    tc_http_response r;
    tc_buf body;

    tc_buf_init(&body);
    tc_http_response_init(&r, 0);
    TC_CHECK_INT(tc_http_response_feed(&r, msg, strlen(msg), &body, NULL), TC_OK);
    TC_CHECK(r.done);
    TC_CHECK_INT(r.status, 204);
    TC_CHECK_INT(r.headers.len, 1);
    tc_http_response_free(&r);
    tc_buf_free(&body);
}

static void test_folded_header(void)
{
    const char *msg = "HTTP/1.1 200 OK\r\nX-Long: one\r\n  two\r\nContent-Length: 0\r\n\r\n";
    tc_http_response r;
    tc_http_response_init(&r, 0);
    TC_CHECK_INT(tc_http_response_feed(&r, msg, strlen(msg), NULL, NULL), TC_OK);
    TC_CHECK_STR(tc_headers_get(&r.headers, "X-Long"), "one two");
    tc_http_response_free(&r);
}

static void test_malformed_responses(void)
{
    static const char *bad[] = {
        "HTTP/2 200 OK\r\n\r\n",
        "SSH-2.0-OpenSSH\r\n",
        "HTTP/1.1 2000 OK\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n",
        "HTTP/1.1 200 OK\r\nno colon here\r\n\r\n",
    };
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(bad); i++) {
        tc_http_response r;
        tc_buf body;
        tc_buf_init(&body);
        tc_http_response_init(&r, 0);
        TC_CHECK_INT(tc_http_response_feed(&r, bad[i], strlen(bad[i]), &body, NULL), TC_ERR_PROTOCOL);
        tc_http_response_free(&r);
        tc_buf_free(&body);
    }
}

static void test_truncated_body_is_an_error(void)
{
    const char *msg = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort";
    tc_http_response r;
    tc_buf body;
    tc_buf_init(&body);
    tc_http_response_init(&r, 0);
    TC_CHECK_INT(tc_http_response_feed(&r, msg, strlen(msg), &body, NULL), TC_OK);
    TC_CHECK_INT(tc_http_response_eof(&r), TC_ERR_CLOSED);
    tc_http_response_free(&r);
    tc_buf_free(&body);
}

static void test_build_request(void)
{
    tc_url url;
    tc_buf req;
    tc_headers extra;

    tc_buf_init(&req);
    tc_headers_init(&extra);
    tc_headers_add(&extra, "Accept", 6, "*/*", 3);
    TC_CHECK_INT(tc_url_parse("http://example.com:8080/a b?q#frag", NULL, &url), TC_OK);
    TC_CHECK_INT(tc_http_build_request(&req, "GET", &url, 0, &extra), TC_OK);
    TC_CHECK_STR(tc_buf_cstr(&req), "GET /a%20b?q HTTP/1.1\r\nHost: example.com:8080\r\nAccept: */*\r\n\r\n");
    tc_buf_clear(&req);
    TC_CHECK_INT(tc_http_build_request(&req, "GET", &url, 1, NULL), TC_OK);
    TC_CHECK_STR(tc_buf_cstr(&req), "GET http://example.com:8080/a%20b?q HTTP/1.1\r\nHost: example.com:8080\r\n\r\n");
    tc_url_free(&url);
    tc_headers_free(&extra);
    tc_buf_free(&req);
}

static void test_inflate_formats(void)
{
    tc_buf out;
    tc_buf_init(&out);
    TC_CHECK_INT(tc_inflate(TC_CODING_GZIP, gzip_text, sizeof gzip_text, 1 << 20, &out), TC_OK);
    TC_CHECK(text_matches(&out));
    tc_buf_clear(&out);
    TC_CHECK_INT(tc_inflate(TC_CODING_DEFLATE, zlib_text, sizeof zlib_text, 1 << 20, &out), TC_OK);
    TC_CHECK(text_matches(&out));
    tc_buf_clear(&out);
    /* "deflate" that is really raw deflate, as some servers send. */
    TC_CHECK_INT(tc_inflate(TC_CODING_DEFLATE, raw_deflate_text, sizeof raw_deflate_text, 1 << 20, &out), TC_OK);
    TC_CHECK(text_matches(&out));
    tc_buf_clear(&out);
    TC_CHECK_INT(tc_inflate(TC_CODING_GZIP, gzip_two_members, sizeof gzip_two_members, 1 << 20, &out), TC_OK);
    TC_CHECK_STR(tc_buf_cstr(&out), "first member, second member");
    tc_buf_clear(&out);
    TC_CHECK_INT(tc_inflate(TC_CODING_BR, br_text, sizeof br_text, 1 << 20, &out), TC_OK);
    TC_CHECK(text_matches(&out));
    tc_buf_free(&out);
}

static void test_inflate_limits_and_errors(void)
{
    tc_buf out;
    static const unsigned char junk[] = { 0x1f, 0x8b, 0x08, 0x00, 0xde, 0xad, 0xbe, 0xef, 0x00, 0x03, 0xff, 0xff };
    tc_buf_init(&out);
    TC_CHECK_INT(tc_inflate(TC_CODING_GZIP, gzip_text, sizeof gzip_text, 100, &out), TC_ERR_TOO_LARGE);
    tc_buf_clear(&out);
    TC_CHECK_INT(tc_inflate(TC_CODING_GZIP, junk, sizeof junk, 1 << 20, &out), TC_ERR_DECODE);
    TC_CHECK_INT(tc_content_coding_from_header("GZIP"), TC_CODING_GZIP);
    TC_CHECK_INT(tc_content_coding_from_header(" br"), TC_CODING_BR);
    TC_CHECK_INT(tc_content_coding_from_header("zstd"), TC_CODING_UNKNOWN);
    /* Brotli: too big, corrupt, and cut short (what there is). */
    TC_CHECK_INT(tc_inflate(TC_CODING_BR, br_text, sizeof br_text, 100, &out), TC_ERR_TOO_LARGE);
    tc_buf_clear(&out);
    TC_CHECK_INT(tc_inflate(TC_CODING_BR, junk, sizeof junk, 1 << 20, &out), TC_ERR_DECODE);
    tc_buf_clear(&out);
    TC_CHECK_INT(tc_inflate(TC_CODING_BR, br_text, sizeof br_text - 4, 1 << 20, &out), TC_OK);
    TC_CHECK(out.len < strlen(TEXT_REPEAT) * 20);
    TC_CHECK_INT(tc_content_coding_from_header(NULL), TC_CODING_IDENTITY);
    tc_buf_free(&out);
}

static void test_utf8(void)
{
    uint32_t cp;
    char enc[4];
    TC_CHECK_INT(tc_utf8_decode("\xC3\xA9", 2, &cp), 2);
    TC_CHECK_INT(cp, 0xE9);
    TC_CHECK_INT(tc_utf8_decode("\xF0\x9F\x98\x80", 4, &cp), 4);
    TC_CHECK_INT(cp, 0x1F600);
    TC_CHECK_INT(tc_utf8_decode("\xE2\x82", 2, &cp), 2); /* truncated */
    TC_CHECK_INT(cp, TC_REPLACEMENT_CHAR);
    TC_CHECK_INT(tc_utf8_decode("\xED\xA0\x80", 3, &cp), 1); /* surrogate */
    TC_CHECK_INT(cp, TC_REPLACEMENT_CHAR);
    TC_CHECK_INT(tc_utf8_decode("\xC0\xAF", 2, &cp), 1); /* overlong */
    TC_CHECK_INT(tc_utf8_encode(0x20AC, enc), 3);
    TC_CHECK(memcmp(enc, "\xE2\x82\xAC", 3) == 0);
}

static void test_tls_setup(void)
{
    char why[160];
    TC_CHECK_INT(tc_tls_init(), TC_OK);
    TC_CHECK(tc_tls_trusted_count() >= 100);
    tc_tls_describe_cert_error(0x01 /* expired */, why, sizeof why);
    TC_CHECK(strstr(why, "expired") != NULL);
}

void tc_suite_net(void)
{
    TC_RUN(test_content_length_response);
    TC_RUN(test_chunked_response_bytewise);
    TC_RUN(test_body_until_close);
    TC_RUN(test_interim_and_empty_responses);
    TC_RUN(test_folded_header);
    TC_RUN(test_malformed_responses);
    TC_RUN(test_truncated_body_is_an_error);
    TC_RUN(test_build_request);
    TC_RUN(test_inflate_formats);
    TC_RUN(test_inflate_limits_and_errors);
    TC_RUN(test_utf8);
    TC_RUN(test_tls_setup);
}
