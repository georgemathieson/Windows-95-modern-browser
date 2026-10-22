#include "loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_http.h"
#include "tc_log.h"
#include "tc_mime.h"

typedef struct start_args {
    tw_load *load;
    tw_load_options opts;
    char *proxy_host;
} start_args;

/* Progress posts are throttled: a fast download shouldn't flood the UI. */
#define PROGRESS_INTERVAL_MS 150

static void post_progress(tw_load *load, int force)
{
    uint32_t now = GetTickCount();
    if (force || now - load->last_post_ms >= PROGRESS_INTERVAL_MS) {
        load->last_post_ms = now;
        PostMessageA(load->notify, WM_TW_PROGRESS, 0, (LPARAM)load);
    }
}

static void on_progress(void *ctx, tc_fetch_phase phase, const char *host, size_t received, int64_t total)
{
    tw_load *load = ctx;
    int phase_changed = load->phase != phase;

    load->phase = phase;
    load->received = received;
    load->total = total;
    if (phase_changed)
        snprintf(load->host, sizeof load->host, "%s", host ? host : "");
    post_progress(load, phase_changed);
}

static void on_sheet(void *ctx, unsigned n, const char *url)
{
    tw_load *load = ctx;
    load->sheet = n;
    snprintf(load->sheet_url, sizeof load->sheet_url, "%s", url);
    tc_log("loader: style sheet %u: %.120s", n, url);
    post_progress(load, 1);
}

typedef struct sheet_fetch {
    tw_load *load;
    tc_fetch_options opts;
} sheet_fetch;

static int fetch_sheet(void *ctx, const char *url, tc_buf *body, char *charset, size_t size)
{
    sheet_fetch *f = ctx;
    if (f->load->cancel)
        return TC_ERR_CANCELLED;
    return tc_page_fetch_http(&f->opts, url, body, charset, size);
}

static void sheet_progress(void *ctx, unsigned n, const char *url)
{
    on_sheet(((sheet_fetch *)ctx)->load, n, url);
}

/* Parse an HTML response, fetch its style sheets and lay it out, here on the
   worker thread so the window stays responsive. */
static void build_page(tw_load *load, const tw_load_options *o, const tc_fetch_options *fo)
{
    tc_buf *body = &load->response.body;
    tc_html_page parsed;
    tc_mime mime;
    sheet_fetch sf;
    uint32_t start;
    int err;

    if (!tc_mime_wants_html(tc_headers_get(&load->response.headers, "Content-Type"), body->data, body->len, &mime))
        return;
    load->stage = TW_STAGE_READING;
    post_progress(load, 1);
    tc_log("loader: parsing %u KB", (unsigned)(body->len / 1024));
    start = tc_time_ms();
    err = tc_html_parse_page(body->data ? body->data : "", body->len, mime.charset, NULL, &parsed);
    if (!err)
        err = tc_page_init(&load->page, load->response.url ? load->response.url : load->url, &parsed);
    else
        tc_html_page_free(&parsed);
    if (err) {
        tc_log("loader: out of memory parsing");
        tc_page_free(&load->page);
        return;
    }
    load->has_page = 1;
    load->parse_ms = tc_time_ms() - start;
    tc_log("loader: parsed in %u ms, %u KB of memory", (unsigned)load->parse_ms,
           (unsigned)((load->page.html.doc->arena.total + load->page.html.text.cap) / 1024));
    tc_buf_free(body);

    load->stage = TW_STAGE_STYLES;
    start = tc_time_ms();
    sf.load = load;
    sf.opts = *fo;
    sf.opts.reload = 0; /* style sheets from the cache even on a reload */
    tc_page_load_styles(&load->page, fetch_sheet, &sf, sheet_progress);
    load->styles_ms = tc_time_ms() - start;
    tc_log("loader: %u style sheets (%u failed) in %u ms", load->page.sheets_loaded, load->page.sheets_failed,
           (unsigned)load->styles_ms);
    if (load->cancel)
        return;

    load->stage = TW_STAGE_LAYOUT;
    post_progress(load, 1);
    if (tc_page_layout(&load->page, o->fonts, (float)o->width, (float)o->height) != TC_OK)
        tc_log("loader: out of memory laying out");
    else
        tc_log("loader: styles in %u ms, layout in %u ms", load->page.style_ms, load->page.layout_ms);
}

