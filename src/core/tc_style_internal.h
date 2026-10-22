/* The style system's insides, shared by tc_style_props.c (properties and
 * their values), tc_style_sheet.c (style sheets) and tc_style.c (the
 * cascade and computed values). */
#ifndef TC_STYLE_INTERNAL_H
#define TC_STYLE_INTERNAL_H

#include "tc_style.h"

/* The longhand properties Tachyon computes: id, name, inherited, how its
   value is parsed and the keywords it takes. Shorthands, logical properties
   and aliases map onto these. */
#define TC_PROPS(X)                                                    \
    X(DISPLAY, "display", 0, T_DISPLAY, NULL)                          \
    X(POSITION, "position", 0, T_KEYWORD, kw_position)                 \
    X(FLOAT, "float", 0, T_KEYWORD, kw_float)                          \
    X(CLEAR, "clear", 0, T_KEYWORD, kw_clear)                          \
    X(BOX_SIZING, "box-sizing", 0, T_KEYWORD, kw_box_sizing)           \
    X(OVERFLOW_X, "overflow-x", 0, T_KEYWORD, kw_overflow)             \
    X(OVERFLOW_Y, "overflow-y", 0, T_KEYWORD, kw_overflow)             \
    X(MARGIN_TOP, "margin-top", 0, T_MARGIN, NULL)                     \
    X(MARGIN_RIGHT, "margin-right", 0, T_MARGIN, NULL)                 \
    X(MARGIN_BOTTOM, "margin-bottom", 0, T_MARGIN, NULL)               \
    X(MARGIN_LEFT, "margin-left", 0, T_MARGIN, NULL)                   \
    X(PADDING_TOP, "padding-top", 0, T_PADDING, NULL)                  \
    X(PADDING_RIGHT, "padding-right", 0, T_PADDING, NULL)              \
    X(PADDING_BOTTOM, "padding-bottom", 0, T_PADDING, NULL)            \
    X(PADDING_LEFT, "padding-left", 0, T_PADDING, NULL)                \
    X(BORDER_TOP_WIDTH, "border-top-width", 0, T_BORDER_WIDTH, NULL)   \
    X(BORDER_RIGHT_WIDTH, "border-right-width", 0, T_BORDER_WIDTH, NULL) \
    X(BORDER_BOTTOM_WIDTH, "border-bottom-width", 0, T_BORDER_WIDTH, NULL) \
    X(BORDER_LEFT_WIDTH, "border-left-width", 0, T_BORDER_WIDTH, NULL) \
    X(BORDER_TOP_STYLE, "border-top-style", 0, T_KEYWORD, kw_border_style) \
    X(BORDER_RIGHT_STYLE, "border-right-style", 0, T_KEYWORD, kw_border_style) \
    X(BORDER_BOTTOM_STYLE, "border-bottom-style", 0, T_KEYWORD, kw_border_style) \
    X(BORDER_LEFT_STYLE, "border-left-style", 0, T_KEYWORD, kw_border_style) \
    X(BORDER_TOP_COLOR, "border-top-color", 0, T_COLOR, NULL)          \
    X(BORDER_RIGHT_COLOR, "border-right-color", 0, T_COLOR, NULL)      \
    X(BORDER_BOTTOM_COLOR, "border-bottom-color", 0, T_COLOR, NULL)    \
    X(BORDER_LEFT_COLOR, "border-left-color", 0, T_COLOR, NULL)        \
    X(BACKGROUND_COLOR, "background-color", 0, T_COLOR, NULL)          \
    X(BACKGROUND_IMAGE, "background-image", 0, T_BG_IMAGE, NULL)       \
    X(BACKGROUND_REPEAT, "background-repeat", 0, T_BG_REPEAT, NULL)    \
    X(BACKGROUND_POSITION, "background-position", 0, T_BG_POSITION, NULL) \
    X(BACKGROUND_SIZE, "background-size", 0, T_BG_SIZE, NULL)          \
    X(BACKGROUND_ORIGIN, "background-origin", 0, T_BG_BOX, NULL)       \
    X(BACKGROUND_CLIP, "background-clip", 0, T_BG_BOX, NULL)           \
    X(OBJECT_FIT, "object-fit", 0, T_KEYWORD, kw_object_fit)           \
    X(OBJECT_POSITION, "object-position", 0, T_POSITION, NULL)         \
    X(WIDTH, "width", 0, T_SIZE, NULL)                                 \
    X(HEIGHT, "height", 0, T_SIZE, NULL)                               \
    X(MIN_WIDTH, "min-width", 0, T_SIZE, NULL)                         \
    X(MIN_HEIGHT, "min-height", 0, T_SIZE, NULL)                       \
    X(MAX_WIDTH, "max-width", 0, T_MAX_SIZE, NULL)                     \
    X(MAX_HEIGHT, "max-height", 0, T_MAX_SIZE, NULL)                   \
    X(TOP, "top", 0, T_INSET, NULL)                                    \
    X(RIGHT, "right", 0, T_INSET, NULL)                                \
    X(BOTTOM, "bottom", 0, T_INSET, NULL)                              \
    X(LEFT, "left", 0, T_INSET, NULL)                                  \
    X(VERTICAL_ALIGN, "vertical-align", 0, T_VALIGN, kw_valign)        \
    X(TEXT_DECORATION_LINE, "text-decoration-line", 0, T_DECORATION_LINE, NULL) \
    X(TEXT_DECORATION_STYLE, "text-decoration-style", 0, T_KEYWORD, kw_decoration_style) \
    X(TEXT_DECORATION_COLOR, "text-decoration-color", 0, T_COLOR, NULL) \
    X(CONTENT, "content", 0, T_CONTENT, NULL)                          \
    X(Z_INDEX, "z-index", 0, T_Z_INDEX, NULL)                          \
    X(COLOR, "color", 1, T_COLOR, NULL)                                \
    X(FONT_FAMILY, "font-family", 1, T_FONT_FAMILY, NULL)              \
    X(FONT_SIZE, "font-size", 1, T_FONT_SIZE, kw_font_size)            \
    X(FONT_WEIGHT, "font-weight", 1, T_FONT_WEIGHT, NULL)              \
    X(FONT_STYLE, "font-style", 1, T_FONT_STYLE, kw_font_style)        \
    X(LINE_HEIGHT, "line-height", 1, T_LINE_HEIGHT, NULL)              \
    X(TEXT_ALIGN, "text-align", 1, T_KEYWORD, kw_text_align)           \
    X(TEXT_TRANSFORM, "text-transform", 1, T_KEYWORD, kw_text_transform) \
    X(WHITE_SPACE_COLLAPSE, "white-space-collapse", 1, T_KEYWORD, kw_collapse) \
    X(TEXT_WRAP_MODE, "text-wrap-mode", 1, T_KEYWORD, kw_wrap_mode)    \
    X(VISIBILITY, "visibility", 1, T_KEYWORD, kw_visibility)           \
    X(LIST_STYLE_TYPE, "list-style-type", 1, T_LIST_STYLE_TYPE, kw_list_style) \
    X(LIST_STYLE_POSITION, "list-style-position", 1, T_KEYWORD, kw_list_position) \
    X(LIST_STYLE_IMAGE, "list-style-image", 1, T_IMAGE, NULL)          \
    X(OVERFLOW_WRAP, "overflow-wrap", 1, T_KEYWORD, kw_overflow_wrap)  \
    X(WORD_BREAK, "word-break", 1, T_KEYWORD, kw_word_break)           \
    X(CURSOR, "cursor", 1, T_CURSOR, kw_cursor)                        \
    X(TEXT_INDENT, "text-indent", 1, T_TEXT_INDENT, NULL)              \
    X(LETTER_SPACING, "letter-spacing", 1, T_SPACING, NULL)            \
    X(WORD_SPACING, "word-spacing", 1, T_SPACING, NULL)

