#include "tc_css_color.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Named and system colours ---- */

typedef struct named_color {
    const char *name;
    uint32_t argb;
} named_color;

/* CSS Color 4's named colours and transparent, sorted for binary search. */
static const named_color named[] = {
    { "aliceblue", 0xFFF0F8FFu },
    { "antiquewhite", 0xFFFAEBD7u },
    { "aqua", 0xFF00FFFFu },
    { "aquamarine", 0xFF7FFFD4u },
    { "azure", 0xFFF0FFFFu },
    { "beige", 0xFFF5F5DCu },
    { "bisque", 0xFFFFE4C4u },
    { "black", 0xFF000000u },
    { "blanchedalmond", 0xFFFFEBCDu },
    { "blue", 0xFF0000FFu },
    { "blueviolet", 0xFF8A2BE2u },
    { "brown", 0xFFA52A2Au },
    { "burlywood", 0xFFDEB887u },
    { "cadetblue", 0xFF5F9EA0u },
    { "chartreuse", 0xFF7FFF00u },
    { "chocolate", 0xFFD2691Eu },
    { "coral", 0xFFFF7F50u },
    { "cornflowerblue", 0xFF6495EDu },
    { "cornsilk", 0xFFFFF8DCu },
    { "crimson", 0xFFDC143Cu },
    { "cyan", 0xFF00FFFFu },
    { "darkblue", 0xFF00008Bu },
    { "darkcyan", 0xFF008B8Bu },
    { "darkgoldenrod", 0xFFB8860Bu },
    { "darkgray", 0xFFA9A9A9u },
    { "darkgreen", 0xFF006400u },
    { "darkgrey", 0xFFA9A9A9u },
    { "darkkhaki", 0xFFBDB76Bu },
    { "darkmagenta", 0xFF8B008Bu },
    { "darkolivegreen", 0xFF556B2Fu },
    { "darkorange", 0xFFFF8C00u },
    { "darkorchid", 0xFF9932CCu },
    { "darkred", 0xFF8B0000u },
    { "darksalmon", 0xFFE9967Au },
    { "darkseagreen", 0xFF8FBC8Fu },
    { "darkslateblue", 0xFF483D8Bu },
    { "darkslategray", 0xFF2F4F4Fu },
    { "darkslategrey", 0xFF2F4F4Fu },
    { "darkturquoise", 0xFF00CED1u },
    { "darkviolet", 0xFF9400D3u },
    { "deeppink", 0xFFFF1493u },
    { "deepskyblue", 0xFF00BFFFu },
    { "dimgray", 0xFF696969u },
    { "dimgrey", 0xFF696969u },
    { "dodgerblue", 0xFF1E90FFu },
    { "firebrick", 0xFFB22222u },
    { "floralwhite", 0xFFFFFAF0u },
    { "forestgreen", 0xFF228B22u },
    { "fuchsia", 0xFFFF00FFu },
    { "gainsboro", 0xFFDCDCDCu },
    { "ghostwhite", 0xFFF8F8FFu },
    { "gold", 0xFFFFD700u },
    { "goldenrod", 0xFFDAA520u },
    { "gray", 0xFF808080u },
    { "green", 0xFF008000u },
    { "greenyellow", 0xFFADFF2Fu },
    { "grey", 0xFF808080u },
    { "honeydew", 0xFFF0FFF0u },
    { "hotpink", 0xFFFF69B4u },
    { "indianred", 0xFFCD5C5Cu },
    { "indigo", 0xFF4B0082u },
    { "ivory", 0xFFFFFFF0u },
    { "khaki", 0xFFF0E68Cu },
    { "lavender", 0xFFE6E6FAu },
    { "lavenderblush", 0xFFFFF0F5u },
    { "lawngreen", 0xFF7CFC00u },
    { "lemonchiffon", 0xFFFFFACDu },
    { "lightblue", 0xFFADD8E6u },
    { "lightcoral", 0xFFF08080u },
    { "lightcyan", 0xFFE0FFFFu },
    { "lightgoldenrodyellow", 0xFFFAFAD2u },
    { "lightgray", 0xFFD3D3D3u },
    { "lightgreen", 0xFF90EE90u },
    { "lightgrey", 0xFFD3D3D3u },
    { "lightpink", 0xFFFFB6C1u },
    { "lightsalmon", 0xFFFFA07Au },
    { "lightseagreen", 0xFF20B2AAu },
    { "lightskyblue", 0xFF87CEFAu },
    { "lightslategray", 0xFF778899u },
    { "lightslategrey", 0xFF778899u },
    { "lightsteelblue", 0xFFB0C4DEu },
    { "lightyellow", 0xFFFFFFE0u },
    { "lime", 0xFF00FF00u },
    { "limegreen", 0xFF32CD32u },
    { "linen", 0xFFFAF0E6u },
    { "magenta", 0xFFFF00FFu },
    { "maroon", 0xFF800000u },
    { "mediumaquamarine", 0xFF66CDAAu },
    { "mediumblue", 0xFF0000CDu },
    { "mediumorchid", 0xFFBA55D3u },
    { "mediumpurple", 0xFF9370DBu },
    { "mediumseagreen", 0xFF3CB371u },
    { "mediumslateblue", 0xFF7B68EEu },
    { "mediumspringgreen", 0xFF00FA9Au },
    { "mediumturquoise", 0xFF48D1CCu },
    { "mediumvioletred", 0xFFC71585u },
    { "midnightblue", 0xFF191970u },
    { "mintcream", 0xFFF5FFFAu },
    { "mistyrose", 0xFFFFE4E1u },
    { "moccasin", 0xFFFFE4B5u },
    { "navajowhite", 0xFFFFDEADu },
    { "navy", 0xFF000080u },
    { "oldlace", 0xFFFDF5E6u },
    { "olive", 0xFF808000u },
    { "olivedrab", 0xFF6B8E23u },
    { "orange", 0xFFFFA500u },
    { "orangered", 0xFFFF4500u },
    { "orchid", 0xFFDA70D6u },
    { "palegoldenrod", 0xFFEEE8AAu },
    { "palegreen", 0xFF98FB98u },
    { "paleturquoise", 0xFFAFEEEEu },
    { "palevioletred", 0xFFDB7093u },
    { "papayawhip", 0xFFFFEFD5u },
    { "peachpuff", 0xFFFFDAB9u },
    { "peru", 0xFFCD853Fu },
    { "pink", 0xFFFFC0CBu },
    { "plum", 0xFFDDA0DDu },
    { "powderblue", 0xFFB0E0E6u },
    { "purple", 0xFF800080u },
    { "rebeccapurple", 0xFF663399u },
    { "red", 0xFFFF0000u },
    { "rosybrown", 0xFFBC8F8Fu },
    { "royalblue", 0xFF4169E1u },
    { "saddlebrown", 0xFF8B4513u },
    { "salmon", 0xFFFA8072u },
    { "sandybrown", 0xFFF4A460u },
    { "seagreen", 0xFF2E8B57u },
    { "seashell", 0xFFFFF5EEu },
    { "sienna", 0xFFA0522Du },
    { "silver", 0xFFC0C0C0u },
    { "skyblue", 0xFF87CEEBu },
    { "slateblue", 0xFF6A5ACDu },
    { "slategray", 0xFF708090u },
    { "slategrey", 0xFF708090u },
    { "snow", 0xFFFFFAFAu },
    { "springgreen", 0xFF00FF7Fu },
    { "steelblue", 0xFF4682B4u },
    { "tan", 0xFFD2B48Cu },
    { "teal", 0xFF008080u },
    { "thistle", 0xFFD8BFD8u },
    { "tomato", 0xFFFF6347u },
    { "transparent", 0x00000000u },
    { "turquoise", 0xFF40E0D0u },
    { "violet", 0xFFEE82EEu },
    { "wheat", 0xFFF5DEB3u },
    { "white", 0xFFFFFFFFu },
    { "whitesmoke", 0xFFF5F5F5u },
    { "yellow", 0xFFFFFF00u },
    { "yellowgreen", 0xFF9ACD32u },
};

