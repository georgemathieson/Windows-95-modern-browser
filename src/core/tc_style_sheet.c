/* Style sheets: rules from CSS Syntax's output, with @media, @supports,
 * @import, @layer and nested style rules (CSS Nesting) flattened into a list
 * of style rules in cascade order. */
#include <stdlib.h>
#include <string.h>

#include "tc_style_internal.h"

#define MAX_NESTING 16 /* deeper rules are dropped */

typedef struct sp {
    tc_stylesheet *sheet;
    tc_arena *tmp;          /* tokens: freed when the sheet is parsed */
    int quirks;
    int nomem;
    int imports_allowed;    /* @import only comes first */
    unsigned anonymous;     /* anonymous layers so far */
} sp;

static void *grow(void *items, uint32_t *cap, uint32_t len, size_t size)
{
    void *grown;
    uint32_t n;
    if (len < *cap)
        return items;
    n = *cap ? *cap * 2 : 16;
    grown = realloc(items, n * size);
    if (grown)
        *cap = n;
    return grown;
}

static tc_rule *new_rule(sp *p, int kind, const tc_cond *cond, int32_t layer)
{
    tc_stylesheet *s = p->sheet;
    tc_rule *grown = grow(s->rules, &s->rules_cap, s->nrules, sizeof *s->rules);
    tc_rule *r;
    if (!grown) {
        p->nomem = 1;
        return NULL;
    }
    s->rules = grown;
    r = &s->rules[s->nrules++];
    memset(r, 0, sizeof *r);
    r->kind = (uint8_t)kind;
    r->cond = cond;
    r->layer = layer;
    return r;
}

/* ---- Layers ---- */

/* The index of a layer name (full and dotted), added if new. */
static int32_t layer_index(sp *p, const char *name)
{
    tc_stylesheet *s = p->sheet;
    const char **grown;
    uint32_t i;
    for (i = 0; i < s->nlayers; i++)
        if (strcmp(s->layers[i], name) == 0)
            return (int32_t)i;
    grown = grow((void *)s->layers, &s->layers_cap, s->nlayers, sizeof *s->layers);
    if (!grown) {
        p->nomem = 1;
        return -1;
    }
    s->layers = grown;
    if (!(name = tc_arena_strndup(&s->arena, name, strlen(name)))) {
        p->nomem = 1;
        return -1;
    }
    s->layers[s->nlayers] = name;
    return (int32_t)s->nlayers++;
}

/* name, inside the layer parent (or at the top), into buf. 0 if too long. */
static int layer_join(const sp *p, int32_t parent, const char *name, char *buf, size_t size)
{
    const char *prefix = parent >= 0 ? p->sheet->layers[parent] : "";
    size_t a = strlen(prefix), b = strlen(name);
    if (a + b + 2 > size)
        return 0;
    memcpy(buf, prefix, a);
    if (a)
        buf[a++] = '.';
    memcpy(buf + a, name, b + 1);
    return 1;
}

/* A layer name from tokens: ident ('.' ident)*. Returns how many tokens it
   used, 0 if it isn't one. */
static uint32_t layer_name(const tc_css_value *v, uint32_t n, char *buf, size_t size)
{
    uint32_t i = 0;
    size_t len = 0;
    for (;;) {
        size_t k;
        if (i >= n || v[i].type != TC_CSS_IDENT)
            return 0;
        k = strlen(v[i].text);
        if (len + k + 2 > size)
            return 0;
        memcpy(buf + len, v[i].text, k);
        len += k;
        i++;
        if (i + 1 < n && tc_css_is_delim(&v[i], '.') && v[i + 1].type == TC_CSS_IDENT) {
            buf[len++] = '.';
            i++;
            continue;
        }
        break;
    }
    buf[len] = '\0';
    return i;
}

/* A new anonymous layer inside parent: a name no style sheet can write
   (it starts with a control character, and holds the sheet's address). */
static int32_t anonymous_layer(sp *p, int32_t parent)
{
    static const char hex[] = "0123456789abcdef";
    char name[48], full[256];
    uintptr_t addr = (uintptr_t)p->sheet;
    unsigned n = p->anonymous++;
    size_t len = 0, i;
    name[len++] = '\x01';
    for (i = 0; i < 2 * sizeof addr; i++)
        name[len++] = hex[(addr >> (4 * (2 * sizeof addr - 1 - i))) & 15];
    name[len++] = '-';
    for (i = 0; i < 8; i++)
        name[len++] = hex[(n >> (4 * (7 - i))) & 15];
    name[len] = '\0';
    if (!layer_join(p, parent, name, full, sizeof full))
        return -1;
    return layer_index(p, full);
}

/* ---- @supports ---- */

static uint32_t skip_ws(const tc_css_value *v, uint32_t n, uint32_t i)
{
    while (i < n && v[i].type == TC_CSS_WHITESPACE)
        i++;
    return i;
}

static int supports_condition(sp *p, const tc_css_value *v, uint32_t n, int depth);

