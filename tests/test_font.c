#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_font.h"
#include "tc_font_check.h"
#include "tc_test.h"
#include "tc_woff.h"

void tc_suite_font(void);

/* The Liberation fonts, natively or through Wine's Z: drive; or failing
   those (on Windows 95 itself), the system's fonts. */
static tc_font_set *test_fonts(int *liberation)
{
    static const char *const dirs[] = {
        "/usr/share/fonts/truetype/liberation",
        "Z:\\usr\\share\\fonts\\truetype\\liberation",
    };
    tc_font_set *set = tc_font_set_new();
    size_t i;
    *liberation = 0;
    if (!set)
        return NULL;
    for (i = 0; i < TC_ARRAY_LEN(dirs) && !*liberation; i++)
        *liberation = tc_font_set_add_dir(set, dirs[i]) > 0;
    if (!*liberation)
        tc_font_set_add_system(set);
    return set;
}

static const char *sans(int liberation)
{
    return liberation ? "Liberation Sans" : "Arial";
}

static void test_matching(void)
{
    static const char *const sans_serif[] = { "sans-serif" };
    static const char *const missing_then_mono[] = { "No Such Font", "monospace" };
    static const char *const helvetica[] = { "Helvetica", "serif" };
    static const char *const nothing[] = { "No Such Font" };
    int liberation, bold, italic;
    tc_font_set *set = test_fonts(&liberation);
    const tc_font *f;

    TC_CHECK(set && tc_font_set_count(set) >= 4);
    if (!set || !tc_font_set_count(set)) {
        printf("     no fonts: install fonts-liberation\n");
        tc_font_set_free(set);
        return;
    }
    f = tc_font_match(set, sans_serif, 1, 400, 0, 16);
    TC_CHECK_STR(tc_font_family(f, &bold, &italic), sans(liberation));
    TC_CHECK(!bold && !italic);
    f = tc_font_match(set, missing_then_mono, 2, 400, 0, 16);
    TC_CHECK_STR(tc_font_family(f, NULL, NULL), liberation ? "Liberation Mono" : "Courier New");
    f = tc_font_match(set, helvetica, 2, 400, 0, 16);
    TC_CHECK_STR(tc_font_family(f, NULL, NULL), sans(liberation));
    f = tc_font_match(set, nothing, 1, 400, 0, 16);
    TC_CHECK_STR(tc_font_family(f, NULL, NULL), liberation ? "Liberation Serif" : "Times New Roman");
    /* Bold and italic faces exist, so nothing is synthesised. */
    f = tc_font_match(set, sans_serif, 1, 700, 1, 16);
    tc_font_family(f, &bold, &italic);
    TC_CHECK(!bold && !italic);
    f = tc_font_match(set, sans_serif, 1, 900, 0, 16);
    tc_font_family(f, &bold, NULL);
    TC_CHECK(!bold);
    /* The same request gives the same font. */
    TC_CHECK(tc_font_match(set, sans_serif, 1, 400, 0, 16) == tc_font_match(set, sans_serif, 1, 400, 0, 16.01f));
    TC_CHECK(tc_font_match(set, sans_serif, 1, 400, 0, 16) != tc_font_match(set, sans_serif, 1, 400, 0, 17));
    tc_font_set_free(set);
}

static void test_metrics_and_measuring(void)
{
    static const char *const sans_serif[] = { "sans-serif" };
    int liberation;
    tc_font_set *set = test_fonts(&liberation);
    const tc_font *f;
    const tc_font_metrics *m;
    float hello, av, a, v;

    if (!set || !tc_font_set_count(set)) {
        tc_font_set_free(set);
        return;
    }
    f = tc_font_match(set, sans_serif, 1, 400, 0, 16);
    m = tc_font_get_metrics(f);
    /* Arial's (and Liberation Sans's) Windows ascent and descent. */
    TC_CHECK(fabs(m->ascent - 1854 * 16 / 2048.0) < 0.01);
    TC_CHECK(fabs(m->descent - 434 * 16 / 2048.0) < 0.01);
    TC_CHECK(m->x_height > 7 && m->x_height < 9);
    TC_CHECK(m->space_width > 4 && m->space_width < 5);
    TC_CHECK(m->underline_offset > 0 && m->underline_thickness >= 1);
    hello = tc_font_measure(f, "Hello", 5, 0);
    TC_CHECK(fabs(hello - 4667 * 16 / 2048.0) < 0.5);
    TC_CHECK(fabs(tc_font_measure(f, "Hello", 5, 1) - (hello + 5)) < 0.01);
    TC_CHECK(tc_font_measure(f, "", 0, 0) == 0);
    /* Kerning. */
    av = tc_font_measure(f, "AV", 2, 0);
    a = tc_font_measure(f, "A", 1, 0);
    v = tc_font_measure(f, "V", 1, 0);
    TC_CHECK(av < a + v);
    /* UTF-8: "é" is one character. */
    TC_CHECK(fabs(tc_font_measure(f, "\xC3\xA9", 2, 0) - tc_font_measure(f, "e", 1, 0)) < 0.01);
    tc_font_set_free(set);
}

