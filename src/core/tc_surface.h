/* A 32-bit pixel surface: everything Tachyon draws ends up in one of these.
 *
 * Pixels are uint32_t 0x00RRGGBB, which is the memory layout of a 32 bpp
 * top-down Windows DIB section, so the Win32 layer can blit a surface
 * without converting it. Colours passed to drawing calls are 0xAARRGGBB;
 * alpha 255 is opaque. */
#ifndef TC_SURFACE_H
#define TC_SURFACE_H

#include "tc_base.h"

typedef uint32_t tc_color;

#define TC_RGBA(r, g, b, a) \
    ((tc_color)(((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define TC_RGB(r, g, b) TC_RGBA(r, g, b, 255)

#define TC_ALPHA(c) ((uint32_t)(c) >> 24)
#define TC_RED(c)   (((uint32_t)(c) >> 16) & 0xFF)
#define TC_GREEN(c) (((uint32_t)(c) >> 8) & 0xFF)
#define TC_BLUE(c)  ((uint32_t)(c) & 0xFF)

typedef struct tc_rect {
    int x, y, w, h;
} tc_rect;

typedef struct tc_surface {
    uint32_t *pixels;
    int width;
    int height;
    int stride;       /* pixels per row */
    int owns_pixels;  /* tc_surface_free releases pixels */
} tc_surface;

/* Allocate a w x h surface cleared to black. */
int tc_surface_init(tc_surface *s, int width, int height);
/* Draw into memory owned by someone else, e.g. a DIB section. */
void tc_surface_wrap(tc_surface *s, uint32_t *pixels, int width, int height, int stride);
void tc_surface_free(tc_surface *s);

tc_rect tc_rect_make(int x, int y, int w, int h);
tc_rect tc_rect_intersect(tc_rect a, tc_rect b);
int tc_rect_is_empty(tc_rect r);

/* Pixel at (x, y) as 0x00RRGGBB; 0 outside the surface. */
uint32_t tc_surface_get(const tc_surface *s, int x, int y);

/* Fill a rectangle, clipped to the surface, blending if alpha < 255. */
void tc_surface_fill(tc_surface *s, tc_rect r, tc_color color);
/* Vertical gradient from `top` to `bottom` (opaque colours). */
void tc_surface_gradient_v(tc_surface *s, tc_rect r, tc_color top, tc_color bottom);

/* Blend src over dst (0x00RRGGBB) with src's alpha. */
uint32_t tc_color_blend(uint32_t dst, tc_color src);

#endif
