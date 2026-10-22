#include "tc_selector.h"

#include <stdlib.h>
#include <string.h>

#include "tc_tags.h"

typedef enum simple_type {
    S_TYPE,      /* tag name */
    S_UNIVERSAL,
    S_ID,
    S_CLASS,
    S_ATTR,
    S_PSEUDO
} simple_type;

typedef enum attr_op {
    A_EXISTS,
    A_EQUALS,
    A_INCLUDES,  /* ~= */
    A_DASH,      /* |= */
    A_PREFIX,    /* ^= */
    A_SUFFIX,    /* $= */
    A_SUBSTRING  /* *= */
} attr_op;

typedef enum pseudo_class {
    PC_ROOT,
    PC_EMPTY,
    PC_FIRST_CHILD,
    PC_LAST_CHILD,
    PC_ONLY_CHILD,
    PC_FIRST_OF_TYPE,
    PC_LAST_OF_TYPE,
    PC_ONLY_OF_TYPE,
    PC_NTH_CHILD,
    PC_NTH_LAST_CHILD,
    PC_NTH_OF_TYPE,
    PC_NTH_LAST_OF_TYPE,
    PC_LINK,           /* :link and :any-link: every link is unvisited */
    PC_NEVER,          /* :visited, :hover, :focus...: not tracked yet */
    PC_ALWAYS,         /* :defined */
    PC_TARGET,
    PC_IS,             /* :is() and :where() */
    PC_NOT,
    PC_HAS,
    PC_LANG,
    PC_DIR,
    PC_CHECKED,
    PC_DISABLED,
    PC_ENABLED,
    PC_REQUIRED,
    PC_OPTIONAL,
    PC_READ_ONLY,
    PC_READ_WRITE,
    PC_PLACEHOLDER_SHOWN,
    PC_OPEN,
    PC_HEADING         /* :heading and :heading(levels): a bitmask of levels in a */
} pseudo_class;

/* Combinators: how a compound relates to the one on its left. */
enum {
    C_NONE,
    C_DESCENDANT,
    C_CHILD,
    C_NEXT,       /* + */
    C_SUBSEQUENT  /* ~ */
};

typedef struct simple {
    uint8_t type;      /* simple_type */
    uint8_t op;        /* attr_op */
    uint8_t pc;        /* pseudo_class */
    uint8_t icase;     /* attribute value: 1 case-insensitive, 2 case-sensitive (the s flag), 0 default */
    uint8_t lead;      /* :has() argument's leading combinator */
    uint16_t tag;      /* S_TYPE: the tag, if known */
    const char *name;  /* S_TYPE: as written; S_ID, S_CLASS, S_ATTR (lower case): the name */
    const char *lower; /* S_TYPE: lower case */
    size_t name_len;
    const char *value; /* S_ATTR; S_PSEUDO :lang()/:dir() */
    size_t value_len;
    int a, b;          /* :nth-*() */
    tc_selector_list *args;
} simple;

struct tc_compound {
    simple *simples;
    uint32_t count;
    uint8_t combinator; /* to the compound on the left */
};

/* ---- Parsing ---- */

typedef struct parser {
    tc_arena *arena;
    const tc_css_value *v;
    uint32_t n, i;
    int depth;          /* nesting of :is() and friends */
} parser;

static const tc_css_value *peek(const parser *p)
{
    return p->i < p->n ? &p->v[p->i] : NULL;
}

static int at_ws(const parser *p)
{
    return p->i < p->n && p->v[p->i].type == TC_CSS_WHITESPACE;
}

static void skip_ws(parser *p)
{
    while (at_ws(p))
        p->i++;
}

static char *lower_copy(tc_arena *a, const char *s)
{
    size_t n = strlen(s), i;
    char *out = tc_arena_alloc(a, n + 1);
    if (!out)
        return NULL;
    for (i = 0; i <= n; i++)
        out[i] = (char)(s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]);
    return out;
}

typedef struct svec {
    simple *items;
    size_t len, cap;
} svec;

static int svec_push(svec *v, const simple *s)
{
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 4;
        simple *grown = realloc(v->items, cap * sizeof *grown);
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = cap;
    }
    v->items[v->len++] = *s;
    return 1;
}

static int parse_list(parser *outer, const tc_css_value *v, uint32_t n, int forgiving, int relative,
                      tc_selector_list **out);

/* The arguments of :nth-child() and friends: "An+B [of S]". */
static int parse_nth(parser *p, const tc_css_value *f, int of_allowed, simple *s)
{
    uint32_t i, end = f->nitems;
    for (i = 0; i < f->nitems; i++)
        if (tc_css_is_ident(&f->items[i], "of")) {
            end = i;
            break;
        }
    if (tc_css_parse_anb(f->items, end, &s->a, &s->b) != TC_OK)
        return 0;
    if (end < f->nitems) {
        if (!of_allowed)
            return 0;
        if (!parse_list(p, f->items + end + 1, f->nitems - end - 1, 0, 0, &s->args))
            return 0;
    }
    return 1;
}

