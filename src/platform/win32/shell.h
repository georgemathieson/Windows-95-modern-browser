/* The browser frame: menu, toolbar, address bar, content view and status bar,
 * plus the back/forward history. */
#ifndef TW_SHELL_H
#define TW_SHELL_H

#include <windows.h>

#define TW_SHELL_CLASS "TachyonFrame"
#define TW_HOME_URL    "about:tachyon"

int tw_shell_register(HINSTANCE instance);

/* Create and show the frame. `smoke_test` makes it close itself after a
   moment, exiting with 0 only if the page was painted. */
HWND tw_shell_create(HINSTANCE instance, const char *start_url, int show, int smoke_test);

#endif
