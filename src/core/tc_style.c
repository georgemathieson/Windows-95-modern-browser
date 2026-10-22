/* The cascade (CSS Cascade 5) and computed values: which declarations win
 * for each element, custom properties and var(), and computing the winners
 * into tc_style, which elements with the same values share. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tc_css_color.h"
#include "tc_style_internal.h"

/* ---- Cascade layers ---- */

typedef struct lnode lnode;
struct lnode {
    const char *name;           /* this segment of the dotted name */
    lnode *parent, *first, *last, *next;
    uint32_t rank;              /* higher wins (normal declarations) */
};

/* ---- Rules in the cascade ---- */

#define MAX_ANCESTOR_HASHES 4

typedef struct entry {
    const tc_selector *sel;
    const tc_rule *rule;
    lnode *layer;
    uint32_t order;             /* the rule's position in the cascade */
    uint32_t rank;              /* the layer's */
    uint32_t ancestors[MAX_ANCESTOR_HASHES]; /* for the ancestor filter */
    uint8_t nancestors;
    uint8_t origin;
} entry;

typedef struct bucket {
    uint8_t kind;               /* tc_selector_key_type */
    const char *name;
    uint32_t hash;
    uint32_t start, count;
} bucket;

typedef struct match {
    const tc_rule *rule;
    uint32_t specificity, order, rank;
    uint8_t origin, pseudo;
} match;

/* ---- Custom properties ---- */

typedef struct cprop {
    const char *name;
    tc_tokens value;
} cprop;

struct tc_custom_props {
    const cprop *items;         /* sorted by name */
    uint32_t count;
};

/* A var() value parsed for an element's custom properties. */
typedef struct var_result {
    const tc_var_value *var;
    const tc_custom_props *custom;
    uint16_t n;
    uint16_t *props;
    tc_pval *vals;
} var_result;

typedef struct slot {
    const tc_element *el;
    tc_element_styles styles;
} slot;

struct tc_styled {
    tc_arena arena;             /* computed styles and everything they point to */
    slot *slots;
    uint32_t slots_cap;
    struct shared {
        uint32_t hash;
        const tc_style *style;
    } *styles;                  /* the distinct styles, hashed */
    uint32_t styles_cap, nstyles;
    var_result *vars;
    uint32_t vars_cap, nvars;
    tc_style_stats stats;
};

typedef struct cascade {
    const tc_decl *win[P_COUNT];
    const tc_decl *ua_win[P_COUNT];
    uint8_t origin[P_COUNT];
    uint8_t set[P_COUNT];       /* the properties with a winner, in no order */
    uint32_t nset;
    const tc_decl **custom;     /* custom property declarations, in cascade order */
    uint32_t ncustom, custom_cap;
} cascade;

typedef struct styler {
    tc_styled *out;
    const tc_document *doc;
    const tc_media_env *env;
    tc_match_context mctx;
    int quirks;
    tc_arena tmp;               /* layers, substitutions: freed after styling */
    lnode root;
    entry *entries;
    uint32_t nentries, entries_cap, order;
    bucket *buckets;            /* hashed */
    uint32_t buckets_cap;
    uint32_t *index;            /* entries by bucket */
    uint32_t *universal;
    uint32_t nuniversal;
    match *matches;
    uint32_t nmatches, matches_cap;
    cascade c;
    tc_style initial;
    float root_font_size;
    const tc_element *body;
    int nomem;
    const tc_cond *last_cond;   /* the last media condition evaluated, and its result */
    int last_cond_result;
    /* A counting Bloom filter of the current element's ancestors' ids,
       classes and tag names: selectors that need an ancestor that can't
       be there are skipped without walking up the tree. */
    uint8_t bloom[4096];
} styler;

static void *grow(void *items, uint32_t *cap, uint32_t len, size_t size)
{
    void *grown;
    uint32_t n;
    if (len < *cap)
        return items;
    n = *cap ? *cap * 2 : 64;
    grown = realloc(items, n * size);
    if (grown)
        *cap = n;
    return grown;
}

static uint32_t hash_bytes(const void *data, size_t len, uint32_t h)
{
    const unsigned char *p = data;
    size_t i;
    for (i = 0; i < len; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint32_t hash_name(const char *s, size_t len, int fold)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (fold && ch >= 'A' && ch <= 'Z')
            ch = (unsigned char)(ch + 32);
        h = (h ^ ch) * 16777619u;
    }
    return h;
}

static int name_eq(const char *a, const char *b, size_t blen, int fold)
{
    size_t i;
    for (i = 0; i < blen; i++) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (!x)
            return 0;
        if (fold) {
            if (x >= 'A' && x <= 'Z')
                x = (unsigned char)(x + 32);
            if (y >= 'A' && y <= 'Z')
                y = (unsigned char)(y + 32);
        }
        if (x != y)
            return 0;
    }
    return a[blen] == '\0';
}

/* ---- Initial values ---- */

static const char *const serif_names[] = { "serif" };
static const tc_font_families serif_family = { (const char **)serif_names, 1 };

static void initial_style(tc_style *s)
{
    int i;
    memset(s, 0, sizeof *s);
    s->display = TC_DISPLAY_INLINE;
    for (i = 0; i < 4; i++) {
        s->margin[i].kind = TC_SIZE_LENGTH;
        s->inset[i].kind = TC_SIZE_AUTO;
        s->border_width[i] = 3;
        s->border_color[i] = 0xFF000000u;
    }
    s->width.kind = s->height.kind = TC_SIZE_AUTO;
    s->min_width.kind = s->min_height.kind = TC_SIZE_AUTO;
    s->max_width.kind = s->max_height.kind = TC_SIZE_NONE;
    s->decoration_color = 0xFF000000u;
    s->currentcolor = TC_CC_BORDER_TOP | TC_CC_BORDER_RIGHT | TC_CC_BORDER_BOTTOM | TC_CC_BORDER_LEFT | TC_CC_DECORATION;
    s->z_auto = 1;
    s->color = 0xFF000000u;
    s->font_family = &serif_family;
    s->font_size = 16;
    s->font_size_keyword = 1 + FS_MEDIUM;
    s->font_size_factor = 1;
    s->font_weight = 400;
    s->list_style_type = TC_LIST_DISC;
}

/* ---- Collecting the rules ---- */

static int cond_matches(styler *st, const tc_cond *c)
{
    const tc_cond *start = c;
    int result = 1;
    if (!c)
        return 1;
    if (c == st->last_cond)
        return st->last_cond_result;
    for (; c && result; c = c->parent)
        result = tc_media_matches(c->media, st->env);
    st->last_cond = start;
    st->last_cond_result = result;
    return result;
}

/* The layer node for a dotted name inside prefix, made if new. */
static lnode *layer_node(styler *st, lnode *prefix, const char *name)
{
    lnode *node = prefix ? prefix : &st->root;
    while (*name) {
        const char *dot = strchr(name, '.');
        size_t len = dot ? (size_t)(dot - name) : strlen(name);
        lnode *k;
        for (k = node->first; k; k = k->next)
            if (strlen(k->name) == len && memcmp(k->name, name, len) == 0)
                break;
        if (!k) {
            k = tc_arena_alloc(&st->tmp, sizeof *k);
            if (!k || !(k->name = tc_arena_strndup(&st->tmp, name, len))) {
                st->nomem = 1;
                return node;
            }
            k->parent = node;
            k->first = k->last = k->next = NULL;
            k->rank = 0;
            if (node->last)
                node->last->next = k;
            else
                node->first = k;
            node->last = k;
        }
        node = k;
        name += len;
        if (*name == '.')
            name++;
    }
    return node;
}

static void add_entry(styler *st, const tc_selector *sel, const tc_rule *rule, lnode *layer, int origin)
{
    entry *grown = grow(st->entries, &st->entries_cap, st->nentries, sizeof *st->entries);
    entry *e;
    if (!grown) {
        st->nomem = 1;
        return;
    }
    st->entries = grown;
    e = &st->entries[st->nentries++];
    e->sel = sel;
    e->rule = rule;
    e->layer = layer;
    e->order = st->order;
    e->rank = 0;
    e->origin = (uint8_t)origin;
    e->nancestors = (uint8_t)tc_selector_ancestor_hashes(sel, st->quirks, e->ancestors, MAX_ANCESTOR_HASHES);
}

static void collect(styler *st, const tc_stylesheet *sheet, lnode *prefix, int depth)
{
    lnode **layers = NULL;
    uint32_t i, k;
    if (sheet->nlayers && !(layers = calloc(sheet->nlayers, sizeof *layers))) {
        st->nomem = 1;
        return;
    }
    for (i = 0; i < sheet->nrules && !st->nomem; i++) {
        const tc_rule *r = &sheet->rules[i];
        lnode *layer = prefix;
        if (!cond_matches(st, r->cond))
            continue;
        if (r->layer >= 0) {
            if (!layers[r->layer])
                layers[r->layer] = layer_node(st, prefix, sheet->layers[r->layer]);
            layer = layers[r->layer];
        }
        switch (r->kind) {
        case RULE_IMPORT:
            if (sheet->imports[r->import].sheet && depth < 8)
                collect(st, sheet->imports[r->import].sheet, layer, depth + 1);
            break;
        case RULE_STYLE:
            for (k = 0; k < r->selectors.count; k++)
                add_entry(st, &r->selectors.items[k], r, layer, sheet->origin);
            st->order++;
            break;
        default:
            break; /* RULE_LAYER: naming it was enough */
        }
    }
    free(layers);
}

/* Layers rank in post-order: sublayers before their parent's own rules,
   and unlayered rules (the root) last of all. */
static void rank_layers(lnode *node, uint32_t *next, int depth)
{
    lnode *k;
    if (depth < 200)
        for (k = node->first; k; k = k->next)
            rank_layers(k, next, depth + 1);
    node->rank = (*next)++;
}

static bucket *find_bucket(styler *st, int kind, const char *name, size_t len, uint32_t hash, int fold)
{
    uint32_t mask = st->buckets_cap - 1, i = (hash ^ (uint32_t)kind) & mask;
    while (st->buckets[i].name) {
        bucket *b = &st->buckets[i];
        if (b->kind == kind && b->hash == hash && name_eq(b->name, name, len, fold))
            return b;
        i = (i + 1) & mask;
    }
    return &st->buckets[i];
}

