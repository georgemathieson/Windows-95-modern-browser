/* The browser frame: menu, toolbar, address bar, content view and status bar,
 * the back/forward history, and starting page loads. */
#ifndef TW_SHELL_H
#define TW_SHELL_H

#include <windows.h>

#include "tc_fetch.h"
#include "tc_font.h"
#include "tw_log.h"

#define TW_SHELL_CLASS "TachyonFrame"
#define TW_HOME_URL    "about:tachyon"

typedef struct tw_shell_options {
    const char *start_url;  /* as typed (ANSI), or NULL for the home page */
    int show;               /* ShowWindow command */
    int smoke_test;         /* close after a moment, exiting with 0 if the page painted */
    const char *proxy_host; /* an HTTP proxy, or NULL; must outlive the window */
    uint16_t proxy_port;
    tw_last_run last_run;   /* a crash last time gets a note on the home page */
    tc_font_set *fonts;     /* these three last as long as the process: */
    tc_conn_pool *pool;     /* loads still running when the window closes */
    tc_cache *cache;        /* go on using them */
} tw_shell_options;

int tw_shell_register(HINSTANCE instance);
HWND tw_shell_create(HINSTANCE instance, const tw_shell_options *opts);

#endif
