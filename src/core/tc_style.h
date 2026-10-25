/* Styles: style sheets, the cascade (CSS Cascade 5) and computed values,
 * for the properties Tachyon lays out and paints.
 *
 * A style sheet is parsed once into rules of typed declarations
 * (shorthands expanded, logical properties mapped to physical ones for
 * horizontal left-to-right text). Styling a document matches every element
 * against the user-agent sheet, the page's sheets, presentational
 * attributes and style attributes, and computes a tc_style for it and for
 * its ::before, ::after and ::marker. Custom properties and var() are
 * substituted when values are computed. */
#ifndef TC_STYLE_H
#define TC_STYLE_H

#include "tc_css_values.h"
#include "tc_dom.h"
#include "tc_selector.h"

/* ---- Computed values ---- */

typedef enum tc_display {
    TC_DISPLAY_NONE,
    TC_DISPLAY_CONTENTS,
    TC_DISPLAY_INLINE,
    TC_DISPLAY_BLOCK,
    TC_DISPLAY_INLINE_BLOCK,
    TC_DISPLAY_LIST_ITEM,
    TC_DISPLAY_FLOW_ROOT,
    TC_DISPLAY_FLEX,
    TC_DISPLAY_INLINE_FLEX,
    TC_DISPLAY_GRID,
    TC_DISPLAY_INLINE_GRID,
    TC_DISPLAY_TABLE,
    TC_DISPLAY_INLINE_TABLE,
    TC_DISPLAY_TABLE_ROW_GROUP,
    TC_DISPLAY_TABLE_HEADER_GROUP,
    TC_DISPLAY_TABLE_FOOTER_GROUP,
    TC_DISPLAY_TABLE_ROW,
    TC_DISPLAY_TABLE_CELL,
    TC_DISPLAY_TABLE_COLUMN_GROUP,
    TC_DISPLAY_TABLE_COLUMN,
    TC_DISPLAY_TABLE_CAPTION,
    TC_DISPLAY_RUBY,
    TC_DISPLAY_RUBY_TEXT
} tc_display;

enum { TC_POSITION_STATIC, TC_POSITION_RELATIVE, TC_POSITION_ABSOLUTE, TC_POSITION_FIXED, TC_POSITION_STICKY };
enum { TC_FLOAT_NONE, TC_FLOAT_LEFT, TC_FLOAT_RIGHT };
enum { TC_CLEAR_NONE, TC_CLEAR_LEFT, TC_CLEAR_RIGHT, TC_CLEAR_BOTH };
enum { TC_VISIBILITY_VISIBLE, TC_VISIBILITY_HIDDEN, TC_VISIBILITY_COLLAPSE };
enum { TC_BOX_CONTENT, TC_BOX_BORDER };
enum {
    TC_BORDER_NONE, TC_BORDER_HIDDEN, TC_BORDER_DOTTED, TC_BORDER_DASHED, TC_BORDER_SOLID, TC_BORDER_DOUBLE,
    TC_BORDER_GROOVE, TC_BORDER_RIDGE, TC_BORDER_INSET, TC_BORDER_OUTSET
};
enum { TC_OVERFLOW_VISIBLE, TC_OVERFLOW_HIDDEN, TC_OVERFLOW_CLIP, TC_OVERFLOW_SCROLL, TC_OVERFLOW_AUTO };
enum { TC_FONT_NORMAL, TC_FONT_ITALIC, TC_FONT_OBLIQUE };
enum { TC_ALIGN_LEFT, TC_ALIGN_RIGHT, TC_ALIGN_CENTER, TC_ALIGN_JUSTIFY };
enum { TC_TRANSFORM_NONE, TC_TRANSFORM_UPPERCASE, TC_TRANSFORM_LOWERCASE, TC_TRANSFORM_CAPITALIZE };
/* white-space is a shorthand for these two (CSS Text 4). */
enum { TC_COLLAPSE_COLLAPSE, TC_COLLAPSE_PRESERVE, TC_COLLAPSE_PRESERVE_BREAKS, TC_COLLAPSE_BREAK_SPACES };
enum { TC_WRAP_MODE_WRAP, TC_WRAP_MODE_NOWRAP };
enum {
    TC_VALIGN_BASELINE, TC_VALIGN_SUB, TC_VALIGN_SUPER, TC_VALIGN_TOP, TC_VALIGN_TEXT_TOP, TC_VALIGN_MIDDLE,
    TC_VALIGN_BOTTOM, TC_VALIGN_TEXT_BOTTOM, TC_VALIGN_LENGTH
};
enum { TC_WRAP_NORMAL, TC_WRAP_BREAK_WORD, TC_WRAP_ANYWHERE };
enum { TC_BREAK_NORMAL, TC_BREAK_ALL, TC_BREAK_KEEP_ALL, TC_BREAK_WORD };
enum {
    TC_LIST_NONE, TC_LIST_DISC, TC_LIST_CIRCLE, TC_LIST_SQUARE, TC_LIST_DECIMAL, TC_LIST_DECIMAL_LEADING_ZERO,
    TC_LIST_LOWER_ALPHA, TC_LIST_UPPER_ALPHA, TC_LIST_LOWER_ROMAN, TC_LIST_UPPER_ROMAN, TC_LIST_LOWER_GREEK,
    TC_LIST_DISCLOSURE_OPEN, TC_LIST_DISCLOSURE_CLOSED, TC_LIST_STRING
};
enum { TC_LIST_OUTSIDE, TC_LIST_INSIDE };
enum { TC_DECORATION_SOLID, TC_DECORATION_DOUBLE, TC_DECORATION_DOTTED, TC_DECORATION_DASHED, TC_DECORATION_WAVY };
/* The cursors Windows 95 has. */
enum {
    TC_CURSOR_AUTO, TC_CURSOR_DEFAULT, TC_CURSOR_NONE, TC_CURSOR_POINTER, TC_CURSOR_TEXT, TC_CURSOR_WAIT,
    TC_CURSOR_PROGRESS, TC_CURSOR_HELP, TC_CURSOR_CROSSHAIR, TC_CURSOR_MOVE, TC_CURSOR_NOT_ALLOWED,
    TC_CURSOR_EW_RESIZE, TC_CURSOR_NS_RESIZE, TC_CURSOR_NESW_RESIZE, TC_CURSOR_NWSE_RESIZE
};
enum { TC_LINE_HEIGHT_NORMAL, TC_LINE_HEIGHT_NUMBER, TC_LINE_HEIGHT_PX };

