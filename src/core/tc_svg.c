/* SVG through NanoSVG (third_party/nanosvg/README.tachyon.md). Its code
 * isn't written for Tachyon's warning flags, so they're relaxed for it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif
#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "tc_image.h"
#include "tc_svg.h"

#define SNIFF_LIMIT 4096 /* bytes of preamble before <svg> */

struct tc_svg {
    NSVGimage *image;
};

static int starts(const char *p, const char *end, const char *s)
{
    size_t n = strlen(s);
    return (size_t)(end - p) >= n && memcmp(p, s, n) == 0;
}

/* Past the next occurrence of s, or NULL. */
static const char *past(const char *p, const char *end, const char *s)
{
    size_t n = strlen(s);
    for (; (size_t)(end - p) >= n; p++)
        if (memcmp(p, s, n) == 0)
            return p + n;
    return NULL;
}

int tc_svg_sniff(const void *data, size_t len)
{
    const char *p = data, *end = p + (len < SNIFF_LIMIT ? len : SNIFF_LIMIT);
    if (starts(p, end, "\xEF\xBB\xBF"))
        p += 3;
    while (p && p < end) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        } else if (starts(p, end, "<?")) {
            p = past(p, end, "?>");
        } else if (starts(p, end, "<!--")) {
            p = past(p, end, "-->");
        } else if (starts(p, end, "<!DOCTYPE") || starts(p, end, "<!doctype")) {
            const char *close = past(p, end, ">"), *open = past(p, end, "[");
            p = open && (!close || open < close) ? past(p, end, "]>") : close;
        } else {
            return starts(p, end, "<svg") && end - p > 4 &&
                   (p[4] == ' ' || p[4] == '>' || p[4] == '/' || p[4] == '\t' || p[4] == '\r' || p[4] == '\n');
        }
    }
    return 0;
}

int tc_svg_parse(const void *data, size_t len, tc_svg **out, float *w, float *h)
{
    char *text;
    NSVGimage *image;
    tc_svg *svg;

    *out = NULL;
    if (len > (16u << 20))
        return TC_ERR_TOO_LARGE;
    text = malloc(len + 1); /* NanoSVG wants a string it may change */
    if (!text)
        return TC_ERR_NOMEM;
    memcpy(text, data, len);
    text[len] = '\0';
    image = nsvgParse(text, "px", 96.0f);
    free(text);
    if (!image)
        return TC_ERR_NOMEM;
    if (!(image->width >= 1 && image->height >= 1 && image->width <= TC_IMAGE_MAX_SIDE &&
          image->height <= TC_IMAGE_MAX_SIDE)) {
        /* No size, or an absurd one. */
        int err = image->width > TC_IMAGE_MAX_SIDE || image->height > TC_IMAGE_MAX_SIDE ? TC_ERR_TOO_LARGE
                                                                                         : TC_ERR_DECODE;
        nsvgDelete(image);
        return err;
    }
    svg = malloc(sizeof *svg);
    if (!svg) {
        nsvgDelete(image);
        return TC_ERR_NOMEM;
    }
    svg->image = image;
    *w = image->width;
    *h = image->height;
    *out = svg;
    return TC_OK;
}

void tc_svg_free(tc_svg *svg)
{
    if (!svg)
        return;
    nsvgDelete(svg->image);
    free(svg);
}

int tc_svg_render(const tc_svg *svg, tc_surface *s)
{
    NSVGrasterizer *r;
    float sx = s->width / svg->image->width, sy = s->height / svg->image->height, scale = sx < sy ? sx : sy;
    int x, y;

    for (y = 0; y < s->height; y++)
        memset(s->pixels + (size_t)y * (size_t)s->stride, 0, (size_t)s->width * 4);
    if (!(r = nsvgCreateRasterizer()))
        return TC_ERR_NOMEM;
    nsvgRasterize(r, svg->image, (s->width - svg->image->width * scale) / 2,
                  (s->height - svg->image->height * scale) / 2, scale, (unsigned char *)s->pixels, s->width,
                  s->height, s->stride * 4);
    nsvgDeleteRasterizer(r);
    /* RGBA bytes, not premultiplied, to premultiplied 0xAARRGGBB. */
    for (y = 0; y < s->height; y++) {
        uint32_t *row = s->pixels + (size_t)y * (size_t)s->stride;
        for (x = 0; x < s->width; x++) {
            const uint8_t *b = (const uint8_t *)&row[x];
            uint32_t a = b[3];
            row[x] = a << 24 | (b[0] * a + 127) / 255 << 16 | (b[1] * a + 127) / 255 << 8 | (b[2] * a + 127) / 255;
        }
    }
    return TC_OK;
}
