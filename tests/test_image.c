#include <stdlib.h>
#include <string.h>

#include "tc_image.h"
#include "tc_test.h"

void tc_suite_image(void);

/* tests/data/images, made by tools/make_test_images.py. Most show this 4x2
   pattern, premultiplied: */
static const uint32_t pattern[2][4] = {
    { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFFFFu },
    { 0xFF000000u, 0x00000000u, 0x80800000u, 0xFFFFFF00u },
};

#define NO_LIMIT 0xFFFFFFFFu

static tc_image *open_image(const char *name)
{
    const tc_test_blob *b = tc_test_blob_get(name);
    tc_image *img = NULL;
    if (b)
        TC_CHECK_INT(tc_image_new(b->data, b->len, &img), TC_OK);
    return img;
}

static uint32_t pixel(const tc_image *img, int x, int y)
{
    const tc_surface *s = tc_image_pixels(img);
    return s && x < s->width && y < s->height ? s->pixels[y * s->stride + x] : 0x12345678u;
}

/* Every channel within tol. */
static int close_to(uint32_t a, uint32_t b, int tol)
{
    int i;
    for (i = 0; i < 32; i += 8) {
        int d = (int)((a >> i) & 255) - (int)((b >> i) & 255);
        if (d < -tol || d > tol)
            return 0;
    }
    return 1;
}

/* An image of the pattern: its pixels, with the transparent and half-red
   ones as given (formats differ there). */
static void check_pattern(const char *name, tc_image_type type, uint32_t transparent, uint32_t half_red, int tol)
{
    tc_image *img = open_image(name);
    int x, y, bad = 0;
    if (!img)
        return;
    TC_CHECK_INT(tc_image_type_of(img), type);
    TC_CHECK_INT(tc_image_width(img), 4);
    TC_CHECK_INT(tc_image_height(img), 2);
    TC_CHECK_INT(tc_image_frames(img), 1);
    TC_CHECK(tc_image_pixels(img) == NULL);
    TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_OK);
    for (y = 0; y < 2; y++)
        for (x = 0; x < 4; x++) {
            uint32_t want = pattern[y][x];
            if (y == 1 && x == 1)
                want = transparent;
            if (y == 1 && x == 2)
                want = half_red;
            if (!close_to(pixel(img, x, y), want, tol)) {
                char msg[120];
                snprintf(msg, sizeof msg, "%s at %d,%d: %08lx, expected %08lx", name, x, y,
                         (unsigned long)pixel(img, x, y), (unsigned long)want);
                tc_test_fail(__FILE__, __LINE__, msg);
                bad = 1;
            }
        }
    tc_test_checks++;
    (void)bad;
    TC_CHECK_INT(tc_image_memory(img), 4 * 2 * 4);
    tc_image_free(img);
}

static void test_still_images(void)
{
    check_pattern("pattern.png", TC_IMAGE_PNG, 0, 0x80800000u, 1);
    check_pattern("pattern.webp", TC_IMAGE_WEBP, 0, 0x80800000u, 1);
    /* No alpha: flattened onto white. */
    check_pattern("pattern.bmp", TC_IMAGE_BMP, 0xFFFFFFFFu, 0xFFFF7F7Fu, 1);
    check_pattern("pattern-bmp3.bmp", TC_IMAGE_BMP, 0xFFFFFFFFu, 0xFFFF7F7Fu, 1);
    /* GIF: transparent or opaque. */
    check_pattern("pattern.gif", TC_IMAGE_GIF, 0, 0xFFFF0000u, 0);
    /* ICO: a 32-bit bitmap with alpha, a 4-bit one with a mask, and a
       PNG (chosen over a smaller bitmap). */
    check_pattern("pattern-32.ico", TC_IMAGE_ICO, 0, 0x80800000u, 1);
    check_pattern("pattern-4.ico", TC_IMAGE_ICO, 0, 0xFFFF0000u, 0);
    check_pattern("pattern-png.ico", TC_IMAGE_ICO, 0, 0x80800000u, 1);
}

static void test_lossy_images(void)
{
    static const char *const names[] = { "halves.jpg", "halves-progressive.jpg", "halves.webp" };
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(names); i++) {
        tc_image *img = open_image(names[i]);
        if (!img)
            continue;
        TC_CHECK_INT(tc_image_width(img), 32);
        TC_CHECK_INT(tc_image_height(img), 32);
        TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_OK);
        TC_CHECK(close_to(pixel(img, 4, 20), 0xFFFF0000u, 24));
        TC_CHECK(close_to(pixel(img, 28, 20), 0xFF0000FFu, 24));
        if (tc_image_type_of(img) == TC_IMAGE_WEBP) /* WebP keeps the transparent corner */
            TC_CHECK_INT(pixel(img, 2, 2) >> 24, 0);
        tc_image_free(img);
    }
}

