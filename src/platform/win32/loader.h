/* Page loads on a worker thread.
 *
 * tw_load_start() runs tc_fetch() on its own thread, then parses the page if
 * it's HTML, and posts messages to a window: WM_TW_PROGRESS while it works
 * and exactly one WM_TW_DONE at the end, both with the tw_load in lParam.
 * The window frees the load when it handles WM_TW_DONE, and never earlier:
 * the thread owns it until then. */
#ifndef TW_LOADER_H
#define TW_LOADER_H

#include <windows.h>

#include "tc_fetch.h"
#include "tc_html_parser.h"

#define WM_TW_PROGRESS (WM_APP + 1)
#define WM_TW_DONE     (WM_APP + 2)

typedef struct tw_load {
    char *url;
    HWND notify;
    volatile int cancel;
    int result;              /* tc_err, valid at WM_TW_DONE */
    tc_response response;    /* valid at WM_TW_DONE; an HTML body moves into page */
    size_t body_len;
    int is_html;             /* page holds the parsed document */
    tc_html_page page;
    uint32_t parse_ms;

    /* Latest progress, written by the worker, read on WM_TW_PROGRESS. */
    volatile tc_fetch_phase phase;
    volatile int parsing;
    volatile size_t received;
    volatile int64_t total;
    char host[256];
    uint32_t last_post_ms;
} tw_load;

/* NULL if out of memory. proxy_host may be NULL. */
tw_load *tw_load_start(HWND notify, const char *url, const char *proxy_host, uint16_t proxy_port);
/* Ask the load to stop. WM_TW_DONE still arrives (with TC_ERR_CANCELLED). */
void tw_load_cancel(tw_load *load);
void tw_load_free(tw_load *load);

#endif