#define TC_PROP_ENUM(id, name, inherited, type, keywords) P_##id,
enum { TC_PROPS(TC_PROP_ENUM) P_COUNT };
#undef TC_PROP_ENUM

/* Shorthands, numbered after the longhands. */
enum {
    SH_MARGIN = P_COUNT, SH_PADDING, SH_INSET, SH_BORDER, SH_BORDER_TOP, SH_BORDER_RIGHT, SH_BORDER_BOTTOM,
    SH_BORDER_LEFT, SH_BORDER_WIDTH, SH_BORDER_STYLE, SH_BORDER_COLOR, SH_BORDER_BLOCK, SH_BORDER_INLINE,
    SH_BORDER_BLOCK_WIDTH, SH_BORDER_BLOCK_STYLE, SH_BORDER_BLOCK_COLOR, SH_BORDER_INLINE_WIDTH,
    SH_BORDER_INLINE_STYLE, SH_BORDER_INLINE_COLOR, SH_MARGIN_BLOCK, SH_MARGIN_INLINE, SH_PADDING_BLOCK,
    SH_PADDING_INLINE, SH_INSET_BLOCK, SH_INSET_INLINE, SH_OVERFLOW, SH_BACKGROUND, SH_FONT, SH_LIST_STYLE,
    SH_TEXT_DECORATION, SH_WHITE_SPACE, SH_TEXT_WRAP, SH_ALL,
    SH_END
};

