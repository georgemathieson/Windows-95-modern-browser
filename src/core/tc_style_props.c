/* Properties: their names, and parsing their values (shorthands included)
 * into the typed declarations the cascade works with. */
#include <stdlib.h>
#include <string.h>

#include "tc_css_color.h"
#include "tc_style_internal.h"

/* ---- Keywords ---- */

typedef struct kw {
    const char *name;
    uint8_t value;
} kw;

#define KW_INHERIT 0xFF /* text-align: match-parent, which is inherit for left-to-right text */

static const kw kw_position[] = {
    { "static", TC_POSITION_STATIC }, { "relative", TC_POSITION_RELATIVE }, { "absolute", TC_POSITION_ABSOLUTE },
    { "fixed", TC_POSITION_FIXED }, { "sticky", TC_POSITION_STICKY }, { "-webkit-sticky", TC_POSITION_STICKY },
    { NULL, 0 }
};
static const kw kw_float[] = {
    { "none", TC_FLOAT_NONE }, { "left", TC_FLOAT_LEFT }, { "right", TC_FLOAT_RIGHT },
    { "inline-start", TC_FLOAT_LEFT }, { "inline-end", TC_FLOAT_RIGHT }, { NULL, 0 }
};
static const kw kw_clear[] = {
    { "none", TC_CLEAR_NONE }, { "left", TC_CLEAR_LEFT }, { "right", TC_CLEAR_RIGHT }, { "both", TC_CLEAR_BOTH },
    { "inline-start", TC_CLEAR_LEFT }, { "inline-end", TC_CLEAR_RIGHT }, { NULL, 0 }
};
static const kw kw_box_sizing[] = { { "content-box", TC_BOX_CONTENT }, { "border-box", TC_BOX_BORDER }, { NULL, 0 } };
static const kw kw_overflow[] = {
    { "visible", TC_OVERFLOW_VISIBLE }, { "hidden", TC_OVERFLOW_HIDDEN }, { "clip", TC_OVERFLOW_CLIP },
    { "scroll", TC_OVERFLOW_SCROLL }, { "auto", TC_OVERFLOW_AUTO }, { "overlay", TC_OVERFLOW_AUTO }, { NULL, 0 }
};
static const kw kw_border_style[] = {
    { "none", TC_BORDER_NONE }, { "hidden", TC_BORDER_HIDDEN }, { "dotted", TC_BORDER_DOTTED },
    { "dashed", TC_BORDER_DASHED }, { "solid", TC_BORDER_SOLID }, { "double", TC_BORDER_DOUBLE },
    { "groove", TC_BORDER_GROOVE }, { "ridge", TC_BORDER_RIDGE }, { "inset", TC_BORDER_INSET },
    { "outset", TC_BORDER_OUTSET }, { NULL, 0 }
};
static const kw kw_valign[] = {
    { "baseline", TC_VALIGN_BASELINE }, { "sub", TC_VALIGN_SUB }, { "super", TC_VALIGN_SUPER },
    { "top", TC_VALIGN_TOP }, { "text-top", TC_VALIGN_TEXT_TOP }, { "middle", TC_VALIGN_MIDDLE },
    { "bottom", TC_VALIGN_BOTTOM }, { "text-bottom", TC_VALIGN_TEXT_BOTTOM }, { NULL, 0 }
};
static const kw kw_decoration_style[] = {
    { "solid", TC_DECORATION_SOLID }, { "double", TC_DECORATION_DOUBLE }, { "dotted", TC_DECORATION_DOTTED },
    { "dashed", TC_DECORATION_DASHED }, { "wavy", TC_DECORATION_WAVY }, { NULL, 0 }
};
static const kw kw_font_size[] = {
    { "xx-small", FS_XX_SMALL }, { "x-small", FS_X_SMALL }, { "small", FS_SMALL }, { "medium", FS_MEDIUM },
    { "large", FS_LARGE }, { "x-large", FS_X_LARGE }, { "xx-large", FS_XX_LARGE }, { "xxx-large", FS_XXX_LARGE },
    { "larger", FS_LARGER }, { "smaller", FS_SMALLER }, { "math", FS_MEDIUM }, { NULL, 0 }
};
static const kw kw_font_style[] = {
    { "normal", TC_FONT_NORMAL }, { "italic", TC_FONT_ITALIC }, { "oblique", TC_FONT_OBLIQUE }, { NULL, 0 }
};
static const kw kw_text_align[] = {
    { "start", TC_ALIGN_LEFT }, { "end", TC_ALIGN_RIGHT }, { "left", TC_ALIGN_LEFT }, { "right", TC_ALIGN_RIGHT },
    { "center", TC_ALIGN_CENTER }, { "justify", TC_ALIGN_JUSTIFY }, { "justify-all", TC_ALIGN_JUSTIFY },
    { "match-parent", KW_INHERIT }, { "-webkit-match-parent", KW_INHERIT }, { "-webkit-left", TC_ALIGN_LEFT },
    { "-webkit-right", TC_ALIGN_RIGHT }, { "-webkit-center", TC_ALIGN_CENTER }, { "-moz-left", TC_ALIGN_LEFT },
    { "-moz-right", TC_ALIGN_RIGHT }, { "-moz-center", TC_ALIGN_CENTER }, { NULL, 0 }
};
static const kw kw_text_transform[] = {
    { "none", TC_TRANSFORM_NONE }, { "capitalize", TC_TRANSFORM_CAPITALIZE },
    { "uppercase", TC_TRANSFORM_UPPERCASE }, { "lowercase", TC_TRANSFORM_LOWERCASE },
    { "full-width", TC_TRANSFORM_NONE }, { "full-size-kana", TC_TRANSFORM_NONE }, { "math-auto", TC_TRANSFORM_NONE },
    { NULL, 0 }
};
static const kw kw_collapse[] = {
    { "collapse", TC_COLLAPSE_COLLAPSE }, { "preserve", TC_COLLAPSE_PRESERVE },
    { "preserve-breaks", TC_COLLAPSE_PRESERVE_BREAKS }, { "break-spaces", TC_COLLAPSE_BREAK_SPACES },
    { "preserve-spaces", TC_COLLAPSE_PRESERVE }, { "discard", TC_COLLAPSE_COLLAPSE }, { NULL, 0 }
};
static const kw kw_wrap_mode[] = { { "wrap", TC_WRAP_MODE_WRAP }, { "nowrap", TC_WRAP_MODE_NOWRAP }, { NULL, 0 } };
static const kw kw_visibility[] = {
    { "visible", TC_VISIBILITY_VISIBLE }, { "hidden", TC_VISIBILITY_HIDDEN },
    { "collapse", TC_VISIBILITY_COLLAPSE }, { NULL, 0 }
};
static const kw kw_list_style[] = {
    { "none", TC_LIST_NONE }, { "disc", TC_LIST_DISC }, { "circle", TC_LIST_CIRCLE }, { "square", TC_LIST_SQUARE },
    { "decimal", TC_LIST_DECIMAL }, { "decimal-leading-zero", TC_LIST_DECIMAL_LEADING_ZERO },
    { "lower-alpha", TC_LIST_LOWER_ALPHA }, { "lower-latin", TC_LIST_LOWER_ALPHA },
    { "upper-alpha", TC_LIST_UPPER_ALPHA }, { "upper-latin", TC_LIST_UPPER_ALPHA },
    { "lower-roman", TC_LIST_LOWER_ROMAN }, { "upper-roman", TC_LIST_UPPER_ROMAN },
    { "lower-greek", TC_LIST_LOWER_GREEK }, { "disclosure-open", TC_LIST_DISCLOSURE_OPEN },
    { "disclosure-closed", TC_LIST_DISCLOSURE_CLOSED }, { NULL, 0 }
};
static const kw kw_list_position[] = { { "outside", TC_LIST_OUTSIDE }, { "inside", TC_LIST_INSIDE }, { NULL, 0 } };
static const kw kw_overflow_wrap[] = {
    { "normal", TC_WRAP_NORMAL }, { "break-word", TC_WRAP_BREAK_WORD }, { "anywhere", TC_WRAP_ANYWHERE }, { NULL, 0 }
};
static const kw kw_word_break[] = {
    { "normal", TC_BREAK_NORMAL }, { "break-all", TC_BREAK_ALL }, { "keep-all", TC_BREAK_KEEP_ALL },
    { "break-word", TC_BREAK_WORD }, { "auto-phrase", TC_BREAK_NORMAL }, { NULL, 0 }
};
static const kw kw_cursor[] = {
    { "auto", TC_CURSOR_AUTO }, { "default", TC_CURSOR_DEFAULT }, { "none", TC_CURSOR_NONE },
    { "pointer", TC_CURSOR_POINTER }, { "hand", TC_CURSOR_POINTER }, { "text", TC_CURSOR_TEXT },
    { "vertical-text", TC_CURSOR_TEXT }, { "wait", TC_CURSOR_WAIT }, { "progress", TC_CURSOR_PROGRESS },
    { "help", TC_CURSOR_HELP }, { "crosshair", TC_CURSOR_CROSSHAIR }, { "cell", TC_CURSOR_CROSSHAIR },
    { "move", TC_CURSOR_MOVE }, { "all-scroll", TC_CURSOR_MOVE }, { "not-allowed", TC_CURSOR_NOT_ALLOWED },
    { "no-drop", TC_CURSOR_NOT_ALLOWED }, { "ew-resize", TC_CURSOR_EW_RESIZE }, { "e-resize", TC_CURSOR_EW_RESIZE },
    { "w-resize", TC_CURSOR_EW_RESIZE }, { "col-resize", TC_CURSOR_EW_RESIZE }, { "ns-resize", TC_CURSOR_NS_RESIZE },
    { "n-resize", TC_CURSOR_NS_RESIZE }, { "s-resize", TC_CURSOR_NS_RESIZE }, { "row-resize", TC_CURSOR_NS_RESIZE },
    { "nesw-resize", TC_CURSOR_NESW_RESIZE }, { "ne-resize", TC_CURSOR_NESW_RESIZE },
    { "sw-resize", TC_CURSOR_NESW_RESIZE }, { "nwse-resize", TC_CURSOR_NWSE_RESIZE },
    { "nw-resize", TC_CURSOR_NWSE_RESIZE }, { "se-resize", TC_CURSOR_NWSE_RESIZE },
    /* No Windows 95 cursor for these. */
    { "context-menu", TC_CURSOR_DEFAULT }, { "alias", TC_CURSOR_DEFAULT }, { "copy", TC_CURSOR_DEFAULT },
    { "grab", TC_CURSOR_DEFAULT }, { "grabbing", TC_CURSOR_DEFAULT }, { "-webkit-grab", TC_CURSOR_DEFAULT },
    { "-webkit-grabbing", TC_CURSOR_DEFAULT }, { "zoom-in", TC_CURSOR_DEFAULT }, { "zoom-out", TC_CURSOR_DEFAULT },
    { "-webkit-zoom-in", TC_CURSOR_DEFAULT }, { "-webkit-zoom-out", TC_CURSOR_DEFAULT }, { NULL, 0 }
};
static const kw kw_object_fit[] = {
    { "fill", TC_FIT_FILL }, { "contain", TC_FIT_CONTAIN }, { "cover", TC_FIT_COVER }, { "none", TC_FIT_NONE },
    { "scale-down", TC_FIT_SCALE_DOWN }, { NULL, 0 }
};
static const kw kw_display[] = {
    { "none", TC_DISPLAY_NONE }, { "contents", TC_DISPLAY_CONTENTS }, { "inline", TC_DISPLAY_INLINE },
    { "block", TC_DISPLAY_BLOCK }, { "inline-block", TC_DISPLAY_INLINE_BLOCK }, { "list-item", TC_DISPLAY_LIST_ITEM },
    { "flow-root", TC_DISPLAY_FLOW_ROOT }, { "flex", TC_DISPLAY_FLEX }, { "inline-flex", TC_DISPLAY_INLINE_FLEX },
    { "grid", TC_DISPLAY_GRID }, { "inline-grid", TC_DISPLAY_INLINE_GRID }, { "table", TC_DISPLAY_TABLE },
    { "inline-table", TC_DISPLAY_INLINE_TABLE }, { "table-row-group", TC_DISPLAY_TABLE_ROW_GROUP },
    { "table-header-group", TC_DISPLAY_TABLE_HEADER_GROUP }, { "table-footer-group", TC_DISPLAY_TABLE_FOOTER_GROUP },
    { "table-row", TC_DISPLAY_TABLE_ROW }, { "table-cell", TC_DISPLAY_TABLE_CELL },
    { "table-column-group", TC_DISPLAY_TABLE_COLUMN_GROUP }, { "table-column", TC_DISPLAY_TABLE_COLUMN },
    { "table-caption", TC_DISPLAY_TABLE_CAPTION }, { "ruby", TC_DISPLAY_RUBY }, { "ruby-text", TC_DISPLAY_RUBY_TEXT },
    { "ruby-base", TC_DISPLAY_INLINE }, { "ruby-base-container", TC_DISPLAY_INLINE },
    { "ruby-text-container", TC_DISPLAY_RUBY_TEXT }, { "run-in", TC_DISPLAY_BLOCK }, { "math", TC_DISPLAY_INLINE },
    { "-webkit-box", TC_DISPLAY_FLEX }, { "-webkit-inline-box", TC_DISPLAY_INLINE_FLEX },
    { "-webkit-flex", TC_DISPLAY_FLEX }, { "-webkit-inline-flex", TC_DISPLAY_INLINE_FLEX },
    { "-moz-box", TC_DISPLAY_FLEX }, { "-moz-inline-box", TC_DISPLAY_INLINE_FLEX },
    { "-ms-flexbox", TC_DISPLAY_FLEX }, { "-ms-inline-flexbox", TC_DISPLAY_INLINE_FLEX },
    { "-ms-grid", TC_DISPLAY_GRID }, { "-ms-inline-grid", TC_DISPLAY_INLINE_GRID }, { NULL, 0 }
};