static int build_buckets(styler *st)
{
    uint32_t i, cap = 64, total = 0;
    int fold = st->quirks;
    while (cap < st->nentries * 2)
        cap *= 2;
    st->buckets = calloc(cap, sizeof *st->buckets);
    st->index = malloc((st->nentries ? st->nentries : 1) * sizeof *st->index);
    st->universal = malloc((st->nentries ? st->nentries : 1) * sizeof *st->universal);
    if (!st->buckets || !st->index || !st->universal)
        return TC_ERR_NOMEM;
    st->buckets_cap = cap;
    /* Count, then place. */
    for (i = 0; i < st->nentries; i++) {
        const char *name;
        int kind = (int)tc_selector_key(st->entries[i].sel, &name);
        bucket *b;
        uint32_t h;
        if (kind == TC_KEY_NONE)
            continue;
        h = hash_name(name, strlen(name), fold && kind != TC_KEY_TAG);
        b = find_bucket(st, kind, name, strlen(name), h, fold && kind != TC_KEY_TAG);
        if (!b->name) {
            b->kind = (uint8_t)kind;
            b->name = name;
            b->hash = h;
        }
        b->count++;
    }
    for (i = 0; i < cap; i++)
        if (st->buckets[i].name) {
            st->buckets[i].start = total;
            total += st->buckets[i].count;
            st->buckets[i].count = 0;
        }
    for (i = 0; i < st->nentries; i++) {
        const char *name;
        int kind = (int)tc_selector_key(st->entries[i].sel, &name);
        bucket *b;
        if (kind == TC_KEY_NONE) {
            st->universal[st->nuniversal++] = i;
            continue;
        }
        b = find_bucket(st, kind, name, strlen(name), hash_name(name, strlen(name), fold && kind != TC_KEY_TAG),
                        fold && kind != TC_KEY_TAG);
        st->index[b->start + b->count++] = i;
    }
    return TC_OK;
}

/* ---- Matching ---- */

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

static int bloom_has(const styler *st, uint32_t h)
{
    return st->bloom[h & 4095] && st->bloom[(h >> 12) & 4095];
}

static void bloom_hash(styler *st, uint32_t h, int add)
{
    uint8_t *a = &st->bloom[h & 4095], *b = &st->bloom[(h >> 12) & 4095];
    /* A saturated counter stays set for good: only a false positive. */
    if (add) {
        *a += *a < 255;
        *b += *b < 255;
    } else {
        *a -= *a > 0 && *a < 255;
        *b -= *b > 0 && *b < 255;
    }
}

/* Add an element's features to the ancestor filter (or take them out). */
static void bloom_element(styler *st, const tc_element *el, int add)
{
    const char *id = tc_dom_get_attr(el, "id"), *cls = tc_dom_get_attr(el, "class");
    bloom_hash(st, tc_selector_hash(TC_KEY_TAG, el->name, strlen(el->name), 1), add);
    if (id && *id)
        bloom_hash(st, tc_selector_hash(TC_KEY_ID, id, strlen(id), st->quirks), add);
    if (cls)
        while (*cls) {
            const char *start;
            while (is_space(*cls))
                cls++;
            start = cls;
            while (*cls && !is_space(*cls))
                cls++;
            if (cls > start)
                bloom_hash(st, tc_selector_hash(TC_KEY_CLASS, start, (size_t)(cls - start), st->quirks), add);
        }
}

static void try_entries(styler *st, const uint32_t *idx, uint32_t n, const tc_element *el)
{
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        const entry *e = &st->entries[idx[i]];
        match *m;
        if (e->sel->pseudo == TC_PSEUDO_OTHER)
            continue;
        for (k = 0; k < e->nancestors && bloom_has(st, e->ancestors[k]); k++)
            ;
        if (k < e->nancestors)
            continue;
        st->out->stats.tested++;
        if (!tc_selector_matches(e->sel, el, &st->mctx))
            continue;
        st->out->stats.matched++;
        m = grow(st->matches, &st->matches_cap, st->nmatches, sizeof *st->matches);
        if (!m) {
            st->nomem = 1;
            return;
        }
        st->matches = m;
        m = &st->matches[st->nmatches++];
        m->rule = e->rule;
        m->specificity = e->sel->specificity;
        m->order = e->order;
        m->rank = e->rank;
        m->origin = e->origin;
        m->pseudo = e->sel->pseudo;
    }
}

static void try_bucket(styler *st, int kind, const char *name, size_t len, const tc_element *el)
{
    int fold = st->quirks && kind != TC_KEY_TAG;
    bucket *b = find_bucket(st, kind, name, len, hash_name(name, len, fold), fold);
    if (b->name)
        try_entries(st, st->index + b->start, b->count, el);
}

static int match_cmp(const void *pa, const void *pb)
{
    const match *a = pa, *b = pb;
    if (a->pseudo != b->pseudo)
        return a->pseudo < b->pseudo ? -1 : 1;
    if (a->origin != b->origin)
        return a->origin < b->origin ? -1 : 1;
    if (a->rank != b->rank)
        return a->rank < b->rank ? -1 : 1;
    if (a->specificity != b->specificity)
        return a->specificity < b->specificity ? -1 : 1;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return 0;
}

static void match_element(styler *st, const tc_element *el)
{
    const char *id = tc_dom_get_attr(el, "id"), *cls = tc_dom_get_attr(el, "class");
    char lower[64];
    size_t len = strlen(el->name), i;

    st->nmatches = 0;
    if (id && *id)
        try_bucket(st, TC_KEY_ID, id, strlen(id), el);
    if (cls)
        while (*cls) {
            const char *start;
            while (is_space(*cls))
                cls++;
            start = cls;
            while (*cls && !is_space(*cls))
                cls++;
            if (cls > start)
                try_bucket(st, TC_KEY_CLASS, start, (size_t)(cls - start), el);
        }
    if (len < sizeof lower) {
        for (i = 0; i < len; i++)
            lower[i] = (char)(el->name[i] >= 'A' && el->name[i] <= 'Z' ? el->name[i] + 32 : el->name[i]);
        lower[len] = '\0';
        try_bucket(st, TC_KEY_TAG, lower, len, el);
    }
    try_entries(st, st->universal, st->nuniversal, el);
    if (st->nmatches > 1)
        qsort(st->matches, st->nmatches, sizeof *st->matches, match_cmp);
}

/* ---- Presentational hints ---- */

#define MAX_HINTS 24

typedef struct hints {
    tc_decl d[MAX_HINTS];
    uint32_t n;
} hints;

static void hint(hints *h, uint16_t prop, const tc_pval *v)
{
    if (h->n < MAX_HINTS) {
        memset(&h->d[h->n], 0, sizeof h->d[h->n]);
        h->d[h->n].prop = prop;
        h->d[h->n].value = *v;
        h->n++;
    }
}

static void hint_keyword(hints *h, uint16_t prop, int k)
{
    tc_pval v;
    memset(&v, 0, sizeof v);
    v.kind = PV_KEYWORD;
    v.keyword = (uint8_t)k;
    hint(h, prop, &v);
}

static void hint_color(hints *h, uint16_t prop, uint32_t argb)
{
    tc_pval v;
    memset(&v, 0, sizeof v);
    v.kind = PV_COLOR;
    v.n.u = argb;
    hint(h, prop, &v);
}

static void hint_px(styler *st, hints *h, uint16_t prop, float value, int percent)
{
    tc_css_value t;
    tc_pval v;
    memset(&t, 0, sizeof t);
    t.type = percent ? TC_CSS_PERCENTAGE : TC_CSS_DIMENSION;
    t.text = "px";
    t.number = value;
    memset(&v, 0, sizeof v);
    v.kind = PV_LENGTH;
    v.ptr = tc_css_parse_calc(&st->out->arena, &t, TC_ALLOW_LENGTH | TC_ALLOW_PERCENT);
    if (v.ptr)
        hint(h, prop, &v);
}

static int ascii_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

/* The HTML Standard's "rules for parsing non-negative integers". */
static int parse_uint(const char *s, int *out)
{
    long v = 0;
    if (!s)
        return 0;
    while (ascii_space(*s))
        s++;
    if (*s == '+')
        s++;
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9') {
        if (v < 100000000)
            v = v * 10 + (*s - '0');
        s++;
    }
    *out = (int)v;
    return 1;
}

/* "Rules for parsing dimension values": a length (pixels) or a percentage. */
static int parse_dimension(const char *s, float *value, int *percent)
{
    double v = 0, scale = 0.1;
    if (!s)
        return 0;
    while (ascii_space(*s))
        s++;
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9') {
        if (v < 1e8)
            v = v * 10 + (*s - '0');
        s++;
    }
    if (*s == '.' && s[1] >= '0' && s[1] <= '9')
        for (s++; *s >= '0' && *s <= '9'; s++, scale /= 10)
            v += (*s - '0') * scale;
    *percent = *s == '%';
    *value = (float)v;
    return 1;
}

static int hex_digit(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The HTML Standard's "rules for parsing a legacy colour value". */
static int legacy_color(const char *in, uint32_t *argb)
{
    char buf[132], *p;
    size_t len, n, i, comp;
    const char *s = in;
    tc_css_value tok;
    tc_css_color color;
    unsigned rgb[3];

    if (!s)
        return 0;
    while (ascii_space(*s))
        s++;
    len = strlen(s);
    while (len && ascii_space(s[len - 1]))
        len--;
    if (!len || (len == 11 && tc_css_ascii_ieq(s, "transparent")))
        return 0;
    if (len < sizeof buf) {
        memcpy(buf, s, len);
        buf[len] = '\0';
        memset(&tok, 0, sizeof tok);
        tok.type = TC_CSS_IDENT;
        tok.text = buf;
        if (tc_css_parse_color(&tok, 1, &color) == TC_OK && color.space != TC_COLOR_CURRENT) {
            *argb = tc_css_color_argb(&color) | 0xFF000000u;
            return 1;
        }
    }
    if (len == 4 && s[0] == '#' && hex_digit(s[1]) >= 0 && hex_digit(s[2]) >= 0 && hex_digit(s[3]) >= 0) {
        *argb = 0xFF000000u | (uint32_t)(hex_digit(s[1]) * 17) << 16 | (uint32_t)(hex_digit(s[2]) * 17) << 8 |
                (uint32_t)(hex_digit(s[3]) * 17);
        return 1;
    }
    /* Code points beyond the BMP become "00"; at most 128 characters. */
    for (i = 0, n = 0; i < len && n < 128; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0xF0) {
            buf[n++] = '0';
            if (n < 128)
                buf[n++] = '0';
            while (i + 1 < len && ((unsigned char)s[i + 1] & 0xC0) == 0x80)
                i++;
        } else if (c >= 0x80) {
            buf[n++] = '0';
            while (i + 1 < len && ((unsigned char)s[i + 1] & 0xC0) == 0x80)
                i++;
        } else {
            buf[n++] = (char)c;
        }
    }
    p = buf;
    if (n && p[0] == '#')
        p++, n--;
    for (i = 0; i < n; i++)
        if (hex_digit(p[i]) < 0)
            p[i] = '0';
    while (n == 0 || n % 3)
        p[n++] = '0';
    comp = n / 3;
    {
        const char *c[3];
        size_t skip = comp > 8 ? comp - 8 : 0;
        for (i = 0; i < 3; i++)
            c[i] = p + i * comp + skip;
        comp -= skip;
        while (comp > 2 && c[0][0] == '0' && c[1][0] == '0' && c[2][0] == '0') {
            for (i = 0; i < 3; i++)
                c[i]++;
            comp--;
        }
        for (i = 0; i < 3; i++) {
            rgb[i] = (unsigned)hex_digit(c[i][0]);
            if (comp >= 2)
                rgb[i] = rgb[i] * 16 + (unsigned)hex_digit(c[i][1]);
        }
    }
    *argb = 0xFF000000u | rgb[0] << 16 | rgb[1] << 8 | rgb[2];
    return 1;
}

