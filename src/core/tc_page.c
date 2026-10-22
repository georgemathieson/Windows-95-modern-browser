/* A page: the document, style sheets, styles and layout. */
#include <stdlib.h>
#include <string.h>

#include "tc_css_syntax.h"
#include "tc_fetch.h"
#include "tc_image.h"
#include "tc_log.h"
#include "tc_mime.h"
#include "tc_page.h"
#include "tc_platform.h"
#include "tc_utf8.h"

#define MAX_SHEETS  64 /* style sheets and imports per page */
#define MAX_IMPORTS 4  /* nested @import depth */

static int fetch_as(void *ctx, const char *url, const char *accept, tc_buf *body, char *charset, size_t charset_size)
{
    tc_fetch_options o = *(const tc_fetch_options *)ctx;
    tc_response r;
    tc_mime mime;
    int err;

    o.accept = accept;
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

int tc_page_fetch_http(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size)
{
    return fetch_as(ctx, url, "text/css,*/*;q=0.1", body, charset, charset_size);
}

/* Only formats Tachyon reads: servers that see image/avif may send it. */
int tc_page_fetch_image_http(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size)
{
    return fetch_as(ctx, url, "image/webp,image/png,image/jpeg,image/gif,image/*;q=0.8,*/*;q=0.5", body, charset,
                    charset_size);
}

/* Any type: browsers ask for fonts so. */
int tc_page_fetch_font_http(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size)
{
    return fetch_as(ctx, url, "*/*", body, charset, charset_size);
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

static void images_free(struct tc_page_images *im);
static void fonts_free(struct tc_page_fonts *pf);

void tc_page_free(tc_page *p)
{
    images_free(p->images);
    tc_layout_free(p->layout);
    fonts_free(p->webfonts); /* after the layout, whose text uses its fonts */
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

/* ---- Pictures ---- */

#define DEFAULT_IMAGE_MEMORY (16u << 20)
#define MAX_PAGE_IMAGES 4096

typedef struct page_image {
    char *url;                /* absolute */
    tc_image *image;          /* once it's arrived */
    uint8_t state;            /* TC_PAGE_IMAGE_* */
    uint8_t waited;           /* layout wanted its size before it came */
    uint8_t shown;            /* an animation painted since its frame changed */
    uint8_t ended;            /* an animation that has played all its loops */
    uint32_t drawn;           /* when its pixels were last asked for */
    tc_rect area;             /* where it's been painted since its frame changed */
    uint32_t due;             /* when its next frame is (tc_page_animate's clock), or 0 */
} page_image;

/* A pointer (an element, a style's url()) and the picture it shows. */
typedef struct ref {
    const void *key;
    uint32_t index;
    float density;
} ref;

struct tc_page_images {
    page_image *items;
    uint32_t n, cap;
    ref *refs;                /* open addressing, by key */
    uint32_t refs_cap, nrefs;
    size_t memory, decoded;   /* the budget, and bytes decoded */
    uint32_t clock;
    tc_url *base;             /* the page's base URL */
    tc_image_source source;
};

static void images_free(struct tc_page_images *im)
{
    uint32_t i;
    if (!im)
        return;
    for (i = 0; i < im->n; i++) {
        free(im->items[i].url);
        tc_image_free(im->items[i].image);
    }
    free(im->items);
    free(im->refs);
    free(im);
}

static uint32_t hash_ptr(const void *p)
{
    uint32_t h = (uint32_t)(uintptr_t)p;
    h ^= h >> 16;
    h *= 0x45D9F3Bu;
    return h ^ (h >> 16);
}

static const ref *ref_find(const struct tc_page_images *im, const void *key)
{
    uint32_t i;
    if (!im->refs_cap)
        return NULL;
    for (i = hash_ptr(key) & (im->refs_cap - 1); im->refs[i].key; i = (i + 1) & (im->refs_cap - 1))
        if (im->refs[i].key == key)
            return &im->refs[i];
    return NULL;
}

static int ref_add(struct tc_page_images *im, const void *key, uint32_t index, float density)
{
    uint32_t i;
    if (2 * (im->nrefs + 1) > im->refs_cap) {
        uint32_t cap = im->refs_cap ? im->refs_cap * 2 : 64, k;
        ref *grown = calloc(cap, sizeof *grown);
        if (!grown)
            return TC_ERR_NOMEM;
        for (k = 0; k < im->refs_cap; k++)
            if (im->refs[k].key) {
                for (i = hash_ptr(im->refs[k].key) & (cap - 1); grown[i].key; i = (i + 1) & (cap - 1))
                    ;
                grown[i] = im->refs[k];
            }
        free(im->refs);
        im->refs = grown;
        im->refs_cap = cap;
    }
    for (i = hash_ptr(key) & (im->refs_cap - 1); im->refs[i].key; i = (i + 1) & (im->refs_cap - 1))
        if (im->refs[i].key == key)
            break;
    if (!im->refs[i].key)
        im->nrefs++;
    im->refs[i].key = key;
    im->refs[i].index = index;
    im->refs[i].density = density;
    return TC_OK;
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* A data: URL's bytes (RFC 2397: base64 or percent-encoded). */
static int data_url(const char *url, tc_buf *out)
{
    const char *comma = strchr(url, ','), *p;
    int base64 = 0;
    if (!comma)
        return TC_ERR_INVALID;
    for (p = url + 5; p + 7 <= comma; p++)
        if (strncmp(p, ";base64", 7) == 0 && (p + 7 == comma || p[7] == ';'))
            base64 = 1;
    p = comma + 1;
    if (base64) {
        uint32_t acc = 0, bits = 0;
        for (; *p; p++) {
            int v = *p >= 'A' && *p <= 'Z' ? *p - 'A' : *p >= 'a' && *p <= 'z' ? *p - 'a' + 26
                    : *p >= '0' && *p <= '9' ? *p - '0' + 52 : *p == '+' || *p == '-' ? 62
                    : *p == '/' || *p == '_' ? 63 : -1;
            if (v < 0) {
                if (*p == '%' && hex_digit(p[1]) >= 0 && hex_digit(p[2]) >= 0)
                    p += 2; /* a percent-encoded '=' or newline */
                continue;
            }
            acc = acc << 6 | (uint32_t)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                if (tc_buf_append_char(out, (char)(acc >> bits & 255)) != TC_OK)
                    return TC_ERR_NOMEM;
            }
        }
        return TC_OK;
    }
    for (; *p; p++) {
        char c = *p;
        if (c == '%' && hex_digit(p[1]) >= 0 && hex_digit(p[2]) >= 0) {
            c = (char)(hex_digit(p[1]) << 4 | hex_digit(p[2]));
            p += 2;
        }
        if (tc_buf_append_char(out, c) != TC_OK)
            return TC_ERR_NOMEM;
    }
    return TC_OK;
}

/* A new picture at the end of the table, for url. */
static int add_image(struct tc_page_images *im, const char *url, page_image **out)
{
    page_image *e;
    if (im->n >= MAX_PAGE_IMAGES)
        return TC_ERR_TOO_LARGE;
    if (im->n == im->cap) {
        uint32_t cap = im->cap ? im->cap * 2 : 16;
        page_image *grown = realloc(im->items, cap * sizeof *grown);
        if (!grown)
            return TC_ERR_NOMEM;
        im->items = grown;
        im->cap = cap;
    }
    e = &im->items[im->n];
    memset(e, 0, sizeof *e);
    if (!(e->url = malloc(strlen(url) + 1)))
        return TC_ERR_NOMEM;
    strcpy(e->url, url);
    *out = e;
    return TC_OK;
}

/* The picture for an absolute URL, added if it's new. */
static int image_index(struct tc_page_images *im, const char *url, uint32_t *index)
{
    uint32_t i;
    page_image *e;
    int err;
    for (i = 0; i < im->n; i++)
        if (strcmp(im->items[i].url, url) == 0) {
            *index = i;
            return TC_OK;
        }
    if ((err = add_image(im, url, &e)) != TC_OK)
        return err;
    if (strncmp(url, "data:", 5) == 0) {
        /* Nothing to fetch. */
        tc_buf bytes;
        tc_buf_init(&bytes);
        e->state = TC_PAGE_IMAGE_FAILED;
        if (data_url(url, &bytes) == TC_OK && tc_image_new(bytes.data ? bytes.data : "", bytes.len, &e->image) == TC_OK)
            e->state = TC_PAGE_IMAGE_LOADED;
        tc_buf_free(&bytes);
    }
    *index = im->n++;
    return TC_OK;
}

/* Resolve a URL against base (NULL: the page's) and find its picture. */
static int image_for(struct tc_page_images *im, const char *url, const char *base, uint32_t *index)
{
    tc_url abs, base_url;
    int err;
    if (base) {
        if (tc_url_parse(base, NULL, &base_url) != TC_OK)
            return TC_ERR_URL;
        err = tc_url_parse(url, &base_url, &abs);
        tc_url_free(&base_url);
    } else {
        err = tc_url_parse(url, im->base, &abs);
    }
    if (err)
        return err;
    err = image_index(im, abs.href, index);
    tc_url_free(&abs);
    return err;
}

/* ---- Inline <svg> ---- */

/* "currentColor", in any case. */
static int current_color(const char *s)
{
    static const char word[] = "currentcolor";
    size_t i;
    for (i = 0; i < sizeof word - 1; i++)
        if ((s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]) != word[i])
            return 0;
    return 1;
}

/* Text for an attribute or a style element, safe inside NanoSVG's quotes
   and tags (it reads no entities), with currentColor made color. */
static int svg_text(tc_buf *out, const char *s, size_t len, tc_color color)
{
    size_t i;
    int err = TC_OK;
    for (i = 0; i < len && !err; i++) {
        if (len - i >= 12 && current_color(s + i)) {
            err = tc_buf_printf(out, "#%06lx", (unsigned long)(color & 0xFFFFFF));
            i += 11;
        } else {
            char c = s[i] == '"' ? '\'' : s[i] == '<' ? ' ' : s[i];
            err = tc_buf_append(out, &c, 1);
        }
    }
    return err;
}

/* An <svg> element as markup for NanoSVG: its SVG elements, their
   attributes, and the text of style elements. */
static int svg_markup(const tc_element *el, const tc_styled *styled, tc_color color, tc_buf *out, int depth)
{
    const tc_element_styles *es = tc_styled_element(styled, el);
    const tc_node *c;
    uint32_t i;
    int err;
    if (es && es->style)
        color = es->style->color;
    err = tc_buf_printf(out, "<%s", el->name);
    for (i = 0; i < el->attr_count && !err; i++) {
        const tc_attr *a = &el->attrs[i];
        err = tc_buf_printf(out, " %s%s%s=\"", a->prefix ? a->prefix : "", a->prefix ? ":" : "", a->name);
        if (!err)
            err = svg_text(out, a->value, a->value_len, color);
        if (!err)
            err = tc_buf_append(out, "\"", 1);
    }
    if (!err)
        err = tc_buf_append(out, ">", 1);
    for (c = el->node.first_child; c && !err && depth < 100; c = c->next) {
        if (c->type == TC_NODE_ELEMENT && c->ns == TC_NS_SVG && c->tag != TC_TAG_FOREIGNOBJECT)
            err = svg_markup((const tc_element *)c, styled, color, out, depth + 1);
        else if (c->type == TC_NODE_TEXT && tc_dom_is(&el->node, TC_NS_SVG, TC_TAG_STYLE))
            err = svg_text(out, ((const tc_chardata *)c)->data, ((const tc_chardata *)c)->len, color);
    }
    return err ? err : tc_buf_printf(out, "</%s>", el->name);
}

/* An inline <svg>'s picture, made from its markup the first time. */
static int inline_svg(struct tc_page_images *im, const tc_element *el, const tc_styled *styled)
{
    page_image *e;
    tc_buf markup;
    int err;
    if (ref_find(im, el))
        return TC_OK;
    if ((err = add_image(im, "", &e)) != TC_OK) /* no URL: never shared */
        return err;
    tc_buf_init(&markup);
    e->state = TC_PAGE_IMAGE_FAILED;
    if (svg_markup(el, styled, 0xFF000000u, &markup, 0) == TC_OK &&
        tc_image_new(markup.data ? markup.data : "", markup.len, &e->image) == TC_OK)
        e->state = TC_PAGE_IMAGE_LOADED;
    tc_buf_free(&markup);
    im->n++;
    return ref_add(im, el, im->n - 1, 1);
}

/* ---- Choosing an <img>'s source (the HTML Standard's srcset and sizes) ---- */

typedef struct candidate {
    const char *url;
    size_t len;
    float density;   /* x descriptor; or, for a w one, -width */
} candidate;

/* Parse srcset into candidates (up to max). */
static size_t parse_srcset(const char *s, candidate *out, size_t max)
{
    size_t n = 0;
    while (*s && n < max) {
        const char *start, *end;
        float density = 1;
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == ',')
            s++;
        if (!*s)
            break;
        start = s;
        while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && *s != '\f')
            s++;
        end = s;
        if (end > start && end[-1] == ',') {
            while (end > start && end[-1] == ',')
                end--;
        } else {
            /* Descriptors, up to the next comma outside parentheses. */
            int depth = 0;
            const char *d = s;
            while (*s && (depth || *s != ',')) {
                if (*s == '(')
                    depth++;
                else if (*s == ')' && depth)
                    depth--;
                s++;
            }
            while (d < s) {
                char *num_end;
                double v;
                while (d < s && (*d == ' ' || *d == '\t' || *d == '\n'))
                    d++;
                if (d >= s)
                    break;
                v = strtod(d, &num_end);
                if (num_end > d && num_end < s && (*num_end == 'x' || *num_end == 'w') && v > 0 && v < 1e6)
                    density = *num_end == 'x' ? (float)v : -(float)v;
                while (d < s && *d != ' ' && *d != '\t' && *d != '\n')
                    d++;
            }
        }
        if (end > start) {
            out[n].url = start;
            out[n].len = (size_t)(end - start);
            out[n].density = density;
            n++;
        }
    }
    return n;
}

