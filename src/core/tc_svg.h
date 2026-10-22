/* SVG pictures (decision 41): NanoSVG parses them, from files or from the
 * markup of inline <svg> elements, and draws them at any size into
 * premultiplied pixels like tc_image's. Shapes and paths, fills and
 * strokes, transforms, viewBox and gradients; no text, filters or
 * scripts. */
#ifndef TC_SVG_H
#define TC_SVG_H

#include "tc_surface.h"

typedef struct tc_svg tc_svg;

/* Whether data looks like an SVG file: an <svg> element first, after any
   byte order mark, white space, XML declaration, comments and doctype. */
int tc_svg_sniff(const void *data, size_t len);

/* Parse an SVG, and its natural size in px (each at least 1). */
int tc_svg_parse(const void *data, size_t len, tc_svg **out, float *w, float *h);
void tc_svg_free(tc_svg *svg);

/* Draw it into all of s, scaled to fit and centred: s is cleared first. */
int tc_svg_render(const tc_svg *svg, tc_surface *s);

#endif