#define TC_DECORATION_UNDERLINE    1u
#define TC_DECORATION_OVERLINE     2u
#define TC_DECORATION_LINE_THROUGH 4u

/* A computed size: a length-percentage, auto, none (max-*), or a sizing
   keyword. */
enum {
    TC_SIZE_LENGTH, TC_SIZE_AUTO, TC_SIZE_NONE, TC_SIZE_MIN_CONTENT, TC_SIZE_MAX_CONTENT, TC_SIZE_FIT_CONTENT,
    TC_SIZE_STRETCH
};
typedef struct tc_size {
    tc_length length;
    uint8_t kind;
} tc_size;

/* font-family: names in order, generic families in lower case. */
typedef struct tc_font_families {
    const char **names;
    uint32_t count;
} tc_font_families;

/* Generated content (content: for ::before, ::after and ::marker). */
enum { TC_CONTENT_STRING, TC_CONTENT_ATTR, TC_CONTENT_OPEN_QUOTE, TC_CONTENT_CLOSE_QUOTE, TC_CONTENT_COUNTER };
typedef struct tc_content_item {
    uint8_t type;
    uint8_t list_style;   /* counter(): its style */
    const char *text;     /* the string, attribute name or counter name */
} tc_content_item;
typedef struct tc_content {
    tc_content_item *items;
    uint32_t count;
} tc_content;
enum { TC_GENERATED_NORMAL, TC_GENERATED_NONE, TC_GENERATED_ITEMS };

/* An image a style names: url() or a gradient. */
enum { TC_CSS_IMAGE_URL, TC_CSS_IMAGE_LINEAR };
typedef struct tc_color_stop {
    uint32_t color;           /* 0xAARRGGBB */
    uint8_t currentcolor;     /* as declared: currentcolor (resolved when computed) */
    uint8_t has_position;
    const tc_calc *declared;  /* as declared: the position */
    tc_length position;       /* computed: along the gradient line, percentages of its length */
} tc_color_stop;
typedef struct tc_css_image {
    uint8_t type;             /* TC_CSS_IMAGE_* */
    uint8_t repeating;        /* repeating-linear-gradient() */
    uint8_t corner;           /* "to top right" and the like: TC_CORNER_* bits; 0 for an angle */
    const char *url;          /* TC_CSS_IMAGE_URL: as written */
    const char *base;         /* the style sheet's URL it's relative to; NULL for the document's */
    float angle;              /* gradients: degrees clockwise from upwards (180: downwards) */
    uint32_t nstops;
    const tc_color_stop *stops;
} tc_css_image;
#define TC_CORNER_TOP    1u
#define TC_CORNER_RIGHT  2u
#define TC_CORNER_BOTTOM 4u
#define TC_CORNER_LEFT   8u

