/* Page loads on a worker thread.
 *
 * tw_load_start() runs tc_fetch() on its own thread; for HTML it then parses
 * the page, fetches its style sheets, computes styles and lays it out, so
 * that the window only has to paint. It posts messages to a window:
 * WM_TW_PROGRESS while it works and exactly one WM_TW_DONE at the end, both
 * with the tw_load in lParam. The window frees the load when it handles
 * WM_TW_DONE, and never earlier: the thread owns it until then. */
#ifndef TW_LOADER_H
#define TW_LOADER_H

#include <windows.h>

#include "tc_fetch.h"
#include "tc_page.h"

#define WM_TW_PROGRESS (WM_APP + 1)
#define WM_TW_DONE     (WM_APP + 2)

/* What a load is doing after the fetch. */
enum { TW_STAGE_FETCHING, TW_STAGE_READING, TW_STAGE_STYLES, TW_STAGE_LAYOUT };

typedef struct tw_load_options {
    const char *proxy_host;  /* an HTTP proxy, or NULL */
    uint16_t proxy_port;
    tc_font_set *fonts;
    tc_conn_pool *pool;      /* connections kept open between loads */
    tc_cache *cache;
    int reload;              /* skip the cache for the page itself */
    int width, height;       /* the view, to lay the page out for */
} tw_load_options;

typedef struct tw_load {
    char *url;
    HWND notify;
    volatile int cancel;
    int result;              /* tc_err, valid at WM_TW_DONE */
    tc_response response;    /* valid at WM_TW_DONE; an HTML body moves into page */
    size_t body_len;
    int has_page;            /* page holds the parsed and laid out document */
    tc_page page;
    uint32_t parse_ms, styles_ms;

    /* Latest progress, written by the worker, read on WM_TW_PROGRESS. */
    volatile tc_fetch_phase phase;
    volatile int stage;
    volatile size_t received;
    volatile int64_t total;
    volatile unsigned sheet;
    char host[256];
    char sheet_url[256];
    uint32_t last_post_ms;
} tw_load;

/* NULL if out of memory. */
tw_load *tw_load_start(HWND notify, const char *url, const tw_load_options *opts);
/* Ask the load to stop. WM_TW_DONE still arrives (with TC_ERR_CANCELLED). */
void tw_load_cancel(tw_load *load);
void tw_load_free(tw_load *load);

#endif