/* A ( ) block or function in a supports condition. */
static int supports_in_parens(sp *p, const tc_css_value *v, int depth)
{
    if (v->type == TC_CSS_BLOCK && v->cp == '(') {
        uint32_t i = skip_ws(v->items, v->nitems, 0);
        tc_css_decl d;
        /* A declaration, or a condition in parentheses. */
        if (i < v->nitems && v->items[i].type == TC_CSS_IDENT && !tc_css_is_ident(&v->items[i], "not") &&
            tc_css_parse_one_declaration(p->tmp, v->items, v->nitems, &d) == TC_OK)
            return tc_prop_supported(p->tmp, d.name, d.value, d.nvalue);
        return supports_condition(p, v->items, v->nitems, depth + 1);
    }
    if (v->type == TC_CSS_FUNCTION && tc_css_ascii_ieq(v->text, "selector")) {
        tc_selector_list list;
        return tc_selectors_parse(p->tmp, v->items, v->nitems, 0, &list) == TC_OK;
    }
    return 0; /* font-tech(), font-format() and anything else */
}

static int supports_condition(sp *p, const tc_css_value *v, uint32_t n, int depth)
{
    uint32_t i = skip_ws(v, n, 0);
    int result, op = 0; /* 1 and, 2 or */
    if (depth > 32 || i >= n)
        return 0;
    if (tc_css_is_ident(&v[i], "not")) {
        i = skip_ws(v, n, i + 1);
        if (i >= n)
            return 0;
        result = !supports_in_parens(p, &v[i], depth);
        return skip_ws(v, n, i + 1) == n ? result : 0;
    }
    result = supports_in_parens(p, &v[i], depth);
    for (i = skip_ws(v, n, i + 1); i < n; i = skip_ws(v, n, i + 1)) {
        int kind = tc_css_is_ident(&v[i], "and") ? 1 : tc_css_is_ident(&v[i], "or") ? 2 : 0;
        int next;
        if (!kind || (op && kind != op))
            return 0;
        op = kind;
        i = skip_ws(v, n, i + 1);
        if (i >= n)
            return 0;
        next = supports_in_parens(p, &v[i], depth);
        result = op == 1 ? result && next : result || next;
    }
    return result;
}

/* ---- Rules ---- */

static void rule_list(sp *p, const tc_css_value *v, uint32_t n, const tc_cond *cond, int32_t layer, int depth);
static void style_rule(sp *p, const tc_css_value *prelude, uint32_t nprelude, const tc_css_value *parent,
                       uint32_t nparent, const tc_css_value *block, const tc_cond *cond, int32_t layer, int depth);

static const tc_cond *media_cond(sp *p, const tc_css_value *prelude, uint32_t n, const tc_cond *parent)
{
    tc_cond *c = tc_arena_alloc(&p->sheet->arena, sizeof *c);
    tc_media_list *list;
    if (!c || tc_media_parse(&p->sheet->arena, prelude, n, &list) != TC_OK) {
        p->nomem = 1;
        return NULL;
    }
    c->media = list;
    c->parent = parent;
    return c;
}

/* @layer's prelude: the names it declares (statements) or the one it opens
   (blocks). Returns the last layer, -1 for none, -2 if invalid. */
static int32_t layer_names(sp *p, const tc_css_value *v, uint32_t n, int32_t parent, int add_rules,
                           const tc_cond *cond)
{
    char name[200], full[256];
    int32_t last = -1;
    uint32_t i = skip_ws(v, n, 0);
    while (i < n) {
        uint32_t used = layer_name(v + i, n - i, name, sizeof name);
        if (!used || !layer_join(p, parent, name, full, sizeof full))
            return -2;
        last = layer_index(p, full);
        if (last < 0)
            return -2;
        if (add_rules && !new_rule(p, RULE_LAYER, cond, last))
            return -2;
        i = skip_ws(v, n, i + used);
        if (i < n) {
            if (v[i].type != TC_CSS_COMMA || !add_rules)
                return -2;
            i = skip_ws(v, n, i + 1);
            if (i >= n)
                return -2;
        }
    }
    return last;
}