static void load_thread(void *arg)
{
    start_args *args = arg;
    tw_load *load = args->load;
    tc_fetch_options opts;

    tc_fetch_options_init(&opts);
    opts.proxy_host = args->proxy_host;
    opts.proxy_port = args->opts.proxy_port;
    opts.cancel = &load->cancel;
    opts.progress = on_progress;
    opts.progress_ctx = load;
    opts.pool = args->opts.pool;
    opts.cache = args->opts.cache;
    opts.reload = args->opts.reload;
    tc_log("loader: fetching %.120s", load->url);
    load->result = tc_fetch(load->url, &opts, &load->response);
    load->body_len = load->response.body.len;
    tc_log("loader: %s, %u bytes%s", load->result == TC_OK ? "fetched" : tc_err_text(load->result),
           (unsigned)load->body_len, load->response.cached ? " (from the cache)" : "");
    if (load->result == TC_OK && !load->cancel) {
        opts.progress = NULL;
        build_page(load, &args->opts, &opts);
    }
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

tw_load *tw_load_start(HWND notify, const char *url, const tw_load_options *o)
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
    args->opts = *o;
    if (!load->url || (o->proxy_host && !(args->proxy_host = copy_string(o->proxy_host))))
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
        if (load->has_page)
            tc_page_free(&load->page);
        free(load->url);
        free(load);
    }
}

/* ---- Pictures ---- */

typedef struct picture {
    char *url;
    size_t index;
    tc_image *image;
} picture;

struct tw_images {
    HWND notify;
    volatile int cancel;
    picture *items;
    unsigned count;
    volatile unsigned done;
    char *proxy_host;
    tw_load_options opts;
};

static void images_thread(void *arg)
{
    tw_images *job = arg;
    tc_fetch_options fo;
    unsigned i;

    tc_fetch_options_init(&fo);
    fo.proxy_host = job->proxy_host;
    fo.proxy_port = job->opts.proxy_port;
    fo.cancel = &job->cancel;
    fo.pool = job->opts.pool;
    fo.cache = job->opts.cache;
    tc_log("loader: %u pictures", job->count);
    for (i = 0; i < job->count && !job->cancel; i++) {
        tc_buf body;
        char charset[64];
        int err;
        tc_buf_init(&body);
        err = tc_page_fetch_image_http(&fo, job->items[i].url, &body, charset, sizeof charset);
        if (!err)
            err = tc_image_new(body.data ? body.data : "", body.len, &job->items[i].image);
        tc_buf_free(&body);
        if (err && err != TC_ERR_CANCELLED)
            tc_trace("picture %u: %s", i, tc_err_text(err));
        job->done = i + 1;
        PostMessageA(job->notify, WM_TW_IMAGE, i, (LPARAM)job);
    }
    tc_log("loader: %u of %u pictures fetched%s", job->done, job->count, job->cancel ? " (stopped)" : "");
    PostMessageA(job->notify, WM_TW_IMAGES_DONE, 0, (LPARAM)job);
}

tw_images *tw_images_start(HWND notify, const tc_page *page, int top, int bottom, const tw_load_options *opts)
{
    size_t i, n = tc_page_image_count(page), count, *order;
    tw_images *job;

    if (!n || !(order = malloc(n * sizeof *order)))
        return NULL;
    count = tc_page_image_order(page, (float)top, (float)bottom, order, n);
    if (!count || !(job = calloc(1, sizeof *job))) {
        free(order);
        return NULL;
    }
    job->notify = notify;
    job->opts = *opts;
    job->items = calloc(count, sizeof *job->items);
    if (!job->items || (opts->proxy_host && !(job->proxy_host = copy_string(opts->proxy_host))))
        goto fail;
    for (i = 0; i < count; i++) {
        job->items[job->count].index = order[i];
        if (!(job->items[job->count].url = copy_string(tc_page_image_url(page, order[i]))))
            goto fail;
        job->count++;
    }
    free(order);
    if (tc_thread_start(images_thread, job) != TC_OK)
        goto fail;
    return job;

fail:
    free(order);
    tw_images_free(job);
    return NULL;
}

size_t tw_images_index(const tw_images *job, WPARAM slot)
{
    return slot < job->count ? job->items[slot].index : (size_t)-1;
}

tc_image *tw_images_take(tw_images *job, WPARAM slot)
{
    tc_image *img;
    if (slot >= job->count)
        return NULL;
    img = job->items[slot].image;
    job->items[slot].image = NULL;
    return img;
}

void tw_images_progress(const tw_images *job, unsigned *done, unsigned *total)
{
    *done = job->done;
    *total = job->count;
}

void tw_images_cancel(tw_images *job)
{
    job->cancel = 1;
}

void tw_images_free(tw_images *job)
{
    unsigned i;
    if (!job)
        return;
    for (i = 0; job->items && i < job->count; i++) {
        free(job->items[i].url);
        tc_image_free(job->items[i].image);
    }
    free(job->items);
    free(job->proxy_host);
    free(job);
}