static int attr_ieq(const tc_element *el, const char *name, const char *value)
{
    const char *v = tc_dom_get_attr(el, name);
    return v && tc_css_ascii_ieq(v, value);
}

static const tc_element *element_parent(const tc_element *el)
{
    return tc_element_parent(el);
}

/* The table a cell, row or row group belongs to (as the UA style sheet's
   "table > tr > td" and "table > tbody > tr > td" selectors see it). */
static const tc_element *cell_table(const tc_element *cell)
{
    const tc_element *row = element_parent(cell), *p;
    if (!row || !tc_dom_is_html(&row->node, TC_TAG_TR))
        return NULL;
    p = element_parent(row);
    if (p && (tc_dom_is_html(&p->node, TC_TAG_TBODY) || tc_dom_is_html(&p->node, TC_TAG_THEAD) ||
              tc_dom_is_html(&p->node, TC_TAG_TFOOT)))
        p = element_parent(p);
    return p && tc_dom_is_html(&p->node, TC_TAG_TABLE) ? p : NULL;
}

static void hint_align(hints *h, const tc_element *el)
{
    const char *align = tc_dom_get_attr(el, "align");
    if (!align)
        return;
    if (tc_css_ascii_ieq(align, "center") || tc_css_ascii_ieq(align, "middle"))
        hint_keyword(h, P_TEXT_ALIGN, TC_ALIGN_CENTER);
    else if (tc_css_ascii_ieq(align, "left"))
        hint_keyword(h, P_TEXT_ALIGN, TC_ALIGN_LEFT);
    else if (tc_css_ascii_ieq(align, "right"))
        hint_keyword(h, P_TEXT_ALIGN, TC_ALIGN_RIGHT);
    else if (tc_css_ascii_ieq(align, "justify"))
        hint_keyword(h, P_TEXT_ALIGN, TC_ALIGN_JUSTIFY);
}

static void hint_dimension(styler *st, hints *h, const tc_element *el, const char *attr, uint16_t prop, int nonzero)
{
    float v;
    int pct;
    if (parse_dimension(tc_dom_get_attr(el, attr), &v, &pct) && (!nonzero || v > 0))
        hint_px(st, h, prop, v, pct);
}

static void hint_border_all(styler *st, hints *h, float width, int style)
{
    int i;
    for (i = 0; i < 4; i++) {
        hint_px(st, h, (uint16_t)(P_BORDER_TOP_WIDTH + i), width, 0);
        if (style >= 0)
            hint_keyword(h, (uint16_t)(P_BORDER_TOP_STYLE + i), style);
    }
}

static void legacy_font_size(hints *h, const char *s)
{
    static const uint8_t sizes[] = { FS_X_SMALL, FS_X_SMALL, FS_SMALL, FS_MEDIUM, FS_LARGE, FS_X_LARGE,
                                     FS_XX_LARGE, FS_XXX_LARGE };
    int mode = 0, v;
    while (ascii_space(*s))
        s++;
    if (*s == '+' || *s == '-')
        mode = *s++ == '+' ? 1 : -1;
    if (*s < '0' || *s > '9' || !parse_uint(s, &v))
        return;
    if (mode)
        v = 3 + mode * v;
    v = TC_CLAMP(v, 1, 7);
    hint_keyword(h, P_FONT_SIZE, sizes[v]);
}

static void presentational_hints(styler *st, const tc_element *el, hints *h)
{
    uint32_t argb;
    int n, side;
    const char *a;

    h->n = 0;
    if (el->node.ns != TC_NS_HTML || (!el->attr_count && el->node.tag != TC_TAG_CENTER))
        return;
    switch (el->node.tag) {
    case TC_TAG_BODY:
        if (legacy_color(tc_dom_get_attr(el, "bgcolor"), &argb))
            hint_color(h, P_BACKGROUND_COLOR, argb);
        if (legacy_color(tc_dom_get_attr(el, "text"), &argb))
            hint_color(h, P_COLOR, argb);
        a = tc_dom_get_attr(el, "marginheight");
        if (parse_uint(a ? a : tc_dom_get_attr(el, "topmargin"), &n))
            hint_px(st, h, P_MARGIN_TOP, (float)n, 0);
        if (parse_uint(a ? a : tc_dom_get_attr(el, "bottommargin"), &n))
            hint_px(st, h, P_MARGIN_BOTTOM, (float)n, 0);
        a = tc_dom_get_attr(el, "marginwidth");
        if (parse_uint(a ? a : tc_dom_get_attr(el, "leftmargin"), &n))
            hint_px(st, h, P_MARGIN_LEFT, (float)n, 0);
        if (parse_uint(a ? a : tc_dom_get_attr(el, "rightmargin"), &n))
            hint_px(st, h, P_MARGIN_RIGHT, (float)n, 0);
        break;
    case TC_TAG_A:
    case TC_TAG_AREA:
        if (st->body && tc_dom_get_attr(el, "href") && legacy_color(tc_dom_get_attr(st->body, "link"), &argb))
            hint_color(h, P_COLOR, argb);
        break;
    case TC_TAG_FONT:
        if (legacy_color(tc_dom_get_attr(el, "color"), &argb))
            hint_color(h, P_COLOR, argb);
        if ((a = tc_dom_get_attr(el, "size")) != NULL)
            legacy_font_size(h, a);
        if ((a = tc_dom_get_attr(el, "face")) != NULL && *a) {
            tc_css_value *v;
            uint32_t nv;
            uint16_t props[P_COUNT];
            tc_pval vals[P_COUNT];
            if (tc_css_parse_values(&st->tmp, a, strlen(a), &v, &nv) == TC_OK &&
                tc_prop_parse(&st->out->arena, P_FONT_FAMILY, v, nv, 0, props, vals) == 1)
                hint(h, P_FONT_FAMILY, &vals[0]);
        }
        break;
    case TC_TAG_DIV:
        hint_align(h, el);
        break;
    case TC_TAG_TABLE:
        if (legacy_color(tc_dom_get_attr(el, "bgcolor"), &argb))
            hint_color(h, P_BACKGROUND_COLOR, argb);
        hint_dimension(st, h, el, "width", P_WIDTH, 1);
        hint_dimension(st, h, el, "height", P_HEIGHT, 0);
        if ((a = tc_dom_get_attr(el, "border")) != NULL) {
            if (!parse_uint(a, &n))
                n = 1;
            if (n > 0)
                hint_border_all(st, h, (float)n, TC_BORDER_OUTSET);
            else
                hint_border_all(st, h, 0, -1);
        }
        if (legacy_color(tc_dom_get_attr(el, "bordercolor"), &argb))
            for (n = 0; n < 4; n++)
                hint_color(h, (uint16_t)(P_BORDER_TOP_COLOR + n), argb);
        break;
    case TC_TAG_TD:
    case TC_TAG_TH: {
        const tc_element *table = cell_table(el);
        hint_dimension(st, h, el, "width", P_WIDTH, 1);
        hint_dimension(st, h, el, "height", P_HEIGHT, 1);
        if (table) {
            const char *rules = tc_dom_get_attr(table, "rules");
            if ((a = tc_dom_get_attr(table, "border")) != NULL && (!parse_uint(a, &n) || n > 0))
                hint_border_all(st, h, 1, TC_BORDER_INSET);
            if (rules) {
                if (tc_css_ascii_ieq(rules, "all"))
                    hint_border_all(st, h, 1, TC_BORDER_SOLID);
                else if (tc_css_ascii_ieq(rules, "rows") || tc_css_ascii_ieq(rules, "cols")) {
                    int rows = tc_css_ascii_ieq(rules, "rows");
                    hint_border_all(st, h, 1, TC_BORDER_NONE);
                    hint_keyword(h, rows ? P_BORDER_TOP_STYLE : P_BORDER_LEFT_STYLE, TC_BORDER_SOLID);
                    hint_keyword(h, rows ? P_BORDER_BOTTOM_STYLE : P_BORDER_RIGHT_STYLE, TC_BORDER_SOLID);
                } else if (tc_css_ascii_ieq(rules, "none") || tc_css_ascii_ieq(rules, "groups"))
                    hint_border_all(st, h, 1, TC_BORDER_NONE);
            }
            if (parse_uint(tc_dom_get_attr(table, "cellpadding"), &n))
                for (side = 0; side < 4; side++)
                    hint_px(st, h, (uint16_t)(P_PADDING_TOP + side), (float)n, 0);
            if (legacy_color(tc_dom_get_attr(table, "bordercolor"), &argb))
                for (n = 0; n < 4; n++)
                    hint_color(h, (uint16_t)(P_BORDER_TOP_COLOR + n), argb);
        }
    }
        /* fall through */
    case TC_TAG_TR:
    case TC_TAG_THEAD:
    case TC_TAG_TBODY:
    case TC_TAG_TFOOT:
        if (legacy_color(tc_dom_get_attr(el, "bgcolor"), &argb))
            hint_color(h, P_BACKGROUND_COLOR, argb);
        hint_align(h, el);
        if (el->node.tag == TC_TAG_TR)
            hint_dimension(st, h, el, "height", P_HEIGHT, 1);
        break;
    case TC_TAG_IMG:
    case TC_TAG_OBJECT:
    case TC_TAG_EMBED:
    case TC_TAG_IFRAME:
    case TC_TAG_VIDEO:
    case TC_TAG_CANVAS:
    case TC_TAG_INPUT:
        if (el->node.tag == TC_TAG_INPUT && !attr_ieq(el, "type", "image"))
            break;
        hint_dimension(st, h, el, "width", P_WIDTH, 0);
        hint_dimension(st, h, el, "height", P_HEIGHT, 0);
        if (el->node.tag != TC_TAG_IFRAME && el->node.tag != TC_TAG_VIDEO && el->node.tag != TC_TAG_CANVAS) {
            if (parse_uint(tc_dom_get_attr(el, "hspace"), &n)) {
                hint_px(st, h, P_MARGIN_LEFT, (float)n, 0);
                hint_px(st, h, P_MARGIN_RIGHT, (float)n, 0);
            }
            if (parse_uint(tc_dom_get_attr(el, "vspace"), &n)) {
                hint_px(st, h, P_MARGIN_TOP, (float)n, 0);
                hint_px(st, h, P_MARGIN_BOTTOM, (float)n, 0);
            }
        }
        if ((el->node.tag == TC_TAG_IMG || el->node.tag == TC_TAG_OBJECT) &&
            parse_uint(tc_dom_get_attr(el, "border"), &n))
            hint_border_all(st, h, (float)n, n > 0 ? TC_BORDER_SOLID : -1);
        break;
    case TC_TAG_HR: {
        int has_color = legacy_color(tc_dom_get_attr(el, "color"), &argb);
        int noshade = tc_dom_get_attr(el, "noshade") != NULL;
        hint_dimension(st, h, el, "width", P_WIDTH, 0);
        if (has_color) {
            hint_color(h, P_COLOR, argb);
            hint_color(h, P_BACKGROUND_COLOR, argb);
        }
        if (parse_uint(tc_dom_get_attr(el, "size"), &n)) {
            if (has_color || noshade || tc_dom_get_attr(el, "color"))
                hint_border_all(st, h, n / 2.0f, -1);
            else if (n == 1)
                hint_px(st, h, P_BORDER_BOTTOM_WIDTH, 0, 0);
            else if (n > 1)
                hint_px(st, h, P_HEIGHT, (float)(n - 2), 0);
        }
        break;
    }
    case TC_TAG_MARQUEE:
        if (legacy_color(tc_dom_get_attr(el, "bgcolor"), &argb))
            hint_color(h, P_BACKGROUND_COLOR, argb);
        hint_dimension(st, h, el, "width", P_WIDTH, 0);
        hint_dimension(st, h, el, "height", P_HEIGHT, 0);
        break;
    case TC_TAG_CENTER:
        hint_keyword(h, P_TEXT_ALIGN, TC_ALIGN_CENTER);
        break;
    default:
        break;
    }
}