static void import_rule(sp *p, const tc_css_rule *r, const tc_cond *cond)
{
    const tc_css_value *v = r->prelude;
    uint32_t n = r->nprelude, i = skip_ws(v, n, 0);
    const char *url = NULL;
    int32_t layer = -1;
    tc_stylesheet *s = p->sheet;
    tc_import *grown;
    tc_rule *rule;

    if (r->block || i >= n)
        return;
    if (v[i].type == TC_CSS_URL || v[i].type == TC_CSS_STRING) {
        url = v[i].text;
    } else if (v[i].type == TC_CSS_FUNCTION && tc_css_ascii_ieq(v[i].text, "url")) {
        uint32_t k = skip_ws(v[i].items, v[i].nitems, 0);
        if (k < v[i].nitems && v[i].items[k].type == TC_CSS_STRING)
            url = v[i].items[k].text;
    }
    if (!url)
        return;
    i = skip_ws(v, n, i + 1);
    if (i < n && tc_css_is_ident(&v[i], "layer")) {
        layer = anonymous_layer(p, -1);
        i = skip_ws(v, n, i + 1);
    } else if (i < n && v[i].type == TC_CSS_FUNCTION && tc_css_ascii_ieq(v[i].text, "layer")) {
        layer = layer_names(p, v[i].items, v[i].nitems, -1, 0, NULL);
        if (layer < 0)
            return;
        i = skip_ws(v, n, i + 1);
    }
    if (i < n && v[i].type == TC_CSS_FUNCTION && tc_css_ascii_ieq(v[i].text, "supports")) {
        tc_css_decl d;
        int ok;
        /* supports() takes a condition or a bare declaration. */
        if (tc_css_parse_one_declaration(p->tmp, v[i].items, v[i].nitems, &d) == TC_OK)
            ok = tc_prop_supported(p->tmp, d.name, d.value, d.nvalue);
        else
            ok = supports_condition(p, v[i].items, v[i].nitems, 0);
        if (!ok)
            return;
        i = skip_ws(v, n, i + 1);
    }
    if (i < n && !(cond = media_cond(p, v + i, n - i, cond)))
        return;
    grown = grow(s->imports, &s->imports_cap, s->nimports, sizeof *s->imports);
    if (!grown) {
        p->nomem = 1;
        return;
    }
    s->imports = grown;
    s->imports[s->nimports].url = tc_arena_strndup(&s->arena, url, strlen(url));
    s->imports[s->nimports].sheet = NULL;
    if (!s->imports[s->nimports].url || !(rule = new_rule(p, RULE_IMPORT, cond, layer))) {
        p->nomem = 1;
        return;
    }
    rule->import = s->nimports++;
}

/* ---- @font-face (CSS Fonts 4) ---- */

#define MAX_FONT_SRCS   8
#define MAX_URANGES     256

/* The tokens of a value, whitespace left out. Returns how many, or
   max + 1 if there are more. */
static uint32_t words(const tc_css_value *v, uint32_t n, const tc_css_value **w, uint32_t max)
{
    uint32_t i, k = 0;
    for (i = 0; i < n; i++)
        if (v[i].type != TC_CSS_WHITESPACE) {
            if (k == max)
                return max + 1;
            w[k++] = &v[i];
        }
    return k;
}

/* font-family: one name, quoted or as identifiers. */
static const char *face_family(sp *p, const tc_css_value *v, uint32_t n)
{
    static const char *const reserved[] = {
        "inherit", "initial", "unset", "revert", "revert-layer", "default", "serif", "sans-serif", "monospace",
        "cursive", "fantasy", "system-ui", "math", "emoji", "fangsong",
    };
    const tc_css_value *w[16];
    uint32_t count = words(v, n, w, 16), i;
    size_t len = 0;
    char *name;
    if (count == 1 && w[0]->type == TC_CSS_STRING)
        return w[0]->text[0] ? tc_arena_strndup(&p->sheet->arena, w[0]->text, strlen(w[0]->text)) : NULL;
    if (!count || count > 16)
        return NULL;
    for (i = 0; i < count; i++) {
        if (w[i]->type != TC_CSS_IDENT)
            return NULL;
        len += strlen(w[i]->text) + 1;
    }
    for (i = 0; count == 1 && i < TC_ARRAY_LEN(reserved); i++)
        if (tc_css_ascii_ieq(w[0]->text, reserved[i]))
            return NULL;
    if (!(name = tc_arena_alloc(&p->sheet->arena, len)))
        return NULL;
    name[0] = '\0';
    for (i = 0; i < count; i++) {
        if (i)
            strcat(name, " ");
        strcat(name, w[i]->text);
    }
    return name;
}

/* A format() or tech() Tachyon reads: TrueType outlines, packed or not.
   Variable fonts are drawn as their default instance. */
static int face_format_ok(const tc_css_value *f)
{
    static const char *const formats[] = {
        "truetype", "opentype", "woff", "woff2", "truetype-variations", "opentype-variations", "woff-variations",
        "woff2-variations",
    };
    uint32_t i;
    size_t k;
    int any = 0;
    for (i = 0; i < f->nitems; i++) {
        const tc_css_value *a = &f->items[i];
        if (a->type == TC_CSS_WHITESPACE || a->type == TC_CSS_COMMA)
            continue;
        if (a->type != TC_CSS_STRING && a->type != TC_CSS_IDENT)
            return 0;
        if (tc_css_ascii_ieq(f->text, "tech")) {
            if (!tc_css_ascii_ieq(a->text, "variations"))
                return 0; /* every technology must be one Tachyon has */
            any = 1;
            continue;
        }
        for (k = 0; k < TC_ARRAY_LEN(formats); k++)
            any |= tc_css_ascii_ieq(a->text, formats[k]);
    }
    return any;
}

/* src: the url()s in a format Tachyon reads, in order. local() is passed
   over: Windows 95 won't have the font. Entries that don't parse are
   dropped, as CSS Fonts 4 says. */
