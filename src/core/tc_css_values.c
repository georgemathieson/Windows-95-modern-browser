#include "tc_css_values.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- Units ---- */

static const struct {
    const char *name;
    uint8_t unit;
} units[] = {
    { "px", TC_UNIT_PX }, { "em", TC_UNIT_EM }, { "rem", TC_UNIT_REM }, { "ex", TC_UNIT_EX },
    { "ch", TC_UNIT_CH }, { "vw", TC_UNIT_VW }, { "vh", TC_UNIT_VH }, { "vmin", TC_UNIT_VMIN },
    { "vmax", TC_UNIT_VMAX }, { "svw", TC_UNIT_VW }, { "svh", TC_UNIT_VH }, { "lvw", TC_UNIT_VW },
    { "lvh", TC_UNIT_VH }, { "dvw", TC_UNIT_VW }, { "dvh", TC_UNIT_VH }, { "cm", TC_UNIT_CM },
    { "mm", TC_UNIT_MM }, { "q", TC_UNIT_Q }, { "in", TC_UNIT_IN }, { "pt", TC_UNIT_PT },
    { "pc", TC_UNIT_PC }, { "deg", TC_UNIT_DEG }, { "s", TC_UNIT_S },
};

static int unit_of(const char *name)
{
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(units); i++)
        if (tc_css_ascii_ieq(name, units[i].name))
            return units[i].unit;
    return TC_UNIT_UNKNOWN;
}

/* ---- Math expressions ---- */

/* The kind of value an expression produces. */
enum { K_INVALID, K_NUMBER, K_LENGTH, K_PERCENT, K_LENGTH_PERCENT, K_OTHER };

typedef struct cparser {
    tc_arena *arena;
    const tc_css_value *v;
    uint32_t n, i;
    int depth;
} cparser;

static tc_calc *leaf(tc_arena *a, int unit, float value)
{
    tc_calc *c = tc_arena_alloc(a, sizeof *c);
    if (c) {
        memset(c, 0, sizeof *c);
        c->op = TC_CALC_VALUE;
        c->unit = (uint8_t)unit;
        c->value = value;
    }
    return c;
}

static tc_calc *node(tc_arena *a, int op, tc_calc **args, uint32_t n)
{
    tc_calc *c = tc_arena_alloc(a, sizeof *c);
    tc_calc **copy = tc_arena_alloc(a, n * sizeof *copy);
    if (!c || !copy)
        return NULL;
    memset(c, 0, sizeof *c);
    memcpy(copy, args, n * sizeof *copy);
    c->op = (uint8_t)op;
    c->args = copy;
    c->nargs = n;
    return c;
}

static int kind_of_unit(int unit)
{
    if (unit == TC_UNIT_NUMBER)
        return K_NUMBER;
    if (unit == TC_UNIT_PERCENT)
        return K_PERCENT;
    if (unit >= TC_UNIT_PX && unit <= TC_UNIT_PC)
        return K_LENGTH;
    return K_OTHER;
}

static int add_kinds(int a, int b)
{
    if (a == K_INVALID || b == K_INVALID)
        return K_INVALID;
    if (a == b)
        return a;
    if ((a == K_LENGTH || a == K_PERCENT || a == K_LENGTH_PERCENT) &&
        (b == K_LENGTH || b == K_PERCENT || b == K_LENGTH_PERCENT))
        return K_LENGTH_PERCENT;
    return K_INVALID;
}

static void cskip_ws(cparser *p)
{
    while (p->i < p->n && p->v[p->i].type == TC_CSS_WHITESPACE)
        p->i++;
}

static tc_calc *parse_sum(cparser *p, int *kind);

static tc_calc *parse_math_function(tc_arena *a, const tc_css_value *f, int depth, int *kind);

static tc_calc *parse_value(cparser *p, int *kind)
{
    const tc_css_value *v;
    cskip_ws(p);
    if (p->i >= p->n) {
        *kind = K_INVALID;
        return NULL;
    }
    v = &p->v[p->i++];
    switch (v->type) {
    case TC_CSS_NUMBER:
        *kind = K_NUMBER;
        return leaf(p->arena, TC_UNIT_NUMBER, (float)v->number);
    case TC_CSS_PERCENTAGE:
        *kind = K_PERCENT;
        return leaf(p->arena, TC_UNIT_PERCENT, (float)v->number);
    case TC_CSS_DIMENSION: {
        int unit = unit_of(v->text);
        *kind = unit == TC_UNIT_UNKNOWN ? K_INVALID : kind_of_unit(unit);
        return *kind == K_INVALID ? NULL : leaf(p->arena, unit, (float)v->number);
    }
    case TC_CSS_IDENT:
        *kind = K_NUMBER;
        if (tc_css_is_ident(v, "pi"))
            return leaf(p->arena, TC_UNIT_NUMBER, 3.14159265f);
        if (tc_css_is_ident(v, "e"))
            return leaf(p->arena, TC_UNIT_NUMBER, 2.71828183f);
        *kind = K_INVALID;
        return NULL;
    case TC_CSS_BLOCK:
        if (v->cp == '(' && p->depth < 32) {
            cparser q = { p->arena, v->items, v->nitems, 0, p->depth + 1 };
            tc_calc *c = parse_sum(&q, kind);
            cskip_ws(&q);
            if (q.i != q.n)
                *kind = K_INVALID;
            return *kind == K_INVALID ? NULL : c;
        }
        break;
    case TC_CSS_FUNCTION:
        if (p->depth < 32)
            return parse_math_function(p->arena, v, p->depth + 1, kind);
        break;
    default:
        break;
    }
    *kind = K_INVALID;
    return NULL;
}