static int dark_pixels(const tc_surface *s, tc_rect r)
{
    int x, y, n = 0;
    for (y = r.y; y < r.y + r.h; y++)
        for (x = r.x; x < r.x + r.w; x++)
            n += (tc_surface_get(s, x, y) & 0xFF) < 128;
    return n;
}

static void test_drawing(void)
{
    static const char *const serif[] = { "serif" };
    int liberation;
    tc_font_set *set = test_fonts(&liberation);
    tc_surface s;
    const tc_font *f;
    float w;

    if (!set || !tc_font_set_count(set) || tc_surface_init(&s, 200, 50) != TC_OK) {
        tc_font_set_free(set);
        return;
    }
    tc_surface_fill(&s, tc_rect_make(0, 0, 200, 50), TC_RGB(255, 255, 255));
    f = tc_font_match(set, serif, 1, 400, 0, 20);
    w = tc_font_draw(f, &s, tc_rect_make(0, 0, 100, 50), 10, 30, "Tachyon Tachyon", 15, 0, TC_RGB(0, 0, 0));
    TC_CHECK(w > 100);
    TC_CHECK(dark_pixels(&s, tc_rect_make(10, 10, 90, 25)) > 50);
    TC_CHECK(dark_pixels(&s, tc_rect_make(100, 0, 100, 50)) == 0);   /* clipped */
    TC_CHECK(dark_pixels(&s, tc_rect_make(0, 31, 100, 3)) < 10);     /* below the baseline: no descenders */
    /* Synthesised bold is darker. */
    {
        tc_font_set *one = tc_font_set_new();
        int n0, n1, bold = 0, italic = 0;
        char path[200];
        const char *dir = "/usr/share/fonts/truetype/liberation";
        if (liberation && one) {
            if (!tc_font_set_add_dir(one, "/usr/share/fonts/truetype/liberation"))
                dir = "Z:\\usr\\share\\fonts\\truetype\\liberation";
            tc_font_set_free(one);
            one = tc_font_set_new();
            snprintf(path, sizeof path, "%s%cLiberationSerif-Regular.ttf", dir, dir[0] == 'Z' ? '\\' : '/');
            TC_CHECK_INT(tc_font_set_add_file(one, path, NULL, 0, 0), TC_OK);
            TC_CHECK_INT(tc_font_set_count(one), 1);
            f = tc_font_match(one, serif, 1, 400, 0, 20);
            tc_surface_fill(&s, tc_rect_make(0, 0, 200, 50), TC_RGB(255, 255, 255));
            tc_font_draw(f, &s, tc_rect_make(0, 0, 200, 50), 0, 30, "mmmm", 4, 0, TC_RGB(0, 0, 0));
            n0 = dark_pixels(&s, tc_rect_make(0, 0, 200, 50));
            f = tc_font_match(one, serif, 1, 700, 1, 20);
            tc_font_family(f, &bold, &italic);
            TC_CHECK(bold && italic);
            tc_surface_fill(&s, tc_rect_make(0, 0, 200, 50), TC_RGB(255, 255, 255));
            tc_font_draw(f, &s, tc_rect_make(0, 0, 200, 50), 0, 30, "mmmm", 4, 0, TC_RGB(0, 0, 0));
            n1 = dark_pixels(&s, tc_rect_make(0, 0, 200, 50));
            TC_CHECK(n1 > n0);
        }
        tc_font_set_free(one);
    }
    tc_surface_free(&s);
    tc_font_set_free(set);
}