static int face_src(sp *p, const tc_css_value *v, uint32_t n, tc_font_face *ff)
{
    const char *urls[MAX_FONT_SRCS], **copy;
    uint32_t i = 0, count = 0, k;
    while (i < n) {
        uint32_t end = i, at;
        const char *url = NULL;
        int ok = 1;
        while (end < n && v[end].type != TC_CSS_COMMA)
            end++;
        at = skip_ws(v, end, i);
        if (at < end && v[at].type == TC_CSS_URL) {
            url = v[at].text;
        } else if (at < end && v[at].type == TC_CSS_FUNCTION && tc_css_ascii_ieq(v[at].text, "url")) {
            uint32_t q = skip_ws(v[at].items, v[at].nitems, 0);
            if (q < v[at].nitems && v[at].items[q].type == TC_CSS_STRING)
                url = v[at].items[q].text;
        }
        for (at = skip_ws(v, end, at + 1); url && at < end; at = skip_ws(v, end, at + 1))
            if (v[at].type != TC_CSS_FUNCTION ||
                !(tc_css_ascii_ieq(v[at].text, "format") || tc_css_ascii_ieq(v[at].text, "tech")) ||
                !face_format_ok(&v[at]))
                ok = 0;
        if (url && ok && *url && count < MAX_FONT_SRCS)
            urls[count++] = url;
        i = end + 1;
    }
    if (!count || !(copy = tc_arena_alloc(&p->sheet->arena, count * sizeof *copy)))
        return 0;
    for (k = 0; k < count; k++)
        if (!(copy[k] = tc_arena_strndup(&p->sheet->arena, urls[k], strlen(urls[k]))))
            return 0;
    ff->urls = copy;
    ff->nurls = count;
    return 1;
}

static int face_weight_value(const tc_css_value *w, int *out)
{
    if (tc_css_is_ident(w, "normal"))
        *out = 400;
    else if (tc_css_is_ident(w, "bold"))
        *out = 700;
    else if (w->type == TC_CSS_NUMBER && w->number >= 1 && w->number <= 1000)
        *out = (int)(w->number + 0.5);
    else
        return 0;
    return 1;
}

/* font-weight: auto, a weight, or a variable font's range. */
static int face_weight(const tc_css_value *v, uint32_t n, tc_font_face *ff)
{
    const tc_css_value *w[3];
    uint32_t count = words(v, n, w, 2);
    int a, b;
    if (count == 1 && tc_css_is_ident(w[0], "auto")) {
        ff->weight_min = ff->weight_max = 400;
        return 1;
    }
    if (count < 1 || count > 2 || !face_weight_value(w[0], &a) || (count == 2 && !face_weight_value(w[1], &b)))
        return 0;
    if (count == 1)
        b = a;
    ff->weight_min = (uint16_t)TC_MIN(a, b); /* a range written backwards is turned round */
    ff->weight_max = (uint16_t)TC_MAX(a, b);
    return 1;
}

/* font-style: italic and oblique are both italic here. */
static int face_style(const tc_css_value *v, uint32_t n, tc_font_face *ff)
{
    const tc_css_value *w[4];
    uint32_t count = words(v, n, w, 3);
    if (count == 1 && (tc_css_is_ident(w[0], "normal") || tc_css_is_ident(w[0], "auto")))
        ff->italic = 0;
    else if (count == 1 && tc_css_is_ident(w[0], "italic"))
        ff->italic = 1;
    else if (count >= 1 && count <= 3 && tc_css_is_ident(w[0], "oblique"))
        ff->italic = 1;
    else
        return 0;
    return 1;
}

static int hex_digit(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* One <urange>, from its tokens' text as written (CSS Syntax 3's
   "consume the value of a unicode-range descriptor"). */
static int urange_text(const char *t, uint32_t *first, uint32_t *last)
{
    uint32_t a = 0, b = 0;
    int digits = 0, marks = 0;
    if ((t[0] != 'u' && t[0] != 'U') || t[1] != '+')
        return 0;
    t += 2;
    for (; hex_digit(*t) >= 0 && digits < 6; t++, digits++)
        a = a << 4 | (uint32_t)hex_digit(*t);
    for (; *t == '?' && digits + marks < 6; t++, marks++)
        ;
    if (!digits && !marks)
        return 0;
    if (marks) {
        if (*t)
            return 0;
        b = (a << 4 * marks) | ((1u << 4 * marks) - 1);
        a <<= 4 * marks;
    } else if (*t == '-') {
        for (t++, digits = 0; hex_digit(*t) >= 0 && digits < 6; t++, digits++)
            b = b << 4 | (uint32_t)hex_digit(*t);
        if (!digits || *t)
            return 0;
    } else if (*t) {
        return 0;
    } else {
        b = a;
    }
    if (b > 0x10FFFF || a > b)
        return 0;
    *first = a;
    *last = b;
    return 1;
}

/* unicode-range: a list of <urange>, each written as tokens without
   whitespace inside (U+0-7F tokenises as an identifier, a number and a
   dimension). */