/* The sizes attribute's answer for this viewport, in CSS pixels. */
static float sizes_width(const char *sizes, const tc_media_env *env)
{
    tc_arena a;
    tc_css_value *v;
    uint32_t nv, i, start = 0;
    float width = env->width;
    if (!sizes)
        return width;
    tc_arena_init(&a);
    if (tc_css_parse_values(&a, sizes, strlen(sizes), &v, &nv) == TC_OK) {
        for (i = 0; i <= nv; i++) {
            uint32_t last, k;
            tc_media_list *media = NULL;
            if (i < nv && v[i].type != TC_CSS_COMMA)
                continue;
            /* The last component is the length; any before it, the condition. */
            for (last = i; last > start && v[last - 1].type == TC_CSS_WHITESPACE; last--)
                ;
            if (last > start) {
                const tc_calc *calc = tc_css_parse_calc(&a, &v[last - 1], TC_ALLOW_LENGTH);
                for (k = start; k < last - 1 && v[k].type == TC_CSS_WHITESPACE; k++)
                    ;
                if (calc && (k == last - 1 || (tc_media_parse(&a, &v[k], last - 1 - k, &media) == TC_OK &&
                                               tc_media_matches(media, env)))) {
                    tc_length_basis b;
                    tc_length len;
                    b.font_size = b.root_font_size = 16;
                    b.viewport_w = env->width;
                    b.viewport_h = env->height;
                    if (tc_calc_compute(&a, calc, &b, &len) == TC_OK) {
                        width = tc_length_resolve(&len, env->width);
                        break;
                    }
                }
            }
            start = i + 1;
        }
    }
    tc_arena_free(&a);
    return width > 0 ? width : env->width;
}

