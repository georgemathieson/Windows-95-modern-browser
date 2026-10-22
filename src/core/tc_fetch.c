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

/* ---- Keeping connections open ---- */

#define POOL_SIZE    8
#define POOL_IDLE_MS 45000 /* servers close idle connections after a minute or so */

typedef struct pooled {
    char key[320];
    conn c;
    tc_tls_info info;
    uint32_t since;
    int used;
} pooled;

struct tc_conn_pool {
    tc_mutex *lock;
    pooled slots[POOL_SIZE];
    unsigned opened, reused;
};

tc_conn_pool *tc_conn_pool_new(void)
{
    tc_conn_pool *p = calloc(1, sizeof *p);
    if (p && !(p->lock = tc_mutex_create())) {
        free(p);
        p = NULL;
    }
    return p;
}

void tc_conn_pool_free(tc_conn_pool *p)
{
    int i;
    if (!p)
        return;
    for (i = 0; i < POOL_SIZE; i++)
        if (p->slots[i].used)
            conn_close(&p->slots[i].c);
    tc_mutex_destroy(p->lock);
    free(p);
}

void tc_conn_pool_stats(tc_conn_pool *p, unsigned *opened, unsigned *reused)
{
    tc_mutex_lock(p->lock);
    *opened = p->opened;
    *reused = p->reused;
    tc_mutex_unlock(p->lock);
}

static void pool_key(const tc_url *url, const tc_fetch_options *o, char *key, size_t size)
{
    snprintf(key, size, "%s://%s:%d|%s:%u", url->scheme, url->host, tc_url_effective_port(url),
             o->proxy_host ? o->proxy_host : "", (unsigned)o->proxy_port);
}

/* An open connection to the same place, if there is one. */
static int pool_take(tc_conn_pool *p, const char *key, conn *c, tc_tls_info *info)
{
    uint32_t now = tc_time_ms();
    int i, best = -1;
    tc_mutex_lock(p->lock);
    for (i = 0; i < POOL_SIZE; i++) {
        pooled *s = &p->slots[i];
        if (!s->used)
            continue;
        if (now - s->since > POOL_IDLE_MS) {
            conn_close(&s->c);
            s->used = 0;
        } else if (strcmp(s->key, key) == 0 && (best < 0 || s->since > p->slots[best].since)) {
            best = i;
        }
    }
    if (best >= 0) {
        *c = p->slots[best].c;
        *info = p->slots[best].info;
        p->slots[best].used = 0;
        p->reused++;
    }
    tc_mutex_unlock(p->lock);
    return best >= 0;
}

static void pool_give(tc_conn_pool *p, const char *key, conn *c, const tc_tls_info *info)
{
    int i, slot = 0;
    tc_mutex_lock(p->lock);
    for (i = 0; i < POOL_SIZE; i++) {
        if (!p->slots[i].used) {
            slot = i;
            break;
        }
        if (p->slots[i].since < p->slots[slot].since)
            slot = i;
    }
    if (p->slots[slot].used)
        conn_close(&p->slots[slot].c);
    snprintf(p->slots[slot].key, sizeof p->slots[slot].key, "%s", key);
    p->slots[slot].c = *c;
    p->slots[slot].info = *info;
    p->slots[slot].since = tc_time_ms();
    p->slots[slot].used = 1;
    tc_mutex_unlock(p->lock);
}

/* Whether the server lets the connection stay open after this response. */
static int keeps_alive(const tc_headers *headers, int minor_version)
{
    const char *conn_header = tc_headers_get(headers, "Connection");
    if (conn_header && strstr(conn_header, "close"))
        return 0;
    return minor_version >= 1 || (conn_header && strstr(conn_header, "keep-alive"));
}

/* ---- Requests ---- */

/* One request and its response. The body stays content-encoded. */
static int load_once(const tc_url *url, const tc_fetch_options *o, tc_response *r, tc_buf *raw)
{
    char key[320];
    int attempt, err = TC_OK;

    pool_key(url, o, key, sizeof key);
    for (attempt = 0; attempt < 2; attempt++) {
        conn c = { NULL, NULL };
        tc_http_response reply;
        tc_headers extra;
        tc_buf req;
        char buf[16384];
        int reused = 0, received = 0;

        tc_buf_clear(raw);
        tc_buf_init(&req);
        tc_headers_init(&extra);
        tc_http_response_init(&reply, 0);
        if (o->pool && attempt == 0 && pool_take(o->pool, key, &c, &r->tls)) {
            reused = 1;
            if (c.tls)
                tc_tls_set_io(c.tls, o->timeout_ms, o->cancel);
        } else {
            err = open_connection(url, o, &c, r);
            if (o->pool && !err) {
                tc_mutex_lock(o->pool->lock);
                o->pool->opened++;
                tc_mutex_unlock(o->pool->lock);
            }
        }
        if (!err) {
            err = add_header(&extra, "User-Agent", o->user_agent ? o->user_agent : tc_fetch_user_agent());
            err = err ? err
                      : add_header(&extra, "Accept", o->accept ? o->accept : "text/html,application/xhtml+xml,*/*;q=0.8");
            err = err ? err : add_header(&extra, "Accept-Language", "en");
            err = err ? err : add_header(&extra, "Accept-Encoding", "gzip, deflate");
            err = err ? err : add_header(&extra, "Connection", o->pool ? "keep-alive" : "close");
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
                    err = received ? tc_http_response_eof(&reply) : TC_ERR_CLOSED;
                    break;
                }
                received = 1;
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
        }
        /* A kept connection the server has since closed: try a new one. */
        if (reused && err && !received && err != TC_ERR_CANCELLED) {
            conn_close(&c);
            tc_http_response_free(&reply);
            tc_headers_free(&extra);
            tc_buf_free(&req);
            err = TC_OK;
            continue;
        }
        if (err == TC_ERR_PROTOCOL)
            snprintf(r->error_detail, sizeof r->error_detail, "%s sent a response Tachyon can't understand.",
                     url->host);
        else if (err == TC_ERR_CLOSED)
            snprintf(r->error_detail, sizeof r->error_detail,
                     "%s closed the connection before the page was complete.", url->host);
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
        if (o->pool && !err && reply.done && keeps_alive(&r->headers, r->minor_version)) {
            if (c.tls)
                tc_tls_set_io(c.tls, o->timeout_ms, NULL);
            pool_give(o->pool, key, &c, &r->tls);
        } else {
            conn_close(&c);
        }
        tc_http_response_free(&reply);
        tc_headers_free(&extra);
        tc_buf_free(&req);
        break;
    }
    return err;
}