static tc_calc *parse_product(cparser *p, int *kind)
{
    tc_calc *left = parse_value(p, kind);
    for (;;) {
        uint32_t save = p->i;
        const tc_css_value *v;
        int op, rkind;
        tc_calc *right, *args[2];
        if (*kind == K_INVALID)
            return NULL;
        cskip_ws(p);
        v = p->i < p->n ? &p->v[p->i] : NULL;
        if (!tc_css_is_delim(v, '*') && !tc_css_is_delim(v, '/')) {
            p->i = save;
            return left;
        }
        op = v->cp == '*' ? TC_CALC_MUL : TC_CALC_DIV;
        p->i++;
        right = parse_value(p, &rkind);
        if (rkind == K_INVALID) {
            *kind = K_INVALID;
            return NULL;
        }
        /* One side of * must be a number; the right side of / must be. */
        if (op == TC_CALC_MUL) {
            if (*kind != K_NUMBER && rkind != K_NUMBER) {
                *kind = K_INVALID;
                return NULL;
            }
            if (*kind == K_NUMBER)
                *kind = rkind;
        } else if (rkind != K_NUMBER) {
            *kind = K_INVALID;
            return NULL;
        }
        args[0] = left;
        args[1] = right;
        left = node(p->arena, op, args, 2);
        if (!left) {
            *kind = K_INVALID;
            return NULL;
        }
    }
}

static tc_calc *parse_sum(cparser *p, int *kind)
{
    tc_calc *left = parse_product(p, kind);
    for (;;) {
        uint32_t save = p->i;
        const tc_css_value *v;
        int op, rkind;
        tc_calc *right, *args[2];
        if (*kind == K_INVALID)
            return NULL;
        /* + and - need whitespace on both sides. */
        if (p->i >= p->n || p->v[p->i].type != TC_CSS_WHITESPACE)
            return left;
        cskip_ws(p);
        v = p->i < p->n ? &p->v[p->i] : NULL;
        if (!tc_css_is_delim(v, '+') && !tc_css_is_delim(v, '-')) {
            p->i = save;
            return left;
        }
        op = v->cp == '+' ? TC_CALC_ADD : TC_CALC_SUB;
        p->i++;
        if (p->i >= p->n || p->v[p->i].type != TC_CSS_WHITESPACE) {
            *kind = K_INVALID;
            return NULL;
        }
        right = parse_product(p, &rkind);
        *kind = add_kinds(*kind, rkind);
        if (*kind == K_INVALID)
            return NULL;
        args[0] = left;
        args[1] = right;
        left = node(p->arena, op, args, 2);
        if (!left) {
            *kind = K_INVALID;
            return NULL;
        }
    }
}

/* calc(), min(), max(), clamp(). */
static tc_calc *parse_math_function(tc_arena *a, const tc_css_value *f, int depth, int *kind)
{
    tc_calc *args[32];
    uint32_t nargs = 0, start = 0, i;
    int op;

    *kind = K_INVALID;
    if (tc_css_ascii_ieq(f->text, "calc") || tc_css_ascii_ieq(f->text, "-webkit-calc")) {
        cparser p = { a, f->items, f->nitems, 0, depth };
        tc_calc *c = parse_sum(&p, kind);
        cskip_ws(&p);
        if (p.i != p.n)
            *kind = K_INVALID;
        return *kind == K_INVALID ? NULL : c;
    }
    if (tc_css_ascii_ieq(f->text, "min"))
        op = TC_CALC_MIN;
    else if (tc_css_ascii_ieq(f->text, "max"))
        op = TC_CALC_MAX;
    else if (tc_css_ascii_ieq(f->text, "clamp"))
        op = TC_CALC_CLAMP;
    else
        return NULL;
    for (i = 0; i <= f->nitems; i++) {
        cparser p;
        int k;
        tc_calc *c;
        if (i < f->nitems && f->items[i].type != TC_CSS_COMMA)
            continue;
        if (nargs == TC_ARRAY_LEN(args)) {
            *kind = K_INVALID;
            return NULL;
        }
        p.arena = a;
        p.v = f->items + start;
        p.n = i - start;
        p.i = 0;
        p.depth = depth;
        start = i + 1;
        c = parse_sum(&p, &k);
        cskip_ws(&p);
        if (k == K_INVALID || p.i != p.n) {
            *kind = K_INVALID;
            return NULL;
        }
        *kind = nargs ? add_kinds(*kind, k) : k;
        if (*kind == K_INVALID)
            return NULL;
        args[nargs++] = c;
    }
    if (nargs == 0 || (op == TC_CALC_CLAMP && nargs != 3)) {
        *kind = K_INVALID;
        return NULL;
    }
    return node(a, op, args, nargs);
}