static int supported_type(const char *type)
{
    static const char *const types[] = { "image/png", "image/apng", "image/jpeg", "image/jpg", "image/pjpeg",
                                         "image/gif", "image/webp", "image/bmp", "image/x-icon",
                                         "image/vnd.microsoft.icon" };
    tc_mime m;
    size_t i;
    if (tc_mime_parse(type, &m) != TC_OK)
        return 0;
    for (i = 0; i < TC_ARRAY_LEN(types); i++)
        if (strcmp(m.essence, types[i]) == 0)
            return 1;
    return 0;
}

/* Pick from srcset (and src, as 1x): the lowest density that's still at
   least 1, or failing that the highest. */
static int pick(const char *srcset, const char *sizes, const char *src, const tc_media_env *env, char *url,
                size_t url_size, float *density)
{
    candidate c[64];
    size_t n = srcset ? parse_srcset(srcset, c, TC_ARRAY_LEN(c) - 1) : 0, i, best = (size_t)-1;
    float w = 0;
    int have_1x = 0;
    for (i = 0; i < n; i++) {
        if (c[i].density < 0) {
            if (!w)
                w = sizes_width(sizes, env);
            c[i].density = -c[i].density / w;
        }
        have_1x |= c[i].density == 1;
    }
    if (src && *src && !have_1x) {
        c[n].url = src;
        c[n].len = strlen(src);
        c[n].density = 1;
        n++;
    }
    for (i = 0; i < n; i++)
        if (c[i].density >= 1 && (best == (size_t)-1 || c[i].density < c[best].density))
            best = i;
    if (best == (size_t)-1)
        for (i = 0; i < n; i++)
            if (best == (size_t)-1 || c[i].density > c[best].density)
                best = i;
    if (best == (size_t)-1 || c[best].len >= url_size)
        return 0;
    memcpy(url, c[best].url, c[best].len);
    url[c[best].len] = '\0';
    *density = c[best].density;
    return 1;
}

