/* Loading a URL over HTTP or HTTPS: the network side of Tachyon.
 *
 * tc_fetch() blocks, so the browser runs it on a worker thread and watches
 * the progress callback; setting *cancel stops it within a fraction of a
 * second (except during a DNS lookup, which Windows can't interrupt).
 *
 * Given a connection pool, connections are kept open after a response and
 * used again for the next request to the same server (HTTP keep-alive), which
 * saves a TLS handshake: seconds on a Pentium. Given a cache, responses are
 * kept in memory and served again (decision 30). */
#ifndef TC_FETCH_H
#define TC_FETCH_H

#include "tc_buf.h"
#include "tc_http.h"
#include "tc_tls.h"

typedef enum tc_fetch_phase {
    TC_FETCH_RESOLVING,   /* looking up the host name */
    TC_FETCH_CONNECTING,
    TC_FETCH_SECURING,    /* TLS handshake */
    TC_FETCH_WAITING,     /* request sent, waiting for the response */
    TC_FETCH_RECEIVING,
    TC_FETCH_REDIRECTING
} tc_fetch_phase;

typedef struct tc_conn_pool tc_conn_pool;
typedef struct tc_cache tc_cache;

typedef struct tc_fetch_options {
    const char *proxy_host;  /* an HTTP proxy, or NULL to connect directly */
    uint16_t proxy_port;
    int max_redirects;       /* default 20 */
    size_t max_body;         /* bytes after decoding; default 32 MB */
    uint32_t timeout_ms;     /* per network operation; default 30 s */
    const char *user_agent;  /* default: Tachyon's own */
    const volatile int *cancel;
    /* Called from the fetching thread. total is -1 when unknown. */
    void (*progress)(void *ctx, tc_fetch_phase phase, const char *host, size_t received, int64_t total);
    void *progress_ctx;
    tc_conn_pool *pool;      /* keep connections open in here, or NULL */
    tc_cache *cache;         /* serve and keep responses here, or NULL */
    int reload;              /* with a cache: fetch anew (and keep the new response) */
    const char *accept;      /* the Accept header; default for pages */
} tc_fetch_options;

typedef struct tc_response {
    char *url;              /* the final URL, after redirects */
    int status;             /* e.g. 200; 0 if no response arrived */
    char *status_text;      /* e.g. "OK" */
    int minor_version;
    tc_headers headers;
    tc_buf body;            /* decoded (gzip/deflate removed) */
    int redirects;
    uint32_t elapsed_ms;
    tc_tls_info tls;        /* version and cipher for https, or why TLS failed */
    char error_detail[256]; /* more about a failure, for error pages */
    int cached;             /* served from the cache */
} tc_response;

void tc_fetch_options_init(tc_fetch_options *o);

/* Load url (GET). The response is filled in as far as it got, even on
   failure. Free it with tc_response_free either way. */
int tc_fetch(const char *url, const tc_fetch_options *opts, tc_response *out);
void tc_response_free(tc_response *r);

/* Connections kept open between requests, for any number of threads. */
tc_conn_pool *tc_conn_pool_new(void);
void tc_conn_pool_free(tc_conn_pool *p);
/* How many connections have been opened, and used again (statistics). */
void tc_conn_pool_stats(tc_conn_pool *p, unsigned *opened, unsigned *reused);

/* Responses kept in memory, up to max_bytes of bodies, the least recently
   used dropped first. Only successful (200) GET responses that don't say
   no-store are kept. For any number of threads. */
tc_cache *tc_cache_new(size_t max_bytes);
void tc_cache_free(tc_cache *c);
void tc_cache_stats(tc_cache *c, unsigned *hits, unsigned *misses);

/* The User-Agent header Tachyon sends. */
const char *tc_fetch_user_agent(void);

#endif