const tc_calc *tc_css_parse_calc(tc_arena *a, const tc_css_value *v, unsigned allow)
{
    int kind;
    const tc_calc *c;

    if (!v)
        return NULL;
    switch (v->type) {
    case TC_CSS_NUMBER:
        if (v->number == 0 && (allow & TC_ALLOW_LENGTH) && !(allow & TC_ALLOW_NUMBER))
            return leaf(a, TC_UNIT_PX, 0);
        return (allow & TC_ALLOW_NUMBER) ? leaf(a, TC_UNIT_NUMBER, (float)v->number) : NULL;
    case TC_CSS_PERCENTAGE:
        return (allow & TC_ALLOW_PERCENT) ? leaf(a, TC_UNIT_PERCENT, (float)v->number) : NULL;
    case TC_CSS_DIMENSION: {
        int unit = unit_of(v->text);
        if (unit < TC_UNIT_PX || unit > TC_UNIT_PC || !(allow & TC_ALLOW_LENGTH))
            return NULL;
        return leaf(a, unit, (float)v->number);
    }
    case TC_CSS_FUNCTION:
        c = parse_math_function(a, v, 0, &kind);
        if (!c)
            return NULL;
        if ((kind == K_NUMBER && (allow & TC_ALLOW_NUMBER)) || (kind == K_LENGTH && (allow & TC_ALLOW_LENGTH)) ||
            (kind == K_PERCENT && (allow & TC_ALLOW_PERCENT)) ||
            (kind == K_LENGTH_PERCENT && (allow & TC_ALLOW_LENGTH) && (allow & TC_ALLOW_PERCENT)))
            return c;
        return NULL;
    default:
        return NULL;
    }
}

int tc_calc_has_percent(const tc_calc *c)
{
    uint32_t i;
    if (c->op == TC_CALC_VALUE)
        return c->unit == TC_UNIT_PERCENT;
    for (i = 0; i < c->nargs; i++)
        if (tc_calc_has_percent(c->args[i]))
            return 1;
    return 0;
}

static float unit_px(int unit, float v, const tc_length_basis *b)
{
    switch (unit) {
    case TC_UNIT_PX: return v;
    case TC_UNIT_EM: return v * b->font_size;
    case TC_UNIT_REM: return v * b->root_font_size;
    case TC_UNIT_EX: return v * b->font_size * 0.5f;
    case TC_UNIT_CH: return v * b->font_size * 0.55f;
    case TC_UNIT_VW: return v * b->viewport_w / 100;
    case TC_UNIT_VH: return v * b->viewport_h / 100;
    case TC_UNIT_VMIN: return v * (b->viewport_w < b->viewport_h ? b->viewport_w : b->viewport_h) / 100;
    case TC_UNIT_VMAX: return v * (b->viewport_w > b->viewport_h ? b->viewport_w : b->viewport_h) / 100;
    case TC_UNIT_CM: return v * 96 / 2.54f;
    case TC_UNIT_MM: return v * 96 / 25.4f;
    case TC_UNIT_Q: return v * 96 / 101.6f;
    case TC_UNIT_IN: return v * 96;
    case TC_UNIT_PT: return v * 96 / 72;
    case TC_UNIT_PC: return v * 16;
    default: return v;
    }
}

/* Linear evaluation: px + pct%, for trees without min/max/clamp. */
static int linear(const tc_calc *c, const tc_length_basis *b, float *px, float *pct)
{
    float lp, lq, rp, rq;
    if (c->op == TC_CALC_VALUE) {
        if (c->unit == TC_UNIT_PERCENT) {
            *px = 0;
            *pct = c->value;
        } else {
            *px = unit_px(c->unit, c->value, b);
            *pct = 0;
        }
        return 1;
    }
    if (c->op > TC_CALC_DIV || c->nargs != 2)
        return 0;
    if (!linear(c->args[0], b, &lp, &lq) || !linear(c->args[1], b, &rp, &rq))
        return 0;
    switch (c->op) {
    case TC_CALC_ADD: *px = lp + rp; *pct = lq + rq; return 1;
    case TC_CALC_SUB: *px = lp - rp; *pct = lq - rq; return 1;
    case TC_CALC_MUL:
        /* One side is a plain number: its px part holds the value. */
        if (c->args[0]->op == TC_CALC_VALUE && c->args[0]->unit == TC_UNIT_NUMBER) {
            *px = rp * lp;
            *pct = rq * lp;
        } else if (c->args[1]->op == TC_CALC_VALUE && c->args[1]->unit == TC_UNIT_NUMBER) {
            *px = lp * rp;
            *pct = lq * rp;
        } else {
            float f = tc_calc_number(c->args[0]) ? tc_calc_number(c->args[0]) : 0;
            if (!tc_calc_has_percent(c->args[1]) && rq == 0 && lq == 0) {
                *px = lp * rp;
                *pct = 0;
            } else {
                *px = rp * f;
                *pct = rq * f;
            }
        }
        return 1;
    case TC_CALC_DIV:
        if (rp == 0)
            return 0;
        *px = lp / rp;
        *pct = lq / rp;
        return 1;
    }
    return 0;
}

