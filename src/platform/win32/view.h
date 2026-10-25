/* The content view: the sunken area of the window where pages appear.
 *
 * Pages are laid out and painted by Tachyon's own engine (tc_page) into a
 * DIB section, then blitted to the screen; scrolling moves what's already
 * drawn and paints only the strip that comes into view. Built-in pages
 * (about:, errors) are HTML too. A document's tree and its source are a
 * menu away, drawn with GDI. */
#ifndef TW_VIEW_H
#define TW_VIEW_H

#include <windows.h>

#include "tc_font.h"
#include "tc_page.h"

#define TW_VIEW_CLASS "TachyonView"

/* Sent to the parent. WM_TW_HOVER: the mouse is over a link (lParam: its
   URL, UTF-8) or has left one (lParam NULL). WM_TW_FOLLOW: a link was
   clicked (lParam: its URL). The strings last for the call only. */
#define WM_TW_HOVER  (WM_APP + 10)
#define WM_TW_FOLLOW (WM_APP + 11)

typedef enum tw_view_mode {
    TW_MODE_PAGE,   /* the rendered page */
    TW_MODE_TREE,   /* its document tree */
    TW_MODE_SOURCE  /* its source */
} tw_view_mode;

int tw_view_register(HINSTANCE instance);
HWND tw_view_create(HWND parent, HINSTANCE instance, int id, tc_font_set *fonts);

/* The size pages should be laid out for. */
void tw_view_page_size(HWND view, int *w, int *h);
/* The document rows in view. */
void tw_view_visible(HWND view, int *top, int *bottom);

/* A page from HTML text (built-in pages). */
int tw_view_show_html(HWND view, const char *url, const char *html);
void tw_view_show_home(HWND view);
void tw_view_show_blank(HWND view);
/* A message page: a heading, then text and a smaller detail line (either
   may be NULL). Used for errors and "loading". */
void tw_view_show_message(HWND view, const char *url, const char *heading, const char *text, const char *detail);
/* Built-in information: about:fonts lists the fonts Tachyon found. */
void tw_view_show_fonts(HWND view);

/* A fetched document that isn't HTML: its source, the response head (in
   blue) then the body. Both are UTF-8. */
int tw_view_show_source(HWND view, const char *url, const char *head, size_t head_len, const char *body,
                        size_t body_len);

/* A loaded HTML page. The view takes over page (leaving it empty) and lays
   it out again if its size has changed. head: the response head, shown
   above the source. */
int tw_view_show_page(HWND view, const char *head, size_t head_len, tc_page *page);

/* Show the page, its tree or its source. Returns whether the current page
   is a document (so the choice is visible). */
int tw_view_set_mode(HWND view, tw_view_mode mode);

/* Scroll to a #fragment of the current page; 0 if it's not there. */
int tw_view_scroll_to_fragment(HWND view, const char *fragment);
/* The current page's URL (UTF-8), or "" if it has none. */
const char *tw_view_url(HWND view);

/* A warning shown on the home page, e.g. about the clock (NULL for none). */
void tw_view_set_warning(HWND view, const char *warning);

/* The page shown, or NULL. It stays the view's: callers copy what they need. */
const tc_page *tw_view_page(HWND view);
/* A picture for the page arrived (index: the page's; img taken over, NULL
   if it failed). Shown, or the page laid out again, a moment later. */
void tw_view_image_arrived(HWND view, size_t index, tc_image *img);
/* A web font for the page arrived (index: the page's; data NULL if it
   couldn't be fetched). The page is laid out again a moment later. */
void tw_view_font_arrived(HWND view, size_t index, const void *data, size_t len);

/* Dither to 256 colours (decision 39) on any display, not only a
   256-colour one, to see what one shows (--dither). */
void tw_view_force_dither(int on);
/* The top-level window's palette and display messages, passed on. On
   WM_QUERYNEWPALETTE and WM_PALETTECHANGED (from another window) the view
   realises its palette, repainting if the colours changed; returns whether
   they did. On WM_DISPLAYCHANGE it checks the colour depth again. */
int tw_view_realize_palette(HWND view);
void tw_view_display_changed(HWND view);

/* How many times the view has been painted (the smoke test checks this). */
int tw_view_paint_count(HWND view);

#endif