static int face_ranges(sp *p, const tc_css_value *v, uint32_t n, tc_font_face *ff)
{
    uint32_t pairs[2 * MAX_URANGES], count = 0, i = 0, *copy;
    while (i < n) {
        char text[64];
        size_t len = 0;
        uint32_t end = i, a, b;
        while (end < n && v[end].type != TC_CSS_COMMA)
            end++;
        while (i < end && v[i].type == TC_CSS_WHITESPACE)
            i++;
        while (end > i && v[end - 1].type == TC_CSS_WHITESPACE)
            end--;
        for (; i < end; i++) {
            const char *part = "";
            char one[2] = { 0, 0 };
            size_t k;
            if (v[i].type == TC_CSS_IDENT)
                part = v[i].text;
            else if (v[i].type == TC_CSS_NUMBER || v[i].type == TC_CSS_DIMENSION)
                part = v[i].repr;
            else if (v[i].type == TC_CSS_DELIM && (v[i].cp == '+' || v[i].cp == '?'))
                one[0] = (char)v[i].cp, part = one;
            else
                return 0;
            k = strlen(part);
            if (v[i].type == TC_CSS_DIMENSION && len + k < sizeof text)
                k += strlen(v[i].text);
            if (len + k >= sizeof text)
                return 0;
            memcpy(text + len, part, strlen(part));
            if (v[i].type == TC_CSS_DIMENSION)
                memcpy(text + len + strlen(part), v[i].text, strlen(v[i].text));
            len += k;
        }
        text[len] = '\0';
        if (!urange_text(text, &a, &b))
            return 0;
        if (count < MAX_URANGES) {
            pairs[2 * count] = a;
            pairs[2 * count + 1] = b;
            count++;
        }
        while (i < n && v[i].type != TC_CSS_COMMA)
            i++;
        i++;
    }
    if (!count || !(copy = tc_arena_alloc(&p->sheet->arena, 2 * count * sizeof *copy)))
        return 0;
    memcpy(copy, pairs, 2 * count * sizeof *copy);
    ff->ranges = copy;
    ff->nranges = count;
    return 1;
}

static void font_face_rule(sp *p, const tc_css_rule *r, const tc_cond *cond, int32_t layer)
{
    tc_font_face *ff;
    tc_css_item *items;
    tc_rule *rule;
    uint32_t count, i;

    if (!r->block || skip_ws(r->prelude, r->nprelude, 0) != r->nprelude)
        return;
    if (tc_css_parse_block(p->tmp, r->block->items, r->block->nitems, &items, &count) != TC_OK ||
        !(ff = tc_arena_alloc(&p->sheet->arena, sizeof *ff))) {
        p->nomem = 1;
        return;
    }
    memset(ff, 0, sizeof *ff);
    ff->weight_min = ff->weight_max = 400;
    ff->base = p->sheet->url;
    /* A descriptor that doesn't parse is ignored; the last that does wins. */
    for (i = 0; i < count; i++) {
        const tc_css_decl *d = &items[i].decl;
        tc_font_face tmp = *ff;
        if (items[i].type != TC_CSS_ITEM_DECL)
            continue;
        if (tc_css_ascii_ieq(d->name, "font-family")) {
            if ((tmp.family = face_family(p, d->value, d->nvalue)) != NULL)
                *ff = tmp;
        } else if (tc_css_ascii_ieq(d->name, "src")) {
            if (face_src(p, d->value, d->nvalue, &tmp))
                *ff = tmp;
        } else if (tc_css_ascii_ieq(d->name, "font-weight")) {
            if (face_weight(d->value, d->nvalue, &tmp))
                *ff = tmp;
        } else if (tc_css_ascii_ieq(d->name, "font-style")) {
            if (face_style(d->value, d->nvalue, &tmp))
                *ff = tmp;
        } else if (tc_css_ascii_ieq(d->name, "unicode-range")) {
            if (face_ranges(p, d->value, d->nvalue, &tmp))
                *ff = tmp;
        }
    }
    if (!ff->family || !ff->nurls)
        return; /* both are needed */
    if (!(rule = new_rule(p, RULE_FONT_FACE, cond, layer)))
        return;
    rule->face = ff;
}

