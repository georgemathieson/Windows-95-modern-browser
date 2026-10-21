/* CSS values (CSS Values and Units 4): lengths, percentages, numbers and
 * the math functions calc(), min(), max() and clamp(), parsed from component
 * values and computed to pixels; and media queries (Media Queries 4) with
 * their environment. */
#ifndef TC_CSS_VALUES_H
#define TC_CSS_VALUES_H

#include "tc_css_syntax.h"

typedef enum tc_unit {
    TC_UNIT_NUMBER,     /* no unit: a <number> */
    TC_UNIT_PERCENT,
    TC_UNIT_PX,
    TC_UNIT_EM,
    TC_UNIT_REM,
    TC_UNIT_EX,
    TC_UNIT_CH,
    TC_UNIT_VW,
    TC_UNIT_VH,
    TC_UNIT_VMIN,
    TC_UNIT_VMAX,
    TC_UNIT_CM,
    TC_UNIT_MM,
    TC_UNIT_Q,
    TC_UNIT_IN,
    TC_UNIT_PT,
    TC_UNIT_PC,
    TC_UNIT_DEG,        /* angles, for completeness in calc() */
    TC_UNIT_S,          /* times */
    TC_UNIT_UNKNOWN
} tc_unit;

/* A math expression tree (calc() and friends), or a single value. */
typedef enum tc_calc_op {
    TC_CALC_VALUE,
    TC_CALC_ADD,
    TC_CALC_SUB,
    TC_CALC_MUL,
    TC_CALC_DIV,
    TC_CALC_MIN,       /* over a list */
    TC_CALC_MAX,
    TC_CALC_CLAMP      /* min, value, max */
} tc_calc_op;

typedef struct tc_calc tc_calc;
struct tc_calc {
    uint8_t op;         /* tc_calc_op */
    uint8_t unit;       /* TC_CALC_VALUE: tc_unit */
    float value;
    tc_calc **args;     /* the operands */
    uint32_t nargs;
};

/* What relative units are relative to. */
typedef struct tc_length_basis {
    float font_size;    /* em */
    float root_font_size;
    float viewport_w, viewport_h;
} tc_length_basis;

/* A computed length-percentage: px + pct% of a basis known at layout time,
   or an expression (min/max/clamp mixing pixels and percentages). */
typedef struct tc_length {
    float px;
    float pct;
    const tc_calc *expr; /* if set, evaluate at layout time; px and pct unused */
} tc_length;

/* Parse one length, percentage or number (or a math function of them).
   allow: bit (1 << unit kind) set; use the TC_ALLOW_* masks. Unitless zero
   counts as a length. Returns NULL if it isn't one. */
#define TC_ALLOW_LENGTH  1u
#define TC_ALLOW_PERCENT 2u
#define TC_ALLOW_NUMBER  4u
const tc_calc *tc_css_parse_calc(tc_arena *a, const tc_css_value *v, unsigned allow);

/* Whether the expression is (or contains) a percentage. */
int tc_calc_has_percent(const tc_calc *c);

/* Compute: relative units resolved; a percentage stays a percentage. */
int tc_calc_compute(tc_arena *a, const tc_calc *c, const tc_length_basis *b, tc_length *out);

/* Resolve a computed length against the percentage basis. */
float tc_length_resolve(const tc_length *len, float basis);

/* A number expression's value (unitless calc()s). */
float tc_calc_number(const tc_calc *c);

/* ---- Media queries ---- */

typedef struct tc_media_env {
    float width, height;   /* the viewport, in CSS pixels */
    float device_width, device_height;
    int color_bits;        /* bits per colour component */
} tc_media_env;

typedef struct tc_media_query tc_media_query;

/* A media query list; NULL means "all". */
typedef struct tc_media_list {
    tc_media_query *queries;
    uint32_t count;
} tc_media_list;

/* Parse a media query list (from @media's prelude, a media attribute...).
   Invalid queries become "not all", as the spec says. */
int tc_media_parse(tc_arena *a, const tc_css_value *v, uint32_t n, tc_media_list **out);
int tc_media_parse_text(tc_arena *a, const char *text, tc_media_list **out);
int tc_media_matches(const tc_media_list *list, const tc_media_env *env);

#endif