/* An <img>'s source, or <input type=image>'s. */
static int choose_source(const tc_element *el, const tc_media_env *env, char *url, size_t url_size, float *density)
{
    const tc_element *parent = tc_element_parent(el);
    if (tc_dom_is_html(&el->node, TC_TAG_INPUT))
        return pick(NULL, NULL, tc_dom_get_attr(el, "src"), env, url, url_size, density);
    if (parent && tc_dom_is_html(&parent->node, TC_TAG_PICTURE)) {
        const tc_node *n;
        for (n = parent->node.first_child; n && n != &el->node; n = n->next) {
            const tc_element *source = (const tc_element *)n;
            const char *media, *type, *srcset;
            if (!tc_dom_is_html(n, TC_TAG_SOURCE) || !(srcset = tc_dom_get_attr(source, "srcset")))
                continue;
            if ((type = tc_dom_get_attr(source, "type")) != NULL && !supported_type(type))
                continue;
            if ((media = tc_dom_get_attr(source, "media")) != NULL) {
                tc_arena a;
                tc_media_list *list = NULL;
                int match;
                tc_arena_init(&a);
                match = tc_media_parse_text(&a, media, &list) == TC_OK && tc_media_matches(list, env);
                tc_arena_free(&a);
                if (!match)
                    continue;
            }
            if (pick(srcset, tc_dom_get_attr(source, "sizes"), NULL, env, url, url_size, density))
                return 1;
        }
    }
    return pick(tc_dom_get_attr(el, "srcset"), tc_dom_get_attr(el, "sizes"), tc_dom_get_attr(el, "src"), env, url,
                url_size, density);
}

/* ---- The image source layout and painting use ---- */

static tc_image *source_element(void *ctx, const tc_element *el, int sizing, float *density)
{
    struct tc_page_images *im = ctx;
    const ref *r = ref_find(im, el);
    page_image *e;
    if (!r)
        return NULL;
    e = &im->items[r->index];
    if (e->state != TC_PAGE_IMAGE_LOADED) {
        if (sizing && e->state == TC_PAGE_IMAGE_WAITING)
            e->waited = 1;
        return NULL;
    }
    *density = r->density;
    return e->image;
}

static tc_image *source_url(void *ctx, const tc_css_image *css)
{
    struct tc_page_images *im = ctx;
    const ref *r = ref_find(im, css);
    return r && im->items[r->index].state == TC_PAGE_IMAGE_LOADED ? im->items[r->index].image : NULL;
}

static const tc_surface *source_pixels(void *ctx, tc_image *img, tc_rect where, int w, int h)
{
    struct tc_page_images *im = ctx;
    uint32_t i, mine = 0;
    int err;
    for (i = 0; i < im->n; i++)
        if (im->items[i].image == img)
            mine = i;
    im->items[mine].drawn = ++im->clock;
    if (tc_image_frames(img) > 1 && !tc_rect_is_empty(where)) {
        im->items[mine].shown = 1;
        im->items[mine].area = tc_rect_union(im->items[mine].area, where);
    }
    if (tc_image_scalable(img)) {
        size_t before = tc_image_memory(img);
        tc_image_want_size(img, w, h); /* may drop what was drawn, to draw it bigger */
        im->decoded -= TC_MIN(im->decoded, before - tc_image_memory(img));
    }
    if (tc_image_pixels(img))
        return tc_image_pixels(img);
    err = tc_image_decode(img, (uint32_t)TC_MIN(im->memory / 16, 0xFFFFFFFFu));
    if (!tc_image_pixels(img)) {
        if (err == TC_ERR_TOO_LARGE)
            im->items[mine].state = TC_PAGE_IMAGE_FAILED; /* drawn as its alt text from now on */
        return NULL;
    }
    im->decoded += tc_image_memory(img);
    im->items[mine].ended = 0; /* back at the first frame */
    im->items[mine].due = 0;
    /* Over budget: drop the pixels drawn longest ago. */
    while (im->decoded > im->memory) {
        uint32_t oldest = (uint32_t)-1, victim = 0;
        for (i = 0; i < im->n; i++) {
            page_image *e = &im->items[i];
            if (e->image && e->image != img && tc_image_pixels(e->image) && e->drawn < oldest)
                oldest = e->drawn, victim = i;
        }
        if (oldest == (uint32_t)-1)
            break;
        im->decoded -= TC_MIN(im->decoded, tc_image_memory(im->items[victim].image));
        tc_image_discard(im->items[victim].image);
    }
    return tc_image_pixels(img);
}

static struct tc_page_images *page_images(tc_page *p)
{
    if (!p->images && (p->images = calloc(1, sizeof *p->images)) != NULL) {
        p->images->memory = DEFAULT_IMAGE_MEMORY;
        p->images->source.element = source_element;
        p->images->source.url = source_url;
        p->images->source.pixels = source_pixels;
        p->images->source.ctx = p->images;
    }
    return p->images;
}

static int add_css_image(struct tc_page_images *im, const tc_css_image *css)
{
    uint32_t index;
    int err;
    if (!css || css->type != TC_CSS_IMAGE_URL || ref_find(im, css))
        return TC_OK;
    err = image_for(im, css->url, css->base, &index);
    if (err == TC_ERR_NOMEM)
        return err;
    return err ? TC_OK : ref_add(im, css, index, 1);
}