/* One rule of a rule list (the top level, or inside @media and friends). */
static void one_rule(sp *p, const tc_css_rule *r, const tc_cond *cond, int32_t layer, int depth)
{
    const char *name = r->name;
    if (depth > MAX_NESTING || p->nomem)
        return;
    if (r->type == TC_CSS_RULE_QUALIFIED) {
        p->imports_allowed = 0;
        if (r->block)
            style_rule(p, r->prelude, r->nprelude, NULL, 0, r->block, cond, layer, depth);
        return;
    }
    if (r->type != TC_CSS_RULE_AT)
        return;
    if (tc_css_ascii_ieq(name, "charset"))
        return;
    if (tc_css_ascii_ieq(name, "import")) {
        if (p->imports_allowed && depth == 0)
            import_rule(p, r, cond);
        return;
    }
    if (tc_css_ascii_ieq(name, "layer")) {
        if (!r->block) {
            layer_names(p, r->prelude, r->nprelude, layer, 1, cond);
            return; /* @layer statements may come before @import */
        }
        p->imports_allowed = 0;
        if (skip_ws(r->prelude, r->nprelude, 0) == r->nprelude) {
            layer = anonymous_layer(p, layer);
            if (layer >= 0)
                new_rule(p, RULE_LAYER, cond, layer);
        } else {
            layer = layer_names(p, r->prelude, r->nprelude, layer, 0, NULL);
            if (layer >= 0)
                new_rule(p, RULE_LAYER, cond, layer);
        }
        if (layer >= 0)
            rule_list(p, r->block->items, r->block->nitems, cond, layer, depth + 1);
        return;
    }
    p->imports_allowed = 0;
    if (!r->block)
        return;
    if (tc_css_ascii_ieq(name, "media")) {
        const tc_cond *c = media_cond(p, r->prelude, r->nprelude, cond);
        if (c)
            rule_list(p, r->block->items, r->block->nitems, c, layer, depth + 1);
    } else if (tc_css_ascii_ieq(name, "supports")) {
        if (supports_condition(p, r->prelude, r->nprelude, 0))
            rule_list(p, r->block->items, r->block->nitems, cond, layer, depth + 1);
    } else if (tc_css_ascii_ieq(name, "font-face")) {
        font_face_rule(p, r, cond, layer);
    }
    /* Everything else (@keyframes, @page, @container, @scope, @property,
       @counter-style...) Tachyon doesn't use yet. */
}

/* The rules in a block (declarations there are invalid and dropped). */
static void rule_list(sp *p, const tc_css_value *v, uint32_t n, const tc_cond *cond, int32_t layer, int depth)
{
    tc_css_item *items;
    uint32_t count, i;
    if (tc_css_parse_block(p->tmp, v, n, &items, &count) != TC_OK) {
        p->nomem = 1;
        return;
    }
    for (i = 0; i < count && !p->nomem; i++)
        if (items[i].type == TC_CSS_ITEM_RULE)
            one_rule(p, &items[i].rule, cond, layer, depth);
}

/* ---- Nesting ---- */

typedef struct tvec {
    tc_css_value *items;
    uint32_t len, cap;
} tvec;

static int tpush(tvec *t, const tc_css_value *v)
{
    tc_css_value *grown = grow(t->items, &t->cap, t->len, sizeof *t->items);
    if (!grown)
        return 0;
    t->items = grown;
    t->items[t->len++] = *v;
    return 1;
}

static int has_amp(const tc_css_value *v, uint32_t n, int depth)
{
    uint32_t i;
    if (depth > 32)
        return 0;
    for (i = 0; i < n; i++)
        if (tc_css_is_delim(&v[i], '&') ||
            ((v[i].type == TC_CSS_FUNCTION || v[i].type == TC_CSS_BLOCK) && has_amp(v[i].items, v[i].nitems, depth + 1)))
            return 1;
    return 0;
}

/* Copy tokens to t with each & replaced by :is(parent). */
static int replace_amp(sp *p, tvec *t, const tc_css_value *v, uint32_t n, const tc_css_value *is, int depth)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (tc_css_is_delim(&v[i], '&')) {
            if (!tpush(t, &is[0]) || !tpush(t, &is[1]))
                return 0;
        } else if ((v[i].type == TC_CSS_FUNCTION || v[i].type == TC_CSS_BLOCK) && depth < 32 &&
                   has_amp(v[i].items, v[i].nitems, depth + 1)) {
            tvec inner = { NULL, 0, 0 };
            tc_css_value copy = v[i];
            if (!replace_amp(p, &inner, v[i].items, v[i].nitems, is, depth + 1)) {
                free(inner.items);
                return 0;
            }
            copy.items = tc_arena_alloc(p->tmp, (inner.len ? inner.len : 1) * sizeof *copy.items);
            if (!copy.items) {
                free(inner.items);
                return 0;
            }
            memcpy(copy.items, inner.items, inner.len * sizeof *copy.items);
            copy.nitems = inner.len;
            free(inner.items);
            if (!tpush(t, &copy))
                return 0;
        } else if (!tpush(t, &v[i])) {
            return 0;
        }
    }
    return 1;
}

/* A nested rule's selector, made absolute: "& " in front of each selector
   without an &, and every & replaced by :is(the parent's selectors).
   TC_ERR_INVALID if a selector is empty. */