float tc_calc_number(const tc_calc *c)
{
    uint32_t i;
    float v;
    switch (c->op) {
    case TC_CALC_VALUE: return c->value;
    case TC_CALC_ADD: return tc_calc_number(c->args[0]) + tc_calc_number(c->args[1]);
    case TC_CALC_SUB: return tc_calc_number(c->args[0]) - tc_calc_number(c->args[1]);
    case TC_CALC_MUL: return tc_calc_number(c->args[0]) * tc_calc_number(c->args[1]);
    case TC_CALC_DIV: v = tc_calc_number(c->args[1]); return v ? tc_calc_number(c->args[0]) / v : 0;
    case TC_CALC_MIN:
    case TC_CALC_MAX:
        v = tc_calc_number(c->args[0]);
        for (i = 1; i < c->nargs; i++) {
            float x = tc_calc_number(c->args[i]);
            v = c->op == TC_CALC_MIN ? (x < v ? x : v) : (x > v ? x : v);
        }
        return v;
    case TC_CALC_CLAMP: {
        float lo = tc_calc_number(c->args[0]), mid = tc_calc_number(c->args[1]), hi = tc_calc_number(c->args[2]);
        v = mid < hi ? mid : hi;
        return v > lo ? v : lo;
    }
    }
    return 0;
}

/* A copy of c with relative units turned into px (percentages kept). */
static tc_calc *absolutize(tc_arena *a, const tc_calc *c, const tc_length_basis *b)
{
    tc_calc *out;
    uint32_t i;
    if (c->op == TC_CALC_VALUE)
        return c->unit == TC_UNIT_PERCENT || c->unit == TC_UNIT_NUMBER ? leaf(a, c->unit, c->value)
                                                                         : leaf(a, TC_UNIT_PX, unit_px(c->unit, c->value, b));
    out = tc_arena_alloc(a, sizeof *out);
    if (!out)
        return NULL;
    *out = *c;
    out->args = tc_arena_alloc(a, c->nargs * sizeof *out->args);
    if (!out->args)
        return NULL;
    for (i = 0; i < c->nargs; i++)
        if (!(out->args[i] = absolutize(a, c->args[i], b)))
            return NULL;
    return out;
}

/* Evaluate an absolutized tree with percentages of basis. */
static float evaluate(const tc_calc *c, float basis)
{
    uint32_t i;
    float v;
    switch (c->op) {
    case TC_CALC_VALUE: return c->unit == TC_UNIT_PERCENT ? c->value * basis / 100 : c->value;
    case TC_CALC_ADD: return evaluate(c->args[0], basis) + evaluate(c->args[1], basis);
    case TC_CALC_SUB: return evaluate(c->args[0], basis) - evaluate(c->args[1], basis);
    case TC_CALC_MUL: return evaluate(c->args[0], basis) * evaluate(c->args[1], basis);
    case TC_CALC_DIV: v = evaluate(c->args[1], basis); return v ? evaluate(c->args[0], basis) / v : 0;
    case TC_CALC_MIN:
    case TC_CALC_MAX:
        v = evaluate(c->args[0], basis);
        for (i = 1; i < c->nargs; i++) {
            float x = evaluate(c->args[i], basis);
            v = c->op == TC_CALC_MIN ? (x < v ? x : v) : (x > v ? x : v);
        }
        return v;
    case TC_CALC_CLAMP: {
        float lo = evaluate(c->args[0], basis), mid = evaluate(c->args[1], basis), hi = evaluate(c->args[2], basis);
        v = mid < hi ? mid : hi;
        return v > lo ? v : lo;
    }
    }
    return 0;
}

int tc_calc_compute(tc_arena *a, const tc_calc *c, const tc_length_basis *b, tc_length *out)
{
    memset(out, 0, sizeof *out);
    if (linear(c, b, &out->px, &out->pct))
        return TC_OK;
    if (!tc_calc_has_percent(c)) {
        tc_calc *abs = absolutize(a, c, b);
        if (!abs)
            return TC_ERR_NOMEM;
        out->px = evaluate(abs, 0);
        return TC_OK;
    }
    out->expr = absolutize(a, c, b);
    return out->expr ? TC_OK : TC_ERR_NOMEM;
}

float tc_length_resolve(const tc_length *len, float basis)
{
    if (len->expr)
        return evaluate(len->expr, basis);
    return len->px + len->pct * basis / 100;
}

/* ---- Media queries (Media Queries 4) ---- */

enum {
    MQ_TRUE,
    MQ_FALSE,     /* an unknown feature, a media type that isn't screen... */
    MQ_NOT,
    MQ_AND,
    MQ_OR,
    MQ_FEATURE
};

/* Comparison bits for range features. */
#define CMP_LT 1
#define CMP_EQ 2
#define CMP_GT 4

typedef struct mq_node mq_node;
struct mq_node {
    uint8_t kind;
    uint8_t feature;     /* MQ_FEATURE: one of the F_* below */
    uint8_t cmp;         /* how the feature compares with value: CMP_* bits */
    uint8_t boolean;     /* (feature) with no value */
    float value;         /* px, a ratio, dppx, or a keyword's index */
    mq_node **kids;
    uint32_t nkids;
};

struct tc_media_query {
    const mq_node *root;
};

enum {
    F_WIDTH, F_HEIGHT, F_ASPECT_RATIO, F_DEVICE_WIDTH, F_DEVICE_HEIGHT, F_DEVICE_ASPECT_RATIO, F_RESOLUTION,
    F_COLOR, F_COLOR_INDEX, F_MONOCHROME, F_GRID, F_ORIENTATION, F_HOVER, F_POINTER, F_PREFERS_COLOR_SCHEME,
    F_PREFERS_REDUCED_MOTION, F_PREFERS_CONTRAST, F_FORCED_COLORS, F_SCRIPTING, F_UPDATE, F_DISPLAY_MODE,
    F_DYNAMIC_RANGE, F_COLOR_GAMUT, F_OVERFLOW, F_INVERTED_COLORS, F_PREFERS_REDUCED_TRANSPARENCY
};

