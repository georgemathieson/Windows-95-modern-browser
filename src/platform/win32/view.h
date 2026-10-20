/* The content view: the sunken area of the window where pages are drawn.
 *
 * Sprint 0 only has built-in pages (about:tachyon, about:blank and a
 * "can't load this yet" page). The view renders into a tc_surface backed by
 * a DIB section and blits it to the screen, the same path the real engine
 * will use from Sprint 3. */
#ifndef TW_VIEW_H
#define TW_VIEW_H

#include <windows.h>

#define TW_VIEW_CLASS "TachyonView"

int tw_view_register(HINSTANCE instance);
HWND tw_view_create(HWND parent, HINSTANCE instance, int id);

/* Show the page for `url` and return its title. */
const char *tw_view_show(HWND view, const char *url);

/* How many times the view has been painted (the smoke test checks this). */
int tw_view_paint_count(HWND view);

#endif
