#include <stdlib.h>
#include <string.h>

#include "tc_inflate.h"
#include "tc_page.h"
#include "tc_png.h"
#include "tc_test.h"

void tc_suite_page(void);

/* Style sheets served from memory, and a record of what was asked for. */
typedef struct server {
    const char *const *files; /* URL, body, URL, body... NULL */
    char asked[1024];
    unsigned count;
} server;

static int serve(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size)
{
    server *s = ctx;
    size_t i;
    (void)charset_size;
    s->count++;
    if (strlen(s->asked) + strlen(url) + 2 < sizeof s->asked) {
        strcat(s->asked, url);
        strcat(s->asked, " ");
    }
    charset[0] = '\0';
    for (i = 0; s->files[i]; i += 2)
        if (strcmp(s->files[i], url) == 0)
            return tc_buf_append(body, s->files[i + 1], strlen(s->files[i + 1]));
    return TC_ERR_INVALID;
}

static int page_from(tc_page *p, const char *url, const char *html)
{
    tc_html_page parsed;
    int err = tc_html_parse_page(html, strlen(html), NULL, NULL, &parsed);
    if (err)
        return err;
    return tc_page_init(p, url, &parsed);
}

/* Checks without stringifying the (long) HTML. */
static int setup_ok(int err, int line)
{
    tc_test_checks++;
    if (err)
        tc_test_fail(__FILE__, line, "setting up the page failed");
    return err;
}
#define PAGE_FROM(...) setup_ok(page_from(__VA_ARGS__), __LINE__)

static const tc_element *by_id(const tc_page *p, const char *id)
{
    const tc_node *n, *root = &p->html.doc->node;
    for (n = tc_dom_next(root, root); n; n = tc_dom_next(root, n)) {
        const char *v;
        if (n->type == TC_NODE_ELEMENT && (v = tc_dom_get_attr((const tc_element *)n, "id")) != NULL &&
            strcmp(v, id) == 0)
            return (const tc_element *)n;
    }
    return NULL;
}

static uint32_t color_of(const tc_page *p, const char *id)
{
    const tc_element_styles *es = tc_styled_element(p->styled, by_id(p, id));
    return es ? es->style->color & 0xFFFFFF : 0x123456;
}

static void test_style_sheets(void)
{
    static const char *const files[] = {
        "http://example.com/dir/a.css", "@import url(b.css); #one { color: #f00 }",
        "http://example.com/dir/b.css", "#two { color: #0f0 }",
        "http://example.com/c.css", "#three { color: #00f }",
        NULL
    };
    server s = { files, "", 0 };
    tc_font_set *fonts = tc_font_set_new();
    tc_page p;

    PAGE_FROM(&p, "http://example.com/page.html",
                           "<!DOCTYPE html><base href='dir/'>"
                           "<link rel=stylesheet href=a.css>"
                           "<link rel=stylesheet href=missing.css>"
                           "<link rel=icon href=icon.css>"
                           "<style>@import '/c.css';</style>"
                           "<p id=one>1<p id=two>2<p id=three>3");
    TC_CHECK_STR(p.base.href, "http://example.com/dir/");
    TC_CHECK_INT(tc_page_load_styles(&p, serve, &s, NULL), TC_OK);
    TC_CHECK_STR(s.asked, "http://example.com/dir/a.css http://example.com/dir/b.css "
                          "http://example.com/dir/missing.css http://example.com/c.css ");
    TC_CHECK_INT(p.sheets_loaded, 3);
    TC_CHECK_INT(p.sheets_failed, 1);
    TC_CHECK_INT(tc_page_layout(&p, fonts, 400, 300), TC_OK);
    TC_CHECK_INT(color_of(&p, "one"), 0xFF0000);
    TC_CHECK_INT(color_of(&p, "two"), 0x00FF00);
    TC_CHECK_INT(color_of(&p, "three"), 0x0000FF);
    /* Laid out again for another size. */
    TC_CHECK_INT(tc_page_layout(&p, fonts, 200, 100), TC_OK);
    TC_CHECK(p.width == 200 && p.height == 100);
    TC_CHECK_INT(color_of(&p, "one"), 0xFF0000);
    tc_page_free(&p);
    tc_font_set_free(fonts);
}