static int add_style_images(struct tc_page_images *im, const tc_style *s)
{
    uint32_t i;
    int err = TC_OK;
    if (!s)
        return TC_OK;
    for (i = 0; s->backgrounds && i < s->backgrounds->count && !err; i++)
        err = add_css_image(im, s->backgrounds->layers[i].image);
    if (!err)
        err = add_css_image(im, s->list_style_image);
    return err;
}

/* The pictures the page uses, in document order. */
static int find_images(tc_page *p, const tc_styled *styled, const tc_media_env *env)
{
    struct tc_page_images *im = page_images(p);
    const tc_node *root, *n;
    uint32_t i;
    int err = TC_OK;
    if (!im)
        return TC_ERR_NOMEM;
    im->base = &p->base;
    root = &p->html.doc->node;
    for (i = 0; i < im->n; i++) { /* a new layout puts them elsewhere */
        im->items[i].shown = 0;
        im->items[i].area = tc_rect_make(0, 0, 0, 0);
    }
    for (n = tc_dom_next(root, root); n && !err; n = tc_dom_next(root, n)) {
        const tc_element *el = (const tc_element *)n;
        const tc_element_styles *es;
        if (n->type != TC_NODE_ELEMENT)
            continue;
        if (tc_dom_is(n, TC_NS_SVG, TC_TAG_SVG) && (!n->parent || n->parent->ns != TC_NS_SVG)) {
            err = inline_svg(im, el, styled);
            if (err != TC_ERR_NOMEM)
                err = TC_OK;
        } else if (tc_dom_is_html(n, TC_TAG_IMG) ||
            (tc_dom_is_html(n, TC_TAG_INPUT) && tc_dom_get_attr(el, "type") &&
             tc_css_ascii_ieq(tc_dom_get_attr(el, "type"), "image"))) {
            char url[4096];
            float density = 1;
            uint32_t index;
            if (choose_source(el, env, url, sizeof url, &density)) {
                err = image_for(im, url, NULL, &index);
                if (!err)
                    err = ref_add(im, el, index, density);
                else if (err != TC_ERR_NOMEM)
                    err = TC_OK;
            }
        }
        es = tc_styled_element(styled, el);
        if (es && !err) {
            err = add_style_images(im, es->style);
            if (!err)
                err = add_style_images(im, es->before);
            if (!err)
                err = add_style_images(im, es->after);
            if (!err)
                err = add_style_images(im, es->marker);
        }
    }
    return err == TC_ERR_NOMEM ? err : TC_OK;
}

size_t tc_page_image_count(const tc_page *p)
{
    return p->images ? p->images->n : 0;
}

const char *tc_page_image_url(const tc_page *p, size_t i)
{
    return p->images && i < p->images->n ? p->images->items[i].url : "";
}

int tc_page_image_state(const tc_page *p, size_t i)
{
    return p->images && i < p->images->n ? p->images->items[i].state : TC_PAGE_IMAGE_FAILED;
}

int tc_page_image_arrived(tc_page *p, size_t i, tc_image *img)
{
    struct tc_page_images *im = p->images;
    page_image *e;
    if (!im || i >= im->n || im->items[i].state != TC_PAGE_IMAGE_WAITING) {
        tc_image_free(img);
        return 0;
    }
    e = &im->items[i];
    e->image = img;
    e->state = img ? TC_PAGE_IMAGE_LOADED : TC_PAGE_IMAGE_FAILED;
    return e->waited && img;
}

/* ---- Which pictures to load first ---- */

#define MAX_WALK_DEPTH 400 /* more than layout nests boxes */

typedef struct load_rank {
    int group;   /* 0: in view, 1: below it, 2: above it, 3: not shown */
    float key;   /* nearest the view first */
    size_t index;
} load_rank;

typedef struct ranking {
    const struct tc_page_images *im;
    float top, bottom;
    load_rank *ranks;  /* by picture */
} ranking;

static void rank_image(ranking *r, const void *key, float y, float h)
{
    const ref *found = key ? ref_find(r->im, key) : NULL;
    load_rank rank, *best;
    if (!found)
        return;
    if (y < r->bottom && y + h > r->top)
        rank.group = 0, rank.key = y;
    else if (y >= r->bottom)
        rank.group = 1, rank.key = y;
    else
        rank.group = 2, rank.key = -y;
    best = &r->ranks[found->index];
    if (rank.group < best->group || (rank.group == best->group && rank.key < best->key))
        best->group = rank.group, best->key = rank.key;
}

static void rank_box(ranking *r, const tc_box *b, float y, float h)
{
    const tc_style *s = b->style;
    uint32_t i;
    if (b->element)
        rank_image(r, b->element, y, h);
    if (!s)
        return;
    for (i = 0; s->backgrounds && i < s->backgrounds->count; i++)
        rank_image(r, s->backgrounds->layers[i].image, y, h);
    rank_image(r, s->list_style_image, y, h);
}

/* Every picture's best place, in one walk of the box tree. */
static void rank_boxes(ranking *r, const tc_box *b, float oy, int depth)
{
    float y = oy + b->y;
    const tc_box *c;
    uint32_t i;
    if (depth > MAX_WALK_DEPTH)
        return;
    rank_box(r, b, y, b->h);
    for (i = 0; i < b->nfrags; i++) {
        const tc_frag *f = &b->frags[i];
        if (f->type == TC_FRAG_ATOMIC)
            rank_boxes(r, f->box, y, depth + 1);
        else if (f->type == TC_FRAG_BOX)
            rank_box(r, f->box, y + f->y, f->h);
    }
    if (!b->has_lines)
        for (c = b->first; c; c = c->next)
            rank_boxes(r, c, y, depth + 1);
}

static int compare_ranks(const void *pa, const void *pb)
{
    const load_rank *a = pa, *b = pb;
    if (a->group != b->group)
        return a->group < b->group ? -1 : 1;
    if (a->key != b->key)
        return a->key < b->key ? -1 : 1;
    return a->index < b->index ? -1 : a->index > b->index;
}