/* ---- The cascade ---- */

static void cascade_reset(cascade *c)
{
    uint32_t i;
    for (i = 0; i < c->nset; i++)
        c->win[c->set[i]] = c->ua_win[c->set[i]] = NULL;
    c->nset = 0;
    c->ncustom = 0;
}

static void cascade_set(styler *st, const tc_decl *d, int origin)
{
    cascade *c = &st->c;
    if (d->prop == P_CUSTOM) {
        const tc_decl **grown = grow((void *)c->custom, &c->custom_cap, c->ncustom, sizeof *c->custom);
        if (!grown) {
            st->nomem = 1;
            return;
        }
        c->custom = grown;
        c->custom[c->ncustom++] = d;
        return;
    }
    if (!c->win[d->prop])
        c->set[c->nset++] = (uint8_t)d->prop;
    c->win[d->prop] = d;
    c->origin[d->prop] = (uint8_t)origin;
    if (origin == TC_ORIGIN_UA)
        c->ua_win[d->prop] = d;
}

static void apply_decls(styler *st, const tc_decl *d, uint32_t n, int important, int origin)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (d[i].important == important)
            cascade_set(st, &d[i], origin);
}

/* Run the cascade over matches [from, to) (sorted; one pseudo-element),
   presentational hints and the style attribute. */
static void run_cascade(styler *st, uint32_t from, uint32_t to, const hints *h, const tc_decl *attr, uint32_t nattr)
{
    const match *m = st->matches;
    uint32_t i, author = from, end;

    cascade_reset(&st->c);
    /* Normal declarations: the user agent's, then hints, then authors' by
       layer, specificity and order, then the style attribute. */
    for (i = from; i < to && m[i].origin == TC_ORIGIN_UA; i++)
        apply_decls(st, m[i].rule->decls, m[i].rule->ndecls, 0, TC_ORIGIN_UA);
    author = i;
    if (h)
        apply_decls(st, h->d, h->n, 0, TC_ORIGIN_AUTHOR);
    for (; i < to; i++)
        apply_decls(st, m[i].rule->decls, m[i].rule->ndecls, 0, TC_ORIGIN_AUTHOR);
    apply_decls(st, attr, nattr, 0, TC_ORIGIN_AUTHOR);
    /* !important ones: authors' with the layer order reversed, the style
       attribute's, then the user agent's. */
    for (end = to; end > author;) {
        uint32_t start = end;
        while (start > author && m[start - 1].rank == m[end - 1].rank)
            start--;
        for (i = start; i < end; i++)
            apply_decls(st, m[i].rule->decls, m[i].rule->ndecls, 1, TC_ORIGIN_AUTHOR);
        end = start;
    }
    apply_decls(st, attr, nattr, 1, TC_ORIGIN_AUTHOR);
    for (i = from; i < author; i++)
        apply_decls(st, m[i].rule->decls, m[i].rule->ndecls, 1, TC_ORIGIN_UA);
}

/* ---- Custom properties and var() ---- */

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