/* Big glyphs aren't cached: each paint rasterises the part in its clip.
   Painting in strips, as scrolling does, must give the same pixels, give
   or take rounding (a level or two, on a few pixels). */
static int close_pixels(const tc_surface *a, const tc_surface *b)
{
    int x, y;
    for (y = 0; y < a->height; y++)
        for (x = 0; x < a->width; x++) {
            int d = (int)(tc_surface_get(a, x, y) & 255) - (int)(tc_surface_get(b, x, y) & 255);
            if (d < -4 || d > 4)
                return 0;
        }
    return 1;
}

static void test_big_glyphs(void)
{
    static const char *const serif[] = { "serif" };
    tc_font_set *one = tc_font_set_new();
    tc_surface a, b;
    const char *dir = "/usr/share/fonts/truetype/liberation";
    char path[200];
    int i, y;

    if (!one || tc_surface_init(&a, 300, 260) != TC_OK || tc_surface_init(&b, 300, 260) != TC_OK) {
        tc_font_set_free(one);
        return;
    }
    /* Only the regular face, so bold and italic are synthesised. */
    snprintf(path, sizeof path, "%s/LiberationSerif-Regular.ttf", dir);
    if (tc_font_set_add_file(one, path, NULL, 0, 0) != TC_OK)
        tc_font_set_add_file(one, "Z:\\usr\\share\\fonts\\truetype\\liberation\\LiberationSerif-Regular.ttf", NULL, 0, 0);
    if (tc_font_set_count(one)) {
        for (i = 0; i < 2; i++) {
            const tc_font *f = tc_font_match(one, serif, 1, i ? 700 : 400, i, 200);
            tc_surface_fill(&a, tc_rect_make(0, 0, 300, 260), TC_RGB(255, 255, 255));
            tc_surface_fill(&b, tc_rect_make(0, 0, 300, 260), TC_RGB(255, 255, 255));
            tc_font_draw(f, &a, tc_rect_make(0, 0, 300, 260), 5, 200, "Ag", 2, 0, TC_RGB(0, 0, 0));
            for (y = 0; y < 260; y += 7)
                tc_font_draw(f, &b, tc_rect_make(0, y, 300, 7), 5, 200, "Ag", 2, 0, TC_RGB(0, 0, 0));
            TC_CHECK(dark_pixels(&a, tc_rect_make(0, 0, 300, 260)) > 5000);
            TC_CHECK(close_pixels(&a, &b));
        }
        /* A glyph far bigger than the surface draws the part that shows. */
        tc_surface_fill(&a, tc_rect_make(0, 0, 300, 260), TC_RGB(255, 255, 255));
        tc_font_draw(tc_font_match(one, serif, 1, 400, 0, 10000), &a, tc_rect_make(0, 0, 300, 260), -2000, 5000,
                     "I", 1, 0, TC_RGB(0, 0, 0));
        TC_CHECK(dark_pixels(&a, tc_rect_make(0, 0, 300, 260)) > 0);
    }
    tc_surface_free(&a);
    tc_surface_free(&b);
    tc_font_set_free(one);
}

static void test_no_fonts(void)
{
    static const char *const serif[] = { "serif" };
    tc_font_set *set = tc_font_set_new();
    const tc_font *f = tc_font_match(set, serif, 1, 400, 0, 10);
    tc_surface s;
    TC_CHECK_STR(tc_font_family(f, NULL, NULL), "");
    TC_CHECK(fabs(tc_font_measure(f, "ab", 2, 0) - 12) < 0.01);
    TC_CHECK(tc_font_get_metrics(f)->ascent > 0);
    TC_CHECK_INT(tc_font_set_add_file(set, "no-such-file.ttf", NULL, 0, 0), TC_ERR_INVALID);
    if (tc_surface_init(&s, 40, 20) == TC_OK) {
        tc_font_draw(f, &s, tc_rect_make(0, 0, 40, 20), 2, 15, "ab", 2, 0, TC_RGB(255, 255, 255));
        TC_CHECK(tc_surface_get(&s, 3, 8) == 0xFFFFFF);
        tc_surface_free(&s);
    }
    /* A listed face whose file is missing is skipped. */
    TC_CHECK_INT(tc_font_set_add_file(set, "missing.ttf", "Arial", 400, 0), TC_OK);
    f = tc_font_match(set, serif, 1, 400, 0, 10);
    TC_CHECK_STR(tc_font_family(f, NULL, NULL), "");
    tc_font_set_free(set);
}

