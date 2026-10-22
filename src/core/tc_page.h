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
} tc_page;

/* A tc_page_fetch_fn over HTTP: ctx is the tc_fetch_options to use (its
   pool and cache too). Only a 200 response that isn't HTML counts. */
int tc_page_fetch_http(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size);

/* Start a page from a parsed document (taken over: html is left empty). */
int tc_page_init(tc_page *p, const char *url, tc_html_page *html);
void tc_page_free(tc_page *p);

/* Fetch the linked style sheets and their @imports. progress (may be NULL)
   hears of each one before it's fetched. */
int tc_page_load_styles(tc_page *p, tc_page_fetch_fn fetch, void *ctx,
                        void (*progress)(void *ctx, unsigned n, const char *url));

/* Compute styles for a viewport of w x h (media queries) and lay out. Run
   again when the viewport changes size. */
int tc_page_layout(tc_page *p, tc_font_set *fonts, float w, float h);

/* The link at a point in the document: the a or area element with an href,
   and its URL (free it with tc_url_free). NULL if there's none. */
const tc_element *tc_page_link_at(const tc_page *p, float x, float y, tc_url *url);
/* The cursor for a point: one of TC_CURSOR_*. */
int tc_page_cursor_at(const tc_page *p, float x, float y);
/* Where a fragment (#name) points in the document: the element with that
   id, or an a with that name. 0 if none. */
int tc_page_fragment(const tc_page *p, const char *fragment, float *x, float *y);

#endif