static const tc_tokens *map_lookup(const tc_custom_props *m, const char *name)
{
    uint32_t lo = 0, hi = m ? m->count : 0;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int cmp = strcmp(name, m->items[mid].name);
        if (!cmp)
            return &m->items[mid].value;
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

enum { CW_PENDING, CW_RESOLVING, CW_DONE, CW_INVALID, CW_INHERIT };

typedef struct cwin {
    const char *name;
    const tc_decl *decl;
    uint32_t order;
    uint8_t state;
    tc_tokens value;
} cwin;

typedef struct resolver {
    styler *st;
    cwin *wins;                     /* this element's custom properties (NULL once they're final) */
    uint32_t nwins;
    const tc_custom_props *parent;  /* or the element's final map */
    int depth;
} resolver;

static int resolve_win(resolver *r, cwin *w);

static const tc_tokens *var_lookup(resolver *r, const char *name)
{
    uint32_t lo = 0, hi = r->nwins;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int cmp = strcmp(name, r->wins[mid].name);
        if (!cmp) {
            cwin *w = &r->wins[mid];
            if (w->state == CW_PENDING)
                resolve_win(r, w);
            if (w->state == CW_DONE)
                return &w->value;
            if (w->state != CW_INHERIT)
                return NULL; /* invalid, or a cycle */
            break;
        }
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return map_lookup(r->parent, name);
}

static int substitute(resolver *r, const tc_css_value *v, uint32_t n, tvec *out, int depth);

/* var(--name[, fallback]) or env(name[, fallback]). */
static int substitute_function(resolver *r, const tc_css_value *f, tvec *out, int depth)
{
    uint32_t i = 0, k;
    const tc_css_value *name;
    int is_env = tc_css_ascii_ieq(f->text, "env");
    while (i < f->nitems && f->items[i].type == TC_CSS_WHITESPACE)
        i++;
    if (i >= f->nitems || f->items[i].type != TC_CSS_IDENT)
        return 0;
    name = &f->items[i++];
    if (!is_env && strncmp(name->text, "--", 2) != 0)
        return 0;
    while (i < f->nitems && f->items[i].type == TC_CSS_WHITESPACE)
        i++;
    if (i < f->nitems && f->items[i].type != TC_CSS_COMMA)
        return 0;
    if (!is_env) {
        const tc_tokens *value = var_lookup(r, name->text);
        if (value) {
            for (k = 0; k < value->n; k++)
                if (!tpush(out, &value->v[k]))
                    return 0;
            return 1;
        }
    } else if (strncmp(name->text, "safe-area-inset-", 16) == 0 && i >= f->nitems) {
        tc_css_value zero;
        memset(&zero, 0, sizeof zero);
        zero.type = TC_CSS_DIMENSION;
        zero.text = "px";
        zero.repr = "0";
        zero.flags = TC_CSS_INTEGER;
        return tpush(out, &zero);
    }
    if (i >= f->nitems)
        return 0; /* no fallback */
    return substitute(r, f->items + i + 1, f->nitems - i - 1, out, depth + 1);
}

static int substitute(resolver *r, const tc_css_value *v, uint32_t n, tvec *out, int depth)
{
    uint32_t i;
    if (depth > 32)
        return 0;
    for (i = 0; i < n; i++) {
        const tc_css_value *t = &v[i];
        if (t->type == TC_CSS_FUNCTION && (tc_css_ascii_ieq(t->text, "var") || tc_css_ascii_ieq(t->text, "env"))) {
            if (!substitute_function(r, t, out, depth))
                return 0;
        } else if ((t->type == TC_CSS_FUNCTION || t->type == TC_CSS_BLOCK) && tc_tokens_have_var(t->items, t->nitems)) {
            tvec inner = { NULL, 0, 0 };
            tc_css_value copy = *t;
            int ok = substitute(r, t->items, t->nitems, &inner, depth + 1);
            if (ok) {
                copy.items = tc_arena_alloc(&r->st->tmp, (inner.len ? inner.len : 1) * sizeof *copy.items);
                if (copy.items) {
                    memcpy(copy.items, inner.items, inner.len * sizeof *copy.items);
                    copy.nitems = inner.len;
                    ok = tpush(out, &copy);
                } else {
                    ok = 0;
                }
            }
            free(inner.items);
            if (!ok)
                return 0;
        } else if (!tpush(out, t)) {
            return 0;
        }
    }
    return 1;
}

static int resolve_win(resolver *r, cwin *w)
{
    const tc_tokens *raw = w->decl->value.ptr;
    tvec out = { NULL, 0, 0 };
    int ok;
    if (w->state != CW_PENDING)
        return w->state == CW_DONE;
    if (r->depth > 64) {
        w->state = CW_INVALID;
        return 0;
    }
    w->state = CW_RESOLVING;
    r->depth++;
    ok = substitute(r, raw->v, raw->n, &out, 0);
    r->depth--;
    if (ok && w->state == CW_RESOLVING) {
        w->value.v = tc_tokens_copy(&r->st->out->arena, out.items, out.len);
        w->value.n = out.len;
        if (!w->value.v)
            r->st->nomem = 1;
    }
    free(out.items);
    w->state = ok && w->value.v ? CW_DONE : CW_INVALID;
    return w->state == CW_DONE;
}

static int cwin_cmp(const void *pa, const void *pb)
{
    const cwin *a = pa, *b = pb;
    int cmp = strcmp(a->name, b->name);
    if (cmp)
        return cmp;
    return a->order < b->order ? -1 : a->order > b->order;
}

static int tokens_equal(const tc_css_value *a, uint32_t na, const tc_css_value *b, uint32_t nb, int depth)
{
    uint32_t i;
    if (na != nb)
        return 0;
    if (a == b)
        return 1;
    if (depth > 32)
        return 0;
    for (i = 0; i < na; i++) {
        if (a[i].type != b[i].type || a[i].flags != b[i].flags || a[i].cp != b[i].cp ||
            a[i].number != b[i].number || strcmp(a[i].text, b[i].text) != 0 ||
            (a[i].repr == NULL) != (b[i].repr == NULL) || (a[i].repr && strcmp(a[i].repr, b[i].repr) != 0) ||
            !tokens_equal(a[i].items, a[i].nitems, b[i].items, b[i].nitems, depth + 1))
            return 0;
    }
    return 1;
}

static int wide_value(const tc_tokens *t)
{
    static const char *const names[] = { "initial", "inherit", "unset", "revert", "revert-layer" };
    uint32_t i, found = 0;
    int k = -1;
    size_t j;
    for (i = 0; i < t->n; i++) {
        if (t->v[i].type == TC_CSS_WHITESPACE)
            continue;
        if (found++ || t->v[i].type != TC_CSS_IDENT)
            return -1;
        for (j = 0; j < TC_ARRAY_LEN(names); j++)
            if (tc_css_ascii_ieq(t->v[i].text, names[j]))
                k = (int)j;
    }
    return found == 1 ? k : -1;
}

/* The element's custom properties: its parent's, with its own
   declarations' values (var() substituted) on top. */
static const tc_custom_props *custom_props(styler *st, const tc_custom_props *parent)
{
    cascade *c = &st->c;
    cwin *wins;
    resolver r;
    uint32_t n = 0, i, k, total, changed = 0;
    tc_custom_props *map;
    cprop *items;

    if (!c->ncustom)
        return parent;
    wins = malloc(c->ncustom * sizeof *wins);
    if (!wins) {
        st->nomem = 1;
        return parent;
    }
    for (i = 0; i < c->ncustom; i++) {
        wins[i].name = c->custom[i]->name;
        wins[i].decl = c->custom[i];
        wins[i].order = i;
        wins[i].state = CW_PENDING;
        wins[i].value.v = NULL;
        wins[i].value.n = 0;
    }
    qsort(wins, c->ncustom, sizeof *wins, cwin_cmp);
    for (i = 0; i < c->ncustom; i++) /* the last declaration of each name wins */
        if (i + 1 == c->ncustom || strcmp(wins[i].name, wins[i + 1].name) != 0)
            wins[n++] = wins[i];
    for (i = 0; i < n; i++) {
        const tc_tokens *raw = wins[i].decl->value.ptr;
        int wide = wide_value(raw);
        if (wide == WIDE_INITIAL)
            wins[i].state = CW_INVALID;
        else if (wide >= 0)
            wins[i].state = CW_INHERIT;
        else if (!wins[i].decl->has_var)
            wins[i].state = CW_DONE, wins[i].value = *raw;
        else
            wins[i].state = CW_PENDING;
    }
    r.st = st;
    r.wins = wins;
    r.nwins = n;
    r.parent = parent;
    r.depth = 0;
    for (i = 0; i < n; i++) {
        const tc_tokens *old = map_lookup(parent, wins[i].name);
        if (wins[i].state == CW_PENDING)
            resolve_win(&r, &wins[i]);
        if (wins[i].state == CW_DONE)
            changed += !old || !tokens_equal(old->v, old->n, wins[i].value.v, wins[i].value.n, 0);
        else if (wins[i].state == CW_INVALID)
            changed += old != NULL;
    }
    if (!changed) {
        free(wins);
        return parent;
    }
    /* Merge the two sorted lists. */
    total = (parent ? parent->count : 0) + n;
    map = tc_arena_alloc(&st->out->arena, sizeof *map);
    items = tc_arena_alloc(&st->out->arena, (total ? total : 1) * sizeof *items);
    if (!map || !items) {
        free(wins);
        st->nomem = 1;
        return parent;
    }
    map->count = 0;
    for (i = 0, k = 0; i < (parent ? parent->count : 0) || k < n;) {
        int cmp = i >= (parent ? parent->count : 0) ? 1 : k >= n ? -1 : strcmp(parent->items[i].name, wins[k].name);
        if (cmp < 0) {
            items[map->count++] = parent->items[i++];
            continue;
        }
        if (cmp == 0 && wins[k].state == CW_INHERIT)
            items[map->count++] = parent->items[i];
        else if (wins[k].state == CW_DONE) {
            items[map->count].name = wins[k].name;
            items[map->count++].value = wins[k].value;
        }
        if (cmp == 0)
            i++;
        k++;
    }
    map->items = items;
    free(wins);
    return map;
}

static var_result *var_slot(styler *st, const tc_var_value *var, const tc_custom_props *custom)
{
    tc_styled *o = st->out;
    uint32_t mask, i;
    if (o->nvars * 2 >= o->vars_cap) {
        uint32_t cap = o->vars_cap ? o->vars_cap * 2 : 256, k;
        var_result *grown = calloc(cap, sizeof *grown);
        if (!grown)
            return NULL;
        for (k = 0; k < o->vars_cap; k++)
            if (o->vars[k].var) {
                uint32_t j = (hash_bytes(&o->vars[k].var, sizeof o->vars[k].var, 2166136261u) ^
                              hash_bytes(&o->vars[k].custom, sizeof o->vars[k].custom, 0)) & (cap - 1);
                while (grown[j].var)
                    j = (j + 1) & (cap - 1);
                grown[j] = o->vars[k];
            }
        free(o->vars);
        o->vars = grown;
        o->vars_cap = cap;
    }
    mask = o->vars_cap - 1;
    i = (hash_bytes(&var, sizeof var, 2166136261u) ^ hash_bytes(&custom, sizeof custom, 0)) & mask;
    while (o->vars[i].var && (o->vars[i].var != var || o->vars[i].custom != custom))
        i = (i + 1) & mask;
    return &o->vars[i];
}

/* A var() declaration's value for prop, for an element with these custom
   properties. 0: invalid at computed-value time. */
static int var_value(styler *st, const tc_var_value *var, uint16_t prop, const tc_custom_props *custom, tc_pval *out)
{
    var_result *res = var_slot(st, var, custom);
    uint32_t i;
    if (!res) {
        st->nomem = 1;
        return 0;
    }
    if (!res->var) {
        resolver r;
        tvec t = { NULL, 0, 0 };
        uint16_t props[P_COUNT];
        tc_pval vals[P_COUNT];
        uint32_t n = 0;
        r.st = st;
        r.wins = NULL;
        r.nwins = 0;
        r.parent = custom;
        r.depth = 0;
        if (substitute(&r, var->tokens.v, var->tokens.n, &t, 0))
            n = tc_prop_parse(&st->out->arena, var->prop, t.items, t.len, st->quirks, props, vals);
        free(t.items);
        res->props = tc_arena_alloc(&st->out->arena, (n ? n : 1) * sizeof *res->props);
        res->vals = tc_arena_alloc(&st->out->arena, (n ? n : 1) * sizeof *res->vals);
        if (!res->props || !res->vals) {
            st->nomem = 1;
            return 0;
        }
        memcpy(res->props, props, n * sizeof *props);
        memcpy(res->vals, vals, n * sizeof *vals);
        res->n = (uint16_t)n;
        res->var = var;
        res->custom = custom;
        st->out->nvars++;
    }
    for (i = 0; i < res->n; i++)
        if (res->props[i] == prop) {
            *out = res->vals[i];
            return out->kind != PV_VAR;
        }
    return 0;
}

/* ---- Computing values ---- */

static void copy_prop(tc_style *s, const tc_style *from, uint16_t p)
{
    int side;
    switch (p) {
    case P_DISPLAY: s->display = from->display; break;
    case P_POSITION: s->position = from->position; break;
    case P_FLOAT: s->float_ = from->float_; break;
    case P_CLEAR: s->clear = from->clear; break;
    case P_BOX_SIZING: s->box_sizing = from->box_sizing; break;
    case P_OVERFLOW_X: s->overflow_x = from->overflow_x; break;
    case P_OVERFLOW_Y: s->overflow_y = from->overflow_y; break;
    case P_MARGIN_TOP: case P_MARGIN_RIGHT: case P_MARGIN_BOTTOM: case P_MARGIN_LEFT:
        s->margin[p - P_MARGIN_TOP] = from->margin[p - P_MARGIN_TOP];
        break;
    case P_PADDING_TOP: case P_PADDING_RIGHT: case P_PADDING_BOTTOM: case P_PADDING_LEFT:
        s->padding[p - P_PADDING_TOP] = from->padding[p - P_PADDING_TOP];
        break;
    case P_BORDER_TOP_WIDTH: case P_BORDER_RIGHT_WIDTH: case P_BORDER_BOTTOM_WIDTH: case P_BORDER_LEFT_WIDTH:
        s->border_width[p - P_BORDER_TOP_WIDTH] = from->border_width[p - P_BORDER_TOP_WIDTH];
        break;
    case P_BORDER_TOP_STYLE: case P_BORDER_RIGHT_STYLE: case P_BORDER_BOTTOM_STYLE: case P_BORDER_LEFT_STYLE:
        s->border_style[p - P_BORDER_TOP_STYLE] = from->border_style[p - P_BORDER_TOP_STYLE];
        break;
    case P_BORDER_TOP_COLOR: case P_BORDER_RIGHT_COLOR: case P_BORDER_BOTTOM_COLOR: case P_BORDER_LEFT_COLOR:
        side = p - P_BORDER_TOP_COLOR;
        s->border_color[side] = from->border_color[side];
        s->currentcolor = (uint8_t)((s->currentcolor & ~(1u << side)) | (from->currentcolor & (1u << side)));
        break;
    case P_BACKGROUND_COLOR:
        s->background_color = from->background_color;
        s->currentcolor = (uint8_t)((s->currentcolor & ~TC_CC_BACKGROUND) | (from->currentcolor & TC_CC_BACKGROUND));
        break;
    case P_WIDTH: s->width = from->width; break;
    case P_HEIGHT: s->height = from->height; break;
    case P_MIN_WIDTH: s->min_width = from->min_width; break;
    case P_MIN_HEIGHT: s->min_height = from->min_height; break;
    case P_MAX_WIDTH: s->max_width = from->max_width; break;
    case P_MAX_HEIGHT: s->max_height = from->max_height; break;
    case P_TOP: case P_RIGHT: case P_BOTTOM: case P_LEFT:
        s->inset[p - P_TOP] = from->inset[p - P_TOP];
        break;
    case P_VERTICAL_ALIGN:
        s->vertical_align = from->vertical_align;
        s->vertical_align_length = from->vertical_align_length;
        break;
    case P_TEXT_DECORATION_LINE: s->decoration_line = from->decoration_line; break;
    case P_TEXT_DECORATION_STYLE: s->decoration_style = from->decoration_style; break;
    case P_TEXT_DECORATION_COLOR:
        s->decoration_color = from->decoration_color;
        s->currentcolor = (uint8_t)((s->currentcolor & ~TC_CC_DECORATION) | (from->currentcolor & TC_CC_DECORATION));
        break;
    case P_CONTENT:
        s->content_kind = from->content_kind;
        s->content = from->content;
        break;
    case P_Z_INDEX:
        s->z_auto = from->z_auto;
        s->z_index = from->z_index;
        break;
    case P_COLOR: s->color = from->color; break;
    case P_FONT_FAMILY: s->font_family = from->font_family; break;
    case P_FONT_SIZE:
        s->font_size = from->font_size;
        s->font_size_keyword = from->font_size_keyword;
        s->font_size_factor = from->font_size_factor;
        break;
    case P_FONT_WEIGHT: s->font_weight = from->font_weight; break;
    case P_FONT_STYLE: s->font_style = from->font_style; break;
    case P_LINE_HEIGHT:
        s->line_height_kind = from->line_height_kind;
        s->line_height = from->line_height;
        break;
    case P_TEXT_ALIGN: s->text_align = from->text_align; break;
    case P_TEXT_TRANSFORM: s->text_transform = from->text_transform; break;
    case P_WHITE_SPACE_COLLAPSE: s->ws_collapse = from->ws_collapse; break;
    case P_TEXT_WRAP_MODE: s->text_wrap = from->text_wrap; break;
    case P_VISIBILITY: s->visibility = from->visibility; break;
    case P_LIST_STYLE_TYPE:
        s->list_style_type = from->list_style_type;
        s->list_style_string = from->list_style_string;
        break;
    case P_LIST_STYLE_POSITION: s->list_style_position = from->list_style_position; break;
    case P_OVERFLOW_WRAP: s->overflow_wrap = from->overflow_wrap; break;
    case P_WORD_BREAK: s->word_break = from->word_break; break;
    case P_CURSOR: s->cursor = from->cursor; break;
    case P_TEXT_INDENT: s->text_indent = from->text_indent; break;
    case P_LETTER_SPACING: s->letter_spacing = from->letter_spacing; break;
    case P_WORD_SPACING: s->word_spacing = from->word_spacing; break;
    default: break;
    }
}

static void basis_of(const styler *st, float font_size, tc_length_basis *b)
{
    b->font_size = font_size;
    b->root_font_size = st->root_font_size;
    b->viewport_w = st->env->width;
    b->viewport_h = st->env->height;
}

/* A computed length-percentage (percentages kept for layout). */
static void length_of(styler *st, float font_size, const tc_pval *v, tc_length *out)
{
    tc_length_basis b;
    memset(out, 0, sizeof *out);
    if (v->kind == PV_NUMBER) {
        out->px = v->n.f;
        return;
    }
    if (v->kind != PV_LENGTH)
        return;
    basis_of(st, font_size, &b);
    if (tc_calc_compute(&st->out->arena, v->ptr, &b, out) != TC_OK)
        st->nomem = 1;
}

/* A length resolved now, percentages against pct_basis. */
static float px_of(styler *st, float font_size, const tc_pval *v, float pct_basis)
{
    tc_length len;
    length_of(st, font_size, v, &len);
    return tc_length_resolve(&len, pct_basis);
}

static void size_of(styler *st, const tc_style *s, const tc_pval *v, tc_size *out)
{
    memset(out, 0, sizeof *out);
    if (v->kind == PV_KEYWORD) {
        out->kind = v->keyword;
    } else {
        out->kind = TC_SIZE_LENGTH;
        length_of(st, s->font_size, v, &out->length);
    }
}

static int is_monospace(const tc_style *s)
{
    return s->font_family && s->font_family->count == 1 && strcmp(s->font_family->names[0], "monospace") == 0;
}

/* The absolute-size keywords' sizes (CSS Fonts 4), for medium = 16px, or
   13px in monospace text as other browsers do. */
static float keyword_size(int k, int mono)
{
    static const float scale[] = { 3.0f / 5, 3.0f / 4, 8.0f / 9, 1, 6.0f / 5, 3.0f / 2, 2, 3 };
    return scale[TC_CLAMP(k, 0, 7)] * (mono ? 13 : 16);
}

static void compute_font_size(styler *st, tc_style *s, const tc_style *parent, const tc_pval *v)
{
    if (v->kind == PV_KEYWORD) {
        if (v->keyword <= FS_XXX_LARGE) {
            s->font_size_keyword = (uint8_t)(1 + v->keyword);
            s->font_size_factor = 1;
        } else {
            float f = v->keyword == FS_LARGER ? 1.2f : 1 / 1.2f;
            if (parent->font_size_keyword)
                s->font_size_factor = parent->font_size_factor * f;
            else
                s->font_size = parent->font_size * f, s->font_size_keyword = 0;
        }
        return;
    }
    if (v->kind == PV_NUMBER) {
        s->font_size = v->n.f;
        s->font_size_keyword = 0;
        return;
    }
    if (v->kind == PV_LENGTH) {
        const tc_calc *c = v->ptr;
        if (c->op == TC_CALC_VALUE && (c->unit == TC_UNIT_EM || c->unit == TC_UNIT_PERCENT) &&
            parent->font_size_keyword) {
            s->font_size_keyword = parent->font_size_keyword;
            s->font_size_factor = parent->font_size_factor * (c->unit == TC_UNIT_EM ? c->value : c->value / 100);
        } else {
            s->font_size = px_of(st, parent->font_size, v, parent->font_size);
            s->font_size_keyword = 0;
            if (s->font_size < 0)
                s->font_size = 0;
        }
    }
}

static const tc_content *resolve_content(styler *st, const tc_content *c, const tc_element *el)
{
    tc_content *out;
    uint32_t i;
    for (i = 0; i < c->count; i++)
        if (c->items[i].type == TC_CONTENT_ATTR)
            break;
    if (i == c->count)
        return c;
    out = tc_arena_alloc(&st->out->arena, sizeof *out);
    if (!out || !(out->items = tc_arena_alloc(&st->out->arena, (c->count ? c->count : 1) * sizeof *out->items))) {
        st->nomem = 1;
        return c;
    }
    out->count = c->count;
    for (i = 0; i < c->count; i++) {
        out->items[i] = c->items[i];
        if (c->items[i].type == TC_CONTENT_ATTR) {
            const char *v = el ? tc_dom_get_attr(el, c->items[i].text) : NULL;
            out->items[i].type = TC_CONTENT_STRING;
            out->items[i].text = v ? v : "";
        }
    }
    return out;
}

static void set_color(tc_style *s, const tc_pval *v, uint32_t *field, uint8_t bit)
{
    if (v->kind == PV_CURRENTCOLOR) {
        s->currentcolor |= bit;
    } else {
        *field = v->n.u;
        s->currentcolor = (uint8_t)(s->currentcolor & ~bit);
    }
}

static float snap_border(float w)
{
    if (w <= 0)
        return 0;
    return w < 1 ? 1 : (float)floor(w);
}

static void apply(styler *st, tc_style *s, const tc_style *parent, uint16_t p, const tc_pval *v, const tc_element *el)
{
    switch (p) {
    case P_DISPLAY: s->display = v->keyword; break;
    case P_POSITION: s->position = v->keyword; break;
    case P_FLOAT: s->float_ = v->keyword; break;
    case P_CLEAR: s->clear = v->keyword; break;
    case P_BOX_SIZING: s->box_sizing = v->keyword; break;
    case P_OVERFLOW_X: s->overflow_x = v->keyword; break;
    case P_OVERFLOW_Y: s->overflow_y = v->keyword; break;
    case P_MARGIN_TOP: case P_MARGIN_RIGHT: case P_MARGIN_BOTTOM: case P_MARGIN_LEFT:
        size_of(st, s, v, &s->margin[p - P_MARGIN_TOP]);
        break;
    case P_PADDING_TOP: case P_PADDING_RIGHT: case P_PADDING_BOTTOM: case P_PADDING_LEFT:
        length_of(st, s->font_size, v, &s->padding[p - P_PADDING_TOP]);
        break;
    case P_BORDER_TOP_WIDTH: case P_BORDER_RIGHT_WIDTH: case P_BORDER_BOTTOM_WIDTH: case P_BORDER_LEFT_WIDTH:
        s->border_width[p - P_BORDER_TOP_WIDTH] = snap_border(px_of(st, s->font_size, v, 0));
        break;
    case P_BORDER_TOP_STYLE: case P_BORDER_RIGHT_STYLE: case P_BORDER_BOTTOM_STYLE: case P_BORDER_LEFT_STYLE:
        s->border_style[p - P_BORDER_TOP_STYLE] = v->keyword;
        break;
    case P_BORDER_TOP_COLOR: case P_BORDER_RIGHT_COLOR: case P_BORDER_BOTTOM_COLOR: case P_BORDER_LEFT_COLOR:
        set_color(s, v, &s->border_color[p - P_BORDER_TOP_COLOR], (uint8_t)(1u << (p - P_BORDER_TOP_COLOR)));
        break;
    case P_BACKGROUND_COLOR: set_color(s, v, &s->background_color, TC_CC_BACKGROUND); break;
    case P_WIDTH: size_of(st, s, v, &s->width); break;
    case P_HEIGHT: size_of(st, s, v, &s->height); break;
    case P_MIN_WIDTH: size_of(st, s, v, &s->min_width); break;
    case P_MIN_HEIGHT: size_of(st, s, v, &s->min_height); break;
    case P_MAX_WIDTH: size_of(st, s, v, &s->max_width); break;
    case P_MAX_HEIGHT: size_of(st, s, v, &s->max_height); break;
    case P_TOP: case P_RIGHT: case P_BOTTOM: case P_LEFT:
        size_of(st, s, v, &s->inset[p - P_TOP]);
        break;
    case P_VERTICAL_ALIGN:
        memset(&s->vertical_align_length, 0, sizeof s->vertical_align_length);
        if (v->kind == PV_KEYWORD) {
            s->vertical_align = v->keyword;
        } else {
            /* Percentages are of the line height. */
            float lh = s->line_height_kind == TC_LINE_HEIGHT_PX       ? s->line_height
                       : s->line_height_kind == TC_LINE_HEIGHT_NUMBER ? s->line_height * s->font_size
                                                                       : 1.2f * s->font_size;
            s->vertical_align = TC_VALIGN_LENGTH;
            s->vertical_align_length.px = px_of(st, s->font_size, v, lh);
        }
        break;
    case P_TEXT_DECORATION_LINE: s->decoration_line = (uint8_t)v->n.i; break;
    case P_TEXT_DECORATION_STYLE: s->decoration_style = v->keyword; break;
    case P_TEXT_DECORATION_COLOR: set_color(s, v, &s->decoration_color, TC_CC_DECORATION); break;
    case P_CONTENT:
        if (v->kind == PV_KEYWORD) {
            s->content_kind = v->keyword == CONTENT_NONE ? TC_GENERATED_NONE : TC_GENERATED_NORMAL;
            s->content = NULL;
        } else {
            s->content_kind = TC_GENERATED_ITEMS;
            s->content = resolve_content(st, v->ptr, el);
        }
        break;
    case P_Z_INDEX:
        s->z_auto = v->kind == PV_KEYWORD;
        s->z_index = v->kind == PV_INT ? v->n.i : 0;
        break;
    case P_COLOR:
        s->color = v->kind == PV_CURRENTCOLOR ? parent->color : v->n.u;
        break;
    case P_FONT_FAMILY: s->font_family = v->ptr; break;
    case P_FONT_SIZE: compute_font_size(st, s, parent, v); break;
    case P_FONT_WEIGHT:
        if (v->kind == PV_NUMBER) {
            s->font_weight = (uint16_t)(v->n.f + 0.5f);
        } else {
            int w = parent->font_weight;
            if (v->keyword == FW_BOLDER)
                s->font_weight = (uint16_t)(w < 350 ? 400 : w < 550 ? 700 : w < 900 ? 900 : w);
            else
                s->font_weight = (uint16_t)(w < 100 ? w : w < 550 ? 100 : w < 750 ? 400 : 700);
        }
        break;
    case P_FONT_STYLE: s->font_style = v->keyword; break;
    case P_LINE_HEIGHT:
        if (v->kind == PV_KEYWORD) {
            s->line_height_kind = TC_LINE_HEIGHT_NORMAL;
            s->line_height = 0;
        } else if (v->kind == PV_NUMBER) {
            s->line_height_kind = TC_LINE_HEIGHT_NUMBER;
            s->line_height = v->n.f;
        } else {
            s->line_height_kind = TC_LINE_HEIGHT_PX;
            s->line_height = px_of(st, s->font_size, v, s->font_size);
        }
        break;
    case P_TEXT_ALIGN: s->text_align = v->keyword; break;
    case P_TEXT_TRANSFORM: s->text_transform = v->keyword; break;
    case P_WHITE_SPACE_COLLAPSE: s->ws_collapse = v->keyword; break;
    case P_TEXT_WRAP_MODE: s->text_wrap = v->keyword; break;
    case P_VISIBILITY: s->visibility = v->keyword; break;
    case P_LIST_STYLE_TYPE:
        if (v->kind == PV_PTR) {
            s->list_style_type = TC_LIST_STRING;
            s->list_style_string = v->ptr;
        } else {
            s->list_style_type = v->keyword;
            s->list_style_string = NULL;
        }
        break;
    case P_LIST_STYLE_POSITION: s->list_style_position = v->keyword; break;
    case P_OVERFLOW_WRAP: s->overflow_wrap = v->keyword; break;
    case P_WORD_BREAK: s->word_break = v->keyword; break;
    case P_CURSOR: s->cursor = v->keyword; break;
    case P_TEXT_INDENT: length_of(st, s->font_size, v, &s->text_indent); break;
    case P_LETTER_SPACING:
        s->letter_spacing = v->kind == PV_KEYWORD ? 0 : px_of(st, s->font_size, v, 0);
        break;
    case P_WORD_SPACING:
        /* Percentages are of the space's width: a quarter of an em, near enough. */
        s->word_spacing = v->kind == PV_KEYWORD ? 0 : px_of(st, s->font_size, v, s->font_size / 4);
        break;
    default: break;
    }
}

/* The declaration that decides p, after revert. */
static const tc_decl *effective(const cascade *c, uint16_t p)
{
    const tc_decl *d = c->win[p];
    if (d && d->value.kind == PV_WIDE &&
        (d->value.keyword == WIDE_REVERT || d->value.keyword == WIDE_REVERT_LAYER)) {
        /* Back to the user agent's value; reverting that is unset. Tachyon
           treats revert-layer as revert. */
        d = c->origin[p] == TC_ORIGIN_AUTHOR ? c->ua_win[p] : NULL;
        if (d && d->value.kind == PV_WIDE && d->value.keyword >= WIDE_REVERT)
            d = NULL;
    }
    return d;
}

static void compute_prop(styler *st, tc_style *s, const tc_style *parent, uint16_t p, const tc_element *el)
{
    const tc_decl *d = effective(&st->c, p);
    tc_pval v;
    if (!d)
        return; /* inherited or initial already */
    v = d->value;
    if (v.kind == PV_VAR && !var_value(st, v.ptr, p, s->custom, &v)) {
        v.kind = PV_WIDE;
        v.keyword = WIDE_UNSET;
    }
    if (v.kind == PV_WIDE) {
        int k = v.keyword;
        if (k != WIDE_INITIAL && k != WIDE_INHERIT)
            k = tc_prop_inherited(p) ? WIDE_INHERIT : WIDE_INITIAL;
        copy_prop(s, k == WIDE_INHERIT ? parent : &st->initial, p);
        return;
    }
    apply(st, s, parent, p, &v, el);
}

static uint8_t blockify(uint8_t d)
{
    switch (d) {
    case TC_DISPLAY_INLINE:
    case TC_DISPLAY_INLINE_BLOCK:
    case TC_DISPLAY_RUBY:
    case TC_DISPLAY_RUBY_TEXT:
    case TC_DISPLAY_TABLE_ROW_GROUP:
    case TC_DISPLAY_TABLE_HEADER_GROUP:
    case TC_DISPLAY_TABLE_FOOTER_GROUP:
    case TC_DISPLAY_TABLE_ROW:
    case TC_DISPLAY_TABLE_CELL:
    case TC_DISPLAY_TABLE_COLUMN_GROUP:
    case TC_DISPLAY_TABLE_COLUMN:
    case TC_DISPLAY_TABLE_CAPTION: return TC_DISPLAY_BLOCK;
    case TC_DISPLAY_INLINE_TABLE: return TC_DISPLAY_TABLE;
    case TC_DISPLAY_INLINE_FLEX: return TC_DISPLAY_FLEX;
    case TC_DISPLAY_INLINE_GRID: return TC_DISPLAY_GRID;
    default: return d;
    }
}

static void compute(styler *st, tc_style *s, const tc_style *parent, const tc_element *el, int is_root)
{
    static const uint16_t first[] = { P_FONT_FAMILY, P_FONT_SIZE, P_COLOR, P_LINE_HEIGHT };
    size_t inh = offsetof(tc_style, color);
    uint16_t p;
    size_t i;
    int atomic;

    memcpy(s, &st->initial, sizeof *s);
    memcpy((char *)s + inh, (const char *)parent + inh, sizeof *s - inh);
    s->custom = custom_props(st, parent->custom);
    /* Fonts first: em units depend on font-size, and font-size on the
       family (monospace). Then colour, for currentcolor. */
    for (i = 0; i < TC_ARRAY_LEN(first); i++) {
        compute_prop(st, s, parent, first[i], el);
        if (first[i] == P_FONT_SIZE) {
            if (s->font_size_keyword)
                s->font_size = keyword_size(s->font_size_keyword - 1, is_monospace(s)) * s->font_size_factor;
            if (is_root)
                st->root_font_size = s->font_size;
        }
    }
    for (i = 0; i < st->c.nset; i++) {
        p = st->c.set[i];
        if (p != P_FONT_FAMILY && p != P_FONT_SIZE && p != P_COLOR && p != P_LINE_HEIGHT)
            compute_prop(st, s, parent, p, el);
    }

    /* currentcolor */
    for (i = 0; i < 4; i++)
        if (s->currentcolor & (1u << i))
            s->border_color[i] = s->color;
    if (s->currentcolor & TC_CC_BACKGROUND)
        s->background_color = s->color;
    if (s->currentcolor & TC_CC_DECORATION)
        s->decoration_color = s->color;
    /* Borders with no style have no width. */
    for (i = 0; i < 4; i++)
        if (s->border_style[i] == TC_BORDER_NONE || s->border_style[i] == TC_BORDER_HIDDEN)
            s->border_width[i] = 0;
    /* Boxes taken out of flow, the root and flex and grid items are blocks. */
    if (s->position == TC_POSITION_ABSOLUTE || s->position == TC_POSITION_FIXED)
        s->float_ = TC_FLOAT_NONE;
    if (is_root && s->display == TC_DISPLAY_CONTENTS)
        s->display = TC_DISPLAY_BLOCK;
    if (s->float_ != TC_FLOAT_NONE || s->position == TC_POSITION_ABSOLUTE || s->position == TC_POSITION_FIXED ||
        is_root || parent->display == TC_DISPLAY_FLEX || parent->display == TC_DISPLAY_INLINE_FLEX ||
        parent->display == TC_DISPLAY_GRID || parent->display == TC_DISPLAY_INLINE_GRID)
        s->display = blockify(s->display);
    /* overflow: visible and clip pair only with each other. */
    if ((s->overflow_x <= TC_OVERFLOW_CLIP) != (s->overflow_y <= TC_OVERFLOW_CLIP)) {
        if (s->overflow_x == TC_OVERFLOW_VISIBLE)
            s->overflow_x = TC_OVERFLOW_AUTO;
        else if (s->overflow_x == TC_OVERFLOW_CLIP)
            s->overflow_x = TC_OVERFLOW_HIDDEN;
        if (s->overflow_y == TC_OVERFLOW_VISIBLE)
            s->overflow_y = TC_OVERFLOW_AUTO;
        else if (s->overflow_y == TC_OVERFLOW_CLIP)
            s->overflow_y = TC_OVERFLOW_HIDDEN;
    }
    /* Text decorations reach in-flow descendants, but not the insides of
       atomic inlines, floats and absolutely positioned boxes. */
    atomic = s->display == TC_DISPLAY_INLINE_BLOCK || s->display == TC_DISPLAY_INLINE_TABLE ||
             s->display == TC_DISPLAY_INLINE_FLEX || s->display == TC_DISPLAY_INLINE_GRID ||
             s->float_ != TC_FLOAT_NONE || s->position == TC_POSITION_ABSOLUTE ||
             s->position == TC_POSITION_FIXED;
    if (atomic) {
        s->decorations = 0;
        s->decorations_color = 0;
    }
    if (s->decoration_line) {
        s->decorations |= s->decoration_line;
        s->decorations_color = s->decoration_color;
    }
}

void tc_style_initial(tc_style *out)
{
    int i;
    initial_style(out);
    for (i = 0; i < 4; i++)
        out->border_width[i] = 0; /* computed: border-style is none */
}

void tc_style_anonymous(tc_style *out, const tc_style *parent, int display)
{
    size_t inh = offsetof(tc_style, color);
    int i;
    tc_style_initial(out);
    memcpy((char *)out + inh, (const char *)parent + inh, sizeof *out - inh);
    out->display = (uint8_t)display;
    for (i = 0; i < 4; i++)
        out->border_color[i] = out->color;
    out->decoration_color = out->color;
}

/* ---- Sharing styles ---- */

/* A hash of the whole struct, a word at a time. */
static uint32_t style_hash(const tc_style *s)
{
    const unsigned char *p = (const unsigned char *)s;
    uint32_t h = 2166136261u, w;
    size_t i;
    for (i = 0; i + 4 <= sizeof *s; i += 4) {
        memcpy(&w, p + i, 4);
        h = (h ^ w) * 0x9E3779B1u;
        h ^= h >> 15;
    }
    return h;
}

static const tc_style *intern(styler *st, const tc_style *s)
{
    tc_styled *o = st->out;
    uint32_t h = style_hash(s), mask, i;
    tc_style *copy;
    if (o->nstyles * 2 >= o->styles_cap) {
        uint32_t cap = o->styles_cap ? o->styles_cap * 2 : 256, k;
        struct shared *grown = calloc(cap, sizeof *grown);
        if (!grown) {
            st->nomem = 1;
            return NULL;
        }
        for (k = 0; k < o->styles_cap; k++)
            if (o->styles[k].style) {
                uint32_t j = o->styles[k].hash & (cap - 1);
                while (grown[j].style)
                    j = (j + 1) & (cap - 1);
                grown[j] = o->styles[k];
            }
        free(o->styles);
        o->styles = grown;
        o->styles_cap = cap;
    }
    mask = o->styles_cap - 1;
    for (i = h & mask; o->styles[i].style; i = (i + 1) & mask)
        if (o->styles[i].hash == h && memcmp(o->styles[i].style, s, sizeof *s) == 0)
            return o->styles[i].style;
    copy = tc_arena_alloc(&o->arena, sizeof *copy);
    if (!copy) {
        st->nomem = 1;
        return NULL;
    }
    memcpy(copy, s, sizeof *copy);
    o->styles[i].hash = h;
    o->styles[i].style = copy;
    o->nstyles++;
    return copy;
}

/* ---- Styling a document ---- */

static slot *find_slot(const tc_styled *s, const tc_element *el)
{
    uintptr_t key = (uintptr_t)el;
    uint32_t mask = s->slots_cap - 1, i = (uint32_t)((key >> 3) * 2654435761u) & mask;
    if (!s->slots_cap)
        return NULL;
    while (s->slots[i].el && s->slots[i].el != el)
        i = (i + 1) & mask;
    return &s->slots[i];
}

const tc_element_styles *tc_styled_element(const tc_styled *s, const tc_element *el)
{
    const slot *sl = s && el ? find_slot(s, el) : NULL;
    return sl && sl->el ? &sl->styles : NULL;
}

/* A pseudo-element's style, from matches [from, to). */
static const tc_style *pseudo_style(styler *st, uint32_t from, uint32_t to, const tc_style *parent,
                                    const tc_element *el, int marker)
{
    tc_style s;
    run_cascade(st, from, to, NULL, NULL, 0);
    compute(st, &s, parent, el, 0);
    if (s.display == TC_DISPLAY_NONE)
        return NULL;
    if (!marker) {
        if (s.content_kind != TC_GENERATED_ITEMS)
            return NULL;
    } else if (s.content_kind == TC_GENERATED_NONE) {
        return NULL;
    }
    return intern(st, &s);
}

static int style_element(styler *st, const tc_element *el, const tc_style *parent, int is_root)
{
    hints h;
    tc_decl *attr = NULL;
    uint32_t nattr = 0, i, start[TC_PSEUDO_OTHER + 1], end[TC_PSEUDO_OTHER + 1];
    const char *style_attr = tc_dom_get_attr(el, "style");
    tc_style s;
    slot *sl;
    const tc_style *main;

    match_element(st, el);
    for (i = 0; i <= TC_PSEUDO_OTHER; i++)
        start[i] = end[i] = 0;
    for (i = 0; i < st->nmatches; i++) {
        uint8_t ps = st->matches[i].pseudo;
        if (start[ps] == end[ps])
            start[ps] = i;
        end[ps] = i + 1;
    }
    presentational_hints(st, el, &h);
    if (style_attr && *style_attr &&
        tc_style_attr_parse(&st->out->arena, style_attr, st->quirks, &attr, &nattr) != TC_OK)
        st->nomem = 1;
    run_cascade(st, start[TC_PSEUDO_NONE], end[TC_PSEUDO_NONE], &h, attr, nattr);
    compute(st, &s, parent, el, is_root);
    main = intern(st, &s);
    if (!main)
        return 0;
    sl = find_slot(st->out, el);
    sl->el = el;
    sl->styles.style = main;
    sl->styles.before = sl->styles.after = sl->styles.marker = NULL;
    st->out->stats.elements++;
    if (main->display == TC_DISPLAY_NONE)
        return 0;
    if (end[TC_PSEUDO_BEFORE] > start[TC_PSEUDO_BEFORE])
        sl->styles.before = pseudo_style(st, start[TC_PSEUDO_BEFORE], end[TC_PSEUDO_BEFORE], main, el, 0);
    if (end[TC_PSEUDO_AFTER] > start[TC_PSEUDO_AFTER])
        sl->styles.after = pseudo_style(st, start[TC_PSEUDO_AFTER], end[TC_PSEUDO_AFTER], main, el, 0);
    if (main->display == TC_DISPLAY_LIST_ITEM)
        sl->styles.marker = pseudo_style(st, start[TC_PSEUDO_MARKER], end[TC_PSEUDO_MARKER], main, el, 1);
    return 1;
}

static int source_applies(styler *st, const tc_style_source *src)
{
    tc_media_list *list;
    int ok;
    if (!src->sheet)
        return 0;
    if (!src->media)
        return 1;
    ok = tc_media_parse_text(&st->tmp, src->media, &list) == TC_OK && tc_media_matches(list, st->env);
    return ok;
}

static const tc_element *find_body(const tc_document *doc)
{
    const tc_node *n, *k;
    for (n = doc->node.first_child; n; n = n->next)
        if (tc_dom_is_html(n, TC_TAG_HTML))
            for (k = n->first_child; k; k = k->next)
                if (tc_dom_is_html(k, TC_TAG_BODY))
                    return (const tc_element *)k;
    return NULL;
}

static void styler_free(styler *st)
{
    free(st->entries);
    free(st->buckets);
    free(st->index);
    free(st->universal);
    free(st->matches);
    free((void *)st->c.custom);
    tc_arena_free(&st->tmp);
}

int tc_style_document(const tc_document *doc, const tc_style_source *sources, size_t count, const tc_media_env *env,
                      const char *target_id, tc_styled **out)
{
    styler st;
    tc_styled *o;
    const tc_node *n;
    uint32_t elements = 0, cap = 16, i, next_rank = 0;
    size_t k;

    *out = NULL;
    o = calloc(1, sizeof *o);
    if (!o)
        return TC_ERR_NOMEM;
    tc_arena_init(&o->arena);
    memset(&st, 0, sizeof st);
    st.out = o;
    st.doc = doc;
    st.env = env;
    st.quirks = doc->mode == TC_QUIRKS;
    st.mctx.quirks = st.quirks;
    st.mctx.target_id = target_id;
    st.root_font_size = 16;
    st.body = find_body(doc);
    tc_arena_init(&st.tmp);
    initial_style(&st.initial);

    /* The rules, in cascade order. */
    if (tc_ua_sheet(0))
        collect(&st, tc_ua_sheet(0), NULL, 0);
    if (st.quirks && tc_ua_sheet(1))
        collect(&st, tc_ua_sheet(1), NULL, 0);
    for (k = 0; k < count; k++)
        if (source_applies(&st, &sources[k]))
            collect(&st, sources[k].sheet, NULL, 0);
    rank_layers(&st.root, &next_rank, 0);
    for (i = 0; i < st.nentries; i++)
        st.entries[i].rank = st.entries[i].layer ? st.entries[i].layer->rank : st.root.rank;
    o->stats.selectors = st.nentries;
    if (st.nomem || build_buckets(&st) != TC_OK) {
        styler_free(&st);
        tc_styled_free(o);
        return TC_ERR_NOMEM;
    }

    for (n = tc_dom_next(&doc->node, &doc->node); n; n = tc_dom_next(&doc->node, n))
        elements += n->type == TC_NODE_ELEMENT;
    while (cap < elements * 2)
        cap *= 2;
    o->slots = calloc(cap, sizeof *o->slots);
    if (!o->slots) {
        styler_free(&st);
        tc_styled_free(o);
        return TC_ERR_NOMEM;
    }
    o->slots_cap = cap;

    /* Tree order, so parents come first; display: none subtrees are skipped. */
    n = doc->node.first_child;
    while (n && !st.nomem) {
        int descend = 0;
        if (n->type == TC_NODE_ELEMENT) {
            const tc_element *el = (const tc_element *)n;
            const tc_element *parent = tc_element_parent(el);
            const tc_element_styles *ps = parent ? tc_styled_element(o, parent) : NULL;
            descend = style_element(&st, el, ps ? ps->style : &st.initial, parent == NULL);
        }
        if (descend && n->first_child) {
            bloom_element(&st, (const tc_element *)n, 1);
            n = n->first_child;
            continue;
        }
        while (n && !n->next) {
            n = n->parent;
            if (n == &doc->node) {
                n = NULL;
                break;
            }
            bloom_element(&st, (const tc_element *)n, 0);
        }
        if (n)
            n = n->next;
    }
    o->stats.styles = o->nstyles;
    o->stats.bytes = o->arena.total + o->slots_cap * sizeof *o->slots + o->styles_cap * sizeof *o->styles;
    if (st.nomem) {
        styler_free(&st);
        tc_styled_free(o);
        return TC_ERR_NOMEM;
    }
    styler_free(&st);
    *out = o;
    return TC_OK;
}

void tc_styled_free(tc_styled *s)
{
    if (!s)
        return;
    free(s->slots);
    free(s->styles);
    free(s->vars);
    tc_arena_free(&s->arena);
    free(s);
}

void tc_styled_stats(const tc_styled *s, tc_style_stats *out)
{
    *out = s->stats;
}

/* ---- A document's style sheets ---- */

static int rel_has(const char *rel, const char *token)
{
    size_t len = strlen(token);
    while (rel && *rel) {
        const char *start;
        while (is_space(*rel))
            rel++;
        start = rel;
        while (*rel && !is_space(*rel))
            rel++;
        if ((size_t)(rel - start) == len) {
            size_t i;
            for (i = 0; i < len; i++)
                if ((start[i] | 0x20) != token[i] && start[i] != token[i])
                    break;
            if (i == len)
                return 1;
        }
    }
    return 0;
}

int tc_style_sources(const tc_document *doc, tc_style_source **out, size_t *count)
{
    tc_style_source *list = NULL;
    uint32_t len = 0, cap = 0;
    const tc_node *n;
    tc_buf text;

    *out = NULL;
    *count = 0;
    tc_buf_init(&text);
    for (n = tc_dom_next(&doc->node, &doc->node); n; n = tc_dom_next(&doc->node, n)) {
        const tc_element *el = (const tc_element *)n;
        tc_style_source src;
        if (n->type != TC_NODE_ELEMENT || n->ns != TC_NS_HTML)
            continue;
        memset(&src, 0, sizeof src);
        src.element = el;
        src.media = tc_dom_get_attr(el, "media");
        if (n->tag == TC_TAG_STYLE) {
            const char *type = tc_dom_get_attr(el, "type");
            const tc_node *k;
            if (type && *type && !tc_css_ascii_ieq(type, "text/css"))
                continue;
            tc_buf_clear(&text);
            for (k = n->first_child; k; k = k->next)
                if (k->type == TC_NODE_TEXT)
                    tc_buf_append(&text, ((const tc_chardata *)k)->data, ((const tc_chardata *)k)->len);
            src.sheet = tc_stylesheet_parse(text.len ? tc_buf_cstr(&text) : "", text.len, TC_ORIGIN_AUTHOR,
                                            doc->mode == TC_QUIRKS, NULL);
            if (!src.sheet)
                goto nomem;
        } else if (n->tag == TC_TAG_LINK) {
            const char *type = tc_dom_get_attr(el, "type");
            src.href = tc_dom_get_attr(el, "href");
            if (!rel_has(tc_dom_get_attr(el, "rel"), "stylesheet") || rel_has(tc_dom_get_attr(el, "rel"), "alternate") ||
                !src.href || !*src.href || tc_dom_get_attr(el, "disabled") ||
                (type && *type && !tc_css_ascii_ieq(type, "text/css")))
                continue;
        } else {
            continue;
        }
        if (len == cap) {
            tc_style_source *grown = grow(list, &cap, len, sizeof *list);
            if (!grown) {
                tc_stylesheet_free(src.sheet);
                goto nomem;
            }
            list = grown;
        }
        list[len++] = src;
    }
    tc_buf_free(&text);
    *out = list;
    *count = len;
    return TC_OK;
nomem:
    tc_buf_free(&text);
    tc_style_sources_free(list, len);
    return TC_ERR_NOMEM;
}

void tc_style_sources_free(tc_style_source *sources, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++)
        tc_stylesheet_free(sources[i].sheet);
    free(sources);
}