/* tests/data/fonts, made by tools/make_test_fonts.py: rectangles a full
   em (1000 units) wide. The collection holds "Tachyon Test CJK" and
   "Tachyon Test Symbols" (an alpha and a snowman). */
static void test_collections(void)
{
    const tc_test_blob *ttc = tc_test_blob_get("tachyon-test.ttc");
    const char *path = "tc-test-fonts.ttc";
    tc_font_set *set = tc_font_set_new();
    int weight, italic;
    FILE *f;
    if (!ttc || !set)
        return;
    TC_CHECK_INT(tc_font_set_add_data(set, ttc->data, ttc->len, NULL), TC_OK);
    TC_CHECK_INT(tc_font_set_count(set), 2);
    TC_CHECK_STR(tc_font_set_face(set, 0, &weight, &italic), "Tachyon Test CJK");
    TC_CHECK_STR(tc_font_set_face(set, 1, &weight, &italic), "Tachyon Test Symbols");
    TC_CHECK(weight == 400 && italic == 0);
    TC_CHECK_INT(tc_font_set_add_data(set, "not a font at all", 17, NULL), TC_ERR_INVALID);
    TC_CHECK_INT(tc_font_set_add_data(set, ttc->data, 40, NULL), TC_ERR_INVALID); /* cut short */
    TC_CHECK_INT(tc_font_set_add_data(set, ttc->data, ttc->len, "Renamed"), TC_OK);
    TC_CHECK_STR(tc_font_set_face(set, 3, &weight, &italic), "Renamed");
    tc_font_set_free(set);
    /* From a file, read a table at a time. */
    f = fopen(path, "wb");
    if (!f || fwrite(ttc->data, 1, ttc->len, f) != ttc->len) {
        if (f)
            fclose(f);
        tc_test_fail(__FILE__, __LINE__, "can't write the test font file");
        return;
    }
    fclose(f);
    set = tc_font_set_new();
    TC_CHECK_INT(tc_font_set_add_file(set, path, NULL, 0, 0), TC_OK);
    TC_CHECK_INT(tc_font_set_add_file(set, path, NULL, 0, 0), TC_OK); /* once only */
    TC_CHECK_INT(tc_font_set_count(set), 2);
    TC_CHECK_STR(tc_font_set_face(set, 1, &weight, &italic), "Tachyon Test Symbols");
    {
        /* Its faces' characters are read from the file too. */
        static const char *const symbols[] = { "Tachyon Test Symbols" };
        const tc_font *sym = tc_font_match(set, symbols, 1, 400, 0, 20);
        TC_CHECK_STR(tc_font_family(tc_font_for_char(sym, 0x65E5), NULL, NULL), "Tachyon Test CJK");
        TC_CHECK(fabs(tc_font_measure(sym, "\xE6\x97\xA5\xE2\x98\x83", 6, 0) - 40) < 0.01); /* 日☃ */
    }
    tc_font_set_free(set);
    remove(path);
}