size_t tc_page_image_order(const tc_page *p, float top, float bottom, size_t *order, size_t max)
{
    const struct tc_page_images *im = p->images;
    ranking r;
    size_t i, n = 0;
    if (!im)
        return 0;
    r.im = im;
    r.top = top;
    r.bottom = bottom;
    r.ranks = malloc((im->n ? im->n : 1) * sizeof *r.ranks);
    for (i = 0; r.ranks && i < im->n; i++) {
        r.ranks[i].group = 3;
        r.ranks[i].key = 0;
        r.ranks[i].index = i;
    }
    if (r.ranks && p->layout && tc_layout_root(p->layout))
        rank_boxes(&r, tc_layout_root(p->layout), 0, 0);
    for (i = 0; i < im->n; i++) /* the waiting ones, together */
        if (im->items[i].state == TC_PAGE_IMAGE_WAITING) {
            if (r.ranks)
                r.ranks[n++] = r.ranks[i];
            else if (n < max)
                order[n++] = i; /* no memory to sort: document order */
        }
    if (r.ranks) {
        qsort(r.ranks, n, sizeof *r.ranks, compare_ranks);
        n = TC_MIN(n, max);
        for (i = 0; i < n; i++)
            order[i] = r.ranks[i].index;
        free(r.ranks);
    }
    return n;
}

int tc_page_load_images(tc_page *p, tc_font_set *fonts, tc_page_fetch_fn fetch, void *ctx,
                        void (*progress)(void *ctx, unsigned n, const char *url))
{
    size_t k, n = tc_page_image_count(p), *order = malloc((n ? n : 1) * sizeof *order);
    unsigned loaded = 0;
    int relayout = 0;
    if (!order)
        return TC_ERR_NOMEM;
    n = tc_page_image_order(p, 0, p->height, order, n);
    for (k = 0; k < n; k++) {
        size_t i = order[k];
        tc_buf body;
        char charset[64];
        tc_image *img = NULL;
        if (progress)
            progress(ctx, ++loaded, tc_page_image_url(p, i));
        tc_buf_init(&body);
        if (fetch(ctx, tc_page_image_url(p, i), &body, charset, sizeof charset) == TC_OK)
            tc_image_new(body.data ? body.data : "", body.len, &img);
        tc_buf_free(&body);
        relayout |= tc_page_image_arrived(p, i, img);
    }
    free(order);
    return relayout && p->layout ? tc_page_layout(p, fonts, p->width, p->height) : TC_OK;
}

uint32_t tc_page_animate(tc_page *p, uint32_t now_ms, tc_rect *dirty)
{
    struct tc_page_images *im = p->images;
    uint32_t i, wait = 0;
    *dirty = tc_rect_make(0, 0, 0, 0);
    for (i = 0; im && i < im->n; i++) {
        page_image *e = &im->items[i];
        uint32_t delay;
        if (e->state != TC_PAGE_IMAGE_LOADED || e->ended || tc_image_frames(e->image) <= 1)
            continue;
        if (!e->shown || !tc_image_pixels(e->image)) {
            e->due = 0; /* off screen: paused until it's painted again */
            continue;
        }
        if (!e->due) {
            e->due = now_ms + tc_image_delay(e->image);
        } else if ((int32_t)(now_ms - e->due) >= 0) {
            size_t before = tc_image_memory(e->image);
            if (!tc_image_next_frame(e->image, &delay)) {
                e->ended = 1;
                continue;
            }
            im->decoded = im->decoded - TC_MIN(im->decoded, before) + tc_image_memory(e->image);
            *dirty = tc_rect_union(*dirty, e->area);
            e->shown = 0;
            e->area = tc_rect_make(0, 0, 0, 0);
            e->due += delay;
            if ((int32_t)(now_ms - e->due) >= 0) /* fallen behind: skip ahead, don't race */
                e->due = now_ms + delay;
        }
        delay = e->due - now_ms;
        if (!wait || delay < wait)
            wait = delay ? delay : 1;
    }
    return wait;
}

void tc_page_set_image_memory(tc_page *p, size_t bytes)
{
    if (page_images(p))
        p->images->memory = bytes;
}

/* ---- Web fonts ---- */

#define MAX_PAGE_FONTS 256
#define PLAN_FAMILIES  8
#define PLAN_FACES     64
#define PLAN_CACHE     32

typedef struct page_font {
    const tc_font_face *rule;
    char *url;                 /* the src being tried, absolute; NULL while unused */
    uint32_t src;              /* its index in the rule's urls */
    uint8_t state;             /* TC_PAGE_FONT_* */
} page_font;

struct tc_page_fonts {
    tc_font_set *set;          /* the page's fonts, in front of the installed ones */
    page_font *items;
    uint32_t n, cap;
};

static void fonts_free(struct tc_page_fonts *pf)
{
    uint32_t i;
    if (!pf)
        return;
    for (i = 0; i < pf->n; i++)
        free(pf->items[i].url);
    free(pf->items);
    tc_font_set_free(pf->set);
    free(pf);
}

/* A URL resolved against base (NULL: the page's), malloc'd. */
static char *resolve(const tc_page *p, const char *url, const char *base)
{
    tc_url abs, base_url;
    char *out = NULL;
    int err;
    if (base) {
        if (tc_url_parse(base, NULL, &base_url) != TC_OK)
            return NULL;
        err = tc_url_parse(url, &base_url, &abs);
        tc_url_free(&base_url);
    } else {
        err = tc_url_parse(url, &p->base, &abs);
    }
    if (err)
        return NULL;
    if ((out = malloc(strlen(abs.href) + 1)) != NULL)
        strcpy(out, abs.href);
    tc_url_free(&abs);
    return out;
}

