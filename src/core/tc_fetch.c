#include "tc_fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_inflate.h"
#include "tc_url.h"
#include "tc_version.h"

#define DEFAULT_MAX_REDIRECTS 20
#define DEFAULT_MAX_BODY      (32u * 1024 * 1024)
#define DEFAULT_TIMEOUT_MS    30000

const char *tc_fetch_user_agent(void)
{
    return "Mozilla/5.0 (Windows 95) " TC_NAME "/" TC_VERSION;
}

void tc_fetch_options_init(tc_fetch_options *o)
{
    memset(o, 0, sizeof *o);
    o->max_redirects = DEFAULT_MAX_REDIRECTS;
    o->max_body = DEFAULT_MAX_BODY;
    o->timeout_ms = DEFAULT_TIMEOUT_MS;
}

void tc_response_free(tc_response *r)
{
    free(r->url);
    free(r->status_text);
    tc_headers_free(&r->headers);
    tc_buf_free(&r->body);
    r->url = r->status_text = NULL;
}

static void report(const tc_fetch_options *o, tc_fetch_phase phase, const char *host, size_t received,
                   int64_t total)
{
    if (o->progress)
        o->progress(o->progress_ctx, phase, host, received, total);
}

/* ---- Connections: a socket, with TLS on top for https ---- */

typedef struct conn {
    tc_socket *sock;
    tc_tls *tls;
} conn;

static int conn_send(conn *c, const char *data, size_t len, const tc_fetch_options *o)
{
    if (c->tls)
        return tc_tls_send(c->tls, data, len);
    while (len) {
        int n = tc_net_send(c->sock, data, len, o->timeout_ms, o->cancel);
        if (n < 0)
            return n;
        data += n;
        len -= (size_t)n;
    }
    return TC_OK;
}

static int conn_recv(conn *c, char *buf, size_t len, const tc_fetch_options *o)
{
    return c->tls ? tc_tls_recv(c->tls, buf, len) : tc_net_recv(c->sock, buf, len, o->timeout_ms, o->cancel);
}

static void conn_close(conn *c)
{
    tc_tls_close(c->tls);
    tc_net_close(c->sock);
    c->tls = NULL;
    c->sock = NULL;
}

/* Ask an HTTP proxy for a tunnel to host:port (for https). */
static int proxy_tunnel(conn *c, const char *host, int port, const tc_fetch_options *o, tc_response *r)
{
    tc_http_response reply;
    tc_buf req;
    char buf[2048];
    int err;

    tc_buf_init(&req);
    err = tc_buf_printf(&req, "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: %s\r\n\r\n", host, port, host,
                        port, tc_fetch_user_agent());
    err = err ? err : conn_send(c, tc_buf_cstr(&req), req.len, o);
    tc_buf_free(&req);
    if (err)
        return err;

    /* A successful CONNECT reply has no body; stop at the end of the headers. */
    tc_http_response_init(&reply, 1);
    while (!err && !reply.headers_done) {
        int n = conn_recv(c, buf, sizeof buf, o);
        if (n <= 0) {
            err = n < 0 ? n : TC_ERR_PROXY;
            break;
        }
        err = tc_http_response_feed(&reply, buf, (size_t)n, NULL, NULL);
    }
    if (!err && reply.status / 100 != 2) {
        snprintf(r->error_detail, sizeof r->error_detail, "The proxy refused the connection: %d %s",
                 reply.status, reply.reason ? reply.reason : "");
        err = TC_ERR_PROXY;
    }
    tc_http_response_free(&reply);
    return err == TC_ERR_PROTOCOL ? TC_ERR_PROXY : err;
}

static int open_connection(const tc_url *url, const tc_fetch_options *o, conn *c, tc_response *r)
{
    int https = strcmp(url->scheme, "https") == 0;
    int port = tc_url_effective_port(url);
    const char *target = o->proxy_host ? o->proxy_host : url->host;
    uint16_t target_port = o->proxy_host ? o->proxy_port : (uint16_t)port;
    uint8_t addr[4];
    int err;

    if (url->host[0] == '[') {
        snprintf(r->error_detail, sizeof r->error_detail,
                 "%s is an IPv6 address. Windows 95 only speaks IPv4.", url->host);
        return TC_ERR_UNSUPPORTED;
    }
    report(o, TC_FETCH_RESOLVING, target, 0, -1);
    err = tc_net_resolve(target, addr);
    if (err) {
        snprintf(r->error_detail, sizeof r->error_detail, "Couldn't find the server %s.", target);
        return err;
    }
    if (o->cancel && *o->cancel)
        return TC_ERR_CANCELLED;
    report(o, TC_FETCH_CONNECTING, target, 0, -1);
    err = tc_net_connect(addr, target_port, o->timeout_ms, o->cancel, &c->sock);
    if (err) {
        snprintf(r->error_detail, sizeof r->error_detail, "Couldn't connect to %s (%u.%u.%u.%u) on port %u.",
                 target, addr[0], addr[1], addr[2], addr[3], target_port);
        return err;
    }
    if (https && o->proxy_host) {
        err = proxy_tunnel(c, url->host, port, o, r);
        if (err)
            return err;
    }
    if (https) {
        report(o, TC_FETCH_SECURING, url->host, 0, -1);
        err = tc_tls_connect(c->sock, url->host, o->timeout_ms, o->cancel, &c->tls, &r->tls);
        if (err == TC_ERR_CERT)
            tc_tls_describe_cert_error(r->tls.cert_flags, r->error_detail, sizeof r->error_detail);
        else if (err == TC_ERR_TLS)
            snprintf(r->error_detail, sizeof r->error_detail, "%s", r->tls.detail);
    }
    return err;
}