static void test_fallback(void)
{
    static const char *const symbols[] = { "Tachyon Test Symbols" };
    const tc_test_blob *ttc = tc_test_blob_get("tachyon-test.ttc"), *cjk = tc_test_blob_get("tachyon-test-cjk.ttf");
    tc_font_set *set = tc_font_set_new();
    const tc_font *f, *fb;
    tc_surface s;
    int bold;
    if (!ttc || !cjk || !set)
        return;
    tc_font_set_add_data(set, ttc->data, ttc->len, NULL);
    f = tc_font_match(set, symbols, 1, 400, 0, 20);
    TC_CHECK_STR(tc_font_family(f, NULL, NULL), "Tachyon Test Symbols");
    /* Its own alpha, then 中 from the other font, a full em each. */
    TC_CHECK(tc_font_for_char(f, 0x391) == f);
    TC_CHECK_STR(tc_font_family(tc_font_for_char(f, 0x4E2D), NULL, NULL), "Tachyon Test CJK");
    TC_CHECK(fabs(tc_font_measure(f, "\xCE\x91\xE4\xB8\xAD", 5, 0) - 40) < 0.01);
    /* Nobody has Thai: its own missing glyph (half an em). Invisible
       characters are nothing. */
    TC_CHECK(tc_font_for_char(f, 0xE01) == f);
    TC_CHECK(fabs(tc_font_measure(f, "\xE0\xB8\x81", 3, 0) - 10) < 0.01);
    TC_CHECK(fabs(tc_font_measure(f, "\xE2\x80\x8B\xEF\xBB\xBF", 6, 0)) < 0.01);
    /* Drawn: 中 is a bar 450 to 550 units across its em. */
    if (tc_surface_init(&s, 60, 30) == TC_OK) {
        tc_surface_fill(&s, tc_rect_make(0, 0, 60, 30), TC_RGB(255, 255, 255));
        tc_font_draw(f, &s, tc_rect_make(0, 0, 60, 30), 0, 24, "\xCE\x91\xE4\xB8\xAD", 5, 0, TC_RGB(0, 0, 0));
        TC_CHECK_INT(tc_surface_get(&s, 10, 18), 0x000000); /* the alpha's box */
        TC_CHECK_INT(tc_surface_get(&s, 30, 10), 0x000000); /* 中's bar */
        TC_CHECK_INT(tc_surface_get(&s, 25, 10), 0xFFFFFF);
        tc_surface_free(&s);
    }
    /* Bold: the fallback is bold too (synthesised, like the font). */
    f = tc_font_match(set, symbols, 1, 700, 0, 20);
    fb = tc_font_for_char(f, 0x4E2D);
    TC_CHECK_STR(tc_font_family(fb, &bold, NULL), "Tachyon Test CJK");
    TC_CHECK(bold);
    tc_font_set_free(set);

    /* The first face in the set that has it: Windows' fonts are added
       before the font pack. */
    set = tc_font_set_new();
    tc_font_set_add_data(set, cjk->data, cjk->len, "First");
    tc_font_set_add_data(set, ttc->data, ttc->len, NULL);
    f = tc_font_match(set, symbols, 1, 400, 0, 20);
    TC_CHECK_STR(tc_font_family(tc_font_for_char(f, 0x3042), NULL, NULL), "First");
    tc_font_set_free(set);
}

