/* fetch: download URLs with Tachyon's network stack and print them.
 *
 *   fetch [-i] [-q] [--dom] [--render] [--width N] [--fonts DIR] [--png FILE]
 *         [--cacert FILE] [--proxy HOST:PORT] [--timeout MS] [--max-redirects N]
 *         URL...
 *
 *   -i        print the status line and headers before the body
 *   -q        don't print the body
 *   --dom     parse the body as HTML and print its document tree instead, in
 *             html5lib-tests' format, with a summary of the parse on stderr
 *   --render  load the page as the browser does (style sheets, styles,
 *             layout) and paint it, a screenful at a time, with a summary on
 *             stderr; --width sets the window's width (800), --fonts adds
 *             fonts from a directory, --png writes the whole page as a PNG
 *
 * Several URLs are fetched in turn over connections kept open between them.
 * Built for the Linux host (tachyon-fetch) and for Windows 95 (FETCH.EXE),
 * where it doubles as a way to test HTTPS on real Windows 95 from a DOS box.
 * A one-line summary goes to stderr. Exit status: 0 when a response arrived
 * (whatever its HTTP status), 1 for bad usage, 2 when the fetch failed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "tc_entropy.h"
#include "tc_fetch.h"
#include "tc_html_parser.h"
#include "tc_http.h"
#include "tc_mime.h"
#include "tc_page.h"
#include "tc_png.h"
#include "tc_version.h"

/* Parse the body as HTML and print the tree. */
static int print_dom(const tc_response *r, int quiet)
{
    tc_mime mime;
    tc_html_page page;
    tc_dom_stats stats;
    tc_buf out;
    uint32_t start = tc_time_ms(), ms;
    int err;

    tc_mime_wants_html(tc_headers_get(&r->headers, "Content-Type"), r->body.data, r->body.len, &mime);
    err = tc_html_parse_page(r->body.data, r->body.len, mime.charset, NULL, &page);
    ms = tc_time_ms() - start;
    if (err) {
        fprintf(stderr, "fetch: parsing failed: %s\n", tc_err_text(err));
        tc_html_page_free(&page);
        return err;
    }
    tc_buf_init(&out);
    if (!quiet) {
        tc_dom_dump(&page.doc->node, &out);
        tc_buf_append_char(&out, '\n');
        fwrite(out.data, 1, out.len, stdout);
    }
    tc_dom_count(&page.doc->node, &stats);
    tc_buf_clear(&out);
    tc_document_title(page.doc, &out);
    fprintf(stderr,
            "fetch: parsed %u elements, %u text nodes, %u comments, depth %u, in %u ms, %u KB; %s%s%s; title: %s\n",
            (unsigned)stats.elements, (unsigned)stats.texts, (unsigned)stats.comments, (unsigned)stats.max_depth,
            (unsigned)ms, (unsigned)((page.doc->arena.total + page.text.cap) / 1024), tc_encoding_name(page.encoding), tc_encoding_supported(page.encoding) ? "" : " (not decoded)",
            page.doc->mode == TC_QUIRKS ? ", quirks mode" : page.doc->mode == TC_LIMITED_QUIRKS ? ", limited quirks" : "",
            tc_buf_cstr(&out));
    tc_buf_free(&out);
    tc_html_page_free(&page);
    return TC_OK;
}

typedef struct render_options {
    int width;
    const char *fonts_dir;
    const char *png;
} render_options;

static void sheet_progress(void *ctx, unsigned n, const char *url)
{
    (void)ctx;
    fprintf(stderr, "fetch: style sheet %u: %.150s\n", n, url);
}