static int add_header(tc_headers *h, const char *name, const char *value)
{
    return tc_headers_add(h, name, strlen(name), value, strlen(value));
}

/* One request and its response. The body stays content-encoded. */
static int load_once(const tc_url *url, const tc_fetch_options *o, tc_response *r, tc_buf *raw)
{
    conn c = { NULL, NULL };
    tc_http_response reply;
    tc_headers extra;
    tc_buf req;
    char buf[16384];
    int err;

    tc_buf_clear(raw);
    tc_buf_init(&req);
    tc_headers_init(&extra);
    tc_http_response_init(&reply, 0);

    err = open_connection(url, o, &c, r);
    if (!err) {
        err = add_header(&extra, "User-Agent", o->user_agent ? o->user_agent : tc_fetch_user_agent());
        err = err ? err : add_header(&extra, "Accept", "text/html,application/xhtml+xml,*/*;q=0.8");
        err = err ? err : add_header(&extra, "Accept-Language", "en");
        err = err ? err : add_header(&extra, "Accept-Encoding", "gzip, deflate");
        err = err ? err : add_header(&extra, "Connection", "close");
        err = err ? err : tc_http_build_request(&req, "GET", url, o->proxy_host && !c.tls, &extra);
        err = err ? err : conn_send(&c, tc_buf_cstr(&req), req.len, o);
    }
    if (!err) {
        report(o, TC_FETCH_WAITING, url->host, 0, -1);
        for (;;) {
            int n = conn_recv(&c, buf, sizeof buf, o);
            if (n < 0) {
                err = n;
                break;
            }
            if (n == 0) {
                err = tc_http_response_eof(&reply);
                break;
            }
            err = tc_http_response_feed(&reply, buf, (size_t)n, raw, NULL);
            if (!err && raw->len > o->max_body)
                err = TC_ERR_TOO_LARGE;
            if (err)
                break;
            if (reply.headers_done)
                report(o, TC_FETCH_RECEIVING, url->host, raw->len, reply.content_length);
            if (reply.done)
                break;
        }
        if (err == TC_ERR_PROTOCOL)
            snprintf(r->error_detail, sizeof r->error_detail, "%s sent a response Tachyon can't understand.",
                     url->host);
        else if (err == TC_ERR_CLOSED)
            snprintf(r->error_detail, sizeof r->error_detail, "%s closed the connection before the page was complete.",
                     url->host);
    }
    if (reply.headers_done) {
        free(r->status_text);
        tc_headers_free(&r->headers);
        r->status = reply.status;
        r->status_text = reply.reason;
        r->minor_version = reply.minor_version;
        r->headers = reply.headers;
        reply.reason = NULL;
        tc_headers_init(&reply.headers);
    }
    tc_http_response_free(&reply);
    tc_headers_free(&extra);
    tc_buf_free(&req);
    conn_close(&c);
    return err;
}

static int is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static int set_url(tc_response *r, const char *href)
{
    size_t n = strlen(href) + 1;
    free(r->url);
    r->url = malloc(n);
    if (!r->url)
        return TC_ERR_NOMEM;
    memcpy(r->url, href, n);
    return TC_OK;
}

int tc_fetch(const char *url_text, const tc_fetch_options *opts, tc_response *r)
{
    tc_fetch_options defaults;
    tc_url url;
    tc_buf raw;
    uint32_t start = tc_time_ms();
    int err;

    memset(r, 0, sizeof *r);
    tc_headers_init(&r->headers);
    tc_buf_init(&r->body);
    if (!opts) {
        tc_fetch_options_init(&defaults);
        opts = &defaults;
    }
    err = tc_url_parse(url_text, NULL, &url);
    if (err)
        return err;
    tc_buf_init(&raw);

    for (;;) {
        const char *location;
        err = set_url(r, url.href);
        if (err)
            break;
        if (strcmp(url.scheme, "http") != 0 && strcmp(url.scheme, "https") != 0) {
            snprintf(r->error_detail, sizeof r->error_detail, "Tachyon can't load %s: addresses yet.", url.scheme);
            err = TC_ERR_UNSUPPORTED;
            break;
        }
        err = load_once(&url, opts, r, &raw);
        if (err)
            break;
        location = tc_headers_get(&r->headers, "Location");
        if (is_redirect(r->status) && location) {
            tc_url next;
            if (r->redirects >= opts->max_redirects) {
                err = TC_ERR_REDIRECTS;
                break;
            }
            if (tc_url_parse(location, &url, &next) != TC_OK) {
                snprintf(r->error_detail, sizeof r->error_detail, "The server redirected to an invalid address.");
                err = TC_ERR_PROTOCOL;
                break;
            }
            tc_url_free(&url);
            url = next;
            r->redirects++;
            report(opts, TC_FETCH_REDIRECTING, url.host ? url.host : "", 0, -1);
            continue;
        }
        err = tc_inflate(tc_content_coding_from_header(tc_headers_get(&r->headers, "Content-Encoding")), raw.data,
                         raw.len, opts->max_body, &r->body);
        if (err == TC_ERR_DECODE)
            snprintf(r->error_detail, sizeof r->error_detail, "The page's compression is unsupported or corrupt.");
        break;
    }
    tc_buf_free(&raw);
    tc_url_free(&url);
    r->elapsed_ms = tc_time_ms() - start;
    return err;
}