/* On to the rule's src from e->src on: waiting for it, or failed if
   there are no more. */
static void next_src(const tc_page *p, page_font *e)
{
    free(e->url);
    e->url = NULL;
    for (; e->src < e->rule->nurls; e->src++)
        if ((e->url = resolve(p, e->rule->urls[e->src], e->rule->base)) != NULL) {
            e->state = TC_PAGE_FONT_WAITING;
            return;
        }
    e->state = TC_PAGE_FONT_FAILED;
}

/* The @font-face rules in force, added to the page's list. The first
   gives the page its own font set. */
static int find_fonts(tc_page *p, tc_font_set *installed, const tc_media_env *env)
{
    struct tc_page_fonts *pf = p->webfonts;
    const tc_font_face **rules;
    size_t n, i;
    uint32_t k;
    int err = tc_style_font_faces(p->sources, p->nsources, env, &rules, &n);
    if (err || !n) {
        free((void *)rules);
        return err;
    }
    if (!pf) {
        if (!(pf = calloc(1, sizeof *pf)) || !(pf->set = tc_font_set_new_child(installed))) {
            free(pf);
            free((void *)rules);
            return TC_ERR_NOMEM;
        }
        p->webfonts = pf;
    }
    for (i = 0; i < n && pf->n < MAX_PAGE_FONTS; i++) {
        for (k = 0; k < pf->n && pf->items[k].rule != rules[i]; k++)
            ;
        if (k < pf->n)
            continue;
        if (pf->n == pf->cap) {
            uint32_t cap = pf->cap ? pf->cap * 2 : 8;
            page_font *grown = realloc(pf->items, cap * sizeof *grown);
            if (!grown) {
                free((void *)rules);
                return TC_ERR_NOMEM;
            }
            pf->items = grown;
            pf->cap = cap;
        }
        memset(&pf->items[pf->n], 0, sizeof pf->items[pf->n]);
        pf->items[pf->n++].rule = rules[i];
    }
    free((void *)rules);
    return TC_OK;
}

/* For one font-family, weight and style: the web faces CSS font matching
   would choose for each family, up to the first installed family. */
typedef struct plan {
    const tc_font_families *families;
    int weight, italic;
    uint32_t nfaces, ngroups;
    page_font *faces[PLAN_FACES];
    uint8_t group_end[PLAN_FAMILIES];
} plan;

typedef struct want_ctx {
    tc_page *p;
    tc_font_set *installed;
    plan cache[PLAN_CACHE];
    int wanted;                /* fonts newly waiting */
} want_ctx;

static const plan *plan_for(want_ctx *w, const tc_style *s)
{
    struct tc_page_fonts *pf = w->p->webfonts;
    int italic = s->font_style != TC_FONT_NORMAL;
    uint32_t slot = (uint32_t)(((uintptr_t)s->font_family >> 4) ^ s->font_weight ^ (uint32_t)italic) % PLAN_CACHE;
    plan *pl = &w->cache[slot];
    uint32_t i, k;
    if (pl->families == s->font_family && pl->weight == s->font_weight && pl->italic == italic)
        return pl;
    pl->families = s->font_family;
    pl->weight = s->font_weight;
    pl->italic = italic;
    pl->nfaces = pl->ngroups = 0;
    for (i = 0; s->font_family && i < s->font_family->count && pl->ngroups < PLAN_FAMILIES; i++) {
        const char *family = s->font_family->names[i];
        int best = -1;
        for (k = 0; k < pf->n; k++) {
            const tc_font_face *r = pf->items[k].rule;
            int score;
            if (!tc_css_ascii_ieq(r->family, family))
                continue;
            score = tc_font_weight_distance(r->weight_min, r->weight_max, s->font_weight) +
                    (r->italic != italic) * 100000;
            if (best < 0 || score < best)
                best = score;
        }
        if (best < 0) {
            if (tc_font_set_has_family(w->installed, family))
                break; /* installed: it, or the fallbacks behind it, will draw the rest */
            continue;
        }
        for (k = 0; k < pf->n && pl->nfaces < PLAN_FACES; k++) {
            const tc_font_face *r = pf->items[k].rule;
            if (tc_css_ascii_ieq(r->family, family) &&
                tc_font_weight_distance(r->weight_min, r->weight_max, s->font_weight) +
                        (r->italic != italic) * 100000 ==
                    best)
                pl->faces[pl->nfaces++] = &pf->items[k];
        }
        pl->group_end[pl->ngroups++] = (uint8_t)pl->nfaces;
    }
    return pl;
}

static int in_ranges(const tc_font_face *r, uint32_t cp)
{
    uint32_t i;
    if (!r->ranges)
        return 1;
    for (i = 0; i < r->nranges; i++)
        if (cp >= r->ranges[2 * i] && cp <= r->ranges[2 * i + 1])
            return 1;
    return 0;
}

/* Each character's face: the first family's face with it in range that
   hasn't failed. */
static void want_text(want_ctx *w, const tc_style *s, const char *text, size_t len)
{
    const plan *pl = plan_for(w, s);
    size_t i = 0;
    if (!pl->ngroups)
        return;
    while (i < len) {
        uint32_t cp, g, k = 0;
        i += tc_utf8_decode(text + i, len - i, &cp);
        if (cp < 0x20)
            continue;
        for (g = 0; g < pl->ngroups; g++) {
            page_font *found = NULL;
            for (; k < pl->group_end[g] && !found; k++)
                if (pl->faces[k]->state != TC_PAGE_FONT_FAILED && in_ranges(pl->faces[k]->rule, cp))
                    found = pl->faces[k];
            k = pl->group_end[g];
            if (!found)
                continue;
            if (found->state == TC_PAGE_FONT_UNUSED) {
                found->src = 0;
                next_src(w->p, found);
                w->wanted += found->state == TC_PAGE_FONT_WAITING;
            }
            break;
        }
    }
}