static const struct {
    const char *name;
    uint8_t pc;
} pseudo_classes[] = {
    { "active", PC_NEVER }, { "any-link", PC_LINK }, { "autofill", PC_NEVER }, { "blank", PC_NEVER },
    { "checked", PC_CHECKED }, { "default", PC_NEVER }, { "defined", PC_ALWAYS }, { "disabled", PC_DISABLED },
    { "empty", PC_EMPTY }, { "enabled", PC_ENABLED }, { "first-child", PC_FIRST_CHILD },
    { "first-of-type", PC_FIRST_OF_TYPE }, { "focus", PC_NEVER }, { "focus-visible", PC_NEVER },
    { "focus-within", PC_NEVER }, { "fullscreen", PC_NEVER }, { "heading", PC_HEADING }, { "hover", PC_NEVER },
    { "in-range", PC_ALWAYS }, { "indeterminate", PC_NEVER }, { "invalid", PC_NEVER },
    { "last-child", PC_LAST_CHILD }, { "last-of-type", PC_LAST_OF_TYPE }, { "link", PC_LINK },
    { "modal", PC_NEVER }, { "only-child", PC_ONLY_CHILD }, { "only-of-type", PC_ONLY_OF_TYPE },
    { "open", PC_OPEN }, { "optional", PC_OPTIONAL }, { "out-of-range", PC_NEVER },
    { "paused", PC_NEVER }, { "picture-in-picture", PC_NEVER }, { "placeholder-shown", PC_PLACEHOLDER_SHOWN },
    { "playing", PC_NEVER }, { "popover-open", PC_NEVER }, { "read-only", PC_READ_ONLY },
    { "read-write", PC_READ_WRITE }, { "required", PC_REQUIRED }, { "root", PC_ROOT }, { "scope", PC_ROOT },
    { "target", PC_TARGET }, { "target-within", PC_NEVER }, { "user-invalid", PC_NEVER },
    { "user-valid", PC_NEVER }, { "valid", PC_ALWAYS }, { "visited", PC_NEVER },
};

static int pseudo_element_name(const char *name, int legacy_only)
{
    static const char *const legacy[] = { "before", "after", "first-line", "first-letter" };
    static const char *const others[] = { "marker", "placeholder", "selection", "backdrop", "file-selector-button",
                                          "cue", "grammar-error", "spelling-error", "target-text", "details-content" };
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(legacy); i++)
        if (tc_css_ascii_ieq(name, legacy[i]))
            return i == 0 ? TC_PSEUDO_BEFORE : i == 1 ? TC_PSEUDO_AFTER : TC_PSEUDO_OTHER;
    if (legacy_only)
        return -1;
    for (i = 0; i < TC_ARRAY_LEN(others); i++)
        if (tc_css_ascii_ieq(name, others[i]))
            return i == 0 ? TC_PSEUDO_MARKER : TC_PSEUDO_OTHER;
    return -1;
}

/* After a ':'. Returns 0 if it isn't a pseudo-class Tachyon knows. */
static int parse_pseudo_class(parser *p, simple *s)
{
    const tc_css_value *v = peek(p);
    size_t i;

    memset(s, 0, sizeof *s);
    s->type = S_PSEUDO;
    if (!v)
        return 0;
    if (v->type == TC_CSS_IDENT) {
        p->i++;
        for (i = 0; i < TC_ARRAY_LEN(pseudo_classes); i++)
            if (tc_css_ascii_ieq(v->text, pseudo_classes[i].name)) {
                s->pc = pseudo_classes[i].pc;
                s->a = s->pc == PC_HEADING ? ~0 : 0;
                return 1;
            }
        return 0;
    }
    if (v->type != TC_CSS_FUNCTION)
        return 0;
    p->i++;
    if (tc_css_ascii_ieq(v->text, "is") || tc_css_ascii_ieq(v->text, "matches") ||
        tc_css_ascii_ieq(v->text, "where")) {
        s->pc = PC_IS;
        s->b = tc_css_ascii_ieq(v->text, "where"); /* no specificity */
        return parse_list(p, v->items, v->nitems, 1, 0, &s->args);
    }
    if (tc_css_ascii_ieq(v->text, "not")) {
        s->pc = PC_NOT;
        return parse_list(p, v->items, v->nitems, 0, 0, &s->args);
    }
    if (tc_css_ascii_ieq(v->text, "has")) {
        s->pc = PC_HAS;
        return parse_list(p, v->items, v->nitems, 0, 1, &s->args);
    }
    if (tc_css_ascii_ieq(v->text, "nth-child") || tc_css_ascii_ieq(v->text, "nth-last-child")) {
        s->pc = tc_css_ascii_ieq(v->text, "nth-child") ? PC_NTH_CHILD : PC_NTH_LAST_CHILD;
        return parse_nth(p, v, 1, s);
    }
    if (tc_css_ascii_ieq(v->text, "nth-of-type") || tc_css_ascii_ieq(v->text, "nth-last-of-type")) {
        s->pc = tc_css_ascii_ieq(v->text, "nth-of-type") ? PC_NTH_OF_TYPE : PC_NTH_LAST_OF_TYPE;
        return parse_nth(p, v, 0, s);
    }
    if (tc_css_ascii_ieq(v->text, "heading")) {
        /* A comma-separated list of levels. */
        uint32_t k;
        int want = 1;
        s->pc = PC_HEADING;
        for (k = 0; k < v->nitems; k++) {
            const tc_css_value *arg = &v->items[k];
            if (arg->type == TC_CSS_WHITESPACE)
                continue;
            if (want && arg->type == TC_CSS_NUMBER && (arg->flags & TC_CSS_INTEGER)) {
                if (arg->number >= 1 && arg->number <= 30)
                    s->a |= 1 << (int)arg->number;
                want = 0;
            } else if (!want && arg->type == TC_CSS_COMMA) {
                want = 1;
            } else {
                return 0;
            }
        }
        return !want;
    }
    if (tc_css_ascii_ieq(v->text, "lang") || tc_css_ascii_ieq(v->text, "dir")) {
        uint32_t k;
        const tc_css_value *arg = NULL;
        s->pc = tc_css_ascii_ieq(v->text, "lang") ? PC_LANG : PC_DIR;
        /* One ident or string (lists of languages: the first is used). */
        for (k = 0; k < v->nitems; k++) {
            if (v->items[k].type == TC_CSS_WHITESPACE)
                continue;
            if (v->items[k].type == TC_CSS_COMMA && arg)
                break;
            if (arg || (v->items[k].type != TC_CSS_IDENT && v->items[k].type != TC_CSS_STRING))
                return 0;
            arg = &v->items[k];
        }
        if (!arg)
            return 0;
        s->value = lower_copy(p->arena, arg->text);
        s->value_len = s->value ? strlen(s->value) : 0;
        return s->value != NULL;
    }
    return 0;
}