/* Background layers: images with their repeat, position, size and boxes. */
enum { TC_BG_REPEAT, TC_BG_NO_REPEAT, TC_BG_SPACE, TC_BG_ROUND };
enum { TC_BG_BORDER_BOX, TC_BG_PADDING_BOX, TC_BG_CONTENT_BOX };
enum { TC_BG_SIZE, TC_BG_COVER, TC_BG_CONTAIN };
typedef struct tc_bg_layer {
    const tc_css_image *image;
    uint8_t repeat_x, repeat_y; /* TC_BG_REPEAT... */
    uint8_t origin, clip;       /* TC_BG_*_BOX */
    uint8_t size;               /* TC_BG_SIZE (width and height), TC_BG_COVER, TC_BG_CONTAIN */
    tc_size width, height;      /* auto or a length-percentage */
    tc_length x, y;             /* position: percentages of (area - image) */
} tc_bg_layer;
typedef struct tc_backgrounds {
    uint32_t count;
    const tc_bg_layer *layers;  /* the first is on top */
} tc_backgrounds;

enum { TC_FIT_FILL, TC_FIT_CONTAIN, TC_FIT_COVER, TC_FIT_NONE, TC_FIT_SCALE_DOWN };

typedef struct tc_custom_props tc_custom_props;

typedef struct tc_style {
    /* Not inherited. */
    uint8_t display, position, float_, clear, box_sizing, overflow_x, overflow_y;
    uint8_t border_style[4];      /* top, right, bottom, left */
    uint8_t vertical_align;
    uint8_t decoration_line;      /* this element's own text-decoration-line */
    uint8_t decoration_style;
    uint8_t z_auto;               /* z-index: auto */
    uint8_t currentcolor;         /* which colours are currentcolor: TC_CC_* bits */
    tc_size margin[4];            /* auto allowed */
    tc_length padding[4];
    float border_width[4];        /* px; 0 when the style is none or hidden */
    uint32_t border_color[4];     /* 0xAARRGGBB, currentcolor resolved */
    uint32_t background_color;
    tc_size width, height, min_width, min_height, max_width, max_height;
    tc_size inset[4];             /* top, right, bottom, left */
    tc_length vertical_align_length;
    uint32_t decoration_color;
    uint8_t content_kind;         /* TC_GENERATED_* */
    const tc_content *content;    /* TC_GENERATED_ITEMS: the items, attr() resolved */
    int z_index;
    const tc_backgrounds *backgrounds; /* background images; NULL for none */
    uint8_t object_fit;           /* TC_FIT_* */
    tc_length object_position[2];

    /* Inherited. */
    uint32_t color;
    const tc_font_families *font_family;
    float font_size;              /* px */
    uint16_t font_weight;         /* 1..1000 */
    uint8_t font_style;
    uint8_t line_height_kind;
    float line_height;            /* a number or px, per line_height_kind */
    uint8_t text_align, text_transform, ws_collapse, text_wrap, visibility, list_style_type, list_style_position;
    uint8_t overflow_wrap, word_break, cursor;
    /* font-size: 1 + the absolute-size keyword it comes from (times
       font_size_factor through em and %), so that monospace text can use a
       smaller medium size, as other browsers do; 0 when it's a length. */
    uint8_t font_size_keyword;
    float font_size_factor;
    tc_length text_indent;
    float letter_spacing, word_spacing; /* px */
    const char *list_style_string;      /* TC_LIST_STRING */
    const tc_css_image *list_style_image; /* NULL for none */

    /* Text decorations in force from this element and its ancestors. */
    uint8_t decorations;
    uint32_t decorations_color;

    const tc_custom_props *custom;
} tc_style;

/* tc_style.currentcolor bits (resolved already; kept for inheritance). */
#define TC_CC_BORDER_TOP    1u
#define TC_CC_BORDER_RIGHT  2u
#define TC_CC_BORDER_BOTTOM 4u
#define TC_CC_BORDER_LEFT   8u
#define TC_CC_BACKGROUND    16u
#define TC_CC_DECORATION    32u

