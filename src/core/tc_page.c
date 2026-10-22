/* A page: the document, style sheets, styles and layout. */
#include <stdlib.h>
#include <string.h>

#include "tc_fetch.h"
#include "tc_mime.h"
#include "tc_page.h"
#include "tc_platform.h"

#define MAX_SHEETS  64 /* style sheets and imports per page */
#define MAX_IMPORTS 4  /* nested @import depth */

int tc_page_fetch_http(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size)
{
    tc_fetch_options o = *(const tc_fetch_options *)ctx;
    tc_response r;
    tc_mime mime;
    int err;

    o.accept = "text/css,*/*;q=0.1";
    o.progress = NULL;
    charset[0] = '\0';
    err = tc_fetch(url, &o, &r);
    if (!err && r.status != 200)
        err = TC_ERR_INVALID;
    if (!err && tc_mime_parse(tc_headers_get(&r.headers, "Content-Type") ? tc_headers_get(&r.headers, "Content-Type")
                                                                            : "",
                              &mime) == TC_OK) {
        if (tc_mime_is_html(&mime))
            err = TC_ERR_INVALID; /* an error page, most likely */
        else if (strlen(mime.charset) < charset_size)
            strcpy(charset, mime.charset);
    }
    if (!err) {
        *body = r.body;
        tc_buf_init(&r.body);
    }
    tc_response_free(&r);
    return err;
}

int tc_page_init(tc_page *p, const char *url, tc_html_page *html)
{
    const tc_node *n;
    memset(p, 0, sizeof *p);
    p->html = *html;
    memset(html, 0, sizeof *html);
    if (tc_url_parse(url, NULL, &p->url) != TC_OK)
        return TC_ERR_URL;
    /* The first <base href> sets the base URL. */
    for (n = tc_dom_next(&p->html.doc->node, &p->html.doc->node); n; n = tc_dom_next(&p->html.doc->node, n)) {
        const char *href;
        if (tc_dom_is_html(n, TC_TAG_BASE) && (href = tc_dom_get_attr((const tc_element *)n, "href")) != NULL) {
            if (tc_url_parse(href, &p->url, &p->base) == TC_OK)
                return TC_OK;
            break;
        }
    }
    return tc_url_parse(p->url.href, NULL, &p->base);
}

void tc_page_free(tc_page *p)
{
    tc_layout_free(p->layout);
    tc_styled_free(p->styled);
    tc_style_sources_free(p->sources, p->nsources);
    tc_html_page_free(&p->html);
    tc_url_free(&p->url);
    tc_url_free(&p->base);
    memset(p, 0, sizeof *p);
}

typedef struct loader {
    tc_page *p;
    tc_page_fetch_fn fetch;
    void *ctx;
    void (*progress)(void *ctx, unsigned n, const char *url);
    unsigned count;
} loader;

static tc_stylesheet *load_sheet(loader *ld, const tc_url *url, int depth);

/* Fetch a sheet's @imports, relative to base. */
static void load_imports(loader *ld, tc_stylesheet *sheet, const tc_url *base, int depth)
{
    size_t i;
    for (i = 0; i < tc_stylesheet_import_count(sheet) && depth < MAX_IMPORTS; i++) {
        tc_url url;
        if (tc_url_parse(tc_stylesheet_import_url(sheet, i), base, &url) == TC_OK) {
            tc_stylesheet_set_import(sheet, i, load_sheet(ld, &url, depth + 1));
            tc_url_free(&url);
        }
    }
}

static tc_stylesheet *load_sheet(loader *ld, const tc_url *url, int depth)
{
    tc_buf bytes, text;
    char charset[64];
    tc_stylesheet *sheet = NULL;

    if (ld->count >= MAX_SHEETS)
        return NULL;
    ld->count++;
    if (ld->progress)
        ld->progress(ld->ctx, ld->count, url->href);
    tc_buf_init(&bytes);
    tc_buf_init(&text);
    charset[0] = '\0';
    if (ld->fetch(ld->ctx, url->href, &bytes, charset, sizeof charset) == TC_OK) {
        tc_css_decode(bytes.data ? bytes.data : "", bytes.len, charset[0] ? charset : NULL,
                      tc_encoding_name(ld->p->html.encoding), &text);
        sheet = tc_stylesheet_parse(text.len ? tc_buf_cstr(&text) : "", text.len, TC_ORIGIN_AUTHOR,
                                    ld->p->html.doc->mode == TC_QUIRKS, url->href);
    }
    tc_buf_free(&bytes);
    tc_buf_free(&text);
    if (sheet) {
        ld->p->sheets_loaded++;
        load_imports(ld, sheet, url, depth);
    } else {
        ld->p->sheets_failed++;
    }
    return sheet;
}