/* After '[': [name], [name op value flag]. */
static int parse_attribute(parser *p, const tc_css_value *block, simple *s)
{
    parser q = { p->arena, block->items, block->nitems, 0, p->depth };
    const tc_css_value *v;

    memset(s, 0, sizeof *s);
    s->type = S_ATTR;
    skip_ws(&q);
    v = peek(&q);
    /* An optional namespace prefix: only "*|" (any) and "|" (none) are known. */
    if (v && (tc_css_is_delim(v, '*') || tc_css_is_delim(v, '|'))) {
        if (tc_css_is_delim(v, '*')) {
            q.i++;
            if (!tc_css_is_delim(peek(&q), '|'))
                return 0;
        }
        q.i++;
        v = peek(&q);
    }
    if (!v || v->type != TC_CSS_IDENT)
        return 0;
    if (q.i + 1 < q.n && tc_css_is_delim(&q.v[q.i + 1], '|') && !(q.i + 2 < q.n && tc_css_is_delim(&q.v[q.i + 2], '=')))
        return 0; /* "ns|attr" with a declared prefix: not supported */
    s->name = lower_copy(p->arena, v->text);
    if (!s->name)
        return 0;
    s->name_len = strlen(s->name);
    q.i++;
    skip_ws(&q);
    v = peek(&q);
    if (!v)
        return 1;
    if (v->type != TC_CSS_DELIM)
        return 0;
    if (v->cp == '=') {
        s->op = A_EQUALS;
        q.i++;
    } else {
        switch (v->cp) {
        case '~': s->op = A_INCLUDES; break;
        case '|': s->op = A_DASH; break;
        case '^': s->op = A_PREFIX; break;
        case '$': s->op = A_SUFFIX; break;
        case '*': s->op = A_SUBSTRING; break;
        default: return 0;
        }
        q.i++;
        if (!tc_css_is_delim(peek(&q), '='))
            return 0;
        q.i++;
    }
    skip_ws(&q);
    v = peek(&q);
    if (!v || (v->type != TC_CSS_IDENT && v->type != TC_CSS_STRING))
        return 0;
    s->value_len = strlen(v->text);
    s->value = tc_arena_strndup(p->arena, v->text, s->value_len);
    if (!s->value)
        return 0;
    q.i++;
    skip_ws(&q);
    v = peek(&q);
    if (v) {
        if (tc_css_is_ident(v, "i"))
            s->icase = 1;
        else if (tc_css_is_ident(v, "s"))
            s->icase = 2;
        else
            return 0;
        q.i++;
        skip_ws(&q);
        if (peek(&q))
            return 0;
    }
    return 1;
}

