/* URLs, parsed the way the WHATWG URL Standard says browsers must.
 *
 *   tc_url base, url;
 *   tc_url_parse("https://example.com/a/b", NULL, &base);
 *   tc_url_parse("../c?x#y", &base, &url);   // https://example.com/c?x#y
 *   ...
 *   tc_url_free(&url);
 *   tc_url_free(&base);
 *
 * Tested against web-platform-tests' urltestdata.json. Known gap: full
 * internationalised domain name mapping (UTS #46); the hosts it affects are
 * listed in tests/data/url_known_failures.txt. */
#ifndef TC_URL_H
#define TC_URL_H

#include "tc_buf.h"

typedef struct tc_url {
    char *href;      /* the whole URL, serialized */
    char *scheme;    /* lowercase, without ':' */
    char *username;  /* percent-encoded; "" when absent */
    char *password;  /* percent-encoded; "" when absent */
    char *host;      /* serialized host (IPv6 in brackets); NULL when the URL has none */
    int port;        /* -1 when absent or the scheme's default */
    char *path;      /* serialized path, e.g. "/a/b"; for an opaque path, the whole of it */
    char *query;     /* without '?'; NULL when absent */
    char *fragment;  /* without '#'; NULL when absent */
    int opaque_path; /* e.g. "mailto:someone@example.com" */
} tc_url;

/* Parse input against an optional base URL. TC_ERR_URL if it isn't a valid
   URL, TC_ERR_NOMEM. On failure *out is zeroed and needs no tc_url_free. */
int tc_url_parse(const char *input, const tc_url *base, tc_url *out);
/* The same for input that may contain NUL bytes. */
int tc_url_parse_n(const char *input, size_t len, const tc_url *base, tc_url *out);

void tc_url_free(tc_url *url);
int tc_url_copy(const tc_url *src, tc_url *dst);

/* http, https, ws, wss, ftp and file. */
int tc_url_is_special(const tc_url *url);
/* The port to connect to: the explicit one, or the scheme's default. -1 if
   the scheme has none. */
int tc_url_effective_port(const tc_url *url);
/* Default port for a scheme, or -1. */
int tc_url_default_port(const char *scheme);

/* Path plus "?query", as sent in an HTTP request line. */
int tc_url_request_target(const tc_url *url, tc_buf *out);

/* Host parsing (tc_url_host.c), exposed for tests. Writes the serialized
   host: a lowercase ASCII domain, a dotted IPv4 address, "[ipv6]" or an
   opaque host. opaque is non-zero for non-special schemes. */
int tc_url_parse_host(const char *input, size_t len, int opaque, tc_buf *out);

#endif
