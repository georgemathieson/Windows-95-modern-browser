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