static void test_import_limits(void)
{
    /* A sheet importing itself stops at the depth limit... */
    static const char *const loop[] = { "http://example.com/a.css", "@import 'a.css'; p { color: red }", NULL };
    /* ...and a page can't load more than 64 sheets. */
    static const char *const none[] = { NULL };
    server s = { loop, "", 0 };
    tc_page p;
    tc_buf html;
    int i;

    PAGE_FROM(&p, "http://example.com/", "<link rel=stylesheet href=a.css>");
    TC_CHECK_INT(tc_page_load_styles(&p, serve, &s, NULL), TC_OK);
    TC_CHECK_INT(s.count, 5);
    TC_CHECK_INT(p.sheets_loaded, 5);
    tc_page_free(&p);

    tc_buf_init(&html);
    for (i = 0; i < 100; i++)
        tc_buf_append(&html, "<link rel=stylesheet href=x.css>", 32);
    s.files = none;
    s.count = 0;
    PAGE_FROM(&p, "http://example.com/", tc_buf_cstr(&html));
    TC_CHECK_INT(tc_page_load_styles(&p, serve, &s, NULL), TC_OK);
    TC_CHECK_INT(s.count, 64);
    TC_CHECK_INT(p.sheets_failed, 64);
    tc_page_free(&p);
    tc_buf_free(&html);
}