static int nest_selector(sp *p, const tc_css_value *rel, uint32_t nrel, const tc_css_value *parent, uint32_t nparent,
                         tc_css_value **out, uint32_t *nout)
{
    tc_css_value is[2], ws;
    tvec t = { NULL, 0, 0 };
    uint32_t start = 0, i;
    int ok = 1, empty = 0;

    memset(is, 0, sizeof is);
    is[0].type = TC_CSS_COLON;
    is[0].text = "";
    is[1].type = TC_CSS_FUNCTION;
    is[1].text = "is";
    is[1].items = (tc_css_value *)parent;
    is[1].nitems = nparent;
    memset(&ws, 0, sizeof ws);
    ws.type = TC_CSS_WHITESPACE;
    ws.text = " ";
    for (i = 0; i <= nrel && ok; i++) {
        uint32_t a, b;
        if (i < nrel && rel[i].type != TC_CSS_COMMA)
            continue;
        a = skip_ws(rel, i, start);
        b = i;
        while (b > a && rel[b - 1].type == TC_CSS_WHITESPACE)
            b--;
        if (a == b) {
            empty = 1;
            break;
        }
        if (start > 0) {
            tc_css_value comma;
            memset(&comma, 0, sizeof comma);
            comma.type = TC_CSS_COMMA;
            comma.text = "";
            ok = tpush(&t, &comma);
        }
        if (ok && !has_amp(rel + a, b - a, 0))
            ok = tpush(&t, &is[0]) && tpush(&t, &is[1]) && tpush(&t, &ws);
        if (ok)
            ok = replace_amp(p, &t, rel + a, b - a, is, 0);
        start = i + 1;
    }
    if (ok && !empty) {
        *out = tc_arena_alloc(p->tmp, (t.len ? t.len : 1) * sizeof **out);
        if (*out)
            memcpy(*out, t.items, t.len * sizeof **out);
        *nout = t.len;
        ok = *out != NULL;
    }
    free(t.items);
    return !ok ? TC_ERR_NOMEM : empty ? TC_ERR_INVALID : TC_OK;
}

/* Declarations so far, as a style rule. */
static void flush(sp *p, const tc_selector_list *sel, tc_decl_vec *decls, const tc_cond *cond, int32_t layer)
{
    tc_rule *r;
    if (!decls->len)
        return;
    r = new_rule(p, RULE_STYLE, cond, layer);
    if (!r)
        return;
    r->selectors = *sel;
    r->decls = tc_arena_alloc(&p->sheet->arena, decls->len * sizeof *r->decls);
    if (!r->decls) {
        p->nomem = 1;
        return;
    }
    memcpy(r->decls, decls->items, decls->len * sizeof *r->decls);
    r->ndecls = decls->len;
    p->sheet->style_rules++;
    decls->len = 0;
}

/* A style rule's contents, or those of a conditional rule nested in one:
   declarations apply to sel, and nested rules are relative to it. */
static void rule_contents(sp *p, const tc_selector_list *sel, const tc_css_value *sel_tokens, uint32_t nsel,
                          const tc_css_value *v, uint32_t n, const tc_cond *cond, int32_t layer, int depth)
{
    tc_decl_vec decls = { NULL, 0, 0 };
    tc_css_item *items;
    uint32_t count, i;

    if (tc_css_parse_block(p->tmp, v, n, &items, &count) != TC_OK) {
        p->nomem = 1;
        return;
    }
    for (i = 0; i < count && !p->nomem; i++) {
        const tc_css_rule *r = &items[i].rule;
        if (items[i].type == TC_CSS_ITEM_DECL) {
            if (tc_decls_add(&p->sheet->arena, &items[i].decl, p->quirks, p->sheet->url, &decls) != TC_OK)
                p->nomem = 1;
            continue;
        }
        if (items[i].type != TC_CSS_ITEM_RULE || depth >= MAX_NESTING || !r->block)
            continue;
        /* Rules after declarations come after them in the cascade, and so
           do declarations after rules. */
        flush(p, sel, &decls, cond, layer);
        if (r->type == TC_CSS_RULE_QUALIFIED) {
            style_rule(p, r->prelude, r->nprelude, sel_tokens, nsel, r->block, cond, layer, depth + 1);
        } else if (tc_css_ascii_ieq(r->name, "media")) {
            const tc_cond *c = media_cond(p, r->prelude, r->nprelude, cond);
            if (c)
                rule_contents(p, sel, sel_tokens, nsel, r->block->items, r->block->nitems, c, layer, depth + 1);
        } else if (tc_css_ascii_ieq(r->name, "supports")) {
            if (supports_condition(p, r->prelude, r->nprelude, 0))
                rule_contents(p, sel, sel_tokens, nsel, r->block->items, r->block->nitems, cond, layer, depth + 1);
        } else if (tc_css_ascii_ieq(r->name, "layer")) {
            int32_t l = skip_ws(r->prelude, r->nprelude, 0) == r->nprelude
                            ? anonymous_layer(p, layer)
                            : layer_names(p, r->prelude, r->nprelude, layer, 0, NULL);
            if (l >= 0 && new_rule(p, RULE_LAYER, cond, l))
                rule_contents(p, sel, sel_tokens, nsel, r->block->items, r->block->nitems, cond, l, depth + 1);
        }
    }
    flush(p, sel, &decls, cond, layer);
    tc_decls_free(&decls);
}