/* Range features take min-/max- prefixes; the rest take keywords. */
static const struct {
    const char *name;
    uint8_t id;
    uint8_t range;
    const char *keywords; /* discrete: the values, "|"-separated; ours is the first */
} features[] = {
    { "width", F_WIDTH, 1, NULL }, { "height", F_HEIGHT, 1, NULL }, { "aspect-ratio", F_ASPECT_RATIO, 1, NULL },
    { "device-width", F_DEVICE_WIDTH, 1, NULL }, { "device-height", F_DEVICE_HEIGHT, 1, NULL },
    { "device-aspect-ratio", F_DEVICE_ASPECT_RATIO, 1, NULL }, { "resolution", F_RESOLUTION, 1, NULL },
    { "-webkit-device-pixel-ratio", F_RESOLUTION, 1, NULL }, { "color", F_COLOR, 1, NULL },
    { "color-index", F_COLOR_INDEX, 1, NULL }, { "monochrome", F_MONOCHROME, 1, NULL },
    { "grid", F_GRID, 0, "0|1" }, { "orientation", F_ORIENTATION, 0, "landscape|portrait" },
    { "hover", F_HOVER, 0, "hover|none" }, { "any-hover", F_HOVER, 0, "hover|none" },
    { "pointer", F_POINTER, 0, "fine|coarse|none" }, { "any-pointer", F_POINTER, 0, "fine|coarse|none" },
    { "prefers-color-scheme", F_PREFERS_COLOR_SCHEME, 0, "light|dark" },
    { "prefers-reduced-motion", F_PREFERS_REDUCED_MOTION, 0, "no-preference|reduce" },
    { "prefers-contrast", F_PREFERS_CONTRAST, 0, "no-preference|more|less|custom" },
    { "forced-colors", F_FORCED_COLORS, 0, "none|active" }, { "scripting", F_SCRIPTING, 0, "none|initial-only|enabled" },
    { "update", F_UPDATE, 0, "fast|slow|none" }, { "display-mode", F_DISPLAY_MODE, 0, "browser|fullscreen|standalone|minimal-ui|picture-in-picture|window-controls-overlay" },
    { "dynamic-range", F_DYNAMIC_RANGE, 0, "standard|high" }, { "color-gamut", F_COLOR_GAMUT, 0, "srgb|p3|rec2020" },
    { "overflow-block", F_OVERFLOW, 0, "scroll|none|paged|optional-paged" },
    { "overflow-inline", F_OVERFLOW, 0, "scroll|none" }, { "inverted-colors", F_INVERTED_COLORS, 0, "none|inverted" },
    { "prefers-reduced-transparency", F_PREFERS_REDUCED_TRANSPARENCY, 0, "no-preference|reduce" },
};

static int keyword_index(const char *list, const char *word)
{
    int index = 0;
    size_t n = strlen(word);
    while (*list) {
        const char *bar = strchr(list, '|');
        size_t len = bar ? (size_t)(bar - list) : strlen(list);
        if (len == n) {
            size_t i;
            for (i = 0; i < n; i++)
                if ((word[i] | 0x20) != list[i] && word[i] != list[i])
                    break;
            if (i == n)
                return index;
        }
        index++;
        if (!bar)
            break;
        list = bar + 1;
    }
    return -1;
}

typedef struct mparser {
    tc_arena *arena;
    const tc_css_value *v;
    uint32_t n, i;
    int depth;
} mparser;

static void mskip(mparser *p)
{
    while (p->i < p->n && p->v[p->i].type == TC_CSS_WHITESPACE)
        p->i++;
}

static mq_node *mq_new(tc_arena *a, int kind)
{
    mq_node *m = tc_arena_alloc(a, sizeof *m);
    if (m) {
        memset(m, 0, sizeof *m);
        m->kind = (uint8_t)kind;
    }
    return m;
}

static mq_node *mq_combine(tc_arena *a, int kind, mq_node **kids, uint32_t n)
{
    mq_node *m = mq_new(a, kind);
    if (!m)
        return NULL;
    m->kids = tc_arena_alloc(a, n * sizeof *m->kids);
    if (!m->kids)
        return NULL;
    memcpy(m->kids, kids, n * sizeof *kids);
    m->nkids = n;
    return m;
}

/* A feature's value at t[i] as a number: px for lengths, a ratio ("16/9"),
   dppx for resolutions. Sets *used to the tokens it took. */
static int feature_value(const tc_css_value *const *t, uint32_t n, uint32_t i, uint32_t *used, float *out)
{
    static const tc_length_basis basis = { 16, 16, 0, 0 };
    const tc_css_value *v;

    if (i >= n)
        return 0;
    v = t[i];
    *used = 1;
    if (v->type == TC_CSS_NUMBER) {
        if (i + 2 < n && tc_css_is_delim(t[i + 1], '/') && t[i + 2]->type == TC_CSS_NUMBER && t[i + 2]->number != 0) {
            *out = (float)(v->number / t[i + 2]->number);
            *used = 3;
            return 1;
        }
        *out = (float)v->number;
        return 1;
    }
    if (v->type != TC_CSS_DIMENSION)
        return 0;
    if (tc_css_ascii_ieq(v->text, "dpi")) {
        *out = (float)(v->number / 96);
        return 1;
    }
    if (tc_css_ascii_ieq(v->text, "dpcm")) {
        *out = (float)(v->number * 2.54 / 96);
        return 1;
    }
    if (tc_css_ascii_ieq(v->text, "dppx") || tc_css_ascii_ieq(v->text, "x")) {
        *out = (float)v->number;
        return 1;
    }
    {
        int unit = unit_of(v->text);
        if (unit >= TC_UNIT_PX && unit <= TC_UNIT_PC) {
            *out = unit_px(unit, (float)v->number, &basis);
            return 1;
        }
    }
    return 0;
}