#define P_CUSTOM 0xFFFF   /* a custom property (--name) */

/* How a declared value is held. */
enum {
    PV_KEYWORD,      /* keyword: the property's enum value */
    PV_LENGTH,       /* ptr: a tc_calc (lengths, percentages, numbers) */
    PV_NUMBER,       /* n.f */
    PV_INT,          /* n.i */
    PV_COLOR,        /* n.u: 0xAARRGGBB */
    PV_CURRENTCOLOR,
    PV_PTR,          /* ptr: font families, content, a list-style string */
    PV_WIDE,         /* keyword: a CSS-wide keyword (WIDE_*) */
    PV_VAR           /* ptr: a tc_var_value, substituted when computed */
};

enum { WIDE_INITIAL, WIDE_INHERIT, WIDE_UNSET, WIDE_REVERT, WIDE_REVERT_LAYER };

/* font-size keywords: the absolute sizes, then the relative ones. */
enum { FS_XX_SMALL, FS_X_SMALL, FS_SMALL, FS_MEDIUM, FS_LARGE, FS_X_LARGE, FS_XX_LARGE, FS_XXX_LARGE, FS_LARGER,
       FS_SMALLER };
/* font-weight keywords (numbers are PV_NUMBER). */
enum { FW_BOLDER = 1, FW_LIGHTER };
/* content keywords (lists are PV_PTR). */
enum { CONTENT_NORMAL, CONTENT_NONE };

typedef struct tc_pval {
    uint8_t kind;      /* PV_* */
    uint8_t keyword;
    union {
        float f;
        int32_t i;
        uint32_t u;
    } n;
    const void *ptr;
} tc_pval;

/* A position (background-position, object-position) as declared: offsets
   from the left or right, and from the top or bottom. */
typedef struct tc_position {
    uint8_t far_x, far_y;      /* measured from the right, from the bottom */
    const tc_calc *x, *y;      /* the offsets; NULL for 0 */
} tc_position;

/* background-size for one layer, as declared. */
typedef struct tc_bg_size_decl {
    uint8_t kind;              /* TC_BG_SIZE, TC_BG_COVER, TC_BG_CONTAIN */
    const tc_calc *w, *h;      /* TC_BG_SIZE: NULL for auto */
} tc_bg_size_decl;

/* A background longhand as declared: one entry per layer (comma), in the
   member its property uses. */
typedef struct tc_bg_list {
    uint32_t count;
    const tc_css_image **images;  /* NULL entries: none */
    uint8_t (*repeat)[2];
    uint8_t *boxes;               /* origin, clip */
    tc_position *positions;
    tc_bg_size_decl *sizes;
} tc_bg_list;