/* Load the page as the browser does, and paint it a screenful at a time. */
static int render_page(const tc_response *r, const tc_fetch_options *opts, const render_options *ro)
{
    static tc_font_set *fonts;
    tc_mime mime;
    tc_html_page parsed;
    tc_page page;
    tc_surface s;
    float doc_w, doc_h, y;
    uint32_t t0 = tc_time_ms(), t1, paint_ms;
    tc_style_stats ss;
    tc_layout_stats ls;
    int err;

    if (!fonts) {
        tc_style_init();
        fonts = tc_font_set_new();
        if (!fonts)
            return TC_ERR_NOMEM;
        if (ro->fonts_dir)
            tc_font_set_add_dir(fonts, ro->fonts_dir);
        tc_font_set_add_system(fonts);
        fprintf(stderr, "fetch: %u font faces\n", (unsigned)tc_font_set_count(fonts));
    }
    tc_mime_wants_html(tc_headers_get(&r->headers, "Content-Type"), r->body.data, r->body.len, &mime);
    err = tc_html_parse_page(r->body.data ? r->body.data : "", r->body.len, mime.charset, NULL, &parsed);
    if (err) {
        tc_html_page_free(&parsed);
        return err;
    }
    err = tc_page_init(&page, r->url, &parsed);
    if (!err)
        err = tc_page_load_styles(&page, tc_page_fetch_http, (void *)opts, sheet_progress);
    t1 = tc_time_ms();
    if (!err)
        err = tc_page_layout(&page, fonts, (float)ro->width, 600);
    if (err) {
        tc_page_free(&page);
        return err;
    }
    tc_layout_size(page.layout, &doc_w, &doc_h);
    paint_ms = tc_time_ms();
    if (ro->png) {
        int h = doc_h > 16384 ? 16384 : (int)(doc_h + 0.5f);
        err = tc_surface_init(&s, ro->width, h);
        if (!err) {
            tc_buf png;
            FILE *f;
            tc_paint(page.layout, &s, tc_rect_make(0, 0, s.width, s.height), 0, 0);
            tc_buf_init(&png);
            if (tc_png_encode(&s, &png) == TC_OK && (f = fopen(ro->png, "wb")) != NULL) {
                fwrite(png.data, 1, png.len, f);
                fclose(f);
            }
            tc_buf_free(&png);
            tc_surface_free(&s);
        }
    } else if ((err = tc_surface_init(&s, ro->width, 600)) == TC_OK) {
        for (y = 0; y < doc_h; y += 600)
            tc_paint(page.layout, &s, tc_rect_make(0, 0, s.width, s.height), 0, y);
        tc_surface_free(&s);
    }
    paint_ms = tc_time_ms() - paint_ms;
    tc_styled_stats(page.styled, &ss);
    tc_layout_stats_of(page.layout, &ls);
    fprintf(stderr,
            "fetch: rendered %.0fx%.0f: %u style sheets (%u failed) in %u ms; style %u ms (%u elements, %u "
            "selectors, %u styles); layout %u ms (%u boxes, %u lines); paint %u ms; %u KB\n",
            doc_w, doc_h, page.sheets_loaded, page.sheets_failed, (unsigned)(t1 - t0), page.style_ms,
            (unsigned)ss.elements, (unsigned)ss.selectors, (unsigned)ss.styles, page.layout_ms, (unsigned)ls.boxes,
            (unsigned)ls.lines, (unsigned)paint_ms, (unsigned)((ss.bytes + ls.bytes) / 1024));
    tc_page_free(&page);
    return err;
}

static int usage(void)
{
    fprintf(stderr, "%s fetch %s\n"
                    "usage: fetch [-i] [-q] [--dom] [--render] [--width N] [--fonts DIR] [--png FILE]\n"
                    "             [--cacert FILE] [--proxy HOST:PORT] [--timeout MS] [--max-redirects N]\n"
                    "             URL...\n",
            TC_NAME, TC_VERSION);
    return 1;
}