/* The tokens of a parenthesised feature, without whitespace. */
static uint32_t compact(const tc_css_value *in, uint32_t n, const tc_css_value **out, uint32_t max)
{
    uint32_t i, k = 0;
    for (i = 0; i < n; i++) {
        if (in[i].type == TC_CSS_WHITESPACE)
            continue;
        if (k == max)
            return max + 1;
        out[k++] = &in[i];
    }
    return k;
}

static int comparison(const tc_css_value *const *t, uint32_t n, uint32_t i, int *cmp, uint32_t *used)
{
    /* '<', '<=', '>', '>=', '=' are delims; "<=" is '<' then '='. */
    if (i >= n || t[i]->type != TC_CSS_DELIM)
        return 0;
    if (t[i]->cp == '=') {
        *cmp = CMP_EQ;
        *used = 1;
        return 1;
    }
    if (t[i]->cp != '<' && t[i]->cp != '>')
        return 0;
    *cmp = t[i]->cp == '<' ? CMP_LT : CMP_GT;
    *used = 1;
    if (i + 1 < n && tc_css_is_delim(t[i + 1], '=')) {
        *cmp |= CMP_EQ;
        *used = 2;
    }
    return 1;
}

static int find_feature(const char *name, int *range, const char **keywords)
{
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(features); i++)
        if (tc_css_ascii_ieq(name, features[i].name)) {
            *range = features[i].range;
            *keywords = features[i].keywords;
            return features[i].id;
        }
    return -1;
}

static int flip(int cmp)
{
    return (cmp & CMP_EQ) | ((cmp & CMP_LT) ? CMP_GT : 0) | ((cmp & CMP_GT) ? CMP_LT : 0);
}

static mq_node *feature_node(tc_arena *a, int id, int cmp, float value)
{
    mq_node *m = mq_new(a, MQ_FEATURE);
    if (m) {
        m->feature = (uint8_t)id;
        m->cmp = (uint8_t)cmp;
        m->value = value;
    }
    return m;
}

/* The inside of "( ... )" that isn't a nested condition: a feature. */
static mq_node *parse_feature(mparser *p, const tc_css_value *block)
{
    const tc_css_value *t[16];
    uint32_t n = compact(block->items, block->nitems, t, 15), used, used2;
    int id, range, cmp, cmp2;
    const char *keywords, *name;
    float value, value2;
    mq_node *m;

    if (n == 0 || n > 15)
        return mq_new(p->arena, MQ_FALSE);
    /* (name) */
    if (n == 1 && t[0]->type == TC_CSS_IDENT) {
        id = find_feature(t[0]->text, &range, &keywords);
        if (id < 0)
            return mq_new(p->arena, MQ_FALSE);
        m = feature_node(p->arena, id, 0, 0);
        if (m)
            m->boolean = 1;
        return m;
    }
    /* (name: value), (min-name: value) */
    if (n >= 3 && t[0]->type == TC_CSS_IDENT && t[1]->type == TC_CSS_COLON) {
        int prefix = 0;
        name = t[0]->text;
        if (strncmp(name, "-webkit-", 8) == 0 && (strncmp(name + 8, "min-", 4) == 0 || strncmp(name + 8, "max-", 4) == 0))
            name += 8;
        if (strncmp(name, "min-", 4) == 0 || strncmp(name, "max-", 4) == 0) {
            prefix = name[1] == 'i' ? CMP_GT | CMP_EQ : CMP_LT | CMP_EQ;
            name += 4;
            if (tc_css_ascii_ieq(name, "device-pixel-ratio"))
                name = "resolution";
        }
        id = find_feature(name, &range, &keywords);
        if (id < 0 || (prefix && !range))
            return mq_new(p->arena, MQ_FALSE);
        if (range) {
            if (!feature_value(t, n, 2, &used, &value) || 2 + used != n)
                return mq_new(p->arena, MQ_FALSE);
            return feature_node(p->arena, id, prefix ? prefix : CMP_EQ, value);
        }
        if (n != 3 || (t[2]->type != TC_CSS_IDENT && t[2]->type != TC_CSS_NUMBER))
            return mq_new(p->arena, MQ_FALSE);
        {
            const char *word = t[2]->type == TC_CSS_IDENT ? t[2]->text : (t[2]->number == 0 ? "0" : "1");
            int k = keyword_index(keywords, word);
            return k < 0 ? mq_new(p->arena, MQ_FALSE) : feature_node(p->arena, id, CMP_EQ, (float)k);
        }
    }
    /* (name op value) */
    if (t[0]->type == TC_CSS_IDENT && comparison(t, n, 1, &cmp, &used)) {
        id = find_feature(t[0]->text, &range, &keywords);
        if (id < 0 || !range || !feature_value(t, n, 1 + used, &used2, &value) || 1 + used + used2 != n)
            return mq_new(p->arena, MQ_FALSE);
        return feature_node(p->arena, id, cmp, value);
    }
    /* (value op name) and (value op name op value) */
    if (!feature_value(t, n, 0, &used, &value) || !comparison(t, n, used, &cmp, &used2))
        return mq_new(p->arena, MQ_FALSE);
    {
        uint32_t i = used + used2;
        mq_node *first, *second, *kids[2];
        if (i >= n || t[i]->type != TC_CSS_IDENT)
            return mq_new(p->arena, MQ_FALSE);
        id = find_feature(t[i]->text, &range, &keywords);
        if (id < 0 || !range)
            return mq_new(p->arena, MQ_FALSE);
        first = feature_node(p->arena, id, flip(cmp), value);
        i++;
        if (i == n || !first)
            return first;
        if (!comparison(t, n, i, &cmp2, &used) || !feature_value(t, n, i + used, &used2, &value2) ||
            i + used + used2 != n)
            return mq_new(p->arena, MQ_FALSE);
        second = feature_node(p->arena, id, cmp2, value2);
        if (!second)
            return NULL;
        kids[0] = first;
        kids[1] = second;
        return mq_combine(p->arena, MQ_AND, kids, 2);
    }
}