/* System colours, as Windows 95 draws them by default. Sorted, lower case. */
static const named_color system_colors[] = {
    { "accentcolor", 0xFF000080u },
    { "accentcolortext", 0xFFFFFFFFu },
    { "activeborder", 0xFFC0C0C0u },
    { "activecaption", 0xFF000080u },
    { "activetext", 0xFFFF0000u },
    { "appworkspace", 0xFF808080u },
    { "background", 0xFF008080u },
    { "buttonborder", 0xFF000000u },
    { "buttonface", 0xFFC0C0C0u },
    { "buttonhighlight", 0xFFFFFFFFu },
    { "buttonshadow", 0xFF808080u },
    { "buttontext", 0xFF000000u },
    { "canvas", 0xFFFFFFFFu },
    { "canvastext", 0xFF000000u },
    { "captiontext", 0xFFFFFFFFu },
    { "field", 0xFFFFFFFFu },
    { "fieldtext", 0xFF000000u },
    { "graytext", 0xFF808080u },
    { "highlight", 0xFF000080u },
    { "highlighttext", 0xFFFFFFFFu },
    { "inactiveborder", 0xFFC0C0C0u },
    { "inactivecaption", 0xFF808080u },
    { "inactivecaptiontext", 0xFFC0C0C0u },
    { "infobackground", 0xFFFFFFE1u },
    { "infotext", 0xFF000000u },
    { "linktext", 0xFF0000EEu },
    { "mark", 0xFFFFFF00u },
    { "marktext", 0xFF000000u },
    { "menu", 0xFFC0C0C0u },
    { "menutext", 0xFF000000u },
    { "scrollbar", 0xFFC0C0C0u },
    { "selecteditem", 0xFF000080u },
    { "selecteditemtext", 0xFFFFFFFFu },
    { "threeddarkshadow", 0xFF000000u },
    { "threedface", 0xFFC0C0C0u },
    { "threedhighlight", 0xFFFFFFFFu },
    { "threedlightshadow", 0xFFDFDFDFu },
    { "threedshadow", 0xFF808080u },
    { "visitedtext", 0xFF551A8Bu },
    { "window", 0xFFFFFFFFu },
    { "windowframe", 0xFF000000u },
    { "windowtext", 0xFF000000u },
};