static void test_links_and_fragments(void)
{
    tc_font_set *fonts = tc_font_set_new();
    tc_page p;
    tc_url url;
    const tc_element *link;
    float x, y;

    PAGE_FROM(&p, "http://example.com/dir/page.html#Caf%C3%A9",
                           "<!DOCTYPE html><meta charset=utf-8><style>body { margin: 0; font: 10px/10px x } div { height: 100px }</style>"
                           "<a id=l href='other.html#top'><b>link</b></a><br>"
                           "<a id=nohref>no href</a>"
                           "<div style='cursor: help'>help</div>"
                           "<a name=named></a><div id=named>the id wins</div>"
                           "<div id=Caf\xC3\xA9>caf\xC3\xA9</div>");
    TC_CHECK_INT(tc_page_layout(&p, fonts, 400, 300), TC_OK);
    link = tc_page_link_at(&p, 5, 5, &url);
    TC_CHECK(link == by_id(&p, "l"));
    if (link) {
        TC_CHECK_STR(url.href, "http://example.com/dir/other.html#top");
        tc_url_free(&url);
    }
    TC_CHECK(tc_page_link_at(&p, 50, 5, NULL) == NULL); /* the same line, past the link */
    TC_CHECK(tc_page_link_at(&p, 5, 15, NULL) == NULL); /* no href */
    TC_CHECK_INT(tc_page_cursor_at(&p, 5, 5), TC_CURSOR_POINTER);
    TC_CHECK_INT(tc_page_cursor_at(&p, 5, 15), TC_CURSOR_AUTO);
    TC_CHECK_INT(tc_page_cursor_at(&p, 5, 25), TC_CURSOR_HELP);
    TC_CHECK(tc_page_fragment(&p, "named", &x, &y));
    TC_CHECK(y == 120);
    TC_CHECK(tc_page_fragment(&p, "Caf%C3%A9", &x, &y));
    TC_CHECK(y == 220);
    TC_CHECK(!tc_page_fragment(&p, "missing", &x, &y));
    TC_CHECK(!tc_page_fragment(&p, "", &x, &y));
    TC_CHECK(!tc_page_fragment(&p, "%", &x, &y));
    tc_page_free(&p);
    tc_font_set_free(fonts);
}

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t crc32_of(const unsigned char *p, size_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < len; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* Undo PNG's filters (the first byte of each row), in place. */
static void unfilter(unsigned char *data, int width, int height)
{
    size_t stride = 1 + 3 * (size_t)width, i;
    int y;
    for (y = 0; y < height; y++) {
        unsigned char *row = data + (size_t)y * stride + 1, *up = y ? row - stride : NULL;
        int type = row[-1];
        TC_CHECK(type <= 4);
        for (i = 0; i < stride - 1; i++) {
            int a = i >= 3 ? row[i - 3] : 0, b = up ? up[i] : 0, c = i >= 3 && up ? up[i - 3] : 0;
            row[i] = (unsigned char)(row[i] + (type == 1 ? a : type == 2 ? b : type == 3 ? (a + b) / 2
                                                : type == 4 ? paeth(a, b, c) : 0));
        }
    }
}

/* Encode, check the chunks, inflate the image data and compare. */
static void png_round_trip(const tc_surface *s)
{
    tc_buf png, idat, raw;
    const unsigned char *d;
    size_t pos = 8, stride = 1 + 3 * (size_t)s->width;
    int x, y, seen_end = 0, ok = 1;

    tc_buf_init(&png);
    tc_buf_init(&idat);
    tc_buf_init(&raw);
    TC_CHECK_INT(tc_png_encode(s, &png), TC_OK);
    d = (const unsigned char *)png.data;
    TC_CHECK(png.len > 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0);
    while (pos + 12 <= png.len && !seen_end) {
        uint32_t len = be32(d + pos);
        if (pos + 12 + len > png.len)
            break;
        TC_CHECK_INT(be32(d + pos + 8 + len), crc32_of(d + pos + 4, len + 4));
        if (memcmp(d + pos + 4, "IHDR", 4) == 0) {
            TC_CHECK_INT(len, 13);
            TC_CHECK_INT(be32(d + pos + 8), s->width);
            TC_CHECK_INT(be32(d + pos + 12), s->height);
            TC_CHECK_INT(d[pos + 16], 8); /* bit depth */
            TC_CHECK_INT(d[pos + 17], 2); /* RGB */
        } else if (memcmp(d + pos + 4, "IDAT", 4) == 0) {
            tc_buf_append(&idat, d + pos + 8, len);
        } else if (memcmp(d + pos + 4, "IEND", 4) == 0) {
            seen_end = 1;
        }
        pos += 12 + len;
    }
    TC_CHECK(seen_end && pos == png.len);
    TC_CHECK_INT(tc_inflate(TC_CODING_DEFLATE, idat.data, idat.len, 64u << 20, &raw), TC_OK);
    TC_CHECK_INT(raw.len, (size_t)s->height * stride);
    if (raw.len == (size_t)s->height * stride)
        unfilter((unsigned char *)raw.data, s->width, s->height);
    for (y = 0; y < s->height && ok && raw.len == (size_t)s->height * stride; y++) {
        const unsigned char *row = (const unsigned char *)raw.data + (size_t)y * stride + 1;
        for (x = 0; x < s->width && ok; x++) {
            uint32_t c = tc_surface_get(s, x, y);
            ok = row[3 * x] == TC_RED(c) && row[3 * x + 1] == TC_GREEN(c) && row[3 * x + 2] == TC_BLUE(c);
        }
        TC_CHECK(ok);
    }
    tc_buf_free(&png);
    tc_buf_free(&idat);
    tc_buf_free(&raw);
}

static void test_png(void)
{
    tc_surface s;
    int x, y;
    uint32_t seed = 1;

    TC_CHECK_INT(tc_surface_init(&s, 1, 1), TC_OK);
    png_round_trip(&s);
    tc_surface_free(&s);

    /* Runs, repeats and noise, as rendered pages have. */
    TC_CHECK_INT(tc_surface_init(&s, 300, 200), TC_OK);
    tc_surface_fill(&s, tc_rect_make(0, 0, 300, 200), TC_RGB(255, 255, 255));
    tc_surface_fill(&s, tc_rect_make(10, 10, 100, 30), TC_RGB(0, 0, 128));
    for (y = 60; y < 120; y++)
        for (x = 0; x < 300; x++) {
            seed = seed * 1103515245u + 12345u;
            if ((seed >> 16) % 7 == 0)
                tc_surface_fill(&s, tc_rect_make(x, y, 1, 1), seed >> 8 | 0xFF000000u);
        }
    for (y = 150; y < 200; y++)
        tc_surface_fill(&s, tc_rect_make(0, y, 300, 1), TC_RGB(y, 255 - y, (y * 7) & 255));
    png_round_trip(&s);
    tc_surface_free(&s);
}

/* Images served from the test files: the URL's last part is the file
   (any query ignored, so one file can be two pictures). */
typedef struct image_server {
    char asked[1024];
    unsigned count;
} image_server;

static int serve_image(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size)
{
    image_server *s = ctx;
    const char *name = strrchr(url, '/');
    size_t i, len;
    (void)charset_size;
    charset[0] = '\0';
    s->count++;
    if (strlen(s->asked) + strlen(url) + 2 < sizeof s->asked) {
        strcat(s->asked, url);
        strcat(s->asked, " ");
    }
    name = name ? name + 1 : url;
    len = strcspn(name, "?");
    for (i = 0; i < tc_test_blob_count; i++)
        if (strlen(tc_test_blobs[i].name) == len && strncmp(tc_test_blobs[i].name, name, len) == 0)
            return tc_buf_append(body, tc_test_blobs[i].data, tc_test_blobs[i].len);
    return TC_ERR_INVALID;
}

static int image_index_of(const tc_page *p, const char *url)
{
    size_t i;
    for (i = 0; i < tc_page_image_count(p); i++)
        if (strcmp(tc_page_image_url(p, i), url) == 0)
            return (int)i;
    return -1;
}

static void test_image_sources(void)
{
    tc_font_set *fonts = tc_font_set_new();
    tc_page p;
    PAGE_FROM(&p, "http://example.com/dir/page.html",
              "<!DOCTYPE html><style>#bg { background: url(/css/bg.png), url(data:image/gif;base64,"
              "R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs=) } li { list-style-image: url('dot.png') }</style>"
              "<img id=a src=a.png>"
              "<img id=b srcset='b1.png 1x, b2.png 2x' src=b0.png>"
              "<img id=c srcset='c-small.png 200w, c-big.png 800w, c-huge.png 1600w' sizes='(max-width: 300px) 100vw, 50vw'>"
              "<picture><source srcset=d.avif type=image/avif><source srcset=d.webp type=image/webp media='(min-width: 2000px)'>"
              "<source srcset='d.webp 1x' type='image/webp; codecs=vp8'><img id=d src=d.jpg></picture>"
              "<img id=e srcset='e,1.png 1x,e2.png 2x'>"
              "<input type=image src=f.png><div id=bg></div><ul><li>x</ul>");
    TC_CHECK_INT(tc_page_layout(&p, fonts, 400, 300), TC_OK);
    TC_CHECK(image_index_of(&p, "http://example.com/dir/a.png") >= 0);
    TC_CHECK(image_index_of(&p, "http://example.com/dir/b1.png") >= 0);   /* 1x for a 1x display */
    TC_CHECK(image_index_of(&p, "http://example.com/dir/b0.png") < 0);
    TC_CHECK(image_index_of(&p, "http://example.com/dir/c-small.png") >= 0); /* 50vw = 200px: 200w is 1x */
    TC_CHECK(image_index_of(&p, "http://example.com/dir/c-big.png") < 0);
    TC_CHECK(image_index_of(&p, "http://example.com/dir/d.webp") >= 0);    /* avif isn't supported; the media fails */
    TC_CHECK(image_index_of(&p, "http://example.com/dir/d.jpg") < 0);
    TC_CHECK(image_index_of(&p, "http://example.com/dir/e,1.png") >= 0);   /* a comma inside a URL */
    TC_CHECK(image_index_of(&p, "http://example.com/dir/f.png") >= 0);
    TC_CHECK(image_index_of(&p, "http://example.com/css/bg.png") >= 0);
    TC_CHECK(image_index_of(&p, "http://example.com/dir/dot.png") >= 0);
    /* The data: URL is decoded straight away. */
    TC_CHECK_INT(tc_page_image_state(&p, (size_t)image_index_of(&p, "data:image/gif;base64,"
                                                                   "R0lGODlhAQABAIAAAP///wAAACwAAAAAAQABAAACAkQBADs=")),
                 TC_PAGE_IMAGE_LOADED);
    TC_CHECK_INT(tc_page_image_state(&p, (size_t)image_index_of(&p, "http://example.com/dir/a.png")),
                 TC_PAGE_IMAGE_WAITING);
    tc_page_free(&p);
    /* At 280px, sizes says 100vw: 200w is under 1x, so 800w (about 2.9x).
       At 100px, 200w is 2x and wins. */
    PAGE_FROM(&p, "http://example.com/", "<img srcset='s.png 200w, m.png 800w' sizes='(max-width: 300px) 100vw, 50vw'>");
    TC_CHECK_INT(tc_page_layout(&p, fonts, 280, 300), TC_OK);
    TC_CHECK(image_index_of(&p, "http://example.com/m.png") >= 0);
    tc_page_free(&p);
    PAGE_FROM(&p, "http://example.com/", "<img srcset='s.png 200w, m.png 800w' sizes='(max-width: 300px) 100vw, 50vw'>");
    TC_CHECK_INT(tc_page_layout(&p, fonts, 100, 300), TC_OK);
    TC_CHECK(image_index_of(&p, "http://example.com/s.png") >= 0);
    tc_page_free(&p);
    tc_font_set_free(fonts);
}

static void test_image_loading(void)
{
    tc_font_set *fonts = tc_font_set_new();
    image_server srv;
    tc_page p;
    tc_surface s;
    float x, y;
    memset(&srv, 0, sizeof srv);
    PAGE_FROM(&p, "http://example.com/",
              "<!DOCTYPE html><style>body { margin: 0 } img { display: block }</style>"
              "<img id=a src=pattern.png><img id=b src=pattern.png width=8 height=4><img id=c src=missing.png alt=x>"
              "<div id=d style='height: 10px; background: url(halves.jpg) no-repeat'></div>");
    TC_CHECK_INT(tc_page_layout(&p, fonts, 400, 300), TC_OK);
    TC_CHECK_INT(tc_page_image_count(&p), 3);
    /* Before the images: #a has no size yet, #b has its attributes'. */
    TC_CHECK(tc_page_fragment(&p, "c", &x, &y));
    TC_CHECK(y == 4);
    TC_CHECK_INT(tc_page_load_images(&p, fonts, serve_image, &srv, NULL), TC_OK);
    TC_CHECK_INT(srv.count, 3);
    TC_CHECK_INT(tc_page_image_state(&p, (size_t)image_index_of(&p, "http://example.com/missing.png")),
                 TC_PAGE_IMAGE_FAILED);
    /* Laid out again: #a is 4x2 now. */
    TC_CHECK(tc_page_fragment(&p, "c", &x, &y));
    TC_CHECK(y == 6);
    /* Painted: the pattern's red corner, and the JPEG behind #d. */
    if (tc_surface_init(&s, 40, 40) == TC_OK) {
        tc_paint(p.layout, &s, tc_rect_make(0, 0, 40, 40), 0, 0);
        TC_CHECK_INT(tc_surface_get(&s, 0, 0), 0xFF0000);
        TC_CHECK_INT(tc_surface_get(&s, 0, 2) & 0xFFFFFF, 0xFF0000); /* #b, 8x4: its red pixel doubled */
        tc_surface_free(&s);
    }
    tc_page_free(&p);

    /* An arrival only asks for a new layout when layout waited for it. */
    PAGE_FROM(&p, "http://example.com/", "<img src=a.png width=10 height=10><img src=b.png>");
    TC_CHECK_INT(tc_page_layout(&p, fonts, 400, 300), TC_OK);
    if (tc_page_image_count(&p) == 2) {
        const tc_test_blob *png = tc_test_blob_get("pattern.png");
        tc_image *img = NULL;
        TC_CHECK_INT(tc_image_new(png->data, png->len, &img), TC_OK);
        TC_CHECK_INT(tc_page_image_arrived(&p, 0, img), 0);
        img = NULL;
        TC_CHECK_INT(tc_image_new(png->data, png->len, &img), TC_OK);
        TC_CHECK_INT(tc_page_image_arrived(&p, 1, img), 1);
        TC_CHECK_INT(tc_page_image_arrived(&p, 1, NULL), 0); /* twice: ignored */
    }
    tc_page_free(&p);
    tc_font_set_free(fonts);
}

static void test_image_memory(void)
{
    tc_font_set *fonts = tc_font_set_new();
    image_server srv;
    tc_page p;
    tc_surface s;
    memset(&srv, 0, sizeof srv);
    /* Room for one 32x32 image's pixels at a time. */
    PAGE_FROM(&p, "http://example.com/", "<!DOCTYPE html><style>body { margin: 0 }</style>"
                                         "<img src=halves.jpg><img src=halves-progressive.jpg>");
    tc_page_set_image_memory(&p, 32 * 32 * 4 + 100);
    TC_CHECK_INT(tc_page_layout(&p, fonts, 400, 300), TC_OK);
    TC_CHECK_INT(tc_page_load_images(&p, fonts, serve_image, &srv, NULL), TC_OK);
    if (tc_surface_init(&s, 100, 40) == TC_OK) {
        tc_paint(p.layout, &s, tc_rect_make(0, 0, 100, 40), 0, 0);
        TC_CHECK((tc_surface_get(&s, 4, 20) & 0xFF0000) > 0xC00000);  /* the first was drawn... */
        TC_CHECK((tc_surface_get(&s, 36, 20) & 0xFF0000) > 0xC00000); /* ...then the second */
        tc_surface_free(&s);
    }
    tc_page_free(&p);
    tc_font_set_free(fonts);
}

/* The order pictures load in: what's in view first. */
static void test_image_order(void)
{
    static const char *const from_top[] = { "top", "bg", "mid", "bottom", "hidden" };
    static const char *const from_middle[] = { "mid", "bottom", "bg", "top", "hidden" };
    tc_font_set *fonts = tc_font_set_new();
    size_t order[8], n, i;
    tc_page p;
    char url[64];
    PAGE_FROM(&p, "http://example.com/",
              "<!DOCTYPE html><style>body { margin: 0 } img { display: block; width: 10px; height: 10px }"
              "#bg { height: 10px; background: url(bg.png) } .pad { height: 1000px }</style>"
              "<img src=hidden.png style='display: none'><img src=top.png><div id=bg></div><div class=pad></div>"
              "<img src=mid.png><div class=pad></div><img src=bottom.png>");
    TC_CHECK_INT(tc_page_layout(&p, fonts, 400, 300), TC_OK);
    n = tc_page_image_order(&p, 0, 300, order, TC_ARRAY_LEN(order));
    TC_CHECK_INT(n, 5);
    for (i = 0; i < n && i < 5; i++) {
        snprintf(url, sizeof url, "http://example.com/%s.png", from_top[i]);
        TC_CHECK_STR(tc_page_image_url(&p, order[i]), url);
    }
    /* Scrolled to the middle: then below, then above, nearest first. */
    n = tc_page_image_order(&p, 1000, 1300, order, TC_ARRAY_LEN(order));
    TC_CHECK_INT(n, 5);
    for (i = 0; i < n && i < 5; i++) {
        snprintf(url, sizeof url, "http://example.com/%s.png", from_middle[i]);
        TC_CHECK_STR(tc_page_image_url(&p, order[i]), url);
    }
    TC_CHECK_INT(tc_page_image_order(&p, 0, 300, order, 2), 2);
    /* Loaded ones aren't listed. */
    tc_page_image_arrived(&p, order[0], NULL);
    TC_CHECK_INT(tc_page_image_order(&p, 0, 300, order, TC_ARRAY_LEN(order)), 4);
    tc_page_free(&p);
    tc_font_set_free(fonts);
}

/* Inline <svg> is a picture made from its markup. */
static void test_inline_svg(void)
{
    tc_font_set *fonts = tc_font_set_new();
    tc_page p;
    tc_surface s;
    PAGE_FROM(&p, "http://example.com/",
              "<!DOCTYPE html><style>body { margin: 0; color: #00f } svg { display: block }</style>"
              "<svg width=8 height=4><rect width=4 height=4 fill=red /><rect x=4 width=4 height=4 fill=currentColor />"
              "<foreignObject><p>not drawn</p></foreignObject></svg>"
              "<svg viewBox='0 0 2 1' style='width: 20px'><rect width=1 height=1 fill=lime /></svg>");
    TC_CHECK_INT(tc_page_layout(&p, fonts, 400, 300), TC_OK);
    TC_CHECK_INT(tc_page_image_count(&p), 2);
    TC_CHECK_INT(tc_page_image_state(&p, 0), TC_PAGE_IMAGE_LOADED);
    TC_CHECK_INT(tc_page_image_state(&p, 1), TC_PAGE_IMAGE_LOADED);
    if (tc_surface_init(&s, 40, 40) == TC_OK) {
        tc_paint(p.layout, &s, tc_rect_make(0, 0, 40, 40), 0, 0);
        TC_CHECK_INT(tc_surface_get(&s, 1, 1), 0xFF0000);
        TC_CHECK_INT(tc_surface_get(&s, 5, 1), 0x0000FF); /* currentColor: the element's color */
        /* The second is 20px wide, so 10px high, and its left half lime. */
        TC_CHECK_INT(tc_surface_get(&s, 5, 9), 0x00FF00);
        TC_CHECK_INT(tc_surface_get(&s, 15, 9), 0xFFFFFF);
        TC_CHECK_INT(tc_surface_get(&s, 5, 15), 0xFFFFFF);
        tc_surface_free(&s);
    }
    tc_page_free(&p);
    tc_font_set_free(fonts);
}

/* frames.gif: red for 100 ms, a green corner for 200, blue for 300. */
static void test_animation(void)
{
    tc_font_set *fonts = tc_font_set_new();
    image_server srv;
    tc_page p;
    tc_surface s;
    tc_rect dirty;
    memset(&srv, 0, sizeof srv);
    PAGE_FROM(&p, "http://example.com/", "<!DOCTYPE html><style>body { margin: 0 } img { display: block }</style>"
                                         "<img src=frames.gif><img src=pattern.png><div style='height: 1000px'></div>"
                                         "<img src=frames.gif?2>");
    TC_CHECK_INT(tc_page_layout(&p, fonts, 400, 300), TC_OK);
    TC_CHECK_INT(tc_page_load_images(&p, fonts, serve_image, &srv, NULL), TC_OK);
    if (tc_surface_init(&s, 40, 40) != TC_OK)
        return;
    /* Nothing painted yet: nothing to animate. */
    TC_CHECK_INT(tc_page_animate(&p, 1000, &dirty), 0);
    TC_CHECK(tc_rect_is_empty(dirty));
    tc_paint(p.layout, &s, tc_rect_make(0, 0, 40, 40), 0, 0);
    TC_CHECK_INT(tc_surface_get(&s, 0, 0), 0xFF0000);
    /* Painted: the first frame's 100 ms start now. */
    TC_CHECK_INT(tc_page_animate(&p, 1000, &dirty), 100);
    TC_CHECK_INT(tc_page_animate(&p, 1060, &dirty), 40);
    TC_CHECK(tc_rect_is_empty(dirty));
    TC_CHECK_INT(tc_page_animate(&p, 1100, &dirty), 200);
    TC_CHECK(dirty.x == 0 && dirty.y == 0 && dirty.w == 4 && dirty.h == 4);
    tc_paint(p.layout, &s, dirty, 0, 0);
    TC_CHECK_INT(tc_surface_get(&s, 0, 0), 0x00FF00);
    TC_CHECK_INT(tc_surface_get(&s, 3, 3), 0xFF0000);
    TC_CHECK_INT(tc_page_animate(&p, 1150, &dirty), 150);
    /* A late call skips ahead rather than racing through frames. */
    TC_CHECK_INT(tc_page_animate(&p, 2000, &dirty), 300);
    TC_CHECK(!tc_rect_is_empty(dirty));
    /* Scrolled away before the new frame is painted: it pauses. */
    tc_paint(p.layout, &s, tc_rect_make(0, 0, 40, 40), 0, 500);
    TC_CHECK_INT(tc_page_animate(&p, 2300, &dirty), 0);
    TC_CHECK(tc_rect_is_empty(dirty));
    /* The second copy comes into view, in document coordinates. */
    tc_paint(p.layout, &s, tc_rect_make(0, 0, 40, 40), 0, 990);
    TC_CHECK_INT(tc_page_animate(&p, 3000, &dirty), 100);
    TC_CHECK_INT(tc_page_animate(&p, 3100, &dirty), 200);
    TC_CHECK(dirty.x == 0 && dirty.y == 1006 && dirty.w == 4 && dirty.h == 4);
    tc_surface_free(&s);
    tc_page_free(&p);
    tc_font_set_free(fonts);
}

void tc_suite_page(void)
{
    TC_RUN(test_style_sheets);
    TC_RUN(test_import_limits);
    TC_RUN(test_links_and_fragments);
    TC_RUN(test_png);
    TC_RUN(test_image_sources);
    TC_RUN(test_image_loading);
    TC_RUN(test_image_memory);
    TC_RUN(test_image_order);
    TC_RUN(test_animation);
    TC_RUN(test_inline_svg);
}
