/* Rendering a page from a file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_page.h"
#include "tc_html_parser.h"
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

/* A URL relative to base's directory, as a file path (local, relative
   references only: no scheme, query or fragment). */
static int resolve(const char *base, const char *url, char *out, size_t size)
{
    const char *slash = strrchr(base, '/'), *bslash = strrchr(base, '\\');
    size_t dir, len = strcspn(url, "?#");
    if (strchr(url, ':') || len == 0)
        return 0;
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
    dir = slash ? (size_t)(slash - base) + 1 : 0;
    if (url[0] == '/')
        dir = 0;
    if (dir + len + 1 > size)
        return 0;
    memcpy(out, base, dir);
    memcpy(out + dir, url, len);
    out[dir + len] = '\0';
    return 1;
}

static tc_stylesheet *load_sheet(const char *path, int quirks, int depth)
{
    tc_buf bytes, text;
    tc_stylesheet *sheet = NULL;
    size_t i;
    tc_buf_init(&bytes);
    tc_buf_init(&text);
    if (read_file(path, &bytes) == TC_OK) {
        tc_css_decode(bytes.data ? bytes.data : "", bytes.len, NULL, NULL, &text);
        sheet = tc_stylesheet_parse(text.len ? tc_buf_cstr(&text) : "", text.len, TC_ORIGIN_AUTHOR, quirks, path);
    }
    tc_buf_free(&bytes);
    tc_buf_free(&text);
    for (i = 0; sheet && depth < 4 && i < tc_stylesheet_import_count(sheet); i++) {
        char sub[1024];
        if (resolve(path, tc_stylesheet_import_url(sheet, i), sub, sizeof sub))
            tc_stylesheet_set_import(sheet, i, load_sheet(sub, quirks, depth + 1));
    }
    return sheet;
}

int render_file(const char *path, tc_font_set *fonts, int w, int h, int full, int dump, rendered *out)
{
    tc_buf html;
    tc_html_page page;
    tc_style_source *sources = NULL;
    size_t count = 0, i;
    tc_styled *styled = NULL;
    tc_layout *layout = NULL;
    tc_media_env env;
    uint32_t t0, t1, t2, t3;
    int err, quirks;

    memset(out, 0, sizeof *out);
    tc_buf_init(&out->boxes);
    tc_buf_init(&html);
    err = read_file(path, &html);
    if (err) {
        tc_buf_free(&html);
        return err;
    }
    t0 = tc_time_ms();
    err = tc_html_parse_page(html.data ? html.data : "", html.len, NULL, NULL, &page);
    tc_buf_free(&html);
    if (err) {
        tc_html_page_free(&page);
        return err;
    }
    quirks = page.doc->mode == TC_QUIRKS;
    err = tc_style_sources(page.doc, &sources, &count);
    for (i = 0; err == TC_OK && i < count; i++) {
        char sheet_path[1024];
        if (sources[i].href && resolve(path, sources[i].href, sheet_path, sizeof sheet_path))
            sources[i].sheet = load_sheet(sheet_path, quirks, 0);
        else if (sources[i].sheet) {
            size_t k;
            for (k = 0; k < tc_stylesheet_import_count(sources[i].sheet); k++)
                if (resolve(path, tc_stylesheet_import_url(sources[i].sheet, k), sheet_path, sizeof sheet_path))
                    tc_stylesheet_set_import(sources[i].sheet, k, load_sheet(sheet_path, quirks, 1));
        }
    }
    t1 = tc_time_ms();
    env.width = env.device_width = (float)w;
    env.height = env.device_height = (float)h;
    env.color_bits = 8;
    if (err == TC_OK)
        err = tc_style_document(page.doc, sources, count, &env, NULL, &styled);
    t2 = tc_time_ms();
    if (err == TC_OK)
        err = tc_layout_document(page.doc, styled, fonts, (float)w, (float)h, &layout);
    t3 = tc_time_ms();
    if (err == TC_OK) {
        tc_layout_size(layout, &out->doc_w, &out->doc_h);
        err = tc_surface_init(&out->surface, w, full ? (int)(out->doc_h + 0.5f) : h);
    }
    if (err == TC_OK) {
        tc_paint(layout, &out->surface, tc_rect_make(0, 0, out->surface.width, out->surface.height), 0, 0);
        out->paint_ms = tc_time_ms() - t3;
        tc_styled_stats(styled, &out->style);
        tc_layout_stats_of(layout, &out->layout);
        if (dump)
            tc_layout_dump(layout, &out->boxes);
    }
    out->parse_ms = t1 - t0;
    out->style_ms = t2 - t1;
    out->layout_ms = t3 - t2;
    tc_layout_free(layout);
    tc_styled_free(styled);
    tc_style_sources_free(sources, count);
    tc_html_page_free(&page);
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
