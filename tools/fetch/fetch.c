/* fetch: download a URL with Tachyon's network stack and print it.
 *
 *   fetch [-i] [-q] [--cacert FILE] [--proxy HOST:PORT] [--timeout MS]
 *         [--max-redirects N] URL
 *
 *   -i   print the status line and headers before the body
 *   -q   don't print the body
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
#include "tc_version.h"

static int usage(void)
{
    fprintf(stderr, "%s fetch %s\n"
                    "usage: fetch [-i] [-q] [--cacert FILE] [--proxy HOST:PORT] [--timeout MS]\n"
                    "             [--max-redirects N] URL\n",
            TC_NAME, TC_VERSION);
    return 1;
}

static int add_cacert(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *pem;
    long len;
    int err;

    if (!f) {
        fprintf(stderr, "fetch: can't open %s\n", path);
        return TC_ERR_INVALID;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    pem = malloc((size_t)len + 1);
    if (!pem || fread(pem, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(pem);
        return TC_ERR_NOMEM;
    }
    fclose(f);
    err = tc_tls_add_trusted_pem(pem, (size_t)len);
    free(pem);
    if (err)
        fprintf(stderr, "fetch: no certificates in %s\n", path);
    return err;
}

int main(int argc, char **argv)
{
    tc_fetch_options opts;
    tc_response r;
    const char *url = NULL;
    char proxy[256];
    int i, include = 0, quiet = 0, err;
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
        } else if (strcmp(argv[i], "--cacert") == 0 && i + 1 < argc) {
            if (add_cacert(argv[++i]))
                return 1;
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
    if (!err && !quiet)
        fwrite(r.body.data ? r.body.data : "", 1, r.body.len, stdout);
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