static uint32_t be16(const uint8_t *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* A table's directory entry in a single font, or NULL. */
static uint8_t *table_entry(uint8_t *font, const char *tag)
{
    uint32_t i, n = be16(font + 4);
    for (i = 0; i < n; i++)
        if (memcmp(font + 12 + 16 * i, tag, 4) == 0)
            return font + 12 + 16 * i;
    return NULL;
}

/* A copy of a test font to break. */
static uint8_t *font_copy(const char *name, size_t *len)
{
    const tc_test_blob *b = tc_test_blob_get(name);
    uint8_t *copy = b ? malloc(b->len) : NULL;
    if (copy)
        memcpy(copy, b->data, b->len);
    *len = b ? b->len : 0;
    return copy;
}

/* The font checker (decision 38) on good fonts, and broken ones. */
static void test_font_check(void)
{
    static const char *const good[] = { "tachyon-test-cjk.ttf", "liberation-subset.ttf", "dejavu-subset.ttf" };
    uint8_t *f, *e, *loca, *glyf, *cmap;
    size_t len, i;
    uint32_t g, n, sub;

    for (i = 0; i < TC_ARRAY_LEN(good); i++) {
        f = font_copy(good[i], &len);
        if (!f)
            continue;
        TC_CHECK_INT(tc_font_check(f, len, 0), TC_OK);
        TC_CHECK(table_entry(f, "GPOS") == NULL); /* hidden: no kerning */
        free(f);
    }
    f = font_copy("tachyon-test.ttc", &len);
    if (f) { /* each face of a collection */
        TC_CHECK_INT(tc_font_check(f, len, be32(f + 12)), TC_OK);
        TC_CHECK_INT(tc_font_check(f, len, be32(f + 16)), TC_OK);
        TC_CHECK_INT(tc_font_check(f, len, 0), TC_ERR_DECODE); /* the collection itself isn't a face */
        free(f);
    }

    if (!(f = font_copy("liberation-subset.ttf", &len)))
        return;
    TC_CHECK_INT(tc_font_check(f, len / 2, 0), TC_ERR_DECODE); /* cut short */
    f[5] = 0xFF; /* 255 tables */
    TC_CHECK_INT(tc_font_check(f, len, 0), TC_ERR_DECODE);
    free(f);
    f = font_copy("liberation-subset.ttf", &len);
    e = table_entry(f, "glyf");
    e[8] = 0x7F; /* past the end */
    TC_CHECK_INT(tc_font_check(f, len, 0), TC_ERR_DECODE);
    free(f);
    /* Not TrueType outlines. */
    /* Two GPOS tables: both hidden. */
    f = font_copy("liberation-subset.ttf", &len);
    memcpy(table_entry(f, "post"), "GPOS", 4);
    TC_CHECK_INT(tc_font_check(f, len, 0), TC_OK);
    TC_CHECK(table_entry(f, "GPOS") == NULL);
    free(f);
    f = font_copy("liberation-subset.ttf", &len);
    memcpy(f, "OTTO", 4);
    TC_CHECK_INT(tc_font_check(f, len, 0), TC_ERR_DECODE);
    free(f);

    /* loca pointing past glyf, and a composite glyph that contains itself. */
    f = font_copy("liberation-subset.ttf", &len);
    loca = f + be32(table_entry(f, "loca") + 8);
    glyf = f + be32(table_entry(f, "glyf") + 8);
    n = be16(f + be32(table_entry(f, "maxp") + 8) + 4);
    TC_CHECK(be16(f + be32(table_entry(f, "head") + 8) + 50) == 0); /* short offsets, in words */
    for (g = 0; g < n; g++) {
        uint32_t from = be16(loca + 2 * g) * 2, to = be16(loca + 2 * g + 2) * 2;
        if (to - from >= 10 && (int16_t)be16(glyf + from) < 0) {
            uint8_t *copy = malloc(len);
            memcpy(copy, f, len);
            copy[glyf - f + from + 12] = (uint8_t)(g >> 8); /* its first part is itself */
            copy[glyf - f + from + 13] = (uint8_t)g;
            TC_CHECK_INT(tc_font_check(copy, len, 0), TC_ERR_DECODE);
            free(copy);
            break;
        }
    }
    TC_CHECK(g < n); /* there was a composite glyph */
    loca[2 * n] = 0xFF;
    TC_CHECK_INT(tc_font_check(f, len, 0), TC_ERR_DECODE);
    free(f);

    /* A glyph of one point: fine on the curve, refused off it (stb_truetype
       would read past its points). */
    f = font_copy("tachyon-test-cjk.ttf", &len);
    loca = f + be32(table_entry(f, "loca") + 8);
    glyf = f + be32(table_entry(f, "glyf") + 8);
    for (g = 0; g < 8; g++) {
        uint32_t from = be16(loca + 2 * g) * 2, to = be16(loca + 2 * g + 2) * 2;
        if (to - from >= 20) {
            static const uint8_t point[] = { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 };
            memcpy(glyf + from, point, sizeof point);
            TC_CHECK_INT(tc_font_check(f, len, 0), TC_OK);
            glyf[from + 14] = 0; /* off the curve */
            TC_CHECK_INT(tc_font_check(f, len, 0), TC_ERR_DECODE);
            break;
        }
    }
    TC_CHECK(g < 8);
    free(f);

    /* A format 4 cmap's search fields are set right whatever they were. */
    f = font_copy("tachyon-test-cjk.ttf", &len);
    cmap = f + be32(table_entry(f, "cmap") + 8);
    for (i = 0, sub = 0; i < be16(cmap + 2); i++)
        if (be16(cmap + 4 + 8 * i) == 3 && be16(cmap + 6 + 8 * i) == 1)
            sub = be32(cmap + 8 + 8 * i);
    TC_CHECK(sub && be16(cmap + sub) == 4);
    if (sub && be16(cmap + sub) == 4) {
        uint32_t segs = be16(cmap + sub + 6) / 2, range = 1;
        while (range * 2 <= segs)
            range *= 2;
        memset(cmap + sub + 8, 0x7F, 6);
        TC_CHECK_INT(tc_font_check(f, len, 0), TC_OK);
        TC_CHECK_INT(be16(cmap + sub + 8), range * 2);
        TC_CHECK_INT(be16(cmap + sub + 12), (segs - range) * 2);
    }
    free(f);
}

/* Text in a family from the set, drawn on white. */
static int draw_text(tc_font_set *set, const char *family, const char *text, tc_surface *s)
{
    const tc_font *f = tc_font_match(set, &family, 1, 400, 0, 24);
    if (tc_surface_init(s, 400, 40) != TC_OK)
        return 0;
    tc_surface_fill(s, tc_rect_make(0, 0, 400, 40), TC_RGB(255, 255, 255));
    tc_font_draw(f, s, tc_rect_make(0, 0, 400, 40), 2, 30, text, strlen(text), 0, TC_RGB(0, 0, 0));
    return strcmp(tc_font_family(f, NULL, NULL), family) == 0;
}

static int same_pixels(const tc_surface *a, const tc_surface *b)
{
    int x, y;
    for (y = 0; y < a->height; y++)
        for (x = 0; x < a->width; x++)
            if (tc_surface_get(a, x, y) != tc_surface_get(b, x, y))
                return 0;
    return 1;
}

/* WOFF and WOFF2 draw exactly as the fonts they were made from. */
static void test_web_fonts(void)
{
    static const struct {
        const char *original, *packed, *text;
    } cases[] = {
        { "tachyon-test-cjk.ttf", "tachyon-test-cjk.woff", "\xE4\xB8\xAD\xE3\x81\x82\xE6\x97\xA5\xE6\x9C\xAC" },
        { "tachyon-test-cjk.ttf", "tachyon-test-cjk.woff2", "\xE4\xB8\xAD\xE3\x81\x82\xE6\x97\xA5\xE6\x9C\xAC" },
        /* glyf, loca and hmtx transformed; accented letters are composites. */
        { "liberation-subset.ttf", "liberation-subset.woff2", "Tachyon AVTo WAVE \xC3\xA9\xC3\xA0\xC3\xBC\xC3\x85" },
    };
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(cases); i++) {
        const tc_test_blob *a = tc_test_blob_get(cases[i].original), *b = tc_test_blob_get(cases[i].packed);
        tc_font_set *set = tc_font_set_new();
        tc_surface sa, sb;
        tc_buf sfnt;
        TC_CHECK(a && b && set);
        if (!a || !b || !set)
            continue;
        TC_CHECK(tc_woff_sniff(b->data, b->len));
        TC_CHECK(!tc_woff_sniff(a->data, a->len));
        tc_buf_init(&sfnt);
        TC_CHECK_INT(tc_woff_decode(b->data, b->len, &sfnt), TC_OK);
        TC_CHECK_INT(tc_font_check((uint8_t *)sfnt.data, sfnt.len, 0), TC_OK);
        tc_buf_free(&sfnt);
        TC_CHECK_INT(tc_font_set_add_data(set, a->data, a->len, "Original"), TC_OK);
        TC_CHECK_INT(tc_font_set_add_data(set, b->data, b->len, "Packed"), TC_OK);
        if (draw_text(set, "Original", cases[i].text, &sa)) {
            if (draw_text(set, "Packed", cases[i].text, &sb)) {
                if (!same_pixels(&sa, &sb))
                    tc_test_fail(__FILE__, __LINE__, cases[i].packed);
                tc_test_checks++;
            } else {
                tc_test_fail(__FILE__, __LINE__, "the packed font wasn't used");
            }
            tc_surface_free(&sb);
        }
        tc_surface_free(&sa);
        /* Cut short: refused. */
        tc_buf_init(&sfnt);
        TC_CHECK(tc_woff_decode(b->data, b->len - 20, &sfnt) != TC_OK);
        tc_buf_free(&sfnt);
        TC_CHECK(tc_font_set_add_data(set, b->data, b->len / 2, "Broken") != TC_OK);
        TC_CHECK_INT(tc_font_set_count(set), 2);
        tc_font_set_free(set);
    }
}