static mq_node *parse_condition(mparser *p, int allow_or);

/* "( condition )", "( feature )" or anything else in parentheses (unknown). */
static mq_node *parse_in_parens(mparser *p)
{
    const tc_css_value *v;
    mskip(p);
    if (p->i >= p->n)
        return NULL;
    v = &p->v[p->i];
    if (v->type == TC_CSS_FUNCTION) {
        p->i++;
        return mq_new(p->arena, MQ_FALSE); /* general-enclosed */
    }
    if (v->type != TC_CSS_BLOCK || v->cp != '(')
        return NULL;
    p->i++;
    {
        /* A nested condition starts with "not", "(" or a function. */
        uint32_t k = 0;
        while (k < v->nitems && v->items[k].type == TC_CSS_WHITESPACE)
            k++;
        if (k < v->nitems && p->depth < 16 &&
            ((v->items[k].type == TC_CSS_BLOCK && v->items[k].cp == '(') || tc_css_is_ident(&v->items[k], "not"))) {
            mparser q = { p->arena, v->items, v->nitems, 0, p->depth + 1 };
            mq_node *m = parse_condition(&q, 1);
            mskip(&q);
            return m && q.i == q.n ? m : mq_new(p->arena, MQ_FALSE);
        }
    }
    return parse_feature(p, v);
}

static mq_node *parse_condition(mparser *p, int allow_or)
{
    mq_node *kids[32];
    uint32_t n = 0;
    int kind = 0;

    mskip(p);
    if (p->i < p->n && tc_css_is_ident(&p->v[p->i], "not")) {
        mq_node *inner;
        p->i++;
        inner = parse_in_parens(p);
        return inner ? mq_combine(p->arena, MQ_NOT, &inner, 1) : NULL;
    }
    kids[n] = parse_in_parens(p);
    if (!kids[n++])
        return NULL;
    for (;;) {
        uint32_t save = p->i;
        int this_kind;
        mskip(p);
        if (p->i >= p->n)
            break;
        if (tc_css_is_ident(&p->v[p->i], "and"))
            this_kind = MQ_AND;
        else if (allow_or && tc_css_is_ident(&p->v[p->i], "or"))
            this_kind = MQ_OR;
        else {
            p->i = save;
            break;
        }
        if (kind && kind != this_kind)
            return NULL; /* "and" and "or" can't mix without parentheses */
        kind = this_kind;
        p->i++;
        if (n == TC_ARRAY_LEN(kids) || !(kids[n++] = parse_in_parens(p)))
            return NULL;
    }
    return n == 1 ? kids[0] : mq_combine(p->arena, kind, kids, n);
}

static const char *const media_types[] = { "all", "screen", "print", "speech", "tty", "tv", "projection",
                                           "handheld", "braille", "embossed", "aural" };

static mq_node *parse_query(mparser *p)
{
    const tc_css_value *v;
    int negate = 0;
    mq_node *type = NULL, *cond;

    mskip(p);
    if (p->i >= p->n)
        return NULL;
    v = &p->v[p->i];
    if (v->type == TC_CSS_IDENT && !tc_css_is_ident(v, "not") && !tc_css_is_ident(v, "only")) {
        /* fall through to a media type */
    } else if (tc_css_is_ident(v, "not") || tc_css_is_ident(v, "only")) {
        uint32_t k = p->i + 1;
        while (k < p->n && p->v[k].type == TC_CSS_WHITESPACE)
            k++;
        if (k < p->n && p->v[k].type == TC_CSS_IDENT) {
            negate = tc_css_is_ident(v, "not");
            p->i = k;
            v = &p->v[p->i];
        } else {
            return parse_condition(p, 1); /* "not (...)" */
        }
    } else {
        return parse_condition(p, 1);
    }
    {
        size_t i;
        int known = 0, matches = 0;
        if (tc_css_is_ident(v, "and") || tc_css_is_ident(v, "or") || tc_css_is_ident(v, "not") ||
            tc_css_is_ident(v, "only") || tc_css_is_ident(v, "layer"))
            return NULL;
        for (i = 0; i < TC_ARRAY_LEN(media_types); i++)
            if (tc_css_ascii_ieq(v->text, media_types[i])) {
                known = 1;
                matches = i < 2;
            }
        (void)known;
        type = mq_new(p->arena, matches ? MQ_TRUE : MQ_FALSE);
        p->i++;
    }
    mskip(p);
    if (p->i < p->n) {
        mq_node *kids[2];
        if (!tc_css_is_ident(&p->v[p->i], "and"))
            return NULL;
        p->i++;
        cond = parse_condition(p, 0);
        if (!cond)
            return NULL;
        kids[0] = type;
        kids[1] = cond;
        type = mq_combine(p->arena, MQ_AND, kids, 2);
    }
    return negate ? mq_combine(p->arena, MQ_NOT, &type, 1) : type;
}

