/* Rendering a page from a file, through tc_page with file: URLs. */
#ifndef _WIN32
#define _XOPEN_SOURCE 700 /* realpath */
#endif
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "render_page.h"
#include "tc_page.h"
#include "tc_platform.h"

static int read_file(const char *path, tc_buf *out)
{
    FILE *f = fopen(path, "rb");
    char chunk[16384];
    size_t n;
    if (!f)
        return TC_ERR_INVALID;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        if (tc_buf_append(out, chunk, n) != TC_OK) {
            fclose(f);
            return TC_ERR_NOMEM;
        }
    fclose(f);
    return TC_OK;
}

static int hex(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
}

/* A file: URL's path, as a file name. */
static int url_to_path(const char *url, char *out, size_t size)
{
    const char *p;
    size_t n = 0;
    if (strncmp(url, "file://", 7) != 0)
        return 0;
    p = strchr(url + 7, '/');
    if (!p)
        return 0;
#ifdef _WIN32
    if (p[1] && p[2] == ':')
        p++; /* /C:/... */
#endif
    for (; *p && *p != '?' && *p != '#' && n + 1 < size; p++) {
        if (*p == '%' && hex(p[1]) >= 0 && hex(p[2]) >= 0) {
            out[n++] = (char)(hex(p[1]) * 16 + hex(p[2]));
            p += 2;
        } else {
            out[n++] = *p;
        }
    }
    out[n] = '\0';
    return 1;
}

/* A file name as a file: URL. */
static int path_to_url(const char *path, char *out, size_t size)
{
    char full[4096];
    size_t i;
#ifdef _WIN32
    if (!GetFullPathNameA(path, sizeof full, full, NULL))
        return 0;
#else
    if (!realpath(path, full))
        return 0;
#endif
    for (i = 0; full[i]; i++)
        if (full[i] == '\\')
            full[i] = '/';
    return snprintf(out, size, "file://%s%s", full[0] == '/' ? "" : "/", full) < (int)size;
}

static int fetch_file(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size)
{
    char path[4096];
    (void)ctx;
    (void)charset_size;
    charset[0] = '\0';
    return url_to_path(url, path, sizeof path) ? read_file(path, body) : TC_ERR_UNSUPPORTED;
}

int render_file(const char *path, tc_font_set *fonts, int w, int h, int full, int dump, rendered *out)
{
    tc_buf html;
    tc_html_page parsed;
    tc_page page;
    char url[4200];
    uint32_t t0, t1;
    int err;

    memset(out, 0, sizeof *out);
    tc_buf_init(&out->boxes);
    tc_buf_init(&html);
    if (!path_to_url(path, url, sizeof url) || (err = read_file(path, &html)) != TC_OK) {
        tc_buf_free(&html);
        return TC_ERR_INVALID;
    }
    t0 = tc_time_ms();
    err = tc_html_parse_page(html.data ? html.data : "", html.len, NULL, NULL, &parsed);
    tc_buf_free(&html);
    if (err) {
        tc_html_page_free(&parsed);
        return err;
    }
    err = tc_page_init(&page, url, &parsed);
    if (!err)
        err = tc_page_load_styles(&page, fetch_file, NULL, NULL);
    t1 = tc_time_ms();
    if (!err)
        err = tc_page_layout(&page, fonts, (float)w, (float)h);
    if (!err)
        err = tc_page_load_images(&page, fonts, fetch_file, NULL, NULL);
    if (!err) {
        tc_layout_size(page.layout, &out->doc_w, &out->doc_h);
        err = tc_surface_init(&out->surface, w, full ? (int)(out->doc_h + 0.5f) : h);
    }
    if (!err) {
        uint32_t t2 = tc_time_ms();
        tc_paint(page.layout, &out->surface, tc_rect_make(0, 0, out->surface.width, out->surface.height), 0, 0);
        out->paint_ms = tc_time_ms() - t2;
        tc_styled_stats(page.styled, &out->style);
        tc_layout_stats_of(page.layout, &out->layout);
        if (dump)
            tc_layout_dump(page.layout, &out->boxes);
    }
    out->parse_ms = t1 - t0;
    out->style_ms = page.style_ms;
    out->layout_ms = page.layout_ms;
    tc_page_free(&page);
    return err;
}

void rendered_free(rendered *r)
{
    tc_surface_free(&r->surface);
    tc_buf_free(&r->boxes);
}

tc_font_set *render_fonts(const char *dir)
{
    static const char *const dirs[] = {
        "/usr/share/fonts/truetype/liberation",
        "Z:\\usr\\share\\fonts\\truetype\\liberation",
    };
    tc_font_set *set = tc_font_set_new();
    size_t i;
    if (!set)
        return NULL;
    if (dir) {
        tc_font_set_add_dir(set, dir);
        return set;
    }
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
        if (tc_font_set_add_dir(set, dirs[i]))
            return set;
    tc_font_set_add_system(set);
    return set;
}