/* A page's web fonts in a child set, in front of the installed ones. */
static void test_page_fonts(void)
{
    static const uint32_t han[] = { 0x4E2D, 0x4E2D }, kana[] = { 0x3000, 0x30FF };
    const tc_test_blob *woff2 = tc_test_blob_get("tachyon-test-cjk.woff2");
    int liberation, bold, italic;
    tc_font_set *set = test_fonts(&liberation), *page;
    const char *web_first[2], *shadowed[1];
    const tc_font *f;

    TC_CHECK(woff2 != NULL);
    if (!set || !woff2 || !(page = tc_font_set_new_child(set))) {
        tc_font_set_free(set);
        return;
    }
    web_first[0] = "Web", web_first[1] = sans(liberation);
    TC_CHECK(!tc_font_set_has_family(page, "Web"));
    TC_CHECK(tc_font_set_has_family(page, "sans-serif"));
    /* Not loaded yet: the next family. */
    f = tc_font_match(page, web_first, 2, 400, 0, 20);
    TC_CHECK_STR(tc_font_family(f, NULL, NULL), sans(liberation));

    /* A face for one character: no space in its range, so it isn't the
       first available font, but that character comes from it. */
    TC_CHECK_INT(tc_font_set_add_web(page, woff2->data, woff2->len, "Web", 400, 400, 0, han, 1), TC_OK);
    TC_CHECK(tc_font_set_has_family(page, "web"));
    TC_CHECK_INT(tc_font_set_count(page), 1);
    f = tc_font_match(page, web_first, 2, 400, 0, 20);
    TC_CHECK_STR(tc_font_family(f, NULL, NULL), sans(liberation));
    TC_CHECK_STR(tc_font_family(tc_font_for_char(f, 0x4E2D), NULL, NULL), "Web");
    /* The font has kana too, but its range leaves them out. */
    TC_CHECK(strcmp(tc_font_family(tc_font_for_char(f, 0x3042), NULL, NULL), "Web") != 0);

    /* Another face of the family for kana, and for everything bold: fonts
       matched before it was added are matched again. */
    TC_CHECK_INT(tc_font_set_add_web(page, woff2->data, woff2->len, "Web", 400, 400, 0, kana, 1), TC_OK);
    TC_CHECK_STR(tc_font_family(tc_font_for_char(f, 0x3042), NULL, NULL), "Web");
    TC_CHECK_INT(tc_font_set_add_web(page, woff2->data, woff2->len, "Web", 600, 900, 0, NULL, 0), TC_OK);
    f = tc_font_match(page, web_first, 2, 700, 0, 20);
    TC_CHECK_STR(tc_font_family(f, &bold, &italic), "Web");
    TC_CHECK(bold && !italic); /* the file is a regular weight: emboldened */
    f = tc_font_match(page, web_first, 2, 400, 1, 20);
    TC_CHECK_STR(tc_font_family(f, &bold, &italic), sans(liberation)); /* the 400s lack a space */

    /* A web font hides an installed family of its name. */
    shadowed[0] = sans(liberation);
    TC_CHECK_INT(tc_font_set_add_web(page, woff2->data, woff2->len, shadowed[0], 400, 400, 0, NULL, 0), TC_OK);
    f = tc_font_match(page, shadowed, 1, 400, 0, 20);
    TC_CHECK_INT(tc_font_get_metrics(f)->ascent * 1000 / 20 + 0.5f, 880); /* the test font's */
    f = tc_font_match(set, shadowed, 1, 400, 0, 20);
    TC_CHECK(tc_font_get_metrics(f)->ascent * 1000 / 20 + 0.5f < 880 - 1 ||
             tc_font_get_metrics(f)->ascent * 1000 / 20 + 0.5f > 880 + 1); /* the parent's own is untouched */

    /* Broken data adds nothing. */
    TC_CHECK(tc_font_set_add_web(page, woff2->data, woff2->len / 2, "Broken", 400, 400, 0, NULL, 0) != TC_OK);
    TC_CHECK(!tc_font_set_has_family(page, "Broken"));
    tc_font_set_free(page);
    tc_font_set_free(set);
}

void tc_suite_font(void)
{
    TC_RUN(test_matching);
    TC_RUN(test_metrics_and_measuring);
    TC_RUN(test_drawing);
    TC_RUN(test_big_glyphs);
    TC_RUN(test_no_fonts);
    TC_RUN(test_collections);
    TC_RUN(test_fallback);
    TC_RUN(test_font_check);
    TC_RUN(test_web_fonts);
    TC_RUN(test_page_fonts);
}