typedef struct tc_tokens {
    const tc_css_value *v;
    uint32_t n;
} tc_tokens;

/* A value with var() in it: the tokens, and the property (a longhand or a
   shorthand) they are a value of. */
typedef struct tc_var_value {
    tc_tokens tokens;
    uint16_t prop;
    const char *base;  /* the style sheet's URL, for url()s (NULL: the document's) */
} tc_var_value;

typedef struct tc_decl {
    uint16_t prop;     /* P_*, or P_CUSTOM */
    uint8_t important;
    uint8_t has_var;   /* custom properties: the value uses var() */
    const char *name;  /* custom properties: the name, with its "--" */
    tc_pval value;     /* custom properties: ptr is a tc_tokens */
} tc_decl;

typedef struct tc_decl_vec {
    tc_decl *items;
    uint32_t len, cap;
} tc_decl_vec;

/* Parse one declaration into longhand declarations appended to out (none
   if it's invalid or unknown). quirks: unitless lengths are pixels. base:
   the style sheet's URL, which its url()s are relative to (NULL: the
   document's). Values live in a. TC_OK or TC_ERR_NOMEM. */
int tc_decls_add(tc_arena *a, const tc_css_decl *d, int quirks, const char *base, tc_decl_vec *out);
void tc_decls_free(tc_decl_vec *v);

/* Parse a value (var() substituted) of prop, a longhand or a shorthand,
   into its longhands' values: returns how many, or 0 if it's invalid. */
uint32_t tc_prop_parse(tc_arena *a, uint16_t prop, const tc_css_value *v, uint32_t n, int quirks, const char *base,
                       uint16_t *props, tc_pval *vals);

/* Whether a declaration of name: value would be accepted (for @supports). */
int tc_prop_supported(tc_arena *a, const char *name, const tc_css_value *v, uint32_t n);

int tc_prop_inherited(uint16_t prop);
const char *tc_prop_name(uint16_t prop);
/* For the unit tests: whether the name table is sorted. */
int tc_prop_names_sorted(void);

/* Whether the tokens contain var() or env(), at any depth. */
int tc_tokens_have_var(const tc_css_value *v, uint32_t n);
/* A deep copy of tokens into a. NULL only when out of memory. */
tc_css_value *tc_tokens_copy(tc_arena *a, const tc_css_value *v, uint32_t n);

/* ---- Style sheets ---- */

enum { RULE_STYLE, RULE_LAYER, RULE_IMPORT };

/* The @media rules a rule is inside, innermost first. */
typedef struct tc_cond tc_cond;
struct tc_cond {
    const tc_media_list *media;
    const tc_cond *parent;
};

typedef struct tc_rule {
    uint8_t kind;               /* RULE_* */
    int32_t layer;              /* index into the sheet's layers, or -1 */
    const tc_cond *cond;
    tc_selector_list selectors; /* RULE_STYLE */
    tc_decl *decls;
    uint32_t ndecls;
    uint32_t import;            /* RULE_IMPORT: index into the sheet's imports */
} tc_rule;

typedef struct tc_import {
    const char *url;
    tc_stylesheet *sheet;       /* NULL until fetched */
} tc_import;

struct tc_stylesheet {
    tc_arena arena;
    int origin;
    const char *url;
    tc_rule *rules;             /* in order (malloc'd) */
    uint32_t nrules, rules_cap;
    tc_import *imports;         /* malloc'd */
    uint32_t nimports, imports_cap;
    const char **layers;        /* layer names, dotted, in order of first mention (malloc'd) */
    uint32_t nlayers, layers_cap;
    size_t style_rules;
};

/* Parse a style attribute's value into declarations in a. */
int tc_style_attr_parse(tc_arena *a, const char *text, int quirks, tc_decl **out, uint32_t *n);

/* The user-agent style sheets (parsed on first use). */
const tc_stylesheet *tc_ua_sheet(int quirks_extras);

extern const char tc_ua_css[];
extern const char tc_ua_quirks_css[];

#endif