int main(int argc, char **argv)
{
    tc_fetch_options opts;
    render_options ro;
    const char *urls[64];
    char proxy[256];
    int i, nurls = 0, include = 0, quiet = 0, dom = 0, render = 0, err = TC_OK, failed = 0;
    unsigned opened, reused;

    tc_fetch_options_init(&opts);
    memset(&ro, 0, sizeof ro);
    ro.width = 800;
    if (tc_net_init() != TC_OK || tc_tls_init() != TC_OK) {
        fprintf(stderr, "fetch: network or TLS setup failed\n");
        return 2;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            include = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "--dom") == 0) {
            dom = 1;
        } else if (strcmp(argv[i], "--render") == 0) {
            render = 1;
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            ro.width = atoi(argv[++i]);
            if (ro.width < 100 || ro.width > 4096)
                return usage();
        } else if (strcmp(argv[i], "--fonts") == 0 && i + 1 < argc) {
            ro.fonts_dir = argv[++i];
        } else if (strcmp(argv[i], "--png") == 0 && i + 1 < argc) {
            ro.png = argv[++i];
        } else if (strcmp(argv[i], "--cacert") == 0 && i + 1 < argc) {
            if (tc_tls_add_trusted_file(argv[++i]) != TC_OK) {
                fprintf(stderr, "fetch: no certificates found in %s\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--proxy") == 0 && i + 1 < argc) {
            char *colon;
            snprintf(proxy, sizeof proxy, "%s", argv[++i]);
            colon = strrchr(proxy, ':');
            if (!colon)
                return usage();
            *colon = '\0';
            opts.proxy_host = proxy;
            opts.proxy_port = (uint16_t)atoi(colon + 1);
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            opts.timeout_ms = (uint32_t)atol(argv[++i]);
        } else if (strcmp(argv[i], "--max-redirects") == 0 && i + 1 < argc) {
            opts.max_redirects = atoi(argv[++i]);
        } else if (argv[i][0] == '-' || nurls == (int)(sizeof urls / sizeof urls[0])) {
            return usage();
        } else {
            urls[nurls++] = argv[i];
        }
    }
    if (!nurls)
        return usage();
    opts.pool = tc_conn_pool_new();
    if (!opts.pool)
        return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); /* the pages' bytes, unchanged */
#endif

    for (i = 0; i < nurls; i++) {
        tc_response r;
        size_t h;
        err = tc_fetch(urls[i], &opts, &r);
        if (r.status && (include || err)) {
            printf("HTTP/1.%d %d %s\r\n", r.minor_version, r.status, r.status_text ? r.status_text : "");
            for (h = 0; h < r.headers.len; h++)
                printf("%s: %s\r\n", r.headers.items[h].name, r.headers.items[h].value);
            printf("\r\n");
        }
        if (!err && !quiet && !dom && !render)
            fwrite(r.body.data ? r.body.data : "", 1, r.body.len, stdout);
        if (!err && dom && print_dom(&r, quiet) != TC_OK)
            err = TC_ERR_NOMEM;
        fflush(stdout);
        if (err) {
            fprintf(stderr, "fetch: error: %s%s%s\n", tc_err_text(err), r.error_detail[0] ? ": " : "",
                    r.error_detail);
        } else {
            fprintf(stderr, "fetch: %d %s, %u bytes, ", r.status, r.status_text ? r.status_text : "",
                    (unsigned)r.body.len);
            if (r.tls.version[0])
                fprintf(stderr, "%s %s, ", r.tls.version, r.tls.cipher);
            fprintf(stderr, "%d redirects, %u ms, %s\n", r.redirects, (unsigned)r.elapsed_ms, r.url);
        }
        if (!err && render && (err = render_page(&r, &opts, &ro)) != TC_OK)
            fprintf(stderr, "fetch: rendering failed: %s\n", tc_err_text(err));
        tc_response_free(&r);
        failed += err != TC_OK;
    }
    tc_conn_pool_stats(opts.pool, &opened, &reused);
    if (nurls > 1 || render)
        fprintf(stderr, "fetch: %u connections opened, %u reused\n", opened, reused);
    tc_conn_pool_free(opts.pool);
    tc_entropy_save_seed();
    return failed ? 2 : 0;
}