/* The styles of an element and of its pseudo-elements (NULL when they
   don't generate boxes). */
typedef struct tc_element_styles {
    const tc_style *style;
    const tc_style *before, *after, *marker;
} tc_element_styles;

/* ---- Style sheets ---- */

enum { TC_ORIGIN_UA, TC_ORIGIN_AUTHOR };

typedef struct tc_stylesheet tc_stylesheet;

/* Parse a style sheet's text (already decoded). quirks: for a quirks-mode
   document (unitless lengths are pixels). url: where it came from, for
   resolving @import (may be NULL). NULL only when out of memory. */
tc_stylesheet *tc_stylesheet_parse(const char *text, size_t len, int origin, int quirks, const char *url);
void tc_stylesheet_free(tc_stylesheet *sheet);

/* The sheet's @import rules, for the loader to fetch: their URLs as
   written (resolve them against the sheet's URL). */
size_t tc_stylesheet_import_count(const tc_stylesheet *sheet);
const char *tc_stylesheet_import_url(const tc_stylesheet *sheet, size_t i);
const char *tc_stylesheet_url(const tc_stylesheet *sheet);
/* Attach a fetched import (the sheet takes ownership). */
void tc_stylesheet_set_import(tc_stylesheet *sheet, size_t i, tc_stylesheet *imported);
/* Statistics: style rules (one per selector list). */
size_t tc_stylesheet_rule_count(const tc_stylesheet *sheet);

/* Parse the user-agent style sheets now. Styling parses them on first use;
   call this once before styling from more than one thread. */
void tc_style_init(void);

/* ---- A document's style sources ---- */

/* <style> and <link rel=stylesheet> elements, in tree order. */
typedef struct tc_style_source {
    const tc_element *element;
    const char *href;           /* <link>: its href; NULL for <style> */
    const char *media;          /* the media attribute, or NULL */
    tc_stylesheet *sheet;       /* filled by the caller (fetched and parsed) */
} tc_style_source;

/* Find them. *out is malloc'd; free it with free(). <style> sheets are
   parsed here; <link> sheets are left for the caller to fetch. */
int tc_style_sources(const tc_document *doc, tc_style_source **out, size_t *count);
void tc_style_sources_free(tc_style_source *sources, size_t count);

/* ---- Styling a document ---- */

typedef struct tc_styled tc_styled;

/* Compute every element's style. env: the viewport, for media queries and
   viewport units. target_id: the URL's fragment, for :target (or NULL).
   The sources' sheets must outlive the result, which points into them. */
int tc_style_document(const tc_document *doc, const tc_style_source *sources, size_t count, const tc_media_env *env,
                      const char *target_id, tc_styled **out);
void tc_styled_free(tc_styled *s);

/* An element's computed styles; NULL for elements that don't take part
   (outside the document, or inside display: none). */
const tc_element_styles *tc_styled_element(const tc_styled *s, const tc_element *el);

/* The style of an anonymous box inside one with style parent: inherited
   values from parent, initial values for the rest, and display. */
void tc_style_anonymous(tc_style *out, const tc_style *parent, int display);
/* Every property's initial value. */
void tc_style_initial(tc_style *out);

/* ---- Web fonts ---- */

/* An @font-face rule (CSS Fonts 4): where a family's face comes from, and
   what it's matched by. */
typedef struct tc_font_face {
    const char *family;
    const char *const *urls;    /* src's url()s, in order: those whose format() Tachyon reads (local() isn't) */
    uint32_t nurls;
    const char *base;           /* the style sheet's URL they're relative to; NULL for the document's */
    uint16_t weight_min, weight_max; /* font-weight: a weight, or a variable font's range */
    uint8_t italic;             /* font-style: italic or oblique */
    const uint32_t *ranges;     /* unicode-range: first, last, first, last...; NULL for every character */
    uint32_t nranges;
} tc_font_face;

/* The @font-face rules in force for a viewport, in the order the sheets
   give them: pointers into the sheets, in *out (malloc'd; free it with
   free()). */
int tc_style_font_faces(const tc_style_source *sources, size_t count, const tc_media_env *env,
                        const tc_font_face ***out, size_t *n);

/* Statistics: elements styled, selectors in the cascade, selector tests
   and matches, distinct computed styles, bytes used. */
typedef struct tc_style_stats {
    size_t elements, selectors, tested, matched, styles, bytes;
} tc_style_stats;
void tc_styled_stats(const tc_styled *s, tc_style_stats *out);

#endif