/* ---- The cache ---- */

typedef struct centry centry;
struct centry {
    centry *next;
    char *key;
    tc_response r;
    uint32_t used;   /* when last used, for dropping the least recently used */
};

struct tc_cache {
    tc_mutex *lock;
    centry *entries;
    size_t bytes, max;
    uint32_t clock;
    unsigned hits, misses;
};

tc_cache *tc_cache_new(size_t max_bytes)
{
    tc_cache *c = calloc(1, sizeof *c);
    if (c && !(c->lock = tc_mutex_create())) {
        free(c);
        c = NULL;
    }
    if (c)
        c->max = max_bytes;
    return c;
}

static void centry_free(centry *e)
{
    free(e->key);
    tc_response_free(&e->r);
    free(e);
}

void tc_cache_free(tc_cache *c)
{
    centry *e, *next;
    if (!c)
        return;
    for (e = c->entries; e; e = next) {
        next = e->next;
        centry_free(e);
    }
    tc_mutex_destroy(c->lock);
    free(c);
}

void tc_cache_stats(tc_cache *c, unsigned *hits, unsigned *misses)
{
    tc_mutex_lock(c->lock);
    *hits = c->hits;
    *misses = c->misses;
    tc_mutex_unlock(c->lock);
}

static char *copy_str(const char *s)
{
    size_t n;
    char *out;
    if (!s)
        return NULL;
    n = strlen(s) + 1;
    out = malloc(n);
    if (out)
        memcpy(out, s, n);
    return out;
}

/* A copy of a response, for the cache or from it. */
static int copy_response(const tc_response *from, tc_response *to)
{
    size_t i;
    memset(to, 0, sizeof *to);
    tc_headers_init(&to->headers);
    tc_buf_init(&to->body);
    to->status = from->status;
    to->minor_version = from->minor_version;
    to->redirects = from->redirects;
    to->tls = from->tls;
    to->url = copy_str(from->url);
    to->status_text = copy_str(from->status_text);
    if ((from->url && !to->url) || (from->status_text && !to->status_text))
        return TC_ERR_NOMEM;
    for (i = 0; i < from->headers.len; i++)
        if (add_header(&to->headers, from->headers.items[i].name, from->headers.items[i].value) != TC_OK)
            return TC_ERR_NOMEM;
    if (from->body.len && tc_buf_append(&to->body, from->body.data, from->body.len) != TC_OK)
        return TC_ERR_NOMEM;
    return TC_OK;
}

static int cache_get(tc_cache *c, const char *key, tc_response *out)
{
    centry *e;
    int found = 0;
    tc_mutex_lock(c->lock);
    for (e = c->entries; e; e = e->next)
        if (strcmp(e->key, key) == 0) {
            e->used = ++c->clock;
            if (copy_response(&e->r, out) == TC_OK)
                found = 1;
            else
                tc_response_free(out);
            break;
        }
    if (found)
        c->hits++;
    else
        c->misses++;
    tc_mutex_unlock(c->lock);
    return found;
}

static void cache_put(tc_cache *c, const char *key, const tc_response *r)
{
    const char *control = tc_headers_get(&r->headers, "Cache-Control");
    centry *e, **p;
    if (r->status != 200 || (control && strstr(control, "no-store")) || r->body.len > c->max / 4)
        return;
    e = calloc(1, sizeof *e);
    if (!e)
        return;
    e->key = copy_str(key);
    if (!e->key || copy_response(r, &e->r) != TC_OK) {
        centry_free(e);
        return;
    }
    tc_mutex_lock(c->lock);
    /* Replace an older copy, then make room. */
    for (p = &c->entries; *p; p = &(*p)->next)
        if (strcmp((*p)->key, key) == 0) {
            centry *old = *p;
            *p = old->next;
            c->bytes -= old->r.body.len;
            centry_free(old);
            break;
        }
    e->used = ++c->clock;
    e->next = c->entries;
    c->entries = e;
    c->bytes += r->body.len;
    while (c->bytes > c->max && c->entries) {
        centry **oldest = &c->entries, *victim;
        for (p = &c->entries; *p; p = &(*p)->next)
            if ((*p)->used < (*oldest)->used)
                oldest = p;
        victim = *oldest;
        *oldest = victim->next;
        c->bytes -= victim->r.body.len;
        centry_free(victim);
    }
    tc_mutex_unlock(c->lock);
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
    char *key = NULL;
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
    if (opts->cache && !opts->reload && cache_get(opts->cache, url.href, r)) {
        r->cached = 1;
        tc_url_free(&url);
        r->elapsed_ms = tc_time_ms() - start;
        return TC_OK;
    }
    key = copy_str(url.href);
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
    if (!err && opts->cache && key)
        cache_put(opts->cache, key, r);
    free(key);
    tc_buf_free(&raw);
    tc_url_free(&url);
    r->elapsed_ms = tc_time_ms() - start;
    return err;
}