/* One compound selector. Returns 0 if invalid or empty; sets *pseudo. */
static int parse_compound(parser *p, tc_compound *out, uint8_t *pseudo)
{
    svec simples = { NULL, 0, 0 };
    const tc_css_value *v = peek(p);
    simple s;
    int ok = 1;

    memset(out, 0, sizeof *out);
    /* A type selector or '*', perhaps with a namespace prefix. */
    if (v && (v->type == TC_CSS_IDENT || tc_css_is_delim(v, '*') || tc_css_is_delim(v, '|'))) {
        const tc_css_value *name = v;
        if (p->i + 1 < p->n && tc_css_is_delim(&p->v[p->i + 1], '|') && !tc_css_is_delim(v, '|')) {
            /* "ns|name": only "*|" is supported (no @namespace). */
            if (!tc_css_is_delim(v, '*')) {
                ok = 0;
                goto done;
            }
            p->i += 2;
            name = peek(p);
        } else if (tc_css_is_delim(v, '|')) {
            p->i++;
            name = peek(p);
        }
        if (!name || (name->type != TC_CSS_IDENT && !tc_css_is_delim(name, '*'))) {
            ok = 0;
            goto done;
        }
        memset(&s, 0, sizeof s);
        if (name->type == TC_CSS_IDENT) {
            s.type = S_TYPE;
            s.name = tc_arena_strndup(p->arena, name->text, strlen(name->text));
            s.lower = lower_copy(p->arena, name->text);
            if (!s.name || !s.lower) {
                ok = 0;
                goto done;
            }
            s.name_len = strlen(s.lower);
            s.tag = (uint16_t)tc_tag_lookup(s.lower, s.name_len);
        } else {
            s.type = S_UNIVERSAL;
        }
        p->i++;
        if (!svec_push(&simples, &s)) {
            ok = 0;
            goto done;
        }
    }
    for (;;) {
        v = peek(p);
        if (!v)
            break;
        memset(&s, 0, sizeof s);
        if (v->type == TC_CSS_HASH) {
            if (!(v->flags & TC_CSS_HASH_ID)) {
                ok = 0;
                break;
            }
            s.type = S_ID;
            s.name_len = strlen(v->text);
            s.name = tc_arena_strndup(p->arena, v->text, s.name_len);
            if (!s.name) {
                ok = 0;
                break;
            }
            p->i++;
        } else if (tc_css_is_delim(v, '.')) {
            p->i++;
            v = peek(p);
            if (!v || v->type != TC_CSS_IDENT) {
                ok = 0;
                break;
            }
            s.type = S_CLASS;
            s.name_len = strlen(v->text);
            s.name = tc_arena_strndup(p->arena, v->text, s.name_len);
            if (!s.name) {
                ok = 0;
                break;
            }
            p->i++;
        } else if (v->type == TC_CSS_BLOCK && v->cp == '[') {
            p->i++;
            if (!parse_attribute(p, v, &s)) {
                ok = 0;
                break;
            }
        } else if (v->type == TC_CSS_COLON) {
            const tc_css_value *next = p->i + 1 < p->n ? &p->v[p->i + 1] : NULL;
            p->i++;
            if (next && next->type == TC_CSS_COLON) {
                int pe;
                p->i++;
                next = peek(p);
                if (!next || next->type != TC_CSS_IDENT || (pe = pseudo_element_name(next->text, 0)) < 0) {
                    ok = 0;
                    break;
                }
                p->i++;
                *pseudo = (uint8_t)pe;
                continue;
            }
            if (next && next->type == TC_CSS_IDENT && pseudo_element_name(next->text, 1) >= 0) {
                *pseudo = (uint8_t)pseudo_element_name(next->text, 1);
                p->i++;
                continue;
            }
            p->depth++;
            if (p->depth > 32 || !parse_pseudo_class(p, &s)) {
                p->depth--;
                ok = 0;
                break;
            }
            p->depth--;
        } else {
            break;
        }
        if (!svec_push(&simples, &s)) {
            ok = 0;
            break;
        }
    }
done:
    if (ok && simples.len == 0 && *pseudo == TC_PSEUDO_NONE)
        ok = 0;
    if (ok) {
        out->count = (uint32_t)simples.len;
        if (simples.len) {
            out->simples = tc_arena_alloc(p->arena, simples.len * sizeof *out->simples);
            if (out->simples)
                memcpy(out->simples, simples.items, simples.len * sizeof *out->simples);
            else
                ok = 0;
        }
    }
    free(simples.items);
    return ok;
}

static uint32_t list_specificity(const tc_selector_list *l)
{
    uint32_t i, best = 0;
    for (i = 0; l && i < l->count; i++)
        if (l->items[i].specificity > best)
            best = l->items[i].specificity;
    return best;
}

static uint32_t add_spec(uint32_t a, uint32_t b)
{
    uint32_t x = (a >> 20) + (b >> 20), y = ((a >> 10) & 1023) + ((b >> 10) & 1023), z = (a & 1023) + (b & 1023);
    if (x > 1023)
        x = 1023;
    if (y > 1023)
        y = 1023;
    if (z > 1023)
        z = 1023;
    return x << 20 | y << 10 | z;
}

static uint32_t compound_specificity(const tc_compound *c)
{
    uint32_t i, spec = 0;
    for (i = 0; i < c->count; i++) {
        const simple *s = &c->simples[i];
        switch (s->type) {
        case S_ID: spec = add_spec(spec, 1u << 20); break;
        case S_CLASS:
        case S_ATTR: spec = add_spec(spec, 1u << 10); break;
        case S_TYPE: spec = add_spec(spec, 1); break;
        case S_PSEUDO:
            if (s->pc == PC_IS)
                spec = add_spec(spec, s->b ? 0 : list_specificity(s->args));
            else if (s->pc == PC_NOT || s->pc == PC_HAS)
                spec = add_spec(spec, list_specificity(s->args));
            else if ((s->pc == PC_NTH_CHILD || s->pc == PC_NTH_LAST_CHILD) && s->args)
                spec = add_spec(spec, add_spec(1u << 10, list_specificity(s->args)));
            else
                spec = add_spec(spec, 1u << 10);
            break;
        default: break;
        }
    }
    return spec;
}