static void test_animation(void)
{
    tc_image *img = open_image("frames.gif");
    uint32_t delay;
    int i;
    if (!img)
        return;
    TC_CHECK_INT(tc_image_frames(img), 3);
    TC_CHECK_INT(tc_image_loops(img), 0);
    TC_CHECK_INT(tc_image_next_frame(img, &delay), 0); /* nothing decoded yet */
    TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_OK);
    TC_CHECK_INT(tc_image_delay(img), 100);
    TC_CHECK_INT(pixel(img, 0, 0), 0xFFFF0000u);
    for (i = 0; i < 2; i++) { /* and round again */
        TC_CHECK_INT(tc_image_next_frame(img, &delay), 1);
        TC_CHECK_INT(delay, 200);
        TC_CHECK_INT(pixel(img, 1, 1), 0xFF00FF00u); /* the small frame */
        TC_CHECK_INT(pixel(img, 3, 3), 0xFFFF0000u); /* over the first */
        TC_CHECK_INT(tc_image_next_frame(img, &delay), 1);
        TC_CHECK_INT(delay, 300);
        TC_CHECK_INT(pixel(img, 0, 0), 0xFF0000FFu);
        TC_CHECK_INT(tc_image_next_frame(img, &delay), 1);
        TC_CHECK_INT(delay, 100);
        TC_CHECK_INT(pixel(img, 3, 3), 0xFFFF0000u);
    }
    /* Dropped and decoded again: back at the start. */
    TC_CHECK_INT(tc_image_next_frame(img, &delay), 1);
    tc_image_discard(img);
    TC_CHECK_INT(tc_image_memory(img), 0);
    TC_CHECK(tc_image_pixels(img) == NULL);
    TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_OK);
    TC_CHECK_INT(pixel(img, 0, 0), 0xFFFF0000u);
    tc_image_free(img);
}

static void test_sniffing(void)
{
    static const struct {
        const char *bytes;
        size_t len;
        tc_image_type type;
    } cases[] = {
        { "\x89PNG\r\n\x1a\n", 8, TC_IMAGE_PNG },
        { "\x89PNG\r\n\x1a", 7, TC_IMAGE_NONE },
        { "\xFF\xD8\xFF\xE0", 4, TC_IMAGE_JPEG },
        { "GIF87a", 6, TC_IMAGE_GIF },
        { "GIF89a", 6, TC_IMAGE_GIF },
        { "GIF90a", 6, TC_IMAGE_NONE },
        { "BM", 2, TC_IMAGE_BMP },
        { "RIFF\1\2\3\4WEBPVP8 ", 16, TC_IMAGE_WEBP },
        { "RIFF\1\2\3\4WAVEfmt ", 16, TC_IMAGE_NONE },
        { "\0\0\1\0", 4, TC_IMAGE_ICO },
        { "\0\0\2\0", 4, TC_IMAGE_ICO },
        { "<svg>", 5, TC_IMAGE_SVG },
        { "\xEF\xBB\xBF <?xml version='1.0'?>\n<!-- c --><!DOCTYPE svg [ <!ENTITY e 'x'> ]><svg x='1'>", 81,
          TC_IMAGE_SVG },
        { "<svg", 4, TC_IMAGE_NONE },
        { "<svgx>", 6, TC_IMAGE_NONE },
        { "<html><svg>", 11, TC_IMAGE_NONE },
        { "", 0, TC_IMAGE_NONE },
    };
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(cases); i++)
        TC_CHECK_INT(tc_image_sniff(cases[i].bytes, cases[i].len), cases[i].type);
    TC_CHECK_STR(tc_image_mime(TC_IMAGE_WEBP), "image/webp");
    TC_CHECK_STR(tc_image_mime(TC_IMAGE_SVG), "image/svg+xml");
    TC_CHECK_STR(tc_image_mime(TC_IMAGE_NONE), "");
}