static int lookup(const named_color *table, size_t n, const char *name, uint32_t *argb)
{
    char lower[32];
    size_t i, lo = 0, hi = n;

    for (i = 0; name[i]; i++) {
        if (i + 1 >= sizeof lower)
            return 0;
        lower[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    }
    lower[i] = '\0';
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(lower, table[mid].name);
        if (cmp == 0) {
            *argb = table[mid].argb;
            return 1;
        }
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

static void set_argb(tc_css_color *out, uint32_t argb)
{
    memset(out, 0, sizeof *out);
    out->space = TC_COLOR_RGB;
    out->c[0] = (argb >> 16) & 255;
    out->c[1] = (argb >> 8) & 255;
    out->c[2] = argb & 255;
    out->alpha = (argb >> 24) / 255.0;
}

/* ---- Hex ---- */

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c |= 0x20;
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int parse_hex(const char *s, tc_css_color *out)
{
    size_t n = strlen(s), i;
    int d[8];
    if (n != 3 && n != 4 && n != 6 && n != 8)
        return TC_ERR_INVALID;
    for (i = 0; i < n; i++)
        if ((d[i] = hex_digit((unsigned char)s[i])) < 0)
            return TC_ERR_INVALID;
    memset(out, 0, sizeof *out);
    out->space = TC_COLOR_RGB;
    if (n <= 4) {
        for (i = 0; i < 3; i++)
            out->c[i] = d[i] * 17;
        out->alpha = n == 4 ? d[3] * 17 / 255.0 : 1;
    } else {
        for (i = 0; i < 3; i++)
            out->c[i] = d[2 * i] * 16 + d[2 * i + 1];
        out->alpha = n == 8 ? (d[6] * 16 + d[7]) / 255.0 : 1;
    }
    return TC_OK;
}

/* ---- Function arguments ---- */

#define MAX_ARGS 5

typedef struct args {
    const tc_css_value *v[MAX_ARGS]; /* NULL for "none" */
    int n;
    const tc_css_value *alpha;
    int has_alpha, alpha_none;
    int legacy;                      /* separated by commas */
} args;

static int is_none(const tc_css_value *v)
{
    return tc_css_is_ident(v, "none");
}

/* Split a colour function's arguments: "a, b, c[, alpha]" or "a b c [/ alpha]". */
static int split_args(const tc_css_value *items, uint32_t count, args *out)
{
    uint32_t i;
    int commas = 0, slash = 0, expect_value = 1;

    memset(out, 0, sizeof *out);
    for (i = 0; i < count; i++)
        if (items[i].type == TC_CSS_COMMA)
            commas = 1;
    out->legacy = commas;
    for (i = 0; i < count; i++) {
        const tc_css_value *v = &items[i];
        if (v->type == TC_CSS_WHITESPACE)
            continue;
        if (commas) {
            if (v->type == TC_CSS_COMMA) {
                if (expect_value)
                    return TC_ERR_INVALID;
                expect_value = 1;
                continue;
            }
            if (!expect_value || out->n == MAX_ARGS || is_none(v))
                return TC_ERR_INVALID;
            out->v[out->n++] = v;
            expect_value = 0;
            continue;
        }
        if (tc_css_is_delim(v, '/')) {
            if (slash)
                return TC_ERR_INVALID;
            slash = 1;
            continue;
        }
        if (slash) {
            if (out->has_alpha)
                return TC_ERR_INVALID;
            out->has_alpha = 1;
            if (is_none(v))
                out->alpha_none = 1;
            else
                out->alpha = v;
            continue;
        }
        if (out->n == MAX_ARGS)
            return TC_ERR_INVALID;
        out->v[out->n++] = is_none(v) ? NULL : v;
    }
    if (commas && expect_value)
        return TC_ERR_INVALID;
    if (slash && !out->has_alpha)
        return TC_ERR_INVALID;
    return TC_OK;
}

/* A number or percentage; pct_scale is what 100% means. */
static int num_or_pct(const tc_css_value *v, double pct_scale, double *out)
{
    if (v->type == TC_CSS_NUMBER) {
        *out = v->number;
        return 1;
    }
    if (v->type == TC_CSS_PERCENTAGE) {
        *out = v->number / 100.0 * pct_scale;
        return 1;
    }
    return 0;
}

/* A hue in degrees: a number or an angle. */
static int hue(const tc_css_value *v, double *out)
{
    if (v->type == TC_CSS_NUMBER) {
        *out = v->number;
        return 1;
    }
    if (v->type == TC_CSS_DIMENSION) {
        if (tc_css_ascii_ieq(v->text, "deg"))
            *out = v->number;
        else if (tc_css_ascii_ieq(v->text, "grad"))
            *out = v->number * 0.9;
        else if (tc_css_ascii_ieq(v->text, "rad"))
            *out = v->number * 180.0 / 3.14159265358979323846;
        else if (tc_css_ascii_ieq(v->text, "turn"))
            *out = v->number * 360.0;
        else
            return 0;
        return 1;
    }
    return 0;
}

static int parse_alpha(const args *a, tc_css_color *out)
{
    double v;
    out->alpha = 1;
    if (a->alpha_none) {
        out->none |= 8;
        out->alpha = 0;
        return 1;
    }
    if (!a->alpha)
        return 1;
    if (!num_or_pct(a->alpha, 1.0, &v))
        return 0;
    out->alpha = v < 0 ? 0 : v > 1 ? 1 : v;
    return 1;
}

/* colorsys.hls_to_rgb, which the tests' expected values come from. */
static double hls_value(double m1, double m2, double h)
{
    h = fmod(h, 1.0);
    if (h < 0)
        h += 1.0;
    if (h < 1.0 / 6.0)
        return m1 + (m2 - m1) * h * 6.0;
    if (h < 0.5)
        return m2;
    if (h < 2.0 / 3.0)
        return m1 + (m2 - m1) * (2.0 / 3.0 - h) * 6.0;
    return m1;
}

static void hls_to_rgb(double h, double l, double s, double rgb[3])
{
    double m1, m2;
    if (s == 0.0) {
        rgb[0] = rgb[1] = rgb[2] = l;
        return;
    }
    m2 = l <= 0.5 ? l * (1.0 + s) : l + s - (l * s);
    m1 = 2.0 * l - m2;
    rgb[0] = hls_value(m1, m2, h + 1.0 / 3.0);
    rgb[1] = hls_value(m1, m2, h);
    rgb[2] = hls_value(m1, m2, h - 1.0 / 3.0);
}

static int parse_rgb(const args *a, tc_css_color *out)
{
    int i, pct = 0;
    if (a->n != 3 && !(a->legacy && a->n == 4))
        return TC_ERR_INVALID;
    memset(out, 0, sizeof *out);
    out->space = TC_COLOR_RGB;
    for (i = 0; i < 3; i++) {
        const tc_css_value *v = a->v[i];
        double x;
        if (!v)
            continue; /* none */
        if (a->legacy) {
            /* All numbers or all percentages. */
            if (i == 0)
                pct = v->type == TC_CSS_PERCENTAGE;
            else if ((v->type == TC_CSS_PERCENTAGE) != pct)
                return TC_ERR_INVALID;
        }
        if (!num_or_pct(v, 255.0, &x))
            return TC_ERR_INVALID;
        out->c[i] = x < 0 ? 0 : x > 255 ? 255 : x;
    }
    if (a->legacy && a->n == 4) {
        args alpha = *a;
        alpha.alpha = a->v[3];
        alpha.alpha_none = 0;
        return parse_alpha(&alpha, out) ? TC_OK : TC_ERR_INVALID;
    }
    return parse_alpha(a, out) ? TC_OK : TC_ERR_INVALID;
}

static int parse_hsl_hwb(const args *a, int hwb, tc_css_color *out)
{
    double h = 0, x = 0, y = 0, rgb[3];
    int i;

    if (a->n != 3 && !(!hwb && a->legacy && a->n == 4))
        return TC_ERR_INVALID;
    if (hwb && a->legacy)
        return TC_ERR_INVALID;
    if (a->v[0] && !hue(a->v[0], &h))
        return TC_ERR_INVALID;
    for (i = 1; i < 3; i++) {
        const tc_css_value *v = a->v[i];
        double *dest = i == 1 ? &x : &y;
        if (!v)
            continue;
        /* Legacy hsl() wants percentages; the modern syntax takes numbers too. */
        if (a->legacy ? v->type != TC_CSS_PERCENTAGE : !num_or_pct(v, 100.0, dest))
            return TC_ERR_INVALID;
        if (a->legacy)
            *dest = v->number;
    }
    memset(out, 0, sizeof *out);
    out->space = TC_COLOR_RGB;
    if (!hwb) {
        x = x < 0 ? 0 : x > 100 ? 100 : x;
        y = y < 0 ? 0 : y > 100 ? 100 : y;
        hls_to_rgb(h / 360.0, y / 100.0, x / 100.0, rgb);
        for (i = 0; i < 3; i++)
            out->c[i] = rgb[i] * 255.0;
    } else if (x + y >= 100) {
        for (i = 0; i < 3; i++)
            out->c[i] = x / (x + y) * 255.0;
    } else {
        hls_to_rgb(h / 360.0, 0.5, 1.0, rgb);
        for (i = 0; i < 3; i++)
            out->c[i] = ((rgb[i] * (100.0 - x - y)) + x) / 100.0 * 255.0;
    }
    if (a->legacy && a->n == 4) {
        args alpha = *a;
        alpha.alpha = a->v[3];
        alpha.alpha_none = 0;
        return parse_alpha(&alpha, out) ? TC_OK : TC_ERR_INVALID;
    }
    return parse_alpha(a, out) ? TC_OK : TC_ERR_INVALID;
}

/* lab(), oklab(), lch(), oklch(): l_pct and c_pct are what 100% means. */
static int parse_lab_like(const args *a, int space, tc_css_color *out)
{
    int polar = space == TC_COLOR_LCH || space == TC_COLOR_OKLCH;
    int ok = space == TC_COLOR_OKLAB || space == TC_COLOR_OKLCH;
    double l_pct = ok ? 1.0 : 100.0, ab_pct = ok ? 0.4 : (polar ? 150.0 : 125.0);
    int i;

    if (a->legacy || a->n != 3)
        return TC_ERR_INVALID;
    memset(out, 0, sizeof *out);
    out->space = (uint8_t)space;
    for (i = 0; i < 3; i++) {
        const tc_css_value *v = a->v[i];
        if (!v) {
            out->none |= (uint8_t)(1u << i);
            continue;
        }
        if (polar && i == 2) {
            double h;
            if (!hue(v, &h))
                return TC_ERR_INVALID;
            h = fmod(h, 360.0);
            out->c[2] = h < 0 ? h + 360.0 : h;
        } else if (!num_or_pct(v, i == 0 ? l_pct : ab_pct, &out->c[i])) {
            return TC_ERR_INVALID;
        }
    }
    return parse_alpha(a, out) ? TC_OK : TC_ERR_INVALID;
}

static const struct {
    const char *name;
    int space;
} color_spaces[] = {
    { "srgb", TC_COLOR_SRGB },
    { "srgb-linear", TC_COLOR_SRGB_LINEAR },
    { "display-p3", TC_COLOR_DISPLAY_P3 },
    { "a98-rgb", TC_COLOR_A98_RGB },
    { "prophoto-rgb", TC_COLOR_PROPHOTO_RGB },
    { "rec2020", TC_COLOR_REC2020 },
    { "xyz", TC_COLOR_XYZ_D65 },
    { "xyz-d50", TC_COLOR_XYZ_D50 },
    { "xyz-d65", TC_COLOR_XYZ_D65 },
};

static int parse_color_function(const tc_css_value *items, uint32_t count, tc_css_color *out)
{
    uint32_t i = 0;
    size_t s;
    args a;

    while (i < count && items[i].type == TC_CSS_WHITESPACE)
        i++;
    if (i == count || items[i].type != TC_CSS_IDENT)
        return TC_ERR_INVALID;
    for (s = 0; s < TC_ARRAY_LEN(color_spaces); s++)
        if (tc_css_ascii_ieq(items[i].text, color_spaces[s].name))
            break;
    if (s == TC_ARRAY_LEN(color_spaces))
        return TC_ERR_INVALID;
    if (split_args(items + i + 1, count - i - 1, &a) != TC_OK || a.legacy || a.n != 3)
        return TC_ERR_INVALID;
    memset(out, 0, sizeof *out);
    out->space = (uint8_t)color_spaces[s].space;
    for (i = 0; i < 3; i++) {
        if (!a.v[i])
            out->none |= (uint8_t)(1u << i);
        else if (!num_or_pct(a.v[i], 1.0, &out->c[i]))
            return TC_ERR_INVALID;
    }
    return parse_alpha(&a, out) ? TC_OK : TC_ERR_INVALID;
}

static int parse_cmyk(const args *a, tc_css_color *out)
{
    int i;
    if (a->legacy ? a->n != 4 && a->n != 5 : a->n != 4)
        return TC_ERR_INVALID;
    memset(out, 0, sizeof *out);
    out->space = TC_COLOR_DEVICE_CMYK;
    for (i = 0; i < 4; i++) {
        if (!a->v[i])
            out->none |= (uint8_t)(1u << i);
        else if (a->legacy ? a->v[i]->type != TC_CSS_NUMBER || !num_or_pct(a->v[i], 1.0, &out->c[i])
                           : !num_or_pct(a->v[i], 1.0, &out->c[i]))
            return TC_ERR_INVALID;
        out->c[i] = out->c[i] < 0 ? 0 : out->c[i] > 1 ? 1 : out->c[i];
    }
    if (a->legacy && a->n == 5) {
        args alpha = *a;
        alpha.alpha = a->v[4];
        return parse_alpha(&alpha, out) ? TC_OK : TC_ERR_INVALID;
    }
    return parse_alpha(a, out) ? TC_OK : TC_ERR_INVALID;
}

int tc_css_parse_color(const tc_css_value *values, uint32_t count, tc_css_color *out)
{
    const tc_css_value *v = NULL;
    const char *name;
    uint32_t i, argb;
    args a;

    for (i = 0; i < count; i++) {
        if (values[i].type == TC_CSS_WHITESPACE)
            continue;
        if (v)
            return TC_ERR_INVALID;
        v = &values[i];
    }
    if (!v)
        return TC_ERR_INVALID;
    if (v->type == TC_CSS_IDENT) {
        if (tc_css_ascii_ieq(v->text, "currentcolor")) {
            memset(out, 0, sizeof *out);
            out->space = TC_COLOR_CURRENT;
            out->alpha = 1;
            return TC_OK;
        }
        if (lookup(named, TC_ARRAY_LEN(named), v->text, &argb) ||
            lookup(system_colors, TC_ARRAY_LEN(system_colors), v->text, &argb)) {
            set_argb(out, argb);
            return TC_OK;
        }
        return TC_ERR_INVALID;
    }
    if (v->type == TC_CSS_HASH)
        return parse_hex(v->text, out);
    if (v->type != TC_CSS_FUNCTION)
        return TC_ERR_INVALID;
    name = v->text;
    if (tc_css_ascii_ieq(name, "color"))
        return parse_color_function(v->items, v->nitems, out);
    if (tc_css_ascii_ieq(name, "light-dark")) {
        /* Both must be colours; Tachyon is always in light mode. */
        tc_css_color dark;
        for (i = 0; i < v->nitems && v->items[i].type != TC_CSS_COMMA; i++)
            ;
        if (i == v->nitems || tc_css_parse_color(v->items, i, out) != TC_OK ||
            tc_css_parse_color(v->items + i + 1, v->nitems - i - 1, &dark) != TC_OK)
            return TC_ERR_INVALID;
        return TC_OK;
    }
    if (split_args(v->items, v->nitems, &a) != TC_OK)
        return TC_ERR_INVALID;
    if (tc_css_ascii_ieq(name, "rgb") || tc_css_ascii_ieq(name, "rgba"))
        return parse_rgb(&a, out);
    if (tc_css_ascii_ieq(name, "hsl") || tc_css_ascii_ieq(name, "hsla"))
        return parse_hsl_hwb(&a, 0, out);
    if (tc_css_ascii_ieq(name, "hwb"))
        return parse_hsl_hwb(&a, 1, out);
    if (tc_css_ascii_ieq(name, "lab"))
        return parse_lab_like(&a, TC_COLOR_LAB, out);
    if (tc_css_ascii_ieq(name, "lch"))
        return parse_lab_like(&a, TC_COLOR_LCH, out);
    if (tc_css_ascii_ieq(name, "oklab"))
        return parse_lab_like(&a, TC_COLOR_OKLAB, out);
    if (tc_css_ascii_ieq(name, "oklch"))
        return parse_lab_like(&a, TC_COLOR_OKLCH, out);
    if (tc_css_ascii_ieq(name, "device-cmyk"))
        return parse_cmyk(&a, out);
    return TC_ERR_INVALID;
}

/* ---- Serializing ---- */

/* Six decimals at most, trailing zeros dropped. */
static void fmt_num(double v, char *out, size_t size)
{
    size_t n;
    snprintf(out, size, "%.6f", v + 0.0000001);
    n = strlen(out);
    if (strchr(out, '.')) {
        while (n && out[n - 1] == '0')
            out[--n] = '\0';
        if (n && out[n - 1] == '.')
            out[--n] = '\0';
    }
    if (strcmp(out, "-0") == 0)
        strcpy(out, "0");
}

static const char *space_name(int space)
{
    switch (space) {
    case TC_COLOR_SRGB: return "srgb";
    case TC_COLOR_SRGB_LINEAR: return "srgb-linear";
    case TC_COLOR_DISPLAY_P3: return "display-p3";
    case TC_COLOR_A98_RGB: return "a98-rgb";
    case TC_COLOR_PROPHOTO_RGB: return "prophoto-rgb";
    case TC_COLOR_REC2020: return "rec2020";
    case TC_COLOR_XYZ_D50: return "xyz-d50";
    case TC_COLOR_XYZ_D65: return "xyz-d65";
    case TC_COLOR_DEVICE_CMYK: return "device-cmyk";
    default: return "?";
    }
}

void tc_css_color_serialize(const tc_css_color *color, char *out, size_t size)
{
    char num[4][40], alpha[40];
    int i, n = color->space == TC_COLOR_DEVICE_CMYK ? 4 : 3;
    const char *fn;

    for (i = 0; i < n; i++) {
        if (color->none & (1u << i))
            strcpy(num[i], "none");
        else
            fmt_num(color->c[i], num[i], sizeof num[i]);
    }
    if (color->none & 8u)
        strcpy(alpha, "none");
    else
        fmt_num(color->alpha, alpha, sizeof alpha);

    switch (color->space) {
    case TC_COLOR_CURRENT:
        snprintf(out, size, "currentcolor");
        return;
    case TC_COLOR_RGB:
        if (color->alpha >= 1)
            snprintf(out, size, "rgb(%s, %s, %s)", num[0], num[1], num[2]);
        else
            snprintf(out, size, "rgba(%s, %s, %s, %s)", num[0], num[1], num[2], alpha);
        return;
    case TC_COLOR_LAB: fn = "lab"; break;
    case TC_COLOR_LCH: fn = "lch"; break;
    case TC_COLOR_OKLAB: fn = "oklab"; break;
    case TC_COLOR_OKLCH: fn = "oklch"; break;
    default: fn = NULL; break;
    }
    if (fn) {
        if (color->alpha >= 1 && !(color->none & 8u))
            snprintf(out, size, "%s(%s %s %s)", fn, num[0], num[1], num[2]);
        else
            snprintf(out, size, "%s(%s %s %s / %s)", fn, num[0], num[1], num[2], alpha);
    } else if (n == 4) {
        if (color->alpha >= 1 && !(color->none & 8u))
            snprintf(out, size, "color(%s %s %s %s %s)", space_name(color->space), num[0], num[1], num[2], num[3]);
        else
            snprintf(out, size, "color(%s %s %s %s %s / %s)", space_name(color->space), num[0], num[1], num[2],
                     num[3], alpha);
    } else {
        if (color->alpha >= 1 && !(color->none & 8u))
            snprintf(out, size, "color(%s %s %s %s)", space_name(color->space), num[0], num[1], num[2]);
        else
            snprintf(out, size, "color(%s %s %s %s / %s)", space_name(color->space), num[0], num[1], num[2], alpha);
    }
}

/* ---- Conversion to sRGB (CSS Color 4's sample code) ---- */

static void mul3(const double m[3][3], const double in[3], double out[3])
{
    int i;
    for (i = 0; i < 3; i++)
        out[i] = m[i][0] * in[0] + m[i][1] * in[1] + m[i][2] * in[2];
}

static double signed_pow(double x, double p)
{
    return x < 0 ? -pow(-x, p) : pow(x, p);
}

static double srgb_to_linear(double x)
{
    double a = fabs(x);
    return a <= 0.04045 ? x / 12.92 : signed_pow((a + 0.055) / 1.055, 2.4) * (x < 0 ? -1 : 1);
}

static double linear_to_srgb(double x)
{
    double a = fabs(x);
    return a <= 0.0031308 ? x * 12.92 : (1.055 * pow(a, 1 / 2.4) - 0.055) * (x < 0 ? -1 : 1);
}

static const double xyz65_to_lin_srgb[3][3] = {
    { 3.2409699419045226, -1.537383177570094, -0.4986107602930034 },
    { -0.9692436362808796, 1.8759675015077202, 0.04155505740717559 },
    { 0.05563007969699366, -0.20397695888897652, 1.0569715142428786 },
};
static const double d50_to_d65[3][3] = {
    { 0.955473421488075, -0.02309845494876471, 0.06325924320057072 },
    { -0.0283697093338637, 1.0099953980813041, 0.021041441191917323 },
    { 0.012314014864481998, -0.020507649298898964, 1.330365926242124 },
};
static const double lin_p3_to_xyz65[3][3] = {
    { 0.4865709486482162, 0.26566769316909306, 0.1982172852343625 },
    { 0.2289745640697488, 0.6917385218365064, 0.079286914093745 },
    { 0.0, 0.04511338185890264, 1.043944368900976 },
};
static const double lin_a98_to_xyz65[3][3] = {
    { 0.5766690429101305, 0.1855582379065463, 0.1882286462349947 },
    { 0.29734497525053605, 0.6273635662554661, 0.07529145849399788 },
    { 0.02703136138641234, 0.07068885253582723, 0.9913375368376388 },
};
static const double lin_prophoto_to_xyz50[3][3] = {
    { 0.7977666449006423, 0.13518129740053308, 0.0313477341283922 },
    { 0.2880748288194013, 0.711835234241873, 0.00008993693872564 },
    { 0.0, 0.0, 0.8251046025104602 },
};
static const double lin_2020_to_xyz65[3][3] = {
    { 0.6369580483012914, 0.14461690358620832, 0.1688809751641721 },
    { 0.2627002120112671, 0.6779980715188708, 0.05930171646986196 },
    { 0.0, 0.028072693049087428, 1.060985057710791 },
};
static const double oklab_to_lms[3][3] = {
    { 1.0, 0.3963377773761749, 0.2158037573099136 },
    { 1.0, -0.1055613458156586, -0.0638541728258133 },
    { 1.0, -0.0894841775298119, -1.2914855480194092 },
};
static const double lms_to_lin_srgb[3][3] = {
    { 4.0767416360759583, -3.3077115392580629, 0.2309699031821043 },
    { -1.2684379732850315, 2.6097573492876882, -0.3413193760025573 },
    { -0.0041960761386756, -0.7034186179359362, 1.7076146940746117 },
};

static void lab_to_xyz50(const double lab[3], double xyz[3])
{
    const double kappa = 24389.0 / 27.0, epsilon = 216.0 / 24389.0;
    const double white[3] = { 0.3457 / 0.3585, 1.0, (1.0 - 0.3457 - 0.3585) / 0.3585 };
    double f1 = (lab[0] + 16) / 116, f0 = lab[1] / 500 + f1, f2 = f1 - lab[2] / 200;
    xyz[0] = (f0 * f0 * f0 > epsilon ? f0 * f0 * f0 : (116 * f0 - 16) / kappa) * white[0];
    xyz[1] = (lab[0] > kappa * epsilon ? f1 * f1 * f1 : lab[0] / kappa) * white[1];
    xyz[2] = (f2 * f2 * f2 > epsilon ? f2 * f2 * f2 : (116 * f2 - 16) / kappa) * white[2];
}

/* Linear sRGB from any space. */
static void to_linear_srgb(const tc_css_color *c, double lin[3])
{
    double v[3], t[3], u[3];
    int i;

    for (i = 0; i < 3; i++)
        v[i] = (c->none & (1u << i)) ? 0 : c->c[i];
    switch (c->space) {
    case TC_COLOR_RGB:
        for (i = 0; i < 3; i++)
            lin[i] = srgb_to_linear(v[i] / 255.0);
        return;
    case TC_COLOR_SRGB:
        for (i = 0; i < 3; i++)
            lin[i] = srgb_to_linear(v[i]);
        return;
    case TC_COLOR_SRGB_LINEAR:
        memcpy(lin, v, sizeof v);
        return;
    case TC_COLOR_DISPLAY_P3:
        for (i = 0; i < 3; i++)
            t[i] = srgb_to_linear(v[i]);
        mul3(lin_p3_to_xyz65, t, u);
        mul3(xyz65_to_lin_srgb, u, lin);
        return;
    case TC_COLOR_A98_RGB:
        for (i = 0; i < 3; i++)
            t[i] = signed_pow(v[i], 563.0 / 256.0);
        mul3(lin_a98_to_xyz65, t, u);
        mul3(xyz65_to_lin_srgb, u, lin);
        return;
    case TC_COLOR_PROPHOTO_RGB:
        for (i = 0; i < 3; i++)
            t[i] = fabs(v[i]) <= 16.0 / 512.0 ? v[i] / 16.0 : signed_pow(v[i], 1.8);
        mul3(lin_prophoto_to_xyz50, t, u);
        mul3(d50_to_d65, u, t);
        mul3(xyz65_to_lin_srgb, t, lin);
        return;
    case TC_COLOR_REC2020: {
        const double alpha = 1.09929682680944, beta = 0.018053968510807;
        for (i = 0; i < 3; i++)
            t[i] = fabs(v[i]) < beta * 4.5 ? v[i] / 4.5 : signed_pow((fabs(v[i]) + alpha - 1) / alpha, 1 / 0.45) *
                                                               (v[i] < 0 ? -1 : 1);
        mul3(lin_2020_to_xyz65, t, u);
        mul3(xyz65_to_lin_srgb, u, lin);
        return;
    }
    case TC_COLOR_XYZ_D50:
        mul3(d50_to_d65, v, t);
        mul3(xyz65_to_lin_srgb, t, lin);
        return;
    case TC_COLOR_XYZ_D65:
        mul3(xyz65_to_lin_srgb, v, lin);
        return;
    case TC_COLOR_LCH:
    case TC_COLOR_OKLCH: {
        double h = v[2] * 3.14159265358979323846 / 180.0, chroma = v[1] < 0 ? 0 : v[1];
        v[1] = chroma * cos(h);
        v[2] = chroma * sin(h);
        if (c->space == TC_COLOR_LCH)
            goto lab;
        goto oklab;
    }
    case TC_COLOR_LAB:
    lab:
        lab_to_xyz50(v, t);
        mul3(d50_to_d65, t, u);
        mul3(xyz65_to_lin_srgb, u, lin);
        return;
    case TC_COLOR_OKLAB:
    oklab:
        mul3(oklab_to_lms, v, t);
        for (i = 0; i < 3; i++)
            t[i] = t[i] * t[i] * t[i];
        mul3(lms_to_lin_srgb, t, lin);
        return;
    case TC_COLOR_DEVICE_CMYK: {
        double k = (c->none & 8u) ? 0 : c->c[3];
        for (i = 0; i < 3; i++)
            lin[i] = srgb_to_linear((1 - v[i]) * (1 - k));
        return;
    }
    default:
        lin[0] = lin[1] = lin[2] = 0;
        return;
    }
}

uint32_t tc_css_color_argb(const tc_css_color *color)
{
    double lin[3];
    uint32_t argb, part;
    int i;

    if (color->space == TC_COLOR_CURRENT)
        return 0xFF000000u;
    part = (uint32_t)(color->alpha * 255.0 + 0.5);
    argb = (part > 255 ? 255 : part) << 24;
    if (color->space == TC_COLOR_RGB) {
        for (i = 0; i < 3; i++) {
            double s = (color->none & (1u << i)) ? 0 : color->c[i];
            s = s < 0 ? 0 : s > 255 ? 255 : s;
            argb |= (uint32_t)(s + 0.5) << (16 - 8 * i);
        }
        return argb;
    }
    to_linear_srgb(color, lin);
    for (i = 0; i < 3; i++) {
        double s = linear_to_srgb(lin[i]);
        s = s < 0 ? 0 : s > 1 ? 1 : s;
        argb |= (uint32_t)(s * 255.0 + 0.5) << (16 - 8 * i);
    }
    return argb;
}