/* One complex selector from p's current position to the end of the range. */
static int parse_complex(parser *p, int relative, tc_selector *out, uint8_t *lead)
{
    tc_compound *compounds = NULL;
    size_t len = 0, cap = 0, i;
    uint8_t comb = C_NONE, pseudo = TC_PSEUDO_NONE;
    int ok = 1;

    memset(out, 0, sizeof *out);
    *lead = C_DESCENDANT;
    skip_ws(p);
    if (relative && peek(p) && peek(p)->type == TC_CSS_DELIM) {
        uint32_t cp = peek(p)->cp;
        if (cp == '>' || cp == '+' || cp == '~') {
            *lead = cp == '>' ? C_CHILD : cp == '+' ? C_NEXT : C_SUBSEQUENT;
            p->i++;
            skip_ws(p);
        }
    }
    for (;;) {
        tc_compound c;
        int had_ws;
        if (pseudo != TC_PSEUDO_NONE) {
            ok = 0; /* nothing may follow a pseudo-element's compound */
            break;
        }
        if (!parse_compound(p, &c, &pseudo)) {
            ok = 0;
            break;
        }
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 4;
            tc_compound *grown = realloc(compounds, ncap * sizeof *grown);
            if (!grown) {
                ok = 0;
                break;
            }
            compounds = grown;
            cap = ncap;
        }
        c.combinator = comb;
        compounds[len++] = c;
        had_ws = at_ws(p);
        skip_ws(p);
        if (!peek(p))
            break;
        if (tc_css_is_delim(peek(p), '>') || tc_css_is_delim(peek(p), '+') || tc_css_is_delim(peek(p), '~')) {
            uint32_t cp = peek(p)->cp;
            comb = cp == '>' ? C_CHILD : cp == '+' ? C_NEXT : C_SUBSEQUENT;
            p->i++;
            skip_ws(p);
        } else if (had_ws) {
            comb = C_DESCENDANT;
        } else {
            ok = 0;
            break;
        }
    }
    if (ok && len) {
        /* Store right to left, each compound with the combinator to its left. */
        out->compounds = tc_arena_alloc(p->arena, len * sizeof *out->compounds);
        if (!out->compounds) {
            ok = 0;
        } else {
            /* Each compound already holds the combinator to its left (the
               leftmost has none). */
            for (i = 0; i < len; i++) {
                out->compounds[i] = compounds[len - 1 - i];
                out->specificity = add_spec(out->specificity, compound_specificity(&out->compounds[i]));
            }
            out->ncompounds = (uint32_t)len;
            out->pseudo = pseudo;
            if (pseudo != TC_PSEUDO_NONE)
                out->specificity = add_spec(out->specificity, 1);
        }
    }
    free(compounds);
    return ok && len;
}

static int parse_list(parser *outer, const tc_css_value *v, uint32_t n, int forgiving, int relative,
                      tc_selector_list **out)
{
    tc_selector_list *list = tc_arena_alloc(outer->arena, sizeof *list);
    tc_selector *items = NULL;
    size_t len = 0, cap = 0;
    uint32_t start = 0, i;
    int ok = 1;

    *out = list;
    if (!list)
        return 0;
    memset(list, 0, sizeof *list);
    for (i = 0; i <= n; i++) {
        tc_selector sel;
        uint8_t lead;
        parser p;
        if (i < n && v[i].type != TC_CSS_COMMA)
            continue;
        p.arena = outer->arena;
        p.v = v + start;
        p.n = i - start;
        p.i = 0;
        p.depth = outer->depth;
        start = i + 1;
        if (!parse_complex(&p, relative, &sel, &lead)) {
            if (forgiving)
                continue;
            ok = 0;
            break;
        }
        if (relative) {
            /* Remember the leading combinator in the leftmost compound. */
            sel.compounds[sel.ncompounds - 1].combinator = lead;
        }
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 4;
            tc_selector *grown = realloc(items, ncap * sizeof *grown);
            if (!grown) {
                ok = 0;
                break;
            }
            items = grown;
            cap = ncap;
        }
        items[len++] = sel;
    }
    if (ok && len) {
        list->items = tc_arena_alloc(outer->arena, len * sizeof *list->items);
        if (list->items) {
            memcpy(list->items, items, len * sizeof *list->items);
            list->count = (uint32_t)len;
        } else {
            ok = 0;
        }
    }
    free(items);
    return ok && (len || forgiving);
}

int tc_selectors_parse(tc_arena *a, const tc_css_value *values, uint32_t count, int forgiving,
                       tc_selector_list *out)
{
    parser p = { a, values, count, 0, 0 };
    tc_selector_list *list;
    memset(out, 0, sizeof *out);
    if (!parse_list(&p, values, count, forgiving, 0, &list) || !list->count)
        return TC_ERR_INVALID;
    *out = *list;
    return TC_OK;
}

uint32_t tc_selector_hash(int kind, const char *name, size_t len, int fold)
{
    uint32_t h = 2166136261u ^ (uint32_t)kind;
    size_t i;
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (fold && ch >= 'A' && ch <= 'Z')
            ch = (unsigned char)(ch + 32);
        h = (h ^ ch) * 16777619u;
    }
    return h;
}

uint32_t tc_selector_ancestor_hashes(const tc_selector *s, int fold, uint32_t *out, uint32_t max)
{
    uint32_t i, k, n = 0;
    /* A compound is an ancestor's when a descendant or child combinator
       joins it to the compound on its right. */
    for (i = 1; i < s->ncompounds && n < max; i++) {
        const tc_compound *c = &s->compounds[i];
        if (s->compounds[i - 1].combinator != C_DESCENDANT && s->compounds[i - 1].combinator != C_CHILD)
            continue;
        for (k = 0; k < c->count && n < max; k++) {
            const simple *x = &c->simples[k];
            if (x->type == S_ID)
                out[n++] = tc_selector_hash(TC_KEY_ID, x->name, x->name_len, fold);
            else if (x->type == S_CLASS)
                out[n++] = tc_selector_hash(TC_KEY_CLASS, x->name, x->name_len, fold);
            else if (x->type == S_TYPE)
                out[n++] = tc_selector_hash(TC_KEY_TAG, x->lower, x->name_len, 0);
        }
    }
    return n;
}

