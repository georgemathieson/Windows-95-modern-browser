#include "tc_surface.h"

#include <stdlib.h>

int tc_surface_init(tc_surface *s, int width, int height)
{
    size_t count;

    s->pixels = NULL;
    s->width = s->height = s->stride = 0;
    s->owns_pixels = 1;
    if (width < 0 || height < 0)
        return TC_ERR_INVALID;
    count = (size_t)width * (size_t)height;
    if (height && count / (size_t)height != (size_t)width)
        return TC_ERR_NOMEM;
    if (count) {
        s->pixels = calloc(count, sizeof(uint32_t));
        if (!s->pixels)
            return TC_ERR_NOMEM;
    }
    s->width = width;
    s->height = height;
    s->stride = width;
    return TC_OK;
}

void tc_surface_wrap(tc_surface *s, uint32_t *pixels, int width, int height, int stride)
{
    s->pixels = pixels;
    s->width = width;
    s->height = height;
    s->stride = stride;
    s->owns_pixels = 0;
}

void tc_surface_free(tc_surface *s)
{
    if (s->owns_pixels)
        free(s->pixels);
    s->pixels = NULL;
    s->width = s->height = s->stride = 0;
}

tc_rect tc_rect_make(int x, int y, int w, int h)
{
    tc_rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

tc_rect tc_rect_intersect(tc_rect a, tc_rect b)
{
    int x0 = TC_MAX(a.x, b.x);
    int y0 = TC_MAX(a.y, b.y);
    int x1 = TC_MIN(a.x + a.w, b.x + b.w);
    int y1 = TC_MIN(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return tc_rect_make(x0, y0, 0, 0);
    return tc_rect_make(x0, y0, x1 - x0, y1 - y0);
}

int tc_rect_is_empty(tc_rect r)
{
    return r.w <= 0 || r.h <= 0;
}

uint32_t tc_surface_get(const tc_surface *s, int x, int y)
{
    if (x < 0 || y < 0 || x >= s->width || y >= s->height)
        return 0;
    return s->pixels[(size_t)y * (size_t)s->stride + (size_t)x];
}

/* x * a / 255, rounded, without a division. */
static uint32_t mul_div255(uint32_t x, uint32_t a)
{
    uint32_t t = x * a + 128;
    return (t + (t >> 8)) >> 8;
}

uint32_t tc_color_blend(uint32_t dst, tc_color src)
{
    uint32_t a = TC_ALPHA(src), ia = 255 - a;
    uint32_t r = mul_div255(TC_RED(src), a) + mul_div255(TC_RED(dst), ia);
    uint32_t g = mul_div255(TC_GREEN(src), a) + mul_div255(TC_GREEN(dst), ia);
    uint32_t b = mul_div255(TC_BLUE(src), a) + mul_div255(TC_BLUE(dst), ia);
    return (r << 16) | (g << 8) | b;
}

void tc_surface_fill(tc_surface *s, tc_rect r, tc_color color)
{
    uint32_t a = TC_ALPHA(color);
    int x, y;

    r = tc_rect_intersect(r, tc_rect_make(0, 0, s->width, s->height));
    if (tc_rect_is_empty(r) || a == 0)
        return;
    for (y = r.y; y < r.y + r.h; y++) {
        uint32_t *row = s->pixels + (size_t)y * (size_t)s->stride;
        if (a == 255) {
            uint32_t px = color & 0x00FFFFFF;
            for (x = r.x; x < r.x + r.w; x++)
                row[x] = px;
        } else {
            for (x = r.x; x < r.x + r.w; x++)
                row[x] = tc_color_blend(row[x], color);
        }
    }
}

void tc_surface_gradient_v(tc_surface *s, tc_rect r, tc_color top, tc_color bottom)
{
    tc_rect clip = tc_rect_intersect(r, tc_rect_make(0, 0, s->width, s->height));
    int y, span = r.h > 1 ? r.h - 1 : 1;

    if (tc_rect_is_empty(clip))
        return;
    for (y = clip.y; y < clip.y + clip.h; y++) {
        /* Position along the full (unclipped) gradient, 0..255. */
        uint32_t t = (uint32_t)((y - r.y) * 255 / span);
        uint32_t red = mul_div255(TC_RED(top), 255 - t) + mul_div255(TC_RED(bottom), t);
        uint32_t green = mul_div255(TC_GREEN(top), 255 - t) + mul_div255(TC_GREEN(bottom), t);
        uint32_t blue = mul_div255(TC_BLUE(top), 255 - t) + mul_div255(TC_BLUE(bottom), t);
        tc_surface_fill(s, tc_rect_make(clip.x, y, clip.w, 1), TC_RGB(red, green, blue));
    }
}

/* A premultiplied pixel over an opaque one. */
static uint32_t over(uint32_t dst, uint32_t src)
{
    uint32_t a = src >> 24, ia, r, g, b;
    if (a == 255)
        return src & 0x00FFFFFFu;
    if (a == 0)
        return dst;
    ia = 255 - a;
    r = TC_RED(src) + mul_div255(TC_RED(dst), ia);
    g = TC_GREEN(src) + mul_div255(TC_GREEN(dst), ia);
    b = TC_BLUE(src) + mul_div255(TC_BLUE(dst), ia);
    return (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
}

/* The source pixels [from, to) that destination pixel i of n covers, out
   of size. */
static void span(int i, int n, int size, int *from, int *to)
{
    *from = (int)((int64_t)i * size / n);
    *to = (int)((int64_t)(i + 1) * size / n);
    if (*to <= *from)
        *to = *from + 1;
    if (*to > size)
        *to = size;
    if (*from >= *to)
        *from = *to - 1;
}

/* Where destination pixel i of n samples a source of size, in 16.16
   fixed point: its centre, mapped back. */
static int32_t sample(int i, int n, int size)
{
    return (int32_t)(((int64_t)(2 * i + 1) * size * 65536 / n - 65536) / 2);
}

static uint32_t lerp(uint32_t a, uint32_t b, uint32_t t) /* t: 0..256 */
{
    uint32_t rb = ((a & 0x00FF00FFu) * (256 - t) + (b & 0x00FF00FFu) * t) >> 8 & 0x00FF00FFu;
    uint32_t ag = ((a >> 8 & 0x00FF00FFu) * (256 - t) + (b >> 8 & 0x00FF00FFu) * t) & 0xFF00FF00u;
    return rb | ag;
}

void tc_surface_draw_image(tc_surface *s, tc_rect clip, const tc_surface *image, tc_rect dst)
{
    tc_rect r = tc_rect_intersect(tc_rect_intersect(clip, tc_rect_make(0, 0, s->width, s->height)), dst);
    int sw = image->width, sh = image->height, x, y;

    if (tc_rect_is_empty(r) || sw <= 0 || sh <= 0)
        return;
    if (dst.w == sw && dst.h == sh) {
        for (y = r.y; y < r.y + r.h; y++) {
            const uint32_t *src = image->pixels + (size_t)(y - dst.y) * (size_t)image->stride + (r.x - dst.x);
            uint32_t *row = s->pixels + (size_t)y * (size_t)s->stride + r.x;
            for (x = 0; x < r.w; x++)
                row[x] = over(row[x], src[x]);
        }
        return;
    }
    if (sw > 2 * dst.w || sh > 2 * dst.h) {
        /* Shrinking a lot: average the pixels each one covers. */
        for (y = r.y; y < r.y + r.h; y++) {
            uint32_t *row = s->pixels + (size_t)y * (size_t)s->stride;
            int y0, y1;
            span(y - dst.y, dst.h, sh, &y0, &y1);
            for (x = r.x; x < r.x + r.w; x++) {
                uint32_t sa = 0, sr = 0, sg = 0, sb = 0, n, c;
                int x0, x1, i, j;
                span(x - dst.x, dst.w, sw, &x0, &x1);
                for (j = y0; j < y1; j++) {
                    const uint32_t *src = image->pixels + (size_t)j * (size_t)image->stride;
                    for (i = x0; i < x1; i++) {
                        c = src[i];
                        sa += c >> 24, sr += c >> 16 & 255, sg += c >> 8 & 255, sb += c & 255;
                    }
                }
                n = (uint32_t)((y1 - y0) * (x1 - x0));
                c = (sa / n) << 24 | (sr / n) << 16 | (sg / n) << 8 | sb / n;
                row[x] = over(row[x], c);
            }
        }
        return;
    }
    /* Bilinear, in 16.16 fixed point. */
    for (y = r.y; y < r.y + r.h; y++) {
        int32_t fy = sample(y - dst.y, dst.h, sh);
        int y0 = fy < 0 ? 0 : fy >> 16, y1 = y0 + 1;
        uint32_t ty = fy < 0 ? 0 : (uint32_t)(fy >> 8 & 255);
        const uint32_t *top, *bottom;
        uint32_t *row = s->pixels + (size_t)y * (size_t)s->stride;
        if (y0 >= sh)
            y0 = sh - 1;
        if (y1 >= sh)
            y1 = sh - 1;
        top = image->pixels + (size_t)y0 * (size_t)image->stride;
        bottom = image->pixels + (size_t)y1 * (size_t)image->stride;
        for (x = r.x; x < r.x + r.w; x++) {
            int32_t fx = sample(x - dst.x, dst.w, sw);
            int x0 = fx < 0 ? 0 : fx >> 16, x1 = x0 + 1;
            uint32_t tx = fx < 0 ? 0 : (uint32_t)(fx >> 8 & 255);
            if (x0 >= sw)
                x0 = sw - 1;
            if (x1 >= sw)
                x1 = sw - 1;
            row[x] = over(row[x], lerp(lerp(top[x0], top[x1], tx), lerp(bottom[x0], bottom[x1], tx), ty));
        }
    }
}