int tc_page_load_styles(tc_page *p, tc_page_fetch_fn fetch, void *ctx,
                        void (*progress)(void *ctx, unsigned n, const char *url))
{
    loader ld;
    size_t i;
    int err;

    if (!p->sources) {
        err = tc_style_sources(p->html.doc, &p->sources, &p->nsources);
        if (err)
            return err;
    }
    ld.p = p;
    ld.fetch = fetch;
    ld.ctx = ctx;
    ld.progress = progress;
    ld.count = 0;
    for (i = 0; i < p->nsources; i++) {
        tc_style_source *src = &p->sources[i];
        if (src->href && !src->sheet) {
            tc_url url;
            if (tc_url_parse(src->href, &p->base, &url) == TC_OK) {
                src->sheet = load_sheet(&ld, &url, 0);
                tc_url_free(&url);
            }
        } else if (src->sheet) {
            load_imports(&ld, src->sheet, &p->base, 0);
        }
    }
    return TC_OK;
}

int tc_page_layout(tc_page *p, tc_font_set *fonts, float w, float h)
{
    tc_media_env env;
    tc_styled *styled = NULL;
    tc_layout *layout = NULL;
    uint32_t t0 = tc_time_ms(), t1;
    int err = TC_OK;

    if (!p->sources)
        err = tc_style_sources(p->html.doc, &p->sources, &p->nsources);
    env.width = env.device_width = w;
    env.height = env.device_height = h;
    env.color_bits = 8;
    if (!err)
        err = tc_style_document(p->html.doc, p->sources, p->nsources, &env, p->url.fragment, &styled);
    t1 = tc_time_ms();
    if (!err)
        err = tc_layout_document(p->html.doc, styled, fonts, w, h, &layout);
    if (err) {
        tc_layout_free(layout);
        tc_styled_free(styled);
        return err;
    }
    tc_layout_free(p->layout);
    tc_styled_free(p->styled);
    p->styled = styled;
    p->layout = layout;
    p->width = w;
    p->height = h;
    p->style_ms = t1 - t0;
    p->layout_ms = tc_time_ms() - t1;
    return TC_OK;
}

static const tc_element *link_element(const tc_element *el)
{
    for (; el; el = tc_element_parent(el))
        if ((tc_dom_is_html(&el->node, TC_TAG_A) || tc_dom_is_html(&el->node, TC_TAG_AREA)) &&
            tc_dom_get_attr(el, "href"))
            return el;
    return NULL;
}

const tc_element *tc_page_link_at(const tc_page *p, float x, float y, tc_url *url)
{
    const tc_element *link = p->layout ? link_element(tc_layout_hit(p->layout, x, y)) : NULL;
    if (link && url && tc_url_parse(tc_dom_get_attr(link, "href"), &p->base, url) != TC_OK)
        return NULL;
    return link;
}

int tc_page_cursor_at(const tc_page *p, float x, float y)
{
    const tc_element *el = p->layout ? tc_layout_hit(p->layout, x, y) : NULL;
    const tc_element_styles *es;
    if (!el)
        return TC_CURSOR_AUTO;
    es = tc_styled_element(p->styled, el);
    if (es && es->style->cursor != TC_CURSOR_AUTO)
        return es->style->cursor;
    return link_element(el) ? TC_CURSOR_POINTER : TC_CURSOR_AUTO;
}

static int find_fragment(const tc_page *p, const char *fragment, float *x, float *y)
{
    const tc_node *n;
    const tc_element *by_name = NULL;
    for (n = tc_dom_next(&p->html.doc->node, &p->html.doc->node); n; n = tc_dom_next(&p->html.doc->node, n)) {
        const tc_element *el = (const tc_element *)n;
        const char *v;
        if (n->type != TC_NODE_ELEMENT)
            continue;
        if ((v = tc_dom_get_attr(el, "id")) != NULL && strcmp(v, fragment) == 0)
            return tc_layout_find(p->layout, el, x, y);
        if (!by_name && tc_dom_is_html(n, TC_TAG_A) && (v = tc_dom_get_attr(el, "name")) != NULL &&
            strcmp(v, fragment) == 0)
            by_name = el;
    }
    return by_name ? tc_layout_find(p->layout, by_name, x, y) : 0;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int tc_page_fragment(const tc_page *p, const char *fragment, float *x, float *y)
{
    char decoded[256];
    size_t i, n = 0;
    if (!p->layout || !fragment || !*fragment)
        return 0;
    if (find_fragment(p, fragment, x, y))
        return 1;
    /* The URL parser percent-encoded the fragment; ids are as written. */
    for (i = 0; fragment[i] && n + 1 < sizeof decoded; i++) {
        int hi, lo;
        if (fragment[i] == '%' && (hi = hex_value(fragment[i + 1])) >= 0 && (lo = hex_value(fragment[i + 2])) >= 0) {
            decoded[n++] = (char)(hi << 4 | lo);
            i += 2;
        } else {
            decoded[n++] = fragment[i];
        }
    }
    decoded[n] = '\0';
    return !fragment[i] && n && strcmp(decoded, fragment) != 0 && find_fragment(p, decoded, x, y);
}
