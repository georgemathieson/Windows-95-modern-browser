#include "loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_http.h"
#include "tc_log.h"
#include "tc_mime.h"

typedef struct start_args {
    tw_load *load;
    char *proxy_host;
    uint16_t proxy_port;
} start_args;

/* Progress posts are throttled: a fast download shouldn't flood the UI. */
#define PROGRESS_INTERVAL_MS 150

static void on_progress(void *ctx, tc_fetch_phase phase, const char *host, size_t received, int64_t total)
{
    tw_load *load = ctx;
    uint32_t now = GetTickCount();
    int phase_changed = load->phase != phase;

    load->phase = phase;
    load->received = received;
    load->total = total;
    if (phase_changed)
        snprintf(load->host, sizeof load->host, "%s", host ? host : "");
    if (phase_changed || now - load->last_post_ms >= PROGRESS_INTERVAL_MS) {
        load->last_post_ms = now;
        PostMessageA(load->notify, WM_TW_PROGRESS, 0, (LPARAM)load);
    }
}

/* Parse an HTML response into a document, here on the worker thread so the
   window stays responsive. The raw body isn't needed after that. */
static void parse_page(tw_load *load)
{
    tc_buf *body = &load->response.body;
    tc_mime mime;
    uint32_t start;

    if (!tc_mime_wants_html(tc_headers_get(&load->response.headers, "Content-Type"), body->data, body->len, &mime))
        return;
    load->parsing = 1;
    PostMessageA(load->notify, WM_TW_PROGRESS, 0, (LPARAM)load);
    tc_log("loader: parsing %u KB", (unsigned)(body->len / 1024));
    start = tc_time_ms();
    if (tc_html_parse_page(body->data, body->len, mime.charset, NULL, &load->page) != TC_OK) {
        tc_log("loader: out of memory parsing");
        tc_html_page_free(&load->page);
        return;
    }
    load->parse_ms = tc_time_ms() - start;
    tc_log("loader: parsed in %u ms, %u KB of memory", (unsigned)load->parse_ms,
           (unsigned)((load->page.doc->arena.total + load->page.text.cap) / 1024));
    load->is_html = 1;
    tc_buf_free(body);
}

static void load_thread(void *arg)
{
    start_args *args = arg;
    tw_load *load = args->load;
    tc_fetch_options opts;

    tc_fetch_options_init(&opts);
    opts.proxy_host = args->proxy_host;
    opts.proxy_port = args->proxy_port;
    opts.cancel = &load->cancel;
    opts.progress = on_progress;
    opts.progress_ctx = load;
    tc_log("loader: fetching %.120s", load->url);
    load->result = tc_fetch(load->url, &opts, &load->response);
    load->body_len = load->response.body.len;
    tc_log("loader: %s, %u bytes", load->result == TC_OK ? "fetched" : tc_err_text(load->result),
           (unsigned)load->body_len);
    if (load->result == TC_OK && !load->cancel)
        parse_page(load);
    free(args->proxy_host);
    free(args);
    PostMessageA(load->notify, WM_TW_DONE, 0, (LPARAM)load);
}

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

tw_load *tw_load_start(HWND notify, const char *url, const char *proxy_host, uint16_t proxy_port)
{
    tw_load *load = calloc(1, sizeof *load);
    start_args *args = calloc(1, sizeof *args);

    if (!load || !args)
        goto fail;
    load->url = copy_string(url);
    load->notify = notify;
    load->phase = TC_FETCH_RESOLVING;
    load->total = -1;
    args->load = load;
    args->proxy_port = proxy_port;
    if (!load->url || (proxy_host && !(args->proxy_host = copy_string(proxy_host))))
        goto fail;
    if (tc_thread_start(load_thread, args) != TC_OK)
        goto fail;
    return load;

fail:
    if (args)
        free(args->proxy_host);
    free(args);
    if (load)
        free(load->url);
    free(load);
    return NULL;
}

void tw_load_cancel(tw_load *load)
{
    load->cancel = 1;
}

void tw_load_free(tw_load *load)
{
    if (load) {
        tc_response_free(&load->response);
        tc_html_page_free(&load->page);
        free(load->url);
        free(load);
    }
}