tc_selector_key_type tc_selector_key(const tc_selector *s, const char **name)
{
    const tc_compound *c = &s->compounds[0];
    uint32_t i;
    for (i = 0; i < c->count; i++)
        if (c->simples[i].type == S_ID) {
            *name = c->simples[i].name;
            return TC_KEY_ID;
        }
    for (i = 0; i < c->count; i++)
        if (c->simples[i].type == S_CLASS) {
            *name = c->simples[i].name;
            return TC_KEY_CLASS;
        }
    for (i = 0; i < c->count; i++)
        if (c->simples[i].type == S_TYPE) {
            *name = c->simples[i].lower;
            return TC_KEY_TAG;
        }
    *name = NULL;
    return TC_KEY_NONE;
}

/* ---- The element, as selectors see it ---- */

const tc_element *tc_element_parent(const tc_element *el)
{
    const tc_node *p = el->node.parent;
    return p && p->type == TC_NODE_ELEMENT ? (const tc_element *)p : NULL;
}

const tc_element *tc_element_prev(const tc_element *el)
{
    const tc_node *n;
    for (n = el->node.prev; n; n = n->prev)
        if (n->type == TC_NODE_ELEMENT)
            return (const tc_element *)n;
    return NULL;
}

const tc_element *tc_element_next(const tc_element *el)
{
    const tc_node *n;
    for (n = el->node.next; n; n = n->next)
        if (n->type == TC_NODE_ELEMENT)
            return (const tc_element *)n;
    return NULL;
}

const char *tc_element_attr(const tc_element *el, const char *name)
{
    return tc_dom_get_attr(el, name);
}

static int lower_eq_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return 0;
    }
    return 1;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* Whether a whitespace-separated list contains word. */
static int list_has(const char *list, const char *word, size_t len, int icase)
{
    const char *p = list;
    if (!len)
        return 0;
    while (*p) {
        const char *start;
        while (is_space(*p))
            p++;
        start = p;
        while (*p && !is_space(*p))
            p++;
        if ((size_t)(p - start) == len && (icase ? lower_eq_n(start, word, len) : memcmp(start, word, len) == 0))
            return 1;
    }
    return 0;
}

int tc_element_has_class(const tc_element *el, const char *name, size_t len, int case_insensitive)
{
    const char *cls = tc_dom_get_attr(el, "class");
    return cls && list_has(cls, name, len, case_insensitive);
}

/* ---- Matching ---- */

static int is_html(const tc_element *el)
{
    return el->node.ns == TC_NS_HTML;
}

/* HTML attributes whose values selectors compare case-insensitively. */
static int icase_attribute(const char *name)
{
    static const char *const names[] = {
        "accept", "accept-charset", "align", "alink", "axis", "bgcolor", "charset", "checked", "clear",
        "codetype", "color", "compact", "declare", "defer", "dir", "direction", "disabled", "enctype", "face",
        "frame", "hreflang", "http-equiv", "lang", "language", "link", "media", "method", "multiple", "nohref",
        "noresize", "noshade", "nowrap", "readonly", "rel", "rev", "rules", "scope", "scrolling", "selected",
        "shape", "target", "text", "type", "valign", "valuetype", "vlink",
    };
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(names); i++)
        if (strcmp(name, names[i]) == 0)
            return 1;
    return 0;
}

static int match_attr(const simple *s, const tc_element *el)
{
    const char *v = tc_dom_get_attr(el, s->name);
    size_t vlen, n = s->value_len;
    int icase;

    if (!v)
        return 0;
    if (s->op == A_EXISTS)
        return 1;
    icase = s->icase == 1 || (s->icase == 0 && is_html(el) && icase_attribute(s->name));
    vlen = strlen(v);
    switch (s->op) {
    case A_EQUALS:
        return vlen == n && (icase ? lower_eq_n(v, s->value, n) : memcmp(v, s->value, n) == 0);
    case A_INCLUDES: {
        size_t i;
        for (i = 0; i < n; i++)
            if (is_space(s->value[i]))
                return 0;
        return list_has(v, s->value, n, icase);
    }
    case A_DASH:
        return vlen >= n && (icase ? lower_eq_n(v, s->value, n) : memcmp(v, s->value, n) == 0) &&
               (vlen == n || v[n] == '-');
    case A_PREFIX:
        return n && vlen >= n && (icase ? lower_eq_n(v, s->value, n) : memcmp(v, s->value, n) == 0);
    case A_SUFFIX:
        return n && vlen >= n && (icase ? lower_eq_n(v + vlen - n, s->value, n) : memcmp(v + vlen - n, s->value, n) == 0);
    case A_SUBSTRING: {
        size_t i;
        if (!n || vlen < n)
            return 0;
        for (i = 0; i + n <= vlen; i++)
            if (icase ? lower_eq_n(v + i, s->value, n) : memcmp(v + i, s->value, n) == 0)
                return 1;
        return 0;
    }
    }
    return 0;
}

static int same_type(const tc_element *a, const tc_element *b)
{
    return a->node.ns == b->node.ns && a->node.tag == b->node.tag &&
           (a->node.tag != TC_TAG_UNKNOWN || strcmp(a->name, b->name) == 0);
}

static int matches_list(const tc_selector_list *l, const tc_element *el, const tc_match_context *ctx);

/* The 1-based position for :nth-*(), counting siblings that qualify. */
static long nth_index(const tc_element *el, int from_end, int of_type, const tc_selector_list *of,
                      const tc_match_context *ctx)
{
    long index = 1;
    const tc_element *sib = from_end ? tc_element_next(el) : tc_element_prev(el);
    for (; sib; sib = from_end ? tc_element_next(sib) : tc_element_prev(sib))
        if ((!of_type || same_type(sib, el)) && (!of || matches_list(of, sib, ctx)))
            index++;
    return index;
}