static const char *const wide_keywords[] = { "initial", "inherit", "unset", "revert", "revert-layer" };

static int kw_find(const kw *table, const tc_css_value *v, uint8_t *out)
{
    if (!table || v->type != TC_CSS_IDENT)
        return 0;
    for (; table->name; table++)
        if (tc_css_ascii_ieq(v->text, table->name)) {
            *out = table->value;
            return 1;
        }
    return 0;
}

static int wide_keyword(const tc_css_value *v)
{
    size_t i;
    if (v->type == TC_CSS_IDENT)
        for (i = 0; i < TC_ARRAY_LEN(wide_keywords); i++)
            if (tc_css_ascii_ieq(v->text, wide_keywords[i]))
                return (int)i;
    return -1;
}

/* ---- The properties ---- */

enum {
    T_KEYWORD, T_DISPLAY, T_MARGIN, T_PADDING, T_INSET, T_BORDER_WIDTH, T_COLOR, T_SIZE, T_MAX_SIZE, T_VALIGN,
    T_DECORATION_LINE, T_CONTENT, T_Z_INDEX, T_FONT_FAMILY, T_FONT_SIZE, T_FONT_WEIGHT, T_FONT_STYLE,
    T_LINE_HEIGHT, T_LIST_STYLE_TYPE, T_CURSOR, T_TEXT_INDENT, T_SPACING, T_BG_IMAGE, T_BG_REPEAT,
    T_BG_POSITION, T_BG_SIZE, T_BG_BOX, T_POSITION, T_IMAGE
};

static const struct prop_info {
    const char *name;
    uint8_t inherited;
    uint8_t type;
    const kw *keywords;
} props[P_COUNT] = {
#define TC_PROP_INFO(id, name, inherited, type, keywords) { name, inherited, type, keywords },
    TC_PROPS(TC_PROP_INFO)
#undef TC_PROP_INFO
};

/* Every name, sorted by strcmp (the unit tests check), for binary search.
   Logical properties map to physical ones: Tachyon lays out horizontal
   left-to-right text only. */
static const struct {
    const char *name;
    uint16_t prop;
} prop_names[] = {
    { "-moz-box-sizing", P_BOX_SIZING }, { "-webkit-box-sizing", P_BOX_SIZING },
    { "-webkit-text-decoration", SH_TEXT_DECORATION }, { "-webkit-text-decoration-color", P_TEXT_DECORATION_COLOR },
    { "-webkit-text-decoration-line", P_TEXT_DECORATION_LINE },
    { "-webkit-text-decoration-style", P_TEXT_DECORATION_STYLE }, { "all", SH_ALL }, { "background", SH_BACKGROUND },
    { "background-clip", P_BACKGROUND_CLIP }, { "background-color", P_BACKGROUND_COLOR },
    { "background-image", P_BACKGROUND_IMAGE }, { "background-origin", P_BACKGROUND_ORIGIN },
    { "background-position", P_BACKGROUND_POSITION }, { "background-repeat", P_BACKGROUND_REPEAT },
    { "background-size", P_BACKGROUND_SIZE }, { "block-size", P_HEIGHT }, { "border", SH_BORDER },
    { "border-block", SH_BORDER_BLOCK }, { "border-block-color", SH_BORDER_BLOCK_COLOR },
    { "border-block-end", SH_BORDER_BOTTOM }, { "border-block-end-color", P_BORDER_BOTTOM_COLOR },
    { "border-block-end-style", P_BORDER_BOTTOM_STYLE }, { "border-block-end-width", P_BORDER_BOTTOM_WIDTH },
    { "border-block-start", SH_BORDER_TOP }, { "border-block-start-color", P_BORDER_TOP_COLOR },
    { "border-block-start-style", P_BORDER_TOP_STYLE }, { "border-block-start-width", P_BORDER_TOP_WIDTH },
    { "border-block-style", SH_BORDER_BLOCK_STYLE }, { "border-block-width", SH_BORDER_BLOCK_WIDTH },
    { "border-bottom", SH_BORDER_BOTTOM }, { "border-bottom-color", P_BORDER_BOTTOM_COLOR },
    { "border-bottom-style", P_BORDER_BOTTOM_STYLE }, { "border-bottom-width", P_BORDER_BOTTOM_WIDTH },
    { "border-color", SH_BORDER_COLOR }, { "border-inline", SH_BORDER_INLINE },
    { "border-inline-color", SH_BORDER_INLINE_COLOR }, { "border-inline-end", SH_BORDER_RIGHT },
    { "border-inline-end-color", P_BORDER_RIGHT_COLOR }, { "border-inline-end-style", P_BORDER_RIGHT_STYLE },
    { "border-inline-end-width", P_BORDER_RIGHT_WIDTH }, { "border-inline-start", SH_BORDER_LEFT },
    { "border-inline-start-color", P_BORDER_LEFT_COLOR }, { "border-inline-start-style", P_BORDER_LEFT_STYLE },
    { "border-inline-start-width", P_BORDER_LEFT_WIDTH }, { "border-inline-style", SH_BORDER_INLINE_STYLE },
    { "border-inline-width", SH_BORDER_INLINE_WIDTH }, { "border-left", SH_BORDER_LEFT },
    { "border-left-color", P_BORDER_LEFT_COLOR }, { "border-left-style", P_BORDER_LEFT_STYLE },
    { "border-left-width", P_BORDER_LEFT_WIDTH }, { "border-right", SH_BORDER_RIGHT },
    { "border-right-color", P_BORDER_RIGHT_COLOR }, { "border-right-style", P_BORDER_RIGHT_STYLE },
    { "border-right-width", P_BORDER_RIGHT_WIDTH }, { "border-style", SH_BORDER_STYLE },
    { "border-top", SH_BORDER_TOP }, { "border-top-color", P_BORDER_TOP_COLOR },
    { "border-top-style", P_BORDER_TOP_STYLE }, { "border-top-width", P_BORDER_TOP_WIDTH },
    { "border-width", SH_BORDER_WIDTH }, { "bottom", P_BOTTOM }, { "box-sizing", P_BOX_SIZING }, { "clear", P_CLEAR },
    { "color", P_COLOR }, { "content", P_CONTENT }, { "cursor", P_CURSOR }, { "display", P_DISPLAY },
    { "float", P_FLOAT }, { "font", SH_FONT }, { "font-family", P_FONT_FAMILY }, { "font-size", P_FONT_SIZE },
    { "font-style", P_FONT_STYLE }, { "font-weight", P_FONT_WEIGHT }, { "height", P_HEIGHT },
    { "inline-size", P_WIDTH }, { "inset", SH_INSET }, { "inset-block", SH_INSET_BLOCK },
    { "inset-block-end", P_BOTTOM }, { "inset-block-start", P_TOP }, { "inset-inline", SH_INSET_INLINE },
    { "inset-inline-end", P_RIGHT }, { "inset-inline-start", P_LEFT }, { "left", P_LEFT },
    { "letter-spacing", P_LETTER_SPACING }, { "line-height", P_LINE_HEIGHT }, { "list-style", SH_LIST_STYLE },
    { "list-style-image", P_LIST_STYLE_IMAGE }, { "list-style-position", P_LIST_STYLE_POSITION }, { "list-style-type", P_LIST_STYLE_TYPE },
    { "margin", SH_MARGIN }, { "margin-block", SH_MARGIN_BLOCK }, { "margin-block-end", P_MARGIN_BOTTOM },
    { "margin-block-start", P_MARGIN_TOP }, { "margin-bottom", P_MARGIN_BOTTOM },
    { "margin-inline", SH_MARGIN_INLINE }, { "margin-inline-end", P_MARGIN_RIGHT },
    { "margin-inline-start", P_MARGIN_LEFT }, { "margin-left", P_MARGIN_LEFT }, { "margin-right", P_MARGIN_RIGHT },
    { "margin-top", P_MARGIN_TOP }, { "max-block-size", P_MAX_HEIGHT }, { "max-height", P_MAX_HEIGHT },
    { "max-inline-size", P_MAX_WIDTH }, { "max-width", P_MAX_WIDTH }, { "min-block-size", P_MIN_HEIGHT },
    { "min-height", P_MIN_HEIGHT }, { "min-inline-size", P_MIN_WIDTH }, { "min-width", P_MIN_WIDTH },
    { "object-fit", P_OBJECT_FIT }, { "object-position", P_OBJECT_POSITION }, { "overflow", SH_OVERFLOW }, { "overflow-wrap", P_OVERFLOW_WRAP }, { "overflow-x", P_OVERFLOW_X },
    { "overflow-y", P_OVERFLOW_Y }, { "padding", SH_PADDING }, { "padding-block", SH_PADDING_BLOCK },
    { "padding-block-end", P_PADDING_BOTTOM }, { "padding-block-start", P_PADDING_TOP },
    { "padding-bottom", P_PADDING_BOTTOM }, { "padding-inline", SH_PADDING_INLINE },
    { "padding-inline-end", P_PADDING_RIGHT }, { "padding-inline-start", P_PADDING_LEFT },
    { "padding-left", P_PADDING_LEFT }, { "padding-right", P_PADDING_RIGHT }, { "padding-top", P_PADDING_TOP },
    { "position", P_POSITION }, { "right", P_RIGHT }, { "text-align", P_TEXT_ALIGN },
    { "text-decoration", SH_TEXT_DECORATION }, { "text-decoration-color", P_TEXT_DECORATION_COLOR },
    { "text-decoration-line", P_TEXT_DECORATION_LINE }, { "text-decoration-style", P_TEXT_DECORATION_STYLE },
    { "text-indent", P_TEXT_INDENT }, { "text-transform", P_TEXT_TRANSFORM }, { "text-wrap", SH_TEXT_WRAP },
    { "text-wrap-mode", P_TEXT_WRAP_MODE }, { "top", P_TOP }, { "vertical-align", P_VERTICAL_ALIGN },
    { "visibility", P_VISIBILITY }, { "white-space", SH_WHITE_SPACE },
    { "white-space-collapse", P_WHITE_SPACE_COLLAPSE }, { "width", P_WIDTH }, { "word-break", P_WORD_BREAK },
    { "word-spacing", P_WORD_SPACING }, { "word-wrap", P_OVERFLOW_WRAP }, { "z-index", P_Z_INDEX },
};

int tc_prop_inherited(uint16_t prop)
{
    return prop < P_COUNT && props[prop].inherited;
}

