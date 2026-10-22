/* A page as the browser has it: the document, its style sheets, computed
 * styles and layout, and what the browser asks of it (links, fragments,
 * cursors). The steps are the same whether the page came over the network
 * (the browser, FETCH --render) or from a file (tachyon-render): only the
 * function that fetches style sheets differs. */
#ifndef TC_PAGE_H
#define TC_PAGE_H

#include "tc_html_parser.h"
#include "tc_layout.h"
#include "tc_url.h"

/* Fetch a URL's bytes into body. charset: the response's charset label, if
   it gave one ("" if not). TC_OK, or an error (the sheet is skipped). */
typedef int (*tc_page_fetch_fn)(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size);

typedef struct tc_page {
    tc_html_page html;          /* the document */
    tc_url url;                 /* where it came from */
    tc_url base;                /* its base URL (<base href>, else url) */
    tc_style_source *sources;   /* <style> and <link rel=stylesheet>, in order */
    size_t nsources;
    unsigned sheets_loaded, sheets_failed;
    tc_styled *styled;
    tc_layout *layout;
    float width, height;        /* the viewport it's laid out for */
    unsigned style_ms, layout_ms;
    struct tc_page_images *images; /* its pictures: see tc_page_image_* */
    struct tc_page_fonts *webfonts; /* its @font-face fonts: see tc_page_font_* */
} tc_page;

/* A tc_page_fetch_fn over HTTP: ctx is the tc_fetch_options to use (its
   pool and cache too). Only a 200 response that isn't HTML counts. The
   first asks for style sheets, the second for images. */
int tc_page_fetch_http(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size);
int tc_page_fetch_image_http(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size);
int tc_page_fetch_font_http(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size);

/* Start a page from a parsed document (taken over: html is left empty). */
int tc_page_init(tc_page *p, const char *url, tc_html_page *html);
void tc_page_free(tc_page *p);

/* Fetch the linked style sheets and their @imports. progress (may be NULL)
   hears of each one before it's fetched. */
int tc_page_load_styles(tc_page *p, tc_page_fetch_fn fetch, void *ctx,
                        void (*progress)(void *ctx, unsigned n, const char *url));

/* Compute styles for a viewport of w x h (media queries) and lay out. Run
   again when the viewport changes size, when an image it waited for
   arrives, or when a font arrives. It finds the page's pictures and web
   fonts too; fonts is the set of installed fonts, which a page with
   @font-face rules puts its own in front of. */
int tc_page_layout(tc_page *p, tc_font_set *fonts, float w, float h);

/* ---- Pictures ---- */

/* The pictures a page shows: each <img>'s chosen source (srcset, sizes,
   <picture>) and the url()s in its styles, by absolute URL, in the order
   the document first uses them. data: URLs are decoded as they're found. */
enum { TC_PAGE_IMAGE_WAITING, TC_PAGE_IMAGE_LOADED, TC_PAGE_IMAGE_FAILED };
size_t tc_page_image_count(const tc_page *p);
const char *tc_page_image_url(const tc_page *p, size_t i);
int tc_page_image_state(const tc_page *p, size_t i);
/* The pictures still waiting, in the order to load them: those showing
   between document rows top and bottom (the viewport) first, then those
   below it, nearest first, then those above, then any the page doesn't
   show. Writes at most max indexes to order; returns how many. */
size_t tc_page_image_order(const tc_page *p, float top, float bottom, size_t *order, size_t max);
/* An image arrived (taken over), or failed to load (NULL). Returns 1 if
   the page must be laid out again, because layout waited for its size. */
int tc_page_image_arrived(tc_page *p, size_t i, tc_image *img);
/* Fetch and decode the header of every picture still waiting, those on
   the first screen first, laying out again (at the last size) if any were
   waited for. For the tools and
   tests; the browser loads them as tc_page_image_arrived describes. */
int tc_page_load_images(tc_page *p, tc_font_set *fonts, tc_page_fetch_fn fetch, void *ctx,
                        void (*progress)(void *ctx, unsigned n, const char *url));
/* Decoded pictures are kept within this many bytes, those least recently
   drawn dropped first (default 16 MB). An image bigger than a quarter of
   it isn't drawn. */
void tc_page_set_image_memory(tc_page *p, size_t bytes);

/* ---- Web fonts ---- */

/* The fonts a page's @font-face rules offer, in the order its style sheets
   give them. Laying out finds those its text needs: a face is wanted when
   its family comes before any installed family in some text's
   font-family, and that text has a character in its unicode-range. Those
   wait to be fetched; the rest never are. Until a font arrives its text
   is laid out with the next family (as font-display: swap does). data:
   URLs are read as they're found. */
enum { TC_PAGE_FONT_UNUSED, TC_PAGE_FONT_WAITING, TC_PAGE_FONT_LOADED, TC_PAGE_FONT_FAILED };
size_t tc_page_font_count(const tc_page *p);
/* The absolute URL being tried ("" while unused). */
const char *tc_page_font_url(const tc_page *p, size_t i);
int tc_page_font_state(const tc_page *p, size_t i);
/* A waiting font's bytes arrived (TrueType, WOFF or WOFF2), or NULL if
   they couldn't be fetched. A font that doesn't load goes on to its
   rule's next src, waiting again, or fails. Returns 1 if the page must
   be laid out again. */
int tc_page_font_arrived(tc_page *p, size_t i, const void *data, size_t len);
/* Fetch every waiting font, then lay out again. For the tools and tests;
   the browser fetches them as tc_page_font_arrived describes. */
int tc_page_load_fonts(tc_page *p, tc_font_set *fonts, tc_page_fetch_fn fetch, void *ctx,
                       void (*progress)(void *ctx, unsigned n, const char *url));

/* Animated pictures. Painting notes which are on screen and where; this
   moves those whose time has come on to their next frame. now_ms: any
   millisecond clock (it may wrap). *dirty: what to paint again, in
   document coordinates (empty for nothing). Returns the milliseconds until
   it should be called again, or 0 when nothing on screen is animating
   (painting an animation starts it again). */
uint32_t tc_page_animate(tc_page *p, uint32_t now_ms, tc_rect *dirty);

/* The link at a point in the document: the a or area element with an href,
   and its URL (free it with tc_url_free). NULL if there's none. */
const tc_element *tc_page_link_at(const tc_page *p, float x, float y, tc_url *url);
/* The cursor for a point: one of TC_CURSOR_*. */
int tc_page_cursor_at(const tc_page *p, float x, float y);
/* Where a fragment (#name) points in the document: the element with that
   id, or an a with that name. 0 if none. */
int tc_page_fragment(const tc_page *p, const char *fragment, float *x, float *y);

#endif