static int nth_matches(int a, int b, long index)
{
    if (a == 0)
        return index == b;
    /* index = a*n + b for some n >= 0 */
    return (index - b) % a == 0 && (index - b) / a >= 0;
}

static const char *element_lang(const tc_element *el)
{
    for (; el; el = tc_element_parent(el)) {
        const char *lang = tc_dom_get_attr(el, "lang");
        if (!lang)
            lang = tc_dom_get_attr(el, "xml:lang");
        if (lang)
            return lang;
    }
    return "";
}

static int is_link(const tc_element *el)
{
    return is_html(el) && (el->node.tag == TC_TAG_A || el->node.tag == TC_TAG_AREA) && tc_dom_get_attr(el, "href");
}

static int is_form_control(const tc_element *el)
{
    if (!is_html(el))
        return 0;
    switch (el->node.tag) {
    case TC_TAG_BUTTON:
    case TC_TAG_INPUT:
    case TC_TAG_SELECT:
    case TC_TAG_TEXTAREA:
    case TC_TAG_OPTGROUP:
    case TC_TAG_OPTION:
    case TC_TAG_FIELDSET:
        return 1;
    default:
        return 0;
    }
}

static int match_complex(const tc_selector *s, uint32_t i, const tc_element *el, const tc_element *anchor,
                         const tc_match_context *ctx);

/* :has(): some element related to el, as the argument's leading combinator
   says, ends a match of the argument. */
static int match_has(const tc_selector_list *l, const tc_element *el, const tc_match_context *ctx)
{
    uint32_t k;
    for (k = 0; k < l->count; k++) {
        const tc_selector *s = &l->items[k];
        uint8_t lead = s->compounds[s->ncompounds - 1].combinator;
        const tc_node *n;
        if (lead == C_NEXT || lead == C_SUBSEQUENT) {
            const tc_element *sib;
            for (sib = tc_element_next(el); sib; sib = tc_element_next(sib)) {
                if (match_complex(s, 0, sib, el, ctx))
                    return 1;
                if (lead == C_NEXT && s->ncompounds == 1)
                    break;
            }
            continue;
        }
        /* Descendants (or children), in tree order. */
        for (n = tc_dom_next(&el->node, &el->node); n; n = tc_dom_next(&el->node, n))
            if (n->type == TC_NODE_ELEMENT && match_complex(s, 0, (const tc_element *)n, el, ctx))
                return 1;
    }
    return 0;
}

static int match_pseudo(const simple *s, const tc_element *el, const tc_match_context *ctx)
{
    const tc_node *n;
    switch (s->pc) {
    case PC_ROOT:
        return el->node.parent && el->node.parent->type == TC_NODE_DOCUMENT;
    case PC_EMPTY:
        for (n = el->node.first_child; n; n = n->next)
            if (n->type == TC_NODE_ELEMENT || (n->type == TC_NODE_TEXT && ((const tc_chardata *)n)->len))
                return 0;
        return 1;
    case PC_FIRST_CHILD: return !tc_element_prev(el);
    case PC_LAST_CHILD: return !tc_element_next(el);
    case PC_ONLY_CHILD: return !tc_element_prev(el) && !tc_element_next(el);
    case PC_FIRST_OF_TYPE: return nth_index(el, 0, 1, NULL, ctx) == 1;
    case PC_LAST_OF_TYPE: return nth_index(el, 1, 1, NULL, ctx) == 1;
    case PC_ONLY_OF_TYPE: return nth_index(el, 0, 1, NULL, ctx) == 1 && nth_index(el, 1, 1, NULL, ctx) == 1;
    case PC_NTH_CHILD:
    case PC_NTH_LAST_CHILD:
        if (s->args && !matches_list(s->args, el, ctx))
            return 0;
        return nth_matches(s->a, s->b, nth_index(el, s->pc == PC_NTH_LAST_CHILD, 0, s->args, ctx));
    case PC_NTH_OF_TYPE:
    case PC_NTH_LAST_OF_TYPE:
        return nth_matches(s->a, s->b, nth_index(el, s->pc == PC_NTH_LAST_OF_TYPE, 1, NULL, ctx));
    case PC_LINK: return is_link(el);
    case PC_NEVER: return 0;
    case PC_ALWAYS: return 1;
    case PC_TARGET: {
        const char *id = tc_dom_get_attr(el, "id");
        return ctx->target_id && id && strcmp(id, ctx->target_id) == 0;
    }
    case PC_IS: return matches_list(s->args, el, ctx);
    case PC_NOT: return !matches_list(s->args, el, ctx);
    case PC_HAS: return match_has(s->args, el, ctx);
    case PC_LANG: {
        const char *lang = element_lang(el);
        size_t n2 = s->value_len;
        return strlen(lang) >= n2 && lower_eq_n(lang, s->value, n2) && (lang[n2] == '\0' || lang[n2] == '-');
    }
    case PC_DIR: {
        const tc_element *e;
        for (e = el; e; e = tc_element_parent(e)) {
            const char *dir = tc_dom_get_attr(e, "dir");
            if (dir && (tc_css_ascii_ieq(dir, "rtl") || tc_css_ascii_ieq(dir, "ltr")))
                return tc_css_ascii_ieq(dir, s->value);
        }
        return strcmp(s->value, "ltr") == 0;
    }
    case PC_CHECKED:
        return is_html(el) && ((el->node.tag == TC_TAG_INPUT && tc_dom_get_attr(el, "checked")) ||
                               (el->node.tag == TC_TAG_OPTION && tc_dom_get_attr(el, "selected")));
    case PC_DISABLED: return is_form_control(el) && tc_dom_get_attr(el, "disabled");
    case PC_ENABLED: return is_form_control(el) && !tc_dom_get_attr(el, "disabled");
    case PC_REQUIRED: return is_form_control(el) && tc_dom_get_attr(el, "required");
    case PC_OPTIONAL: return is_form_control(el) && !tc_dom_get_attr(el, "required");
    case PC_READ_WRITE:
        return is_html(el) && ((el->node.tag == TC_TAG_INPUT || el->node.tag == TC_TAG_TEXTAREA) &&
                               !tc_dom_get_attr(el, "readonly") && !tc_dom_get_attr(el, "disabled"));
    case PC_READ_ONLY:
        return !(is_html(el) && ((el->node.tag == TC_TAG_INPUT || el->node.tag == TC_TAG_TEXTAREA) &&
                                 !tc_dom_get_attr(el, "readonly") && !tc_dom_get_attr(el, "disabled")));
    case PC_PLACEHOLDER_SHOWN:
        return is_html(el) && (el->node.tag == TC_TAG_INPUT || el->node.tag == TC_TAG_TEXTAREA) &&
               tc_dom_get_attr(el, "placeholder") && !tc_dom_get_attr(el, "value");
    case PC_OPEN:
        return is_html(el) && (el->node.tag == TC_TAG_DETAILS || el->node.tag == TC_TAG_DIALOG) &&
               tc_dom_get_attr(el, "open");
    case PC_HEADING:
        return is_html(el) && el->node.tag >= TC_TAG_H1 && el->node.tag <= TC_TAG_H6 &&
               (s->a & 1 << (el->node.tag - TC_TAG_H1 + 1)) != 0;
    }
    return 0;
}

