/* Dithering for 256-colour displays (decision 39): a fixed palette and an
 * ordered dither to it, so photographs and gradients keep their shades
 * where Windows would round each pixel to its nearest colour.
 *
 * The palette is a 6x6x6 colour cube and 20 greys between the cube's own
 * six, 236 colours, which leaves Windows its 20 static ones. Near-grey
 * pixels (most text and page backgrounds) are dithered along the 26 greys,
 * the rest in the cube. A colour in the palette comes out flat. */
#ifndef TC_DITHER_H
#define TC_DITHER_H

#include "tc_surface.h"

#define TC_DITHER_COLOURS 236

/* The palette, 0x00RRGGBB. Index 36r + 6g + b is the cube's colour with
   levels r, g and b (0 to 5, 51 apart); the greys follow, darkest first. */
void tc_dither_palette(uint32_t colours[TC_DITHER_COLOURS]);

/* Dither the rect r of src (opaque pixels) to palette indexes. dst holds
   a byte per pixel, laid out like src with rows dst_stride bytes apart.
   The pattern is anchored at (-ox, -oy): give the scroll position, and
   pixels moved by scrolling still match the ones dithered after. */
void tc_dither(const tc_surface *src, tc_rect r, uint8_t *dst, int dst_stride, int ox, int oy);

#endif