static void style_rule(sp *p, const tc_css_value *prelude, uint32_t nprelude, const tc_css_value *parent,
                       uint32_t nparent, const tc_css_value *block, const tc_cond *cond, int32_t layer, int depth)
{
    tc_selector_list *sel = tc_arena_alloc(&p->sheet->arena, sizeof *sel);
    int err;
    if (!sel) {
        p->nomem = 1;
        return;
    }
    if (parent && (err = nest_selector(p, prelude, nprelude, parent, nparent, (tc_css_value **)&prelude,
                                       &nprelude)) != TC_OK) {
        if (err == TC_ERR_NOMEM)
            p->nomem = 1;
        return; /* invalid: dropped like any rule with an invalid selector */
    }
    if (tc_selectors_parse(&p->sheet->arena, prelude, nprelude, 0, sel) != TC_OK)
        return; /* an invalid selector drops the rule, nested rules and all */
    rule_contents(p, sel, prelude, nprelude, block->items, block->nitems, cond, layer, depth);
}

/* ---- Style sheets ---- */

void tc_stylesheet_free(tc_stylesheet *sheet)
{
    uint32_t i;
    if (!sheet)
        return;
    for (i = 0; i < sheet->nimports; i++)
        tc_stylesheet_free(sheet->imports[i].sheet);
    free(sheet->imports);
    free(sheet->rules);
    free((void *)sheet->layers);
    tc_arena_free(&sheet->arena);
    free(sheet);
}

tc_stylesheet *tc_stylesheet_parse(const char *text, size_t len, int origin, int quirks, const char *url)
{
    tc_stylesheet *sheet = calloc(1, sizeof *sheet);
    tc_arena tmp;
    tc_css_value *v;
    tc_css_rule *rules;
    uint32_t n, nrules, i;
    sp p;

    if (!sheet)
        return NULL;
    tc_arena_init(&sheet->arena);
    sheet->origin = origin;
    if (url && !(sheet->url = tc_arena_strndup(&sheet->arena, url, strlen(url)))) {
        tc_stylesheet_free(sheet);
        return NULL;
    }
    tc_arena_init(&tmp);
    memset(&p, 0, sizeof p);
    p.sheet = sheet;
    p.tmp = &tmp;
    p.quirks = quirks;
    p.imports_allowed = 1;
    if (tc_css_parse_values(&tmp, text, len, &v, &n) != TC_OK ||
        tc_css_parse_rules(&tmp, v, n, 1, 0, &rules, &nrules) != TC_OK)
        p.nomem = 1;
    else
        for (i = 0; i < nrules && !p.nomem; i++)
            one_rule(&p, &rules[i], NULL, -1, 0);
    tc_arena_free(&tmp);
    if (p.nomem) {
        tc_stylesheet_free(sheet);
        return NULL;
    }
    return sheet;
}

size_t tc_stylesheet_import_count(const tc_stylesheet *sheet)
{
    return sheet->nimports;
}

const char *tc_stylesheet_import_url(const tc_stylesheet *sheet, size_t i)
{
    return i < sheet->nimports ? sheet->imports[i].url : NULL;
}

const char *tc_stylesheet_url(const tc_stylesheet *sheet)
{
    return sheet->url;
}

void tc_stylesheet_set_import(tc_stylesheet *sheet, size_t i, tc_stylesheet *imported)
{
    if (i >= sheet->nimports) {
        tc_stylesheet_free(imported);
        return;
    }
    tc_stylesheet_free(sheet->imports[i].sheet);
    sheet->imports[i].sheet = imported;
}

size_t tc_stylesheet_rule_count(const tc_stylesheet *sheet)
{
    return sheet->style_rules;
}

int tc_style_attr_parse(tc_arena *a, const char *text, int quirks, tc_decl **out, uint32_t *n)
{
    tc_arena tmp;
    tc_css_value *v;
    tc_css_item *items;
    tc_decl_vec decls = { NULL, 0, 0 };
    uint32_t nv, count, i;
    int err = TC_OK;

    *out = NULL;
    *n = 0;
    tc_arena_init(&tmp);
    if (tc_css_parse_values(&tmp, text, strlen(text), &v, &nv) != TC_OK ||
        tc_css_parse_block(&tmp, v, nv, &items, &count) != TC_OK)
        err = TC_ERR_NOMEM;
    for (i = 0; err == TC_OK && i < count; i++)
        if (items[i].type == TC_CSS_ITEM_DECL)
            err = tc_decls_add(a, &items[i].decl, quirks, NULL, &decls);
    if (err == TC_OK && decls.len) {
        *out = tc_arena_alloc(a, decls.len * sizeof **out);
        if (*out) {
            memcpy(*out, decls.items, decls.len * sizeof **out);
            *n = decls.len;
        } else {
            err = TC_ERR_NOMEM;
        }
    }
    tc_decls_free(&decls);
    tc_arena_free(&tmp);
    return err;
}

/* ---- The user-agent style sheets ---- */

static tc_stylesheet *ua_sheets[2];

const tc_stylesheet *tc_ua_sheet(int quirks_extras)
{
    tc_stylesheet **s = &ua_sheets[quirks_extras ? 1 : 0];
    if (!*s) {
        const char *css = quirks_extras ? tc_ua_quirks_css : tc_ua_css;
        *s = tc_stylesheet_parse(css, strlen(css), TC_ORIGIN_UA, 0, NULL);
    }
    return *s;
}

void tc_style_init(void)
{
    tc_ua_sheet(0);
    tc_ua_sheet(1);
}
