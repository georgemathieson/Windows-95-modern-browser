/* fetch: download a URL with Tachyon's network stack and print it.
 *
 *   fetch [-i] [-q] [--dom] [--cacert FILE] [--proxy HOST:PORT] [--timeout MS]
 *         [--max-redirects N] URL
 *
 *   -i     print the status line and headers before the body
 *   -q     don't print the body
 *   --dom  parse the body as HTML and print its document tree instead, in
 *          html5lib-tests' format, with a summary of the parse on stderr
 *
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

static int usage(void)
{
    fprintf(stderr, "%s fetch %s\n"
                    "usage: fetch [-i] [-q] [--dom] [--cacert FILE] [--proxy HOST:PORT] [--timeout MS]\n"
                    "             [--max-redirects N] URL\n",
            TC_NAME, TC_VERSION);
    return 1;
}

int main(int argc, char **argv)
{
    tc_fetch_options opts;
    tc_response r;
    const char *url = NULL;
    char proxy[256];
    int i, include = 0, quiet = 0, dom = 0, err;
    size_t h;

    tc_fetch_options_init(&opts);
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
        } else if (argv[i][0] == '-' || url) {
            return usage();
        } else {
            url = argv[i];
        }
    }
    if (!url)
        return usage();

    err = tc_fetch(url, &opts, &r);
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); /* the page's bytes, unchanged */
#endif
    if (r.status && (include || err)) {
        printf("HTTP/1.%d %d %s\r\n", r.minor_version, r.status, r.status_text ? r.status_text : "");
        for (h = 0; h < r.headers.len; h++)
            printf("%s: %s\r\n", r.headers.items[h].name, r.headers.items[h].value);
        printf("\r\n");
    }
    if (!err && !quiet && !dom)
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
    tc_response_free(&r);
    tc_entropy_save_seed();
    return err ? 2 : 0;
}