static int match_compound(const tc_compound *c, const tc_element *el, const tc_match_context *ctx)
{
    uint32_t i;
    for (i = 0; i < c->count; i++) {
        const simple *s = &c->simples[i];
        switch (s->type) {
        case S_UNIVERSAL:
            break;
        case S_TYPE:
            if (is_html(el)) {
                if (s->tag != TC_TAG_UNKNOWN ? el->node.tag != s->tag : strcmp(el->name, s->lower) != 0)
                    return 0;
            } else if (strcmp(el->name, s->name) != 0) {
                return 0;
            }
            break;
        case S_ID: {
            const char *id = tc_dom_get_attr(el, "id");
            if (!id || strlen(id) != s->name_len ||
                (ctx->quirks ? !lower_eq_n(id, s->name, s->name_len) : strcmp(id, s->name) != 0))
                return 0;
            break;
        }
        case S_CLASS:
            if (!tc_element_has_class(el, s->name, s->name_len, ctx->quirks))
                return 0;
            break;
        case S_ATTR:
            if (!match_attr(s, el))
                return 0;
            break;
        case S_PSEUDO:
            if (!match_pseudo(s, el, ctx))
                return 0;
            break;
        }
    }
    return 1;
}

/* Does el match compounds i (from the right) onwards? anchor: for :has(),
   the element the leftmost compound must be related to. */
static int match_complex(const tc_selector *s, uint32_t i, const tc_element *el, const tc_element *anchor,
                         const tc_match_context *ctx)
{
    const tc_compound *c = &s->compounds[i];
    const tc_element *e;

    if (!match_compound(c, el, ctx))
        return 0;
    if (i + 1 == s->ncompounds) {
        if (!anchor)
            return 1;
        switch (c->combinator) {
        case C_CHILD: return tc_element_parent(el) == anchor;
        case C_NEXT: return tc_element_prev(el) == anchor;
        case C_SUBSEQUENT:
            for (e = tc_element_prev(el); e; e = tc_element_prev(e))
                if (e == anchor)
                    return 1;
            return 0;
        default:
            for (e = tc_element_parent(el); e; e = tc_element_parent(e))
                if (e == anchor)
                    return 1;
            return 0;
        }
    }
    switch (c->combinator) {
    case C_CHILD:
        e = tc_element_parent(el);
        return e && match_complex(s, i + 1, e, anchor, ctx);
    case C_DESCENDANT:
        for (e = tc_element_parent(el); e; e = tc_element_parent(e))
            if (match_complex(s, i + 1, e, anchor, ctx))
                return 1;
        return 0;
    case C_NEXT:
        e = tc_element_prev(el);
        return e && match_complex(s, i + 1, e, anchor, ctx);
    case C_SUBSEQUENT:
        for (e = tc_element_prev(el); e; e = tc_element_prev(e))
            if (match_complex(s, i + 1, e, anchor, ctx))
                return 1;
        return 0;
    }
    return 0;
}

static int matches_list(const tc_selector_list *l, const tc_element *el, const tc_match_context *ctx)
{
    uint32_t i;
    for (i = 0; l && i < l->count; i++)
        if (l->items[i].pseudo == TC_PSEUDO_NONE && match_complex(&l->items[i], 0, el, NULL, ctx))
            return 1;
    return 0;
}

int tc_selector_matches(const tc_selector *s, const tc_element *el, const tc_match_context *ctx)
{
    return match_complex(s, 0, el, NULL, ctx);
}
