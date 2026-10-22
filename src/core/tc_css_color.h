/* CSS colours (CSS Color Levels 3 to 5): named and system colours, hex,
 * rgb(), hsl(), hwb(), lab(), lch(), oklab(), oklch(), color() and
 * device-cmyk(), kept as written and converted to 8-bit sRGB for painting. */
#ifndef TC_CSS_COLOR_H
#define TC_CSS_COLOR_H

#include "tc_css_syntax.h"

typedef enum tc_color_space {
    TC_COLOR_RGB,           /* sRGB from named, hex, rgb(), hsl(), hwb(): components 0..255 */
    TC_COLOR_CURRENT,       /* currentcolor: resolved when styles are computed */
    TC_COLOR_SRGB,          /* color(srgb ...) and the other color() spaces: 0..1 */
    TC_COLOR_SRGB_LINEAR,
    TC_COLOR_DISPLAY_P3,
    TC_COLOR_A98_RGB,
    TC_COLOR_PROPHOTO_RGB,
    TC_COLOR_REC2020,
    TC_COLOR_XYZ_D50,
    TC_COLOR_XYZ_D65,
    TC_COLOR_LAB,
    TC_COLOR_LCH,
    TC_COLOR_OKLAB,
    TC_COLOR_OKLCH,
    TC_COLOR_DEVICE_CMYK
} tc_color_space;

typedef struct tc_css_color {
    uint8_t space;          /* tc_color_space */
    uint8_t none;           /* bit i: component i is "none" */
    double c[4];            /* device-cmyk uses all four */
    double alpha;           /* 0..1 */
} tc_css_color;

/* A colour from component values (whitespace around it is allowed). */
int tc_css_parse_color(const tc_css_value *values, uint32_t count, tc_css_color *out);

/* How CSS Color 4 serializes it, e.g. "rgb(255, 0, 0)", "lab(50 20 -10 / 0.5)". */
void tc_css_color_serialize(const tc_css_color *color, char *out, size_t size);

/* 0xAARRGGBB in sRGB, clipped to its gamut. currentcolor gives black. */
uint32_t tc_css_color_argb(const tc_css_color *color);

#endif