/* SVG: drawn at their natural size, or bigger when asked. */
static void test_svg(void)
{
    tc_image *img = open_image("shapes.svg");
    const tc_surface *s;
    if (!img)
        return;
    TC_CHECK_INT(tc_image_type_of(img), TC_IMAGE_SVG);
    TC_CHECK(tc_image_scalable(img));
    TC_CHECK_INT(tc_image_width(img), 8);
    TC_CHECK_INT(tc_image_height(img), 4);
    TC_CHECK_INT(tc_image_frames(img), 1);
    TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_OK);
    TC_CHECK(close_to(pixel(img, 1, 1), 0xFFFF0000u, 1));
    TC_CHECK(close_to(pixel(img, 5, 1), 0x80000080u, 1)); /* half blue, premultiplied */
    tc_image_free(img);

    img = open_image("viewbox.svg"); /* 16x16, a viewBox four times smaller */
    if (!img)
        return;
    TC_CHECK_INT(tc_image_width(img), 16);
    TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_OK);
    TC_CHECK(close_to(pixel(img, 12, 4), 0xFF00FF00u, 1)); /* the moved group */
    TC_CHECK(close_to(pixel(img, 4, 12), 0xFF000000u, 1)); /* the stroke */
    TC_CHECK_INT(pixel(img, 4, 4), 0);
    /* Wanted bigger: drawn again, crisp. */
    tc_image_want_size(img, 32, 32);
    TC_CHECK(tc_image_pixels(img) == NULL);
    TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_OK);
    s = tc_image_pixels(img);
    TC_CHECK(s && s->width == 32 && s->height == 32);
    TC_CHECK(close_to(pixel(img, 24, 8), 0xFF00FF00u, 1));
    TC_CHECK(close_to(pixel(img, 8, 24), 0xFF000000u, 1));
    TC_CHECK_INT(tc_image_width(img), 16); /* its own size doesn't change */
    /* Smaller: what's drawn will do. A different shape: enough to cover
       it, in its own shape. */
    tc_image_want_size(img, 10, 10);
    TC_CHECK(tc_image_pixels(img) != NULL);
    tc_image_want_size(img, 64, 16);
    TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_OK);
    s = tc_image_pixels(img);
    TC_CHECK(s && s->width == 64 && s->height == 64);
    /* More pixels than allowed: drawn smaller. */
    tc_image_want_size(img, 1000, 1000);
    TC_CHECK_INT(tc_image_decode(img, 100 * 100), TC_OK);
    s = tc_image_pixels(img);
    TC_CHECK(s && s->width * s->height <= 100 * 100 && s->width >= 99);
    TC_CHECK_INT(tc_image_memory(img), (size_t)s->width * s->height * 4);
    tc_image_free(img);

    /* Drawable however absurd the geometry: an edge almost horizontal, a
       point 1e30 away, a stroke 1e30 wide (NanoSVG converted each to int
       out of range; the image fuzzer found the first). */
    {
        static const char *const absurd[] = { "svg-flat-edge.svg", "svg-far-point.svg", "svg-wide-stroke.svg" };
        size_t i;
        for (i = 0; i < TC_ARRAY_LEN(absurd); i++) {
            img = open_image(absurd[i]);
            TC_CHECK(img != NULL);
            if (!img)
                continue;
            TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_OK);
            TC_CHECK(tc_image_pixels(img) != NULL);
            tc_image_free(img);
        }
    }

    /* No size at all, or an absurd one. */
    TC_CHECK_INT(tc_image_new("<svg></svg>", 11, &img), TC_ERR_DECODE);
    TC_CHECK_INT(tc_image_new("<svg width='100000' height='10'></svg>", 38, &img), TC_ERR_TOO_LARGE);
}

static void test_bad_images(void)
{
    const tc_test_blob *png = tc_test_blob_get("pattern.png");
    unsigned char *copy;
    tc_image *img = NULL;

    TC_CHECK_INT(tc_image_new("not an image", 12, &img), TC_ERR_DECODE);
    TC_CHECK(img == NULL);
    TC_CHECK_INT(tc_image_new("\x89PNG\r\n\x1a\n", 8, &img), TC_ERR_DECODE);
    TC_CHECK_INT(tc_image_new("\0\0\1\0\1\0", 6, &img), TC_ERR_DECODE); /* an ICO with no entry */
    if (!png)
        return;
    copy = malloc(png->len);
    if (!copy)
        return;
    /* Too wide: the header's width says 40,000 (its checksum is wrong too,
       but the size is checked first). */
    memcpy(copy, png->data, png->len);
    copy[16] = 0, copy[17] = 0, copy[18] = 0x9C, copy[19] = 0x40;
    img = NULL;
    TC_CHECK(tc_image_new(copy, png->len, &img) != TC_OK);
    tc_image_free(img);
    /* Too many pixels for the caller. */
    TC_CHECK_INT(tc_image_new(png->data, png->len, &img), TC_OK);
    TC_CHECK_INT(tc_image_decode(img, 7), TC_ERR_TOO_LARGE);
    TC_CHECK(tc_image_pixels(img) == NULL);
    TC_CHECK_INT(tc_image_decode(img, 8), TC_OK);
    tc_image_free(img);
    /* Cut short: the header reads, the pixels don't all arrive. */
    TC_CHECK_INT(tc_image_new(png->data, png->len - 20, &img), TC_OK);
    if (img) {
        TC_CHECK_INT(tc_image_decode(img, NO_LIMIT), TC_ERR_DECODE);
        TC_CHECK(tc_image_pixels(img) != NULL); /* what there was is kept */
        tc_image_free(img);
    }
    free(copy);
}

void tc_suite_image(void)
{
    TC_RUN(test_still_images);
    TC_RUN(test_lossy_images);
    TC_RUN(test_animation);
    TC_RUN(test_sniffing);
    TC_RUN(test_svg);
    TC_RUN(test_bad_images);
}