int tc_media_parse(tc_arena *a, const tc_css_value *v, uint32_t n, tc_media_list **out)
{
    tc_media_query queries[32];
    uint32_t count = 0, start = 0, i;
    tc_media_list *list;

    *out = NULL;
    for (i = 0; i < n && v[i].type == TC_CSS_WHITESPACE; i++)
        ;
    if (i == n)
        return TC_OK; /* empty: all */
    for (i = 0; i <= n; i++) {
        mparser p;
        mq_node *q;
        if (i < n && v[i].type != TC_CSS_COMMA)
            continue;
        p.arena = a;
        p.v = v + start;
        p.n = i - start;
        p.i = 0;
        p.depth = 0;
        start = i + 1;
        q = parse_query(&p);
        mskip(&p);
        if (!q || p.i != p.n)
            q = mq_new(a, MQ_FALSE); /* an invalid query is "not all" */
        if (!q)
            return TC_ERR_NOMEM;
        if (count < TC_ARRAY_LEN(queries))
            queries[count++].root = q;
    }
    list = tc_arena_alloc(a, sizeof *list);
    if (!list)
        return TC_ERR_NOMEM;
    list->queries = tc_arena_alloc(a, count * sizeof *list->queries);
    if (!list->queries)
        return TC_ERR_NOMEM;
    memcpy(list->queries, queries, count * sizeof *queries);
    list->count = count;
    *out = list;
    return TC_OK;
}

int tc_media_parse_text(tc_arena *a, const char *text, tc_media_list **out)
{
    tc_css_value *v;
    uint32_t n;
    int err = tc_css_parse_values(a, text, strlen(text), &v, &n);
    if (err != TC_OK) {
        *out = NULL;
        return err;
    }
    return tc_media_parse(a, v, n, out);
}

static int compare(float actual, int cmp, float value)
{
    return ((cmp & CMP_LT) && actual < value) || ((cmp & CMP_EQ) && fabs(actual - value) < 0.001f) ||
           ((cmp & CMP_GT) && actual > value);
}

static int feature_matches(const mq_node *m, const tc_media_env *env)
{
    float actual;
    switch (m->feature) {
    case F_WIDTH: actual = env->width; break;
    case F_HEIGHT: actual = env->height; break;
    case F_ASPECT_RATIO: actual = env->height ? env->width / env->height : 0; break;
    case F_DEVICE_WIDTH: actual = env->device_width; break;
    case F_DEVICE_HEIGHT: actual = env->device_height; break;
    case F_DEVICE_ASPECT_RATIO: actual = env->device_height ? env->device_width / env->device_height : 0; break;
    case F_RESOLUTION: actual = 1; break;
    case F_COLOR: actual = (float)env->color_bits; break;
    case F_COLOR_INDEX:
    case F_MONOCHROME: actual = 0; break;
    case F_ORIENTATION:
        /* Keyword features: index 0 is "landscape", 1 "portrait". */
        return m->boolean || (int)m->value == (env->height > env->width);
    default:
        /* Keyword features where Tachyon has the first keyword (0 for grid). */
        return m->boolean ? m->feature != F_GRID && m->feature != F_FORCED_COLORS &&
                                m->feature != F_INVERTED_COLORS && m->feature != F_SCRIPTING &&
                                m->feature != F_PREFERS_REDUCED_MOTION && m->feature != F_PREFERS_CONTRAST &&
                                m->feature != F_PREFERS_REDUCED_TRANSPARENCY
                          : m->value == 0;
    }
    if (m->boolean)
        return actual != 0;
    return compare(actual, m->cmp, m->value);
}

static int node_matches(const mq_node *m, const tc_media_env *env)
{
    uint32_t i;
    switch (m->kind) {
    case MQ_TRUE: return 1;
    case MQ_FALSE: return 0;
    case MQ_NOT: return !node_matches(m->kids[0], env);
    case MQ_AND:
        for (i = 0; i < m->nkids; i++)
            if (!node_matches(m->kids[i], env))
                return 0;
        return 1;
    case MQ_OR:
        for (i = 0; i < m->nkids; i++)
            if (node_matches(m->kids[i], env))
                return 1;
        return 0;
    case MQ_FEATURE: return feature_matches(m, env);
    }
    return 0;
}

int tc_media_matches(const tc_media_list *list, const tc_media_env *env)
{
    uint32_t i;
    if (!list)
        return 1;
    for (i = 0; i < list->count; i++)
        if (node_matches(list->queries[i].root, env))
            return 1;
    return 0;
}