const char *tc_prop_name(uint16_t prop)
{
    size_t i;
    if (prop < P_COUNT)
        return props[prop].name;
    for (i = 0; i < TC_ARRAY_LEN(prop_names); i++)
        if (prop_names[i].prop == prop)
            return prop_names[i].name;
    return "?";
}

/* The property (longhand or shorthand) a name means, or -1. */
static int prop_lookup(const char *name)
{
    char lower[40];
    size_t len = strlen(name), i, lo = 0, hi = TC_ARRAY_LEN(prop_names);
    if (len >= sizeof lower)
        return -1;
    for (i = 0; i <= len; i++)
        lower[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int cmp = strcmp(lower, prop_names[mid].name);
        if (cmp == 0)
            return prop_names[mid].prop;
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return -1;
}

/* The longhands a property sets. */
static uint32_t longhands(uint16_t prop, uint16_t *out)
{
    uint32_t n = 0, i;
    int first = -1, part;
    switch (prop) {
    case SH_MARGIN: first = P_MARGIN_TOP; break;
    case SH_PADDING: first = P_PADDING_TOP; break;
    case SH_INSET: first = P_TOP; break;
    case SH_BORDER_WIDTH: first = P_BORDER_TOP_WIDTH; break;
    case SH_BORDER_STYLE: first = P_BORDER_TOP_STYLE; break;
    case SH_BORDER_COLOR: first = P_BORDER_TOP_COLOR; break;
    case SH_BORDER:
        for (part = 0; part < 3; part++)
            for (i = 0; i < 4; i++)
                out[n++] = (uint16_t)(P_BORDER_TOP_WIDTH + part * 4 + (int)i);
        return n;
    case SH_BORDER_TOP:
    case SH_BORDER_RIGHT:
    case SH_BORDER_BOTTOM:
    case SH_BORDER_LEFT:
        for (part = 0; part < 3; part++)
            out[n++] = (uint16_t)(P_BORDER_TOP_WIDTH + part * 4 + (prop - SH_BORDER_TOP));
        return n;
    case SH_BORDER_BLOCK:
    case SH_BORDER_INLINE:
        for (part = 0; part < 3; part++) {
            int a = prop == SH_BORDER_BLOCK ? 0 : 3, b = prop == SH_BORDER_BLOCK ? 2 : 1;
            out[n++] = (uint16_t)(P_BORDER_TOP_WIDTH + part * 4 + a);
            out[n++] = (uint16_t)(P_BORDER_TOP_WIDTH + part * 4 + b);
        }
        return n;
    case SH_BORDER_BLOCK_WIDTH: out[0] = P_BORDER_TOP_WIDTH, out[1] = P_BORDER_BOTTOM_WIDTH; return 2;
    case SH_BORDER_BLOCK_STYLE: out[0] = P_BORDER_TOP_STYLE, out[1] = P_BORDER_BOTTOM_STYLE; return 2;
    case SH_BORDER_BLOCK_COLOR: out[0] = P_BORDER_TOP_COLOR, out[1] = P_BORDER_BOTTOM_COLOR; return 2;
    case SH_BORDER_INLINE_WIDTH: out[0] = P_BORDER_LEFT_WIDTH, out[1] = P_BORDER_RIGHT_WIDTH; return 2;
    case SH_BORDER_INLINE_STYLE: out[0] = P_BORDER_LEFT_STYLE, out[1] = P_BORDER_RIGHT_STYLE; return 2;
    case SH_BORDER_INLINE_COLOR: out[0] = P_BORDER_LEFT_COLOR, out[1] = P_BORDER_RIGHT_COLOR; return 2;
    case SH_MARGIN_BLOCK: out[0] = P_MARGIN_TOP, out[1] = P_MARGIN_BOTTOM; return 2;
    case SH_MARGIN_INLINE: out[0] = P_MARGIN_LEFT, out[1] = P_MARGIN_RIGHT; return 2;
    case SH_PADDING_BLOCK: out[0] = P_PADDING_TOP, out[1] = P_PADDING_BOTTOM; return 2;
    case SH_PADDING_INLINE: out[0] = P_PADDING_LEFT, out[1] = P_PADDING_RIGHT; return 2;
    case SH_INSET_BLOCK: out[0] = P_TOP, out[1] = P_BOTTOM; return 2;
    case SH_INSET_INLINE: out[0] = P_LEFT, out[1] = P_RIGHT; return 2;
    case SH_OVERFLOW: out[0] = P_OVERFLOW_X, out[1] = P_OVERFLOW_Y; return 2;
    case SH_BACKGROUND:
        out[0] = P_BACKGROUND_COLOR, out[1] = P_BACKGROUND_IMAGE, out[2] = P_BACKGROUND_REPEAT;
        out[3] = P_BACKGROUND_POSITION, out[4] = P_BACKGROUND_SIZE, out[5] = P_BACKGROUND_ORIGIN;
        out[6] = P_BACKGROUND_CLIP;
        return 7;
    case SH_FONT:
        out[0] = P_FONT_STYLE, out[1] = P_FONT_WEIGHT, out[2] = P_FONT_SIZE, out[3] = P_LINE_HEIGHT;
        out[4] = P_FONT_FAMILY;
        return 5;
    case SH_LIST_STYLE: out[0] = P_LIST_STYLE_TYPE, out[1] = P_LIST_STYLE_POSITION, out[2] = P_LIST_STYLE_IMAGE; return 3;
    case SH_TEXT_DECORATION:
        out[0] = P_TEXT_DECORATION_LINE, out[1] = P_TEXT_DECORATION_STYLE, out[2] = P_TEXT_DECORATION_COLOR;
        return 3;
    case SH_WHITE_SPACE: out[0] = P_WHITE_SPACE_COLLAPSE, out[1] = P_TEXT_WRAP_MODE; return 2;
    case SH_TEXT_WRAP: out[0] = P_TEXT_WRAP_MODE; return 1;
    case SH_ALL:
        for (i = 0; i < P_COUNT; i++)
            out[i] = (uint16_t)i;
        return P_COUNT;
    default:
        if (prop < P_COUNT) {
            out[0] = prop;
            return 1;
        }
        return 0;
    }
    for (i = 0; i < 4; i++)
        out[i] = (uint16_t)(first + i);
    return 4;
}

/* ---- Values ---- */

typedef struct pctx {
    tc_arena *a;
    int quirks;
    const char *base;  /* url()s are relative to this style sheet's URL (NULL: the document's) */
} pctx;

#define MAX_WORDS 256

/* The component values that aren't whitespace; 0 if there are too many. */
static uint32_t words_of(const tc_css_value *v, uint32_t n, const tc_css_value **w)
{
    uint32_t i, count = 0;
    for (i = 0; i < n; i++)
        if (v[i].type != TC_CSS_WHITESPACE) {
            if (count == MAX_WORDS)
                return 0;
            w[count++] = &v[i];
        }
    return count;
}

static void set_keyword(tc_pval *out, int k)
{
    memset(out, 0, sizeof *out);
    out->kind = PV_KEYWORD;
    out->keyword = (uint8_t)k;
}

static void set_number(tc_pval *out, float f)
{
    memset(out, 0, sizeof *out);
    out->kind = PV_NUMBER;
    out->n.f = f;
}

/* A length-percentage. quirk: in quirks mode, a unitless number is pixels
   (the "unitless length" quirk, for the properties it applies to). */
static int parse_lp(pctx *c, const tc_css_value *v, unsigned allow, int nonneg, int quirk, tc_pval *out)
{
    const tc_calc *calc;
    if (quirk && c->quirks && v->type == TC_CSS_NUMBER && v->number != 0 && (allow & TC_ALLOW_LENGTH)) {
        tc_css_value px = *v;
        px.type = TC_CSS_DIMENSION;
        px.text = "px";
        calc = tc_css_parse_calc(c->a, &px, allow);
    } else {
        calc = tc_css_parse_calc(c->a, v, allow);
    }
    if (!calc || (nonneg && calc->op == TC_CALC_VALUE && calc->value < 0))
        return 0;
    memset(out, 0, sizeof *out);
    out->kind = PV_LENGTH;
    out->ptr = calc;
    return 1;
}

static int parse_color(const tc_css_value *v, tc_pval *out)
{
    tc_css_color color;
    if (tc_css_parse_color(v, 1, &color) != TC_OK)
        return 0;
    memset(out, 0, sizeof *out);
    if (color.space == TC_COLOR_CURRENT) {
        out->kind = PV_CURRENTCOLOR;
    } else {
        out->kind = PV_COLOR;
        out->n.u = tc_css_color_argb(&color);
    }
    return 1;
}

#define K_AUTO   1u
#define K_NONE   2u
#define K_SIZING 4u /* min-content, max-content, fit-content, stretch */

static int parse_size(pctx *c, const tc_css_value *v, unsigned keywords, int nonneg, tc_pval *out)
{
    static const kw sizing[] = {
        { "min-content", TC_SIZE_MIN_CONTENT }, { "-moz-min-content", TC_SIZE_MIN_CONTENT },
        { "-webkit-min-content", TC_SIZE_MIN_CONTENT }, { "max-content", TC_SIZE_MAX_CONTENT },
        { "-moz-max-content", TC_SIZE_MAX_CONTENT }, { "-webkit-max-content", TC_SIZE_MAX_CONTENT },
        { "fit-content", TC_SIZE_FIT_CONTENT }, { "-moz-fit-content", TC_SIZE_FIT_CONTENT },
        { "-webkit-fit-content", TC_SIZE_FIT_CONTENT }, { "stretch", TC_SIZE_STRETCH },
        { "-webkit-fill-available", TC_SIZE_STRETCH }, { "-moz-available", TC_SIZE_STRETCH }, { NULL, 0 }
    };
    uint8_t k;
    if (v->type == TC_CSS_IDENT) {
        if ((keywords & K_AUTO) && tc_css_ascii_ieq(v->text, "auto"))
            k = TC_SIZE_AUTO;
        else if ((keywords & K_NONE) && tc_css_ascii_ieq(v->text, "none"))
            k = TC_SIZE_NONE;
        else if (!(keywords & K_SIZING) || !kw_find(sizing, v, &k))
            return 0;
        set_keyword(out, k);
        return 1;
    }
    if (v->type == TC_CSS_FUNCTION && (keywords & K_SIZING) &&
        (tc_css_ascii_ieq(v->text, "fit-content") || tc_css_ascii_ieq(v->text, "-webkit-fit-content"))) {
        set_keyword(out, TC_SIZE_FIT_CONTENT);
        return 1;
    }
    return parse_lp(c, v, TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, nonneg, 1, out);
}

/* display: one keyword, or the two- and three-keyword forms. */
static int parse_display(const tc_css_value *const *w, uint32_t n, tc_pval *out)
{
    static const char *const outsides[] = { "block", "inline", "run-in" };
    static const char *const insides[] = { "flow", "flow-root", "table", "flex", "grid", "ruby", "math" };
    int outside = -1, inside = -1, list_item = 0;
    uint32_t i;
    size_t k;
    uint8_t d;

    if (n == 1)
        return kw_find(kw_display, w[0], &d) ? (set_keyword(out, d), 1) : 0;
    if (n > 3)
        return 0;
    for (i = 0; i < n; i++) {
        int found = 0;
        if (w[i]->type != TC_CSS_IDENT)
            return 0;
        for (k = 0; k < TC_ARRAY_LEN(outsides) && !found; k++)
            if (tc_css_ascii_ieq(w[i]->text, outsides[k])) {
                if (outside >= 0)
                    return 0;
                outside = (int)k;
                found = 1;
            }
        for (k = 0; k < TC_ARRAY_LEN(insides) && !found; k++)
            if (tc_css_ascii_ieq(w[i]->text, insides[k])) {
                if (inside >= 0)
                    return 0;
                inside = (int)k;
                found = 1;
            }
        if (!found && tc_css_ascii_ieq(w[i]->text, "list-item")) {
            if (list_item)
                return 0;
            list_item = found = 1;
        }
        if (!found)
            return 0;
    }
    if (list_item) {
        if (inside > 1)
            return 0;
        d = TC_DISPLAY_LIST_ITEM;
    } else {
        int in = outside == 1;
        if (outside < 0)
            in = inside == 5; /* ruby alone is inline */
        switch (inside) {
        case 1: d = in ? TC_DISPLAY_INLINE_BLOCK : TC_DISPLAY_FLOW_ROOT; break;
        case 2: d = in ? TC_DISPLAY_INLINE_TABLE : TC_DISPLAY_TABLE; break;
        case 3: d = in ? TC_DISPLAY_INLINE_FLEX : TC_DISPLAY_FLEX; break;
        case 4: d = in ? TC_DISPLAY_INLINE_GRID : TC_DISPLAY_GRID; break;
        case 5: d = in ? TC_DISPLAY_RUBY : TC_DISPLAY_BLOCK; break;
        default: d = in ? TC_DISPLAY_INLINE : TC_DISPLAY_BLOCK; break;
        }
    }
    set_keyword(out, d);
    return 1;
}

static char *copy_str(tc_arena *a, const char *s)
{
    return tc_arena_strndup(a, s, strlen(s));
}

static int parse_families(pctx *c, const tc_css_value *const *w, uint32_t n, tc_pval *out)
{
    static const char *const generics[] = {
        "serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui", "ui-serif", "ui-sans-serif",
        "ui-monospace", "ui-rounded", "math", "emoji", "fangsong"
    };
    tc_font_families *f;
    const char **names;
    uint32_t i = 0, count = 0;

    if (!n)
        return 0;
    f = tc_arena_alloc(c->a, sizeof *f);
    names = tc_arena_alloc(c->a, n * sizeof *names);
    if (!f || !names)
        return 0;
    while (i < n) {
        if (w[i]->type == TC_CSS_STRING) {
            names[count] = copy_str(c->a, w[i]->text);
            i++;
        } else if (w[i]->type == TC_CSS_IDENT) {
            uint32_t start = i, k;
            size_t len = 0;
            char *name;
            while (i < n && w[i]->type == TC_CSS_IDENT)
                len += strlen(w[i++]->text) + 1;
            if (i - start == 1) {
                size_t g;
                if (wide_keyword(w[start]) >= 0 || tc_css_ascii_ieq(w[start]->text, "default"))
                    return 0;
                for (g = 0; g < TC_ARRAY_LEN(generics); g++)
                    if (tc_css_ascii_ieq(w[start]->text, generics[g]))
                        break;
                if (g < TC_ARRAY_LEN(generics)) {
                    names[count++] = generics[g];
                    goto next;
                }
            }
            name = tc_arena_alloc(c->a, len);
            if (!name)
                return 0;
            name[0] = '\0';
            for (k = start; k < i; k++) {
                if (k > start)
                    strcat(name, " ");
                strcat(name, w[k]->text);
            }
            names[count] = name;
        } else {
            return 0;
        }
        if (!names[count])
            return 0;
        count++;
    next:
        if (i < n) {
            if (w[i]->type != TC_CSS_COMMA || i + 1 == n)
                return 0;
            i++;
        }
    }
    f->names = names;
    f->count = count;
    memset(out, 0, sizeof *out);
    out->kind = PV_PTR;
    out->ptr = f;
    return 1;
}

static int parse_content(pctx *c, const tc_css_value *const *w, uint32_t n, tc_pval *out)
{
    tc_content *content;
    uint32_t i;

    if (n == 1 && w[0]->type == TC_CSS_IDENT) {
        if (tc_css_ascii_ieq(w[0]->text, "normal") || tc_css_ascii_ieq(w[0]->text, "none")) {
            set_keyword(out, tc_css_ascii_ieq(w[0]->text, "none") ? CONTENT_NONE : CONTENT_NORMAL);
            return 1;
        }
    }
    content = tc_arena_alloc(c->a, sizeof *content);
    if (!content || !(content->items = tc_arena_alloc(c->a, (n ? n : 1) * sizeof *content->items)))
        return 0;
    content->count = 0;
    for (i = 0; i < n; i++) {
        const tc_css_value *v = w[i];
        tc_content_item *item = &content->items[content->count];
        memset(item, 0, sizeof *item);
        if (v->type == TC_CSS_STRING) {
            item->type = TC_CONTENT_STRING;
            item->text = copy_str(c->a, v->text);
        } else if (v->type == TC_CSS_IDENT) {
            if (tc_css_ascii_ieq(v->text, "open-quote"))
                item->type = TC_CONTENT_OPEN_QUOTE;
            else if (tc_css_ascii_ieq(v->text, "close-quote"))
                item->type = TC_CONTENT_CLOSE_QUOTE;
            else if (tc_css_ascii_ieq(v->text, "no-open-quote") || tc_css_ascii_ieq(v->text, "no-close-quote"))
                continue;
            else
                return 0;
            item->text = "";
        } else if (v->type == TC_CSS_FUNCTION) {
            const tc_css_value *args[8];
            uint32_t nargs = 0, k;
            for (k = 0; k < v->nitems && nargs < TC_ARRAY_LEN(args); k++)
                if (v->items[k].type != TC_CSS_WHITESPACE && v->items[k].type != TC_CSS_COMMA)
                    args[nargs++] = &v->items[k];
            if (tc_css_ascii_ieq(v->text, "attr")) {
                char *name;
                size_t j;
                if (!nargs || args[0]->type != TC_CSS_IDENT)
                    return 0;
                item->type = TC_CONTENT_ATTR;
                name = copy_str(c->a, args[0]->text);
                if (name)
                    for (j = 0; name[j]; j++)
                        if (name[j] >= 'A' && name[j] <= 'Z')
                            name[j] = (char)(name[j] + 32);
                item->text = name;
            } else if (tc_css_ascii_ieq(v->text, "counter") || tc_css_ascii_ieq(v->text, "counters")) {
                uint32_t style_arg = tc_css_ascii_ieq(v->text, "counter") ? 1 : 2;
                if (!nargs || args[0]->type != TC_CSS_IDENT)
                    return 0;
                item->type = TC_CONTENT_COUNTER;
                item->text = copy_str(c->a, args[0]->text);
                item->list_style = TC_LIST_DECIMAL;
                if (nargs > style_arg && !kw_find(kw_list_style, args[style_arg], &item->list_style))
                    item->list_style = TC_LIST_DECIMAL;
            } else if (tc_css_ascii_ieq(v->text, "url") || strstr(v->text, "gradient") ||
                       tc_css_ascii_ieq(v->text, "image-set") || tc_css_ascii_ieq(v->text, "-webkit-image-set")) {
                continue; /* images: Sprint 4 */
            } else {
                return 0;
            }
        } else if (v->type == TC_CSS_URL) {
            continue;
        } else if (tc_css_is_delim(v, '/')) {
            break; /* alternative text follows */
        } else {
            return 0;
        }
        if (!item->text)
            return 0;
        content->count++;
    }
    memset(out, 0, sizeof *out);
    out->kind = PV_PTR;
    out->ptr = content;
    return 1;
}

static int is_integer(const tc_css_value *v)
{
    return v->type == TC_CSS_NUMBER && (v->flags & TC_CSS_INTEGER);
}

/* A <number> (or a calc() of numbers). */
static int parse_number(pctx *c, const tc_css_value *v, float *out)
{
    const tc_calc *calc = tc_css_parse_calc(c->a, v, TC_ALLOW_NUMBER);
    if (!calc)
        return 0;
    *out = tc_calc_number(calc);
    return 1;
}

static int parse_font_weight(pctx *c, const tc_css_value *v, tc_pval *out)
{
    float f;
    if (tc_css_is_ident(v, "normal"))
        set_number(out, 400);
    else if (tc_css_is_ident(v, "bold"))
        set_number(out, 700);
    else if (tc_css_is_ident(v, "bolder"))
        set_keyword(out, FW_BOLDER);
    else if (tc_css_is_ident(v, "lighter"))
        set_keyword(out, FW_LIGHTER);
    else if (parse_number(c, v, &f) && f >= 1 && f <= 1000)
        set_number(out, f);
    else
        return 0;
    return 1;
}

static int parse_font_size(pctx *c, const tc_css_value *v, tc_pval *out)
{
    uint8_t k;
    if (kw_find(kw_font_size, v, &k)) {
        set_keyword(out, k);
        return 1;
    }
    return parse_lp(c, v, TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, 1, 1, out);
}

static int parse_line_height(pctx *c, const tc_css_value *v, tc_pval *out)
{
    float f;
    if (tc_css_is_ident(v, "normal")) {
        set_keyword(out, 0);
        return 1;
    }
    if (parse_number(c, v, &f)) {
        if (f < 0)
            return 0;
        set_number(out, f);
        return 1;
    }
    return parse_lp(c, v, TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, 1, 0, out);
}

/* text-decoration-line: none, or underline || overline || line-through || blink. */
static int parse_decoration_line(const tc_css_value *const *w, uint32_t n, int32_t *bits)
{
    static const struct {
        const char *name;
        uint8_t bit;
    } lines[] = {
        { "underline", TC_DECORATION_UNDERLINE }, { "overline", TC_DECORATION_OVERLINE },
        { "line-through", TC_DECORATION_LINE_THROUGH }, { "blink", 8 },
        { "spelling-error", 16 }, { "grammar-error", 32 },
    };
    uint32_t i, seen = 0;
    size_t k;
    if (n == 1 && tc_css_is_ident(w[0], "none")) {
        *bits = 0;
        return 1;
    }
    if (!n)
        return 0;
    for (i = 0; i < n; i++) {
        for (k = 0; k < TC_ARRAY_LEN(lines); k++)
            if (tc_css_is_ident(w[i], lines[k].name))
                break;
        if (k == TC_ARRAY_LEN(lines) || (seen & lines[k].bit))
            return 0;
        seen |= lines[k].bit;
    }
    *bits = (int32_t)(seen & 7u);
    return 1;
}

/* ---- Images, positions and backgrounds ---- */

#define MAX_LAYERS 64 /* background layers, gradient stops */

/* url(x) or url("x"): the URL as written, or NULL. */
static const char *url_of(const tc_css_value *v)
{
    uint32_t i;
    if (v->type == TC_CSS_URL)
        return v->text;
    if (v->type != TC_CSS_FUNCTION || !tc_css_ascii_ieq(v->text, "url"))
        return NULL;
    for (i = 0; i < v->nitems; i++)
        if (v->items[i].type == TC_CSS_STRING)
            return v->items[i].text;
        else if (v->items[i].type != TC_CSS_WHITESPACE)
            return NULL;
    return NULL;
}

/* An <angle> in degrees. */
static int parse_angle(const tc_css_value *v, float *deg)
{
    static const struct {
        const char *unit;
        double scale;
    } units[] = { { "deg", 1 }, { "grad", 0.9 }, { "rad", 57.29577951 }, { "turn", 360 } };
    size_t i;
    if (v->type == TC_CSS_NUMBER && v->number == 0) {
        *deg = 0;
        return 1;
    }
    if (v->type != TC_CSS_DIMENSION)
        return 0;
    for (i = 0; i < TC_ARRAY_LEN(units); i++)
        if (tc_css_ascii_ieq(v->text, units[i].unit)) {
            double d = v->number * units[i].scale;
            if (!(d > -1e7 && d < 1e7))
                return 0;
            *deg = (float)d;
            return 1;
        }
    return 0;
}

/* top, right, bottom or left as TC_CORNER_* bits; 0 if it's none. */
static unsigned side_bit(const tc_css_value *v)
{
    if (tc_css_is_ident(v, "top"))
        return TC_CORNER_TOP;
    if (tc_css_is_ident(v, "right"))
        return TC_CORNER_RIGHT;
    if (tc_css_is_ident(v, "bottom"))
        return TC_CORNER_BOTTOM;
    if (tc_css_is_ident(v, "left"))
        return TC_CORNER_LEFT;
    return 0;
}

/* A function's arguments, split at commas, without whitespace. */
static uint32_t split_args(const tc_css_value *f, const tc_css_value **w, uint32_t *start, uint32_t *len,
                           uint32_t max_groups)
{
    uint32_t i, n = 0, groups = 0;
    start[0] = 0;
    len[0] = 0;
    for (i = 0; i < f->nitems && n < MAX_WORDS; i++) {
        const tc_css_value *v = &f->items[i];
        if (v->type == TC_CSS_WHITESPACE)
            continue;
        if (v->type == TC_CSS_COMMA) {
            if (++groups >= max_groups)
                return 0;
            start[groups] = n;
            len[groups] = 0;
            continue;
        }
        w[n++] = v;
        len[groups]++;
    }
    return groups + 1;
}

/* linear-gradient() and repeating-linear-gradient(), and the old
   -webkit- form, whose sides say where it starts rather than ends. */
static int parse_gradient(pctx *c, const tc_css_value *f, tc_css_image *img)
{
    const tc_css_value *w[MAX_WORDS];
    uint32_t start[MAX_LAYERS], len[MAX_LAYERS], groups, g, first = 0, nstops = 0;
    const char *name = f->text;
    int legacy = 0;
    tc_color_stop *stops;

    if (name[0] == '-') {
        if (tc_css_ascii_ieq(name, "-webkit-linear-gradient") || tc_css_ascii_ieq(name, "-moz-linear-gradient") ||
            tc_css_ascii_ieq(name, "-o-linear-gradient"))
            legacy = 1;
        else if (tc_css_ascii_ieq(name, "-webkit-repeating-linear-gradient") ||
                 tc_css_ascii_ieq(name, "-moz-repeating-linear-gradient"))
            legacy = 1, img->repeating = 1;
        else
            return 0;
    } else if (tc_css_ascii_ieq(name, "repeating-linear-gradient")) {
        img->repeating = 1;
    } else if (!tc_css_ascii_ieq(name, "linear-gradient")) {
        return 0;
    }
    img->type = TC_CSS_IMAGE_LINEAR;
    img->angle = 180;
    groups = split_args(f, w, start, len, MAX_LAYERS);
    if (groups < 2)
        return 0;
    /* The direction, if there is one. */
    if (len[0] && parse_angle(w[start[0]], &img->angle)) {
        if (len[0] != 1)
            return 0;
        if (legacy)
            img->angle = 90 - img->angle; /* counterclockwise from east */
        first = 1;
    } else if (len[0] && (tc_css_is_ident(w[start[0]], "to") || (legacy && side_bit(w[start[0]])))) {
        unsigned bits = 0, k;
        for (k = legacy ? 0 : 1; k < len[0]; k++) {
            unsigned b = side_bit(w[start[0] + k]);
            if (!b || (bits & b))
                return 0;
            bits |= b;
        }
        if (!bits || ((bits & TC_CORNER_TOP) && (bits & TC_CORNER_BOTTOM)) ||
            ((bits & TC_CORNER_LEFT) && (bits & TC_CORNER_RIGHT)))
            return 0;
        if (legacy) /* where it starts: it goes the other way */
            bits = ((bits & 3u) << 2) | ((bits >> 2) & 3u);
        if (bits == TC_CORNER_TOP)
            img->angle = 0;
        else if (bits == TC_CORNER_RIGHT)
            img->angle = 90;
        else if (bits == TC_CORNER_BOTTOM)
            img->angle = 180;
        else if (bits == TC_CORNER_LEFT)
            img->angle = 270;
        else
            img->corner = (uint8_t)bits;
        first = 1;
    }
    stops = tc_arena_alloc(c->a, 2 * groups * sizeof *stops);
    if (!stops)
        return 0;
    for (g = first; g < groups; g++) {
        tc_pval v;
        uint32_t k, at = start[g];
        if (!len[g])
            return 0;
        if (len[g] == 1 && !parse_color(w[at], &v))
            continue; /* a colour hint: Tachyon interpolates linearly */
        if (!parse_color(w[at], &v) || len[g] > 3)
            return 0;
        for (k = 0; k < (len[g] > 1 ? len[g] - 1 : 1); k++) {
            tc_color_stop *st = &stops[nstops++];
            memset(st, 0, sizeof *st);
            st->color = v.kind == PV_COLOR ? v.n.u : 0;
            st->currentcolor = v.kind == PV_CURRENTCOLOR;
            if (len[g] > 1) {
                tc_pval pos;
                if (!parse_lp(c, w[at + 1 + k], TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, 0, 0, &pos))
                    return 0;
                st->declared = pos.ptr;
                st->has_position = 1;
            }
        }
    }
    if (nstops < 2)
        return 0;
    img->stops = stops;
    img->nstops = nstops;
    return 1;
}

/* An <image>: url(), a gradient, or image-set() (its first image). */
static const tc_css_image *parse_image(pctx *c, const tc_css_value *v)
{
    tc_css_image *img;
    const char *url = url_of(v);
    if (!url && v->type == TC_CSS_FUNCTION &&
        (tc_css_ascii_ieq(v->text, "image-set") || tc_css_ascii_ieq(v->text, "-webkit-image-set"))) {
        uint32_t i;
        for (i = 0; i < v->nitems && !url; i++)
            url = v->items[i].type == TC_CSS_STRING ? v->items[i].text : url_of(&v->items[i]);
        if (!url)
            return NULL;
    }
    if (!url && v->type != TC_CSS_FUNCTION)
        return NULL;
    img = tc_arena_alloc(c->a, sizeof *img);
    if (!img)
        return NULL;
    memset(img, 0, sizeof *img);
    if (url) {
        img->type = TC_CSS_IMAGE_URL;
        img->url = copy_str(c->a, url);
        img->base = c->base;
        return img->url ? img : NULL;
    }
    return parse_gradient(c, v, img) ? img : NULL;
}

/* The words of a comma-separated list, by layer. */
static uint32_t split_layers(const tc_css_value *const *w, uint32_t n, uint32_t *start, uint32_t *len)
{
    uint32_t i, layers = 1;
    start[0] = 0;
    len[0] = 0;
    for (i = 0; i < n; i++) {
        if (w[i]->type == TC_CSS_COMMA) {
            if (layers == MAX_LAYERS)
                return 0;
            start[layers] = i + 1;
            len[layers++] = 0;
        } else {
            len[layers - 1]++;
        }
    }
    for (i = 0; i < layers; i++)
        if (!len[i])
            return 0;
    return layers;
}

/* A <bg-position> (1 to 4 words). */
enum { POS_LP, POS_LEFT, POS_RIGHT, POS_TOP, POS_BOTTOM, POS_CENTER };

static int pos_horizontal(int k)
{
    return k == POS_LEFT || k == POS_RIGHT;
}

static int pos_vertical(int k)
{
    return k == POS_TOP || k == POS_BOTTOM;
}

/* One axis of a position from a keyword (with an optional offset) or a
   length-percentage. */
static void pos_axis(pctx *c, int k, const tc_calc *off, uint8_t *far, const tc_calc **at)
{
    static tc_css_value half = { TC_CSS_PERCENTAGE, 0, 0, "", "50", 50, NULL, 0 };
    *far = k == POS_RIGHT || k == POS_BOTTOM;
    *at = k == POS_CENTER ? tc_css_parse_calc(c->a, &half, TC_ALLOW_PERCENT) : off;
}

static int parse_position(pctx *c, const tc_css_value *const *w, uint32_t n, tc_position *out)
{
    int kind[4], a, b;
    const tc_calc *off[4];
    uint32_t i;
    memset(out, 0, sizeof *out);
    if (!n || n > 4)
        return 0;
    for (i = 0; i < n; i++) {
        tc_pval v;
        off[i] = NULL;
        kind[i] = tc_css_is_ident(w[i], "left")     ? POS_LEFT
                  : tc_css_is_ident(w[i], "right")  ? POS_RIGHT
                  : tc_css_is_ident(w[i], "top")    ? POS_TOP
                  : tc_css_is_ident(w[i], "bottom") ? POS_BOTTOM
                  : tc_css_is_ident(w[i], "center") ? POS_CENTER
                                                    : POS_LP;
        if (kind[i] == POS_LP) {
            if (!parse_lp(c, w[i], TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, 0, 0, &v))
                return 0;
            off[i] = v.ptr;
        }
    }
    if (n == 1) {
        if (pos_vertical(kind[0])) {
            pos_axis(c, kind[0], NULL, &out->far_y, &out->y);
            pos_axis(c, POS_CENTER, NULL, &out->far_x, &out->x);
        } else {
            pos_axis(c, kind[0], off[0], &out->far_x, &out->x);
            pos_axis(c, POS_CENTER, NULL, &out->far_y, &out->y);
        }
        return 1;
    }
    if (n == 2) {
        a = kind[0], b = kind[1];
        if (pos_vertical(a) || (a == POS_CENTER && pos_horizontal(b))) {
            /* Keywords, vertical first. */
            if (b == POS_LP || pos_vertical(b))
                return 0;
            pos_axis(c, b, NULL, &out->far_x, &out->x);
            pos_axis(c, a, NULL, &out->far_y, &out->y);
        } else {
            if (pos_horizontal(b))
                return 0;
            pos_axis(c, a, off[0], &out->far_x, &out->x);
            pos_axis(c, b, off[1], &out->far_y, &out->y);
        }
        return 1;
    }
    /* Three or four words: two keywords, each but center with an optional
       offset; center takes whichever axis the other doesn't. */
    {
        int k[2];
        const tc_calc *o[2];
        uint32_t pairs = 0;
        for (i = 0; i < n;) {
            if (kind[i] == POS_LP || pairs == 2)
                return 0;
            k[pairs] = kind[i];
            o[pairs] = NULL;
            if (i + 1 < n && kind[i + 1] == POS_LP) {
                if (kind[i] == POS_CENTER)
                    return 0;
                o[pairs] = off[i + 1];
                i++;
            }
            i++;
            pairs++;
        }
        if (pairs != 2)
            return 0;
        if (pos_vertical(k[0]) || pos_horizontal(k[1]) || (k[0] == POS_CENTER && k[1] == POS_CENTER)) {
            int tk = k[0];
            const tc_calc *to = o[0];
            k[0] = k[1], o[0] = o[1], k[1] = tk, o[1] = to;
        }
        if (pos_vertical(k[0]) || pos_horizontal(k[1]))
            return 0;
        pos_axis(c, k[0], o[0], &out->far_x, &out->x);
        pos_axis(c, k[1], o[1], &out->far_y, &out->y);
        return 1;
    }
}

/* One layer's background-repeat (1 or 2 words). */
static int parse_repeat(const tc_css_value *const *w, uint32_t n, uint8_t rep[2])
{
    static const kw words[] = { { "repeat", TC_BG_REPEAT }, { "no-repeat", TC_BG_NO_REPEAT },
                                { "space", TC_BG_SPACE }, { "round", TC_BG_ROUND }, { NULL, 0 } };
    if (n == 1 && tc_css_is_ident(w[0], "repeat-x")) {
        rep[0] = TC_BG_REPEAT, rep[1] = TC_BG_NO_REPEAT;
        return 1;
    }
    if (n == 1 && tc_css_is_ident(w[0], "repeat-y")) {
        rep[0] = TC_BG_NO_REPEAT, rep[1] = TC_BG_REPEAT;
        return 1;
    }
    if (n < 1 || n > 2 || !kw_find(words, w[0], &rep[0]))
        return 0;
    rep[1] = rep[0];
    return n == 1 || kw_find(words, w[1], &rep[1]);
}

/* One layer's background-size (1 or 2 words). */
static int parse_bg_size(pctx *c, const tc_css_value *const *w, uint32_t n, tc_bg_size_decl *out)
{
    uint32_t i;
    memset(out, 0, sizeof *out);
    if (n == 1 && (tc_css_is_ident(w[0], "cover") || tc_css_is_ident(w[0], "contain"))) {
        out->kind = tc_css_is_ident(w[0], "cover") ? TC_BG_COVER : TC_BG_CONTAIN;
        return 1;
    }
    if (n < 1 || n > 2)
        return 0;
    for (i = 0; i < n; i++) {
        tc_pval v;
        if (tc_css_is_ident(w[i], "auto"))
            continue;
        if (!parse_lp(c, w[i], TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, 1, 0, &v))
            return 0;
        if (i == 0)
            out->w = v.ptr;
        else
            out->h = v.ptr;
    }
    return 1;
}

static int parse_box(const tc_css_value *v, uint8_t *out)
{
    if (tc_css_is_ident(v, "border-box") || tc_css_is_ident(v, "text"))
        *out = TC_BG_BORDER_BOX;
    else if (tc_css_is_ident(v, "padding-box"))
        *out = TC_BG_PADDING_BOX;
    else if (tc_css_is_ident(v, "content-box"))
        *out = TC_BG_CONTENT_BOX;
    else
        return 0;
    return 1;
}

static tc_bg_list *new_bg_list(pctx *c, uint32_t layers, uint16_t prop)
{
    tc_bg_list *l = tc_arena_alloc(c->a, sizeof *l);
    if (!l)
        return NULL;
    memset(l, 0, sizeof *l);
    l->count = layers;
    switch (prop) {
    case P_BACKGROUND_IMAGE: l->images = tc_arena_alloc(c->a, layers * sizeof *l->images); return l->images ? l : NULL;
    case P_BACKGROUND_REPEAT: l->repeat = tc_arena_alloc(c->a, layers * sizeof *l->repeat); return l->repeat ? l : NULL;
    case P_BACKGROUND_POSITION:
        l->positions = tc_arena_alloc(c->a, layers * sizeof *l->positions);
        return l->positions ? l : NULL;
    case P_BACKGROUND_SIZE: l->sizes = tc_arena_alloc(c->a, layers * sizeof *l->sizes); return l->sizes ? l : NULL;
    default: l->boxes = tc_arena_alloc(c->a, layers * sizeof *l->boxes); return l->boxes ? l : NULL;
    }
}

/* A background longhand: a value per layer. */
static int parse_bg_longhand(pctx *c, uint16_t prop, const tc_css_value *const *w, uint32_t n, tc_pval *out)
{
    uint32_t start[MAX_LAYERS], len[MAX_LAYERS], layers = split_layers(w, n, start, len), i;
    tc_bg_list *l;
    if (!layers || !(l = new_bg_list(c, layers, prop)))
        return 0;
    for (i = 0; i < layers; i++) {
        const tc_css_value *const *lw = w + start[i];
        uint32_t ln = len[i];
        int ok;
        switch (prop) {
        case P_BACKGROUND_IMAGE:
            l->images[i] = NULL;
            ok = ln == 1 && (tc_css_is_ident(lw[0], "none") || (l->images[i] = parse_image(c, lw[0])) != NULL);
            break;
        case P_BACKGROUND_REPEAT: ok = parse_repeat(lw, ln, l->repeat[i]); break;
        case P_BACKGROUND_POSITION: ok = parse_position(c, lw, ln, &l->positions[i]); break;
        case P_BACKGROUND_SIZE: ok = parse_bg_size(c, lw, ln, &l->sizes[i]); break;
        default: ok = ln == 1 && parse_box(lw[0], &l->boxes[i]); break;
        }
        if (!ok)
            return 0;
    }
    memset(out, 0, sizeof *out);
    out->kind = PV_PTR;
    out->ptr = l;
    return 1;
}

/* One longhand's value from its words. */
static int parse_longhand(pctx *c, uint16_t prop, const tc_css_value *const *w, uint32_t n, tc_pval *out)
{
    const struct prop_info *info = &props[prop];
    uint8_t k;
    memset(out, 0, sizeof *out);
    if (!n)
        return 0;
    switch (info->type) {
    case T_KEYWORD:
        if (n != 1 || !kw_find(info->keywords, w[0], &k))
            return 0;
        set_keyword(out, k);
        if (k == KW_INHERIT && prop == P_TEXT_ALIGN) {
            out->kind = PV_WIDE;
            out->keyword = WIDE_INHERIT;
        }
        return 1;
    case T_DISPLAY:
        return parse_display(w, n, out);
    case T_MARGIN:
    case T_INSET:
        return n == 1 && parse_size(c, w[0], K_AUTO, 0, out);
    case T_PADDING:
        return n == 1 && parse_lp(c, w[0], TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, 1, 1, out);
    case T_SIZE:
        return n == 1 && parse_size(c, w[0], K_AUTO | K_SIZING, 1, out);
    case T_MAX_SIZE:
        return n == 1 && parse_size(c, w[0], K_NONE | K_SIZING, 1, out);
    case T_BORDER_WIDTH:
        if (n != 1)
            return 0;
        if (tc_css_is_ident(w[0], "thin"))
            set_number(out, 1);
        else if (tc_css_is_ident(w[0], "medium"))
            set_number(out, 3);
        else if (tc_css_is_ident(w[0], "thick"))
            set_number(out, 5);
        else
            return parse_lp(c, w[0], TC_ALLOW_LENGTH, 1, 1, out);
        return 1;
    case T_COLOR:
        return n == 1 && parse_color(w[0], out);
    case T_VALIGN:
        if (n != 1)
            return 0;
        if (kw_find(info->keywords, w[0], &k)) {
            set_keyword(out, k);
            return 1;
        }
        return parse_lp(c, w[0], TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, 0, 1, out);
    case T_DECORATION_LINE:
        out->kind = PV_INT;
        return parse_decoration_line(w, n, &out->n.i);
    case T_CONTENT:
        return parse_content(c, w, n, out);
    case T_BG_IMAGE:
    case T_BG_REPEAT:
    case T_BG_POSITION:
    case T_BG_SIZE:
    case T_BG_BOX:
        return parse_bg_longhand(c, prop, w, n, out);
    case T_POSITION: {
        tc_position *pos = tc_arena_alloc(c->a, sizeof *pos);
        if (!pos || !parse_position(c, w, n, pos))
            return 0;
        out->kind = PV_PTR;
        out->ptr = pos;
        return 1;
    }
    case T_IMAGE:
        if (n != 1)
            return 0;
        if (tc_css_is_ident(w[0], "none")) {
            set_keyword(out, 0);
            return 1;
        }
        out->kind = PV_PTR;
        out->ptr = parse_image(c, w[0]);
        return out->ptr != NULL;
    case T_Z_INDEX:
        if (n != 1)
            return 0;
        if (tc_css_is_ident(w[0], "auto")) {
            set_keyword(out, 0);
            return 1;
        }
        if (!is_integer(w[0]))
            return 0;
        out->kind = PV_INT;
        out->n.i = w[0]->number > 1e9 ? 1000000000 : w[0]->number < -1e9 ? -1000000000 : (int32_t)w[0]->number;
        return 1;
    case T_FONT_FAMILY:
        return parse_families(c, w, n, out);
    case T_FONT_SIZE:
        return n == 1 && parse_font_size(c, w[0], out);
    case T_FONT_WEIGHT:
        return n == 1 && parse_font_weight(c, w[0], out);
    case T_FONT_STYLE:
        if (!kw_find(info->keywords, w[0], &k))
            return 0;
        /* oblique may give an angle, which Tachyon ignores. */
        if (n == 2 && !(k == TC_FONT_OBLIQUE && w[1]->type == TC_CSS_DIMENSION))
            return 0;
        if (n > 2)
            return 0;
        set_keyword(out, k);
        return 1;
    case T_LINE_HEIGHT:
        return n == 1 && parse_line_height(c, w[0], out);
    case T_LIST_STYLE_TYPE:
        if (n != 1)
            return 0;
        if (kw_find(info->keywords, w[0], &k)) {
            set_keyword(out, k);
        } else if (w[0]->type == TC_CSS_STRING) {
            out->kind = PV_PTR;
            out->ptr = copy_str(c->a, w[0]->text);
            return out->ptr != NULL;
        } else if (w[0]->type == TC_CSS_IDENT ||
                   (w[0]->type == TC_CSS_FUNCTION && tc_css_ascii_ieq(w[0]->text, "symbols"))) {
            set_keyword(out, TC_LIST_DECIMAL); /* a counter style Tachyon doesn't know */
        } else {
            return 0;
        }
        return 1;
    case T_CURSOR: {
        /* [<url> [x y]?,]* <keyword> */
        uint32_t i;
        if (!kw_find(info->keywords, w[n - 1], &k))
            return 0;
        for (i = 0; i + 1 < n; i++)
            if (w[i]->type != TC_CSS_URL && w[i]->type != TC_CSS_FUNCTION && w[i]->type != TC_CSS_NUMBER &&
                w[i]->type != TC_CSS_COMMA)
                return 0;
        if (n > 1 && w[n - 2]->type != TC_CSS_COMMA)
            return 0;
        set_keyword(out, k);
        return 1;
    }
    case T_TEXT_INDENT: {
        uint32_t i;
        int have = 0;
        for (i = 0; i < n; i++) {
            if (tc_css_is_ident(w[i], "hanging") || tc_css_is_ident(w[i], "each-line"))
                continue;
            if (have || !parse_lp(c, w[i], TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, 0, 1, out))
                return 0;
            have = 1;
        }
        return have;
    }
    case T_SPACING:
        if (n != 1)
            return 0;
        if (tc_css_is_ident(w[0], "normal")) {
            set_keyword(out, 0);
            return 1;
        }
        return parse_lp(c, w[0], TC_ALLOW_LENGTH | (prop == P_WORD_SPACING ? TC_ALLOW_PERCENT : 0), 0, 1, out);
    }
    return 0;
}

/* ---- Shorthands ---- */

typedef struct outv {
    uint16_t *props;
    tc_pval *vals;
    uint32_t n;
} outv;

static void emit(outv *o, uint16_t prop, const tc_pval *v)
{
    o->props[o->n] = prop;
    o->vals[o->n++] = *v;
}

/* margin, padding, inset and border-width/-style/-color: 1 to 4 values. */
static int sh_box(pctx *c, uint16_t first, const tc_css_value *const *w, uint32_t n, outv *o)
{
    tc_pval v[4];
    uint32_t i;
    if (n < 1 || n > 4)
        return 0;
    for (i = 0; i < n; i++)
        if (!parse_longhand(c, first, &w[i], 1, &v[i]))
            return 0;
    if (n < 2)
        v[1] = v[0];
    if (n < 3)
        v[2] = v[0];
    if (n < 4)
        v[3] = v[1];
    for (i = 0; i < 4; i++)
        emit(o, (uint16_t)(first + i), &v[i]);
    return 1;
}

/* The -block and -inline forms: 1 or 2 values, start then end. */
static int sh_pair(pctx *c, uint16_t start, uint16_t end, const tc_css_value *const *w, uint32_t n, outv *o)
{
    tc_pval a, b;
    if (n < 1 || n > 2 || !parse_longhand(c, start, &w[0], 1, &a))
        return 0;
    b = a;
    if (n == 2 && !parse_longhand(c, end, &w[1], 1, &b))
        return 0;
    emit(o, start, &a);
    emit(o, end, &b);
    return 1;
}

/* border and its one- and two-side forms: <width> || <style> || <color>.
   sides: bit i for side i (top, right, bottom, left). */
static int sh_border(pctx *c, unsigned sides, const tc_css_value *const *w, uint32_t n, outv *o)
{
    tc_pval width, style, color, v;
    int have_width = 0, have_style = 0, have_color = 0;
    uint32_t i;
    if (n < 1 || n > 3)
        return 0;
    set_number(&width, 3);
    set_keyword(&style, TC_BORDER_NONE);
    memset(&color, 0, sizeof color);
    color.kind = PV_CURRENTCOLOR;
    for (i = 0; i < n; i++) {
        if (!have_width && parse_longhand(c, P_BORDER_TOP_WIDTH, &w[i], 1, &v))
            width = v, have_width = 1;
        else if (!have_style && parse_longhand(c, P_BORDER_TOP_STYLE, &w[i], 1, &v))
            style = v, have_style = 1;
        else if (!have_color && parse_color(w[i], &v))
            color = v, have_color = 1;
        else
            return 0;
    }
    for (i = 0; i < 4; i++)
        if (sides & (1u << i)) {
            emit(o, (uint16_t)(P_BORDER_TOP_WIDTH + i), &width);
            emit(o, (uint16_t)(P_BORDER_TOP_STYLE + i), &style);
            emit(o, (uint16_t)(P_BORDER_TOP_COLOR + i), &color);
        }
    return 1;
}

/* background: Tachyon keeps only the colour, but checks the rest. */
static int is_position_word(const tc_css_value *v)
{
    return tc_css_is_ident(v, "left") || tc_css_is_ident(v, "right") || tc_css_is_ident(v, "top") ||
           tc_css_is_ident(v, "bottom") || tc_css_is_ident(v, "center") || v->type == TC_CSS_NUMBER ||
           v->type == TC_CSS_PERCENTAGE || v->type == TC_CSS_DIMENSION ||
           (v->type == TC_CSS_FUNCTION && (tc_css_ascii_ieq(v->text, "calc") || tc_css_ascii_ieq(v->text, "min") ||
                                           tc_css_ascii_ieq(v->text, "max") || tc_css_ascii_ieq(v->text, "clamp")));
}

static int is_repeat_word(const tc_css_value *v)
{
    return tc_css_is_ident(v, "repeat") || tc_css_is_ident(v, "repeat-x") || tc_css_is_ident(v, "repeat-y") ||
           tc_css_is_ident(v, "no-repeat") || tc_css_is_ident(v, "space") || tc_css_is_ident(v, "round");
}

/* background: layers of [image || position [/ size] || repeat || attachment
   || box{1,2}], the last with a colour too. */
static int sh_background(pctx *c, const tc_css_value *const *w, uint32_t n, outv *o)
{
    static const uint16_t lists[] = { P_BACKGROUND_IMAGE, P_BACKGROUND_REPEAT, P_BACKGROUND_POSITION,
                                      P_BACKGROUND_SIZE, P_BACKGROUND_ORIGIN, P_BACKGROUND_CLIP };
    uint32_t start[MAX_LAYERS], len[MAX_LAYERS], layers = split_layers(w, n, start, len), i, k;
    tc_bg_list *l[TC_ARRAY_LEN(lists)];
    tc_pval color, v;

    if (!layers)
        return 0;
    for (k = 0; k < TC_ARRAY_LEN(lists); k++)
        if (!(l[k] = new_bg_list(c, layers, lists[k])))
            return 0;
    memset(&color, 0, sizeof color);
    color.kind = PV_COLOR; /* transparent */
    for (i = 0; i < layers; i++) {
        const tc_css_value *const *lw = w + start[i];
        uint32_t ln = len[i], j = 0, nboxes = 0;
        int have_image = 0, have_pos = 0, have_repeat = 0, have_attach = 0, have_color = 0;
        tc_css_image const **image = &l[0]->images[i];
        *image = NULL;
        l[1]->repeat[i][0] = l[1]->repeat[i][1] = TC_BG_REPEAT;
        memset(&l[2]->positions[i], 0, sizeof l[2]->positions[i]);
        memset(&l[3]->sizes[i], 0, sizeof l[3]->sizes[i]);
        l[4]->boxes[i] = TC_BG_PADDING_BOX;
        l[5]->boxes[i] = TC_BG_BORDER_BOX;
        while (j < ln) {
            const tc_css_value *t = lw[j];
            if (!have_image && tc_css_is_ident(t, "none")) {
                have_image = 1, j++;
            } else if (!have_image && (*image = parse_image(c, t)) != NULL) {
                have_image = 1, j++;
            } else if (!have_pos && is_position_word(t)) {
                uint32_t m = j;
                while (m < ln && m - j < 4 && is_position_word(lw[m]))
                    m++;
                if (!parse_position(c, lw + j, m - j, &l[2]->positions[i]))
                    return 0;
                have_pos = 1, j = m;
                if (j < ln && tc_css_is_delim(lw[j], '/')) {
                    m = ++j;
                    while (m < ln && m - j < 2 &&
                           (tc_css_is_ident(lw[m], "auto") || tc_css_is_ident(lw[m], "cover") ||
                            tc_css_is_ident(lw[m], "contain") || is_position_word(lw[m])))
                        m++;
                    if (!parse_bg_size(c, lw + j, m - j, &l[3]->sizes[i]))
                        return 0;
                    j = m;
                }
            } else if (!have_repeat && is_repeat_word(t)) {
                uint32_t m = j + 1;
                if (m < ln && is_repeat_word(lw[m]) && !tc_css_is_ident(t, "repeat-x") &&
                    !tc_css_is_ident(t, "repeat-y"))
                    m++;
                if (!parse_repeat(lw + j, m - j, l[1]->repeat[i]))
                    return 0;
                have_repeat = 1, j = m;
            } else if (!have_attach && (tc_css_is_ident(t, "scroll") || tc_css_is_ident(t, "fixed") ||
                                        tc_css_is_ident(t, "local"))) {
                have_attach = 1, j++; /* Tachyon scrolls every background */
            } else if (nboxes < 2 && parse_box(t, &l[5]->boxes[i])) {
                if (!nboxes)
                    l[4]->boxes[i] = l[5]->boxes[i];
                nboxes++, j++;
            } else if (i + 1 == layers && !have_color && parse_color(t, &v)) {
                color = v;
                have_color = 1, j++;
            } else {
                return 0;
            }
        }
    }
    emit(o, P_BACKGROUND_COLOR, &color);
    for (k = 0; k < TC_ARRAY_LEN(lists); k++) {
        memset(&v, 0, sizeof v);
        v.kind = PV_PTR;
        v.ptr = l[k];
        emit(o, lists[k], &v);
    }
    return 1;
}

static int sh_font(pctx *c, const tc_css_value *const *w, uint32_t n, outv *o)
{
    static const char *const systems[] = { "caption", "icon", "menu", "message-box", "small-caption", "status-bar" };
    static const char *const stretches[] = {
        "ultra-condensed", "extra-condensed", "condensed", "semi-condensed", "semi-expanded", "expanded",
        "extra-expanded", "ultra-expanded"
    };
    static const char *sans[] = { "sans-serif" };
    static const tc_font_families system_family = { sans, 1 };
    tc_pval style, weight, size, line_height, family, v;
    int have_style = 0, have_weight = 0, have_variant = 0, have_stretch = 0;
    uint32_t i = 0, count = 0;
    size_t k;

    set_keyword(&style, TC_FONT_NORMAL);
    set_number(&weight, 400);
    set_keyword(&line_height, 0);
    if (n == 1 && w[0]->type == TC_CSS_IDENT)
        for (k = 0; k < TC_ARRAY_LEN(systems); k++)
            if (tc_css_ascii_ieq(w[0]->text, systems[k])) {
                /* Windows 95's dialog font, as other browsers use for form controls. */
                set_number(&size, 13.333f);
                memset(&family, 0, sizeof family);
                family.kind = PV_PTR;
                family.ptr = &system_family;
                goto done;
            }
    for (; i < n && count < 4; i++, count++) {
        const tc_css_value *t = w[i];
        if (tc_css_is_ident(t, "normal"))
            continue;
        if (!have_style && kw_find(kw_font_style, t, &style.keyword)) {
            have_style = 1;
            if (style.keyword == TC_FONT_OBLIQUE && i + 1 < n && w[i + 1]->type == TC_CSS_DIMENSION)
                i++;
            continue;
        }
        if (!have_variant && tc_css_is_ident(t, "small-caps")) {
            have_variant = 1;
            continue;
        }
        if (!have_weight && t->type != TC_CSS_DIMENSION && t->type != TC_CSS_PERCENTAGE &&
            parse_font_weight(c, t, &v)) {
            weight = v;
            have_weight = 1;
            continue;
        }
        if (!have_stretch && t->type == TC_CSS_IDENT) {
            for (k = 0; k < TC_ARRAY_LEN(stretches); k++)
                if (tc_css_ascii_ieq(t->text, stretches[k]))
                    break;
            if (k < TC_ARRAY_LEN(stretches)) {
                have_stretch = 1;
                continue;
            }
        }
        break;
    }
    if (i >= n || !parse_font_size(c, w[i], &size))
        return 0;
    i++;
    if (i < n && tc_css_is_delim(w[i], '/')) {
        if (i + 1 >= n || !parse_line_height(c, w[i + 1], &line_height))
            return 0;
        i += 2;
    }
    if (!parse_families(c, w + i, n - i, &family))
        return 0;
done:
    emit(o, P_FONT_STYLE, &style);
    emit(o, P_FONT_WEIGHT, &weight);
    emit(o, P_FONT_SIZE, &size);
    emit(o, P_LINE_HEIGHT, &line_height);
    emit(o, P_FONT_FAMILY, &family);
    return 1;
}

static int sh_list_style(pctx *c, const tc_css_value *const *w, uint32_t n, outv *o)
{
    tc_pval type, position, image;
    int nones = 0, have_type = 0, have_position = 0, have_image = 0;
    uint32_t i;
    if (n < 1 || n > 3)
        return 0;
    set_keyword(&type, TC_LIST_DISC);
    set_keyword(&position, TC_LIST_OUTSIDE);
    set_keyword(&image, 0);
    for (i = 0; i < n; i++) {
        if (tc_css_is_ident(w[i], "none"))
            nones++;
        else if (!have_position && kw_find(kw_list_position, w[i], &position.keyword))
            have_position = 1;
        else if (!have_image && (w[i]->type == TC_CSS_URL || w[i]->type == TC_CSS_FUNCTION) &&
                 !tc_css_ascii_ieq(w[i]->text, "symbols")) {
            have_image = 1;
            image.kind = PV_PTR;
            if (!(image.ptr = parse_image(c, w[i])))
                return 0;
        }
        else if (!have_type && parse_longhand(c, P_LIST_STYLE_TYPE, &w[i], 1, &type))
            have_type = 1;
        else
            return 0;
    }
    if (nones && !have_type) {
        set_keyword(&type, TC_LIST_NONE);
        have_type = 1;
        nones--;
    }
    if (nones && !have_image)
        nones--;
    if (nones)
        return 0;
    emit(o, P_LIST_STYLE_TYPE, &type);
    emit(o, P_LIST_STYLE_POSITION, &position);
    emit(o, P_LIST_STYLE_IMAGE, &image);
    return 1;
}

static int sh_text_decoration(pctx *c, const tc_css_value *const *w, uint32_t n, outv *o)
{
    const tc_css_value *line_words[8];
    tc_pval line, style, color, v;
    uint32_t i, nline = 0;
    int have_style = 0, have_color = 0, have_thickness = 0;
    if (!n)
        return 0;
    memset(&line, 0, sizeof line);
    line.kind = PV_INT;
    set_keyword(&style, TC_DECORATION_SOLID);
    memset(&color, 0, sizeof color);
    color.kind = PV_CURRENTCOLOR;
    for (i = 0; i < n; i++) {
        int32_t bits;
        if (nline < TC_ARRAY_LEN(line_words) && parse_decoration_line(&w[i], 1, &bits)) {
            line_words[nline++] = w[i];
        } else if (!have_style && kw_find(kw_decoration_style, w[i], &v.keyword)) {
            set_keyword(&style, v.keyword);
            have_style = 1;
        } else if (!have_thickness && (tc_css_is_ident(w[i], "auto") || tc_css_is_ident(w[i], "from-font") ||
                                       parse_lp(c, w[i], TC_ALLOW_LENGTH | TC_ALLOW_PERCENT, 0, 0, &v))) {
            have_thickness = 1;
        } else if (!have_color && parse_color(w[i], &v)) {
            color = v;
            have_color = 1;
        } else {
            return 0;
        }
    }
    if (nline && !parse_decoration_line(line_words, nline, &line.n.i))
        return 0;
    emit(o, P_TEXT_DECORATION_LINE, &line);
    emit(o, P_TEXT_DECORATION_STYLE, &style);
    emit(o, P_TEXT_DECORATION_COLOR, &color);
    return 1;
}

static int sh_white_space(const tc_css_value *const *w, uint32_t n, int wrap_only, outv *o)
{
    static const struct {
        const char *name;
        uint8_t collapse, wrap;
    } legacy[] = {
        { "normal", TC_COLLAPSE_COLLAPSE, TC_WRAP_MODE_WRAP }, { "pre", TC_COLLAPSE_PRESERVE, TC_WRAP_MODE_NOWRAP },
        { "nowrap", TC_COLLAPSE_COLLAPSE, TC_WRAP_MODE_NOWRAP },
        { "pre-wrap", TC_COLLAPSE_PRESERVE, TC_WRAP_MODE_WRAP },
        { "-moz-pre-wrap", TC_COLLAPSE_PRESERVE, TC_WRAP_MODE_WRAP },
        { "pre-line", TC_COLLAPSE_PRESERVE_BREAKS, TC_WRAP_MODE_WRAP },
        { "break-spaces", TC_COLLAPSE_BREAK_SPACES, TC_WRAP_MODE_WRAP },
    };
    static const char *const others[] = {
        "discard-before", "discard-after", "discard-inner",                       /* white-space-trim */
        "auto", "balance", "stable", "pretty", "avoid-orphans"                    /* text-wrap-style */
    };
    tc_pval collapse, wrap;
    int have_collapse = 0, have_wrap = 0;
    uint32_t i;
    size_t k;
    if (!n)
        return 0;
    set_keyword(&collapse, TC_COLLAPSE_COLLAPSE);
    set_keyword(&wrap, TC_WRAP_MODE_WRAP);
    if (n == 1 && !wrap_only)
        for (k = 0; k < TC_ARRAY_LEN(legacy); k++)
            if (tc_css_is_ident(w[0], legacy[k].name)) {
                collapse.keyword = legacy[k].collapse;
                wrap.keyword = legacy[k].wrap;
                goto done;
            }
    for (i = 0; i < n; i++) {
        if (!wrap_only && !have_collapse && kw_find(kw_collapse, w[i], &collapse.keyword)) {
            have_collapse = 1;
            continue;
        }
        if (!have_wrap && kw_find(kw_wrap_mode, w[i], &wrap.keyword)) {
            have_wrap = 1;
            continue;
        }
        for (k = wrap_only ? 3 : 0; k < (wrap_only ? TC_ARRAY_LEN(others) : 3); k++)
            if (tc_css_is_ident(w[i], others[k]))
                break;
        if (k == (wrap_only ? TC_ARRAY_LEN(others) : 3))
            return 0;
    }
done:
    if (!wrap_only)
        emit(o, P_WHITE_SPACE_COLLAPSE, &collapse);
    emit(o, P_TEXT_WRAP_MODE, &wrap);
    return 1;
}

static int parse_shorthand(pctx *c, uint16_t prop, const tc_css_value *const *w, uint32_t n, outv *o)
{
    switch (prop) {
    case SH_MARGIN: return sh_box(c, P_MARGIN_TOP, w, n, o);
    case SH_PADDING: return sh_box(c, P_PADDING_TOP, w, n, o);
    case SH_INSET: return sh_box(c, P_TOP, w, n, o);
    case SH_BORDER_WIDTH: return sh_box(c, P_BORDER_TOP_WIDTH, w, n, o);
    case SH_BORDER_STYLE: return sh_box(c, P_BORDER_TOP_STYLE, w, n, o);
    case SH_BORDER_COLOR: return sh_box(c, P_BORDER_TOP_COLOR, w, n, o);
    case SH_BORDER: return sh_border(c, 15, w, n, o);
    case SH_BORDER_TOP: return sh_border(c, 1, w, n, o);
    case SH_BORDER_RIGHT: return sh_border(c, 2, w, n, o);
    case SH_BORDER_BOTTOM: return sh_border(c, 4, w, n, o);
    case SH_BORDER_LEFT: return sh_border(c, 8, w, n, o);
    case SH_BORDER_BLOCK: return sh_border(c, 5, w, n, o);
    case SH_BORDER_INLINE: return sh_border(c, 10, w, n, o);
    case SH_BORDER_BLOCK_WIDTH: return sh_pair(c, P_BORDER_TOP_WIDTH, P_BORDER_BOTTOM_WIDTH, w, n, o);
    case SH_BORDER_BLOCK_STYLE: return sh_pair(c, P_BORDER_TOP_STYLE, P_BORDER_BOTTOM_STYLE, w, n, o);
    case SH_BORDER_BLOCK_COLOR: return sh_pair(c, P_BORDER_TOP_COLOR, P_BORDER_BOTTOM_COLOR, w, n, o);
    case SH_BORDER_INLINE_WIDTH: return sh_pair(c, P_BORDER_LEFT_WIDTH, P_BORDER_RIGHT_WIDTH, w, n, o);
    case SH_BORDER_INLINE_STYLE: return sh_pair(c, P_BORDER_LEFT_STYLE, P_BORDER_RIGHT_STYLE, w, n, o);
    case SH_BORDER_INLINE_COLOR: return sh_pair(c, P_BORDER_LEFT_COLOR, P_BORDER_RIGHT_COLOR, w, n, o);
    case SH_MARGIN_BLOCK: return sh_pair(c, P_MARGIN_TOP, P_MARGIN_BOTTOM, w, n, o);
    case SH_MARGIN_INLINE: return sh_pair(c, P_MARGIN_LEFT, P_MARGIN_RIGHT, w, n, o);
    case SH_PADDING_BLOCK: return sh_pair(c, P_PADDING_TOP, P_PADDING_BOTTOM, w, n, o);
    case SH_PADDING_INLINE: return sh_pair(c, P_PADDING_LEFT, P_PADDING_RIGHT, w, n, o);
    case SH_INSET_BLOCK: return sh_pair(c, P_TOP, P_BOTTOM, w, n, o);
    case SH_INSET_INLINE: return sh_pair(c, P_LEFT, P_RIGHT, w, n, o);
    case SH_OVERFLOW: return sh_pair(c, P_OVERFLOW_X, P_OVERFLOW_Y, w, n, o);
    case SH_BACKGROUND: return sh_background(c, w, n, o);
    case SH_FONT: return sh_font(c, w, n, o);
    case SH_LIST_STYLE: return sh_list_style(c, w, n, o);
    case SH_TEXT_DECORATION: return sh_text_decoration(c, w, n, o);
    case SH_WHITE_SPACE: return sh_white_space(w, n, 0, o);
    case SH_TEXT_WRAP: return sh_white_space(w, n, 1, o);
    default: return 0; /* all: CSS-wide keywords only */
    }
}

uint32_t tc_prop_parse(tc_arena *a, uint16_t prop, const tc_css_value *v, uint32_t n, int quirks, const char *base,
                       uint16_t *out_props, tc_pval *vals)
{
    const tc_css_value *w[MAX_WORDS];
    pctx c;
    outv o;
    uint32_t count = words_of(v, n, w), i;
    int wide;

    c.a = a;
    c.quirks = quirks;
    c.base = base;
    if (!count)
        return 0;
    wide = wide_keyword(w[0]);
    if (wide >= 0) {
        if (count != 1)
            return 0;
        count = longhands(prop, out_props);
        for (i = 0; i < count; i++) {
            memset(&vals[i], 0, sizeof vals[i]);
            vals[i].kind = PV_WIDE;
            vals[i].keyword = (uint8_t)wide;
        }
        return count;
    }
    for (i = 1; i < count; i++)
        if (wide_keyword(w[i]) >= 0)
            return 0;
    if (prop < P_COUNT) {
        out_props[0] = prop;
        return parse_longhand(&c, prop, w, count, &vals[0]) ? 1 : 0;
    }
    o.props = out_props;
    o.vals = vals;
    o.n = 0;
    return parse_shorthand(&c, prop, w, count, &o) ? o.n : 0;
}

/* ---- Tokens ---- */

#define MAX_TOKEN_DEPTH 64

static int have_var(const tc_css_value *v, uint32_t n, int depth)
{
    uint32_t i;
    if (depth > MAX_TOKEN_DEPTH)
        return 1; /* so it gets substituted, and fails there */
    for (i = 0; i < n; i++) {
        if (v[i].type == TC_CSS_FUNCTION && (tc_css_ascii_ieq(v[i].text, "var") || tc_css_ascii_ieq(v[i].text, "env")))
            return 1;
        if ((v[i].type == TC_CSS_FUNCTION || v[i].type == TC_CSS_BLOCK) && have_var(v[i].items, v[i].nitems, depth + 1))
            return 1;
    }
    return 0;
}

int tc_tokens_have_var(const tc_css_value *v, uint32_t n)
{
    return have_var(v, n, 0);
}

static int copy_into(tc_arena *a, tc_css_value *out, const tc_css_value *v, uint32_t n, int depth)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        out[i] = v[i];
        out[i].text = v[i].text ? copy_str(a, v[i].text) : NULL;
        out[i].repr = v[i].repr ? copy_str(a, v[i].repr) : NULL;
        if ((v[i].text && !out[i].text) || (v[i].repr && !out[i].repr))
            return 0;
        out[i].items = NULL;
        out[i].nitems = 0;
        if (v[i].nitems && depth < MAX_TOKEN_DEPTH) {
            out[i].items = tc_arena_alloc(a, v[i].nitems * sizeof *out[i].items);
            if (!out[i].items || !copy_into(a, out[i].items, v[i].items, v[i].nitems, depth + 1))
                return 0;
            out[i].nitems = v[i].nitems;
        }
    }
    return 1;
}