static void want_boxes(want_ctx *w, const tc_box *b, int depth)
{
    const tc_box *c;
    uint32_t i;
    if (depth > MAX_WALK_DEPTH)
        return;
    for (i = 0; i < b->nfrags; i++) {
        const tc_frag *f = &b->frags[i];
        if (f->type == TC_FRAG_TEXT && f->box && f->box->style)
            want_text(w, f->box->style, f->text, f->len);
        else if (f->type == TC_FRAG_ATOMIC)
            want_boxes(w, f->box, depth + 1);
    }
    if (!b->has_lines)
        for (c = b->first; c; c = c->next)
            want_boxes(w, c, depth + 1);
}

/* Read a waiting font that's a data: URL. Returns 1 if it loaded. */
static int read_data_font(tc_page *p, size_t i)
{
    page_font *e = &p->webfonts->items[i];
    int loaded = 0;
    while (e->state == TC_PAGE_FONT_WAITING && strncmp(e->url, "data:", 5) == 0) {
        tc_buf bytes;
        tc_buf_init(&bytes);
        if (data_url(e->url, &bytes) == TC_OK)
            loaded = tc_page_font_arrived(p, i, bytes.data, bytes.len);
        else
            loaded = tc_page_font_arrived(p, i, NULL, 0);
        tc_buf_free(&bytes);
    }
    return loaded;
}

/* Which fonts the laid out text needs. Returns 1 if a data: URL font
   among them loaded, so the page must be laid out again. */
static int want_fonts(tc_page *p, tc_font_set *installed)
{
    struct tc_page_fonts *pf = p->webfonts;
    want_ctx *w;
    uint32_t i;
    int unused = 0, loaded = 0;
    if (!pf || !p->layout || !tc_layout_root(p->layout))
        return 0;
    for (i = 0; i < pf->n; i++)
        unused |= pf->items[i].state == TC_PAGE_FONT_UNUSED;
    if (!unused || !(w = calloc(1, sizeof *w)))
        return 0;
    w->p = p;
    w->installed = installed;
    want_boxes(w, tc_layout_root(p->layout), 0);
    if (w->wanted)
        tc_trace("page: %d web fonts wanted", w->wanted);
    free(w);
    for (i = 0; i < pf->n; i++)
        if (pf->items[i].state == TC_PAGE_FONT_WAITING)
            loaded |= read_data_font(p, i);
    return loaded;
}

size_t tc_page_font_count(const tc_page *p)
{
    return p->webfonts ? p->webfonts->n : 0;
}

const char *tc_page_font_url(const tc_page *p, size_t i)
{
    return p->webfonts && i < p->webfonts->n && p->webfonts->items[i].url ? p->webfonts->items[i].url : "";
}

int tc_page_font_state(const tc_page *p, size_t i)
{
    return p->webfonts && i < p->webfonts->n ? p->webfonts->items[i].state : TC_PAGE_FONT_FAILED;
}

int tc_page_font_arrived(tc_page *p, size_t i, const void *data, size_t len)
{
    struct tc_page_fonts *pf = p->webfonts;
    page_font *e;
    const tc_font_face *r;
    int err = TC_ERR_INVALID;
    if (!pf || i >= pf->n || pf->items[i].state != TC_PAGE_FONT_WAITING)
        return 0;
    e = &pf->items[i];
    r = e->rule;
    if (data)
        err = tc_font_set_add_web(pf->set, data, len, r->family, r->weight_min, r->weight_max, r->italic, r->ranges,
                                  r->nranges);
    if (!err) {
        e->state = TC_PAGE_FONT_LOADED;
        return 1;
    }
    tc_log("font %.100s: %s", e->url, data ? tc_err_text(err) : "not fetched");
    e->src++;
    next_src(p, e);
    return 0;
}

int tc_page_load_fonts(tc_page *p, tc_font_set *fonts, tc_page_fetch_fn fetch, void *ctx,
                       void (*progress)(void *ctx, unsigned n, const char *url))
{
    unsigned fetched = 0, round;
    int err = TC_OK;
    /* Each src is tried once, so this ends; another round only if laying
       out again wants more. */
    for (round = 0; round < 4 && !err; round++) {
        size_t i;
        int relayout = 0, waiting = 1;
        while (waiting) {
            waiting = 0;
            for (i = 0; i < tc_page_font_count(p); i++) {
                tc_buf body;
                char charset[64];
                if (tc_page_font_state(p, i) != TC_PAGE_FONT_WAITING)
                    continue;
                waiting = 1;
                if (progress)
                    progress(ctx, ++fetched, tc_page_font_url(p, i));
                tc_buf_init(&body);
                if (fetch(ctx, tc_page_font_url(p, i), &body, charset, sizeof charset) == TC_OK)
                    relayout |= tc_page_font_arrived(p, i, body.data ? body.data : "", body.len);
                else
                    relayout |= tc_page_font_arrived(p, i, NULL, 0);
                tc_buf_free(&body);
            }
        }
        if (!relayout || !p->layout)
            break;
        err = tc_page_layout(p, fonts, p->width, p->height);
    }
    return err;
}

/* ---- Layout ---- */

static int layout_once(tc_page *p, tc_font_set *fonts, float w, float h)
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
        err = find_images(p, styled, &env);
    if (!err)
        err = find_fonts(p, fonts, &env);
    if (!err)
        err = tc_layout_document(p->html.doc, styled, p->webfonts ? p->webfonts->set : fonts, &p->images->source, w,
                                 h, &layout);
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

int tc_page_layout(tc_page *p, tc_font_set *fonts, float w, float h)
{
    int err = layout_once(p, fonts, w, h);
    /* Fonts in data: URLs arrive at once: once more with them. */
    if (!err && want_fonts(p, fonts))
        err = layout_once(p, fonts, w, h);
    return err;
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
