/* The content view: the sunken area of the window where pages appear.
 *
 * Until Tachyon renders HTML (Sprint 3) it shows built-in pages, messages
 * such as errors, a parsed page's document tree, and the source of fetched
 * documents. Drawing goes into a tc_surface backed by a DIB section and is
 * blitted to the screen, the path the real renderer will use; text is GDI's
 * for now. */
#ifndef TW_VIEW_H
#define TW_VIEW_H

#include <windows.h>

#include "tc_html_parser.h"

#define TW_VIEW_CLASS "TachyonView"

int tw_view_register(HINSTANCE instance);
HWND tw_view_create(HWND parent, HINSTANCE instance, int id);

void tw_view_show_home(HWND view);
void tw_view_show_blank(HWND view);
/* A message page: a heading in the banner, then text and a smaller detail
   line (either may be NULL). Used for errors and "loading". */
void tw_view_show_message(HWND view, const char *url, const char *heading, const char *text,
                          const char *detail);
/* A fetched document's source: the response head (status line and headers,
   drawn in blue) followed by the body. Both are UTF-8. */
int tw_view_show_source(HWND view, const char *url, const char *head, size_t head_len, const char *body,
                        size_t body_len);

/* A parsed HTML page: its document tree, or its decoded source after
   tw_view_set_show_source(view, 1). The view takes over page (leaving it
   empty). head is the response head, shown above the source. */
int tw_view_show_document(HWND view, const char *url, const char *head, size_t head_len, tc_html_page *page);

/* Whether documents show their source instead of their tree. Returns
   whether the current page is a document (so the choice is visible). */
int tw_view_set_show_source(HWND view, int show_source);

/* A warning shown on the home page, e.g. about the clock (NULL for none). */
void tw_view_set_warning(HWND view, const char *warning);

/* How many times the view has been painted (the smoke test checks this). */
int tw_view_paint_count(HWND view);

#endif