tc_css_value *tc_tokens_copy(tc_arena *a, const tc_css_value *v, uint32_t n)
{
    tc_css_value *out = tc_arena_alloc(a, (n ? n : 1) * sizeof *out);
    return out && copy_into(a, out, v, n, 0) ? out : NULL;
}

/* ---- Declarations ---- */

static int push(tc_decl_vec *vec, const tc_decl *d)
{
    if (vec->len == vec->cap) {
        uint32_t cap = vec->cap ? vec->cap * 2 : 16;
        tc_decl *grown = realloc(vec->items, cap * sizeof *grown);
        if (!grown)
            return TC_ERR_NOMEM;
        vec->items = grown;
        vec->cap = cap;
    }
    vec->items[vec->len++] = *d;
    return TC_OK;
}

void tc_decls_free(tc_decl_vec *v)
{
    free(v->items);
    memset(v, 0, sizeof *v);
}

int tc_decls_add(tc_arena *a, const tc_css_decl *d, int quirks, const char *base, tc_decl_vec *out)
{
    uint16_t lh[P_COUNT];
    tc_pval vals[P_COUNT];
    tc_decl decl;
    uint32_t n, i;
    int prop;

    memset(&decl, 0, sizeof decl);
    decl.important = (uint8_t)(d->important != 0);
    if (d->name[0] == '-' && d->name[1] == '-' && d->name[2]) {
        tc_tokens *t = tc_arena_alloc(a, sizeof *t);
        if (!t || !(decl.name = copy_str(a, d->name)) || !(t->v = tc_tokens_copy(a, d->value, d->nvalue)))
            return TC_ERR_NOMEM;
        t->n = d->nvalue;
        decl.prop = P_CUSTOM;
        decl.has_var = (uint8_t)tc_tokens_have_var(d->value, d->nvalue);
        decl.value.kind = PV_PTR;
        decl.value.ptr = t;
        return push(out, &decl);
    }
    prop = prop_lookup(d->name);
    if (prop < 0)
        return TC_OK;
    if (tc_tokens_have_var(d->value, d->nvalue)) {
        tc_var_value *var = tc_arena_alloc(a, sizeof *var);
        if (!var || !(var->tokens.v = tc_tokens_copy(a, d->value, d->nvalue)))
            return TC_ERR_NOMEM;
        var->tokens.n = d->nvalue;
        var->prop = (uint16_t)prop;
        var->base = base;
        decl.value.kind = PV_VAR;
        decl.value.ptr = var;
        n = longhands((uint16_t)prop, lh);
        for (i = 0; i < n; i++) {
            decl.prop = lh[i];
            if (push(out, &decl) != TC_OK)
                return TC_ERR_NOMEM;
        }
        return TC_OK;
    }
    n = tc_prop_parse(a, (uint16_t)prop, d->value, d->nvalue, quirks, base, lh, vals);
    for (i = 0; i < n; i++) {
        decl.prop = lh[i];
        decl.value = vals[i];
        if (push(out, &decl) != TC_OK)
            return TC_ERR_NOMEM;
    }
    return TC_OK;
}

int tc_prop_supported(tc_arena *a, const char *name, const tc_css_value *v, uint32_t n)
{
    uint16_t lh[P_COUNT];
    tc_pval vals[P_COUNT];
    int prop;
    if (name[0] == '-' && name[1] == '-' && name[2])
        return 1;
    prop = prop_lookup(name);
    if (prop < 0)
        return 0;
    if (tc_tokens_have_var(v, n))
        return 1;
    return tc_prop_parse(a, (uint16_t)prop, v, n, 0, NULL, lh, vals) > 0;
}

int tc_prop_names_sorted(void)
{
    size_t i;
    for (i = 1; i < TC_ARRAY_LEN(prop_names); i++)
        if (strcmp(prop_names[i - 1].name, prop_names[i].name) >= 0)
            return 0;
    return 1;
}
