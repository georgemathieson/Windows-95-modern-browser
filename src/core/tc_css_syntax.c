#include "tc_css_syntax.h"

#include <stdlib.h>
#include <string.h>

#include "tc_utf8.h"

/* ---- Preprocessing and character classes ---- */

/* CSS Syntax 3.3: newlines become LF, NUL and surrogates U+FFFD. Invalid
   UTF-8 also becomes U+FFFD, so the tokenizer can trust its input. */
static int preprocess(const char *in, size_t len, tc_buf *out)
{
    size_t i = 0;
    if (tc_buf_reserve(out, len + 1) != TC_OK)
        return TC_ERR_NOMEM;
    while (i < len) {
        unsigned char c = (unsigned char)in[i];
        if (c == '\r') {
            tc_buf_append_char(out, '\n');
            i += i + 1 < len && in[i + 1] == '\n' ? 2 : 1;
        } else if (c == '\f') {
            tc_buf_append_char(out, '\n');
            i++;
        } else if (c == 0) {
            tc_buf_append(out, "\xEF\xBF\xBD", 3);
            i++;
        } else if (c < 0x80) {
            tc_buf_append_char(out, (char)c);
            i++;
        } else {
            uint32_t cp;
            size_t n = tc_utf8_decode(in + i, len - i, &cp);
            if (cp == TC_REPLACEMENT_CHAR)
                tc_buf_append(out, "\xEF\xBF\xBD", 3);
            else
                tc_buf_append(out, in + i, n);
            i += n;
        }
    }
    return out->data ? TC_OK : TC_ERR_NOMEM;
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_hex(int c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hex_value(int c)
{
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

static int is_ws(int c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

/* Bytes of UTF-8 count as non-ASCII code points, which are ident code points. */
static int is_ident_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80 || c == '_';
}

static int is_ident_char(int c)
{
    return is_ident_start(c) || is_digit(c) || c == '-';
}

static int is_non_printable(int c)
{
    return (c >= 0 && c <= 8) || c == 0x0B || (c >= 0x0E && c <= 0x1F) || c == 0x7F;
}

int tc_css_ascii_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

int tc_css_is_ident(const tc_css_value *v, const char *name)
{
    return v && v->type == TC_CSS_IDENT && tc_css_ascii_ieq(v->text, name);
}

int tc_css_is_delim(const tc_css_value *v, uint32_t cp)
{
    return v && v->type == TC_CSS_DELIM && v->cp == cp;
}

/* ---- The tokenizer (CSS Syntax 4) ---- */

typedef struct tokenizer {
    const char *s;
    size_t len, pos;
    tc_arena *arena;
    tc_buf scratch;
    int nomem;
} tokenizer;

static int peek(const tokenizer *t, size_t k)
{
    return t->pos + k < t->len ? (unsigned char)t->s[t->pos + k] : -1;
}

/* A backslash at the very end is valid too: it means U+FFFD. */
static int valid_escape_at(const tokenizer *t, size_t k)
{
    return peek(t, k) == '\\' && peek(t, k + 1) != '\n';
}

static int starts_ident_at(const tokenizer *t, size_t k)
{
    int c = peek(t, k);
    if (c == '-')
        return is_ident_start(peek(t, k + 1)) || peek(t, k + 1) == '-' || valid_escape_at(t, k + 1);
    if (c == '\\')
        return valid_escape_at(t, k);
    return c >= 0 && is_ident_start(c);
}

static int starts_number_at(const tokenizer *t, size_t k)
{
    int c = peek(t, k);
    if (c == '+' || c == '-')
        return is_digit(peek(t, k + 1)) || (peek(t, k + 1) == '.' && is_digit(peek(t, k + 2)));
    if (c == '.')
        return is_digit(peek(t, k + 1));
    return is_digit(c);
}

static size_t utf8_len(unsigned char lead)
{
    return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
}

/* After the backslash. */
static void consume_escape(tokenizer *t, tc_buf *out)
{
    char utf8[4];
    int c = peek(t, 0);

    if (c == -1) {
        tc_buf_append(out, "\xEF\xBF\xBD", 3);
    } else if (is_hex(c)) {
        uint32_t v = 0;
        int n;
        for (n = 0; n < 6 && is_hex(peek(t, 0)); n++, t->pos++)
            v = v * 16 + (uint32_t)hex_value(peek(t, 0));
        if (is_ws(peek(t, 0)))
            t->pos++;
        if (v == 0 || (v >= 0xD800 && v <= 0xDFFF) || v > 0x10FFFF)
            v = TC_REPLACEMENT_CHAR;
        tc_buf_append(out, utf8, tc_utf8_encode(v, utf8));
    } else {
        size_t n = utf8_len((unsigned char)c);
        if (t->pos + n > t->len)
            n = t->len - t->pos;
        tc_buf_append(out, t->s + t->pos, n);
        t->pos += n;
    }
}

static void consume_ident_sequence(tokenizer *t, tc_buf *out)
{
    for (;;) {
        int c = peek(t, 0);
        if (c >= 0 && is_ident_char(c)) {
            tc_buf_append_char(out, (char)c);
            t->pos++;
        } else if (valid_escape_at(t, 0)) {
            t->pos++;
            consume_escape(t, out);
        } else {
            return;
        }
    }
}

static const char *keep(tokenizer *t, const tc_buf *b)
{
    char *s = tc_arena_strndup(t->arena, b->data ? b->data : "", b->len);
    if (!s) {
        t->nomem = 1;
        return "";
    }
    return s;
}

/* An exact conversion for ordinary numbers (few digits, small exponents),
   the same on every platform; strtod for the rest. */
static double number_value(const char *repr, size_t len)
{
    static const double pow10[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
    char copy[64];
    uint64_t mantissa = 0;
    int digits = 0, exp = 0, neg = 0, eneg = 0, e = 0;
    size_t i = 0;

    if (i < len && (repr[i] == '+' || repr[i] == '-'))
        neg = repr[i++] == '-';
    for (; i < len && is_digit(repr[i]); i++, digits++)
        mantissa = mantissa * 10 + (uint64_t)(repr[i] - '0');
    if (i < len && repr[i] == '.')
        for (i++; i < len && is_digit(repr[i]); i++, digits++, exp--)
            mantissa = mantissa * 10 + (uint64_t)(repr[i] - '0');
    if (i < len && (repr[i] == 'e' || repr[i] == 'E')) {
        i++;
        if (i < len && (repr[i] == '+' || repr[i] == '-'))
            eneg = repr[i++] == '-';
        for (; i < len && is_digit(repr[i]) && e < 100000; i++)
            e = e * 10 + (repr[i] - '0');
        exp += eneg ? -e : e;
    }
    if (digits <= 15 && exp >= -22 && exp <= 22) {
        double v = (double)mantissa;
        v = exp < 0 ? v / pow10[-exp] : v * pow10[exp];
        return neg ? -v : v;
    }
    if (len >= sizeof copy)
        len = sizeof copy - 1;
    memcpy(copy, repr, len);
    copy[len] = '\0';
    return strtod(copy, NULL);
}

static void consume_number(tokenizer *t, tc_css_value *tok)
{
    size_t start = t->pos;
    int integer = 1;

    if (peek(t, 0) == '+' || peek(t, 0) == '-')
        t->pos++;
    while (is_digit(peek(t, 0)))
        t->pos++;
    if (peek(t, 0) == '.' && is_digit(peek(t, 1))) {
        integer = 0;
        t->pos += 2;
        while (is_digit(peek(t, 0)))
            t->pos++;
    }
    if ((peek(t, 0) == 'e' || peek(t, 0) == 'E') &&
        (is_digit(peek(t, 1)) || ((peek(t, 1) == '+' || peek(t, 1) == '-') && is_digit(peek(t, 2))))) {
        integer = 0;
        t->pos += is_digit(peek(t, 1)) ? 1 : 2;
        while (is_digit(peek(t, 0)))
            t->pos++;
    }
    tok->repr = tc_arena_strndup(t->arena, t->s + start, t->pos - start);
    if (!tok->repr) {
        t->nomem = 1;
        tok->repr = "";
    }
    tok->number = number_value(t->s + start, t->pos - start);
    tok->flags = integer ? TC_CSS_INTEGER : 0;
}

static void consume_numeric(tokenizer *t, tc_css_value *tok)
{
    consume_number(t, tok);
    if (starts_ident_at(t, 0)) {
        tc_buf_clear(&t->scratch);
        consume_ident_sequence(t, &t->scratch);
        tok->type = TC_CSS_DIMENSION;
        tok->text = keep(t, &t->scratch);
    } else if (peek(t, 0) == '%') {
        t->pos++;
        tok->type = TC_CSS_PERCENTAGE;
    } else {
        tok->type = TC_CSS_NUMBER;
    }
}

static void consume_bad_url_remnants(tokenizer *t)
{
    for (;;) {
        int c = peek(t, 0);
        if (c == -1)
            return;
        if (c == ')') {
            t->pos++;
            return;
        }
        if (valid_escape_at(t, 0)) {
            t->pos++;
            consume_escape(t, &t->scratch); /* the value is thrown away */
        } else {
            t->pos++;
        }
    }
}

/* After "url(" and any whitespace. */
static void consume_url(tokenizer *t, tc_css_value *tok)
{
    tc_buf_clear(&t->scratch);
    tok->type = TC_CSS_URL;
    for (;;) {
        int c = peek(t, 0);
        if (c == ')') {
            t->pos++;
            break;
        }
        if (c == -1) {
            tok->flags |= TC_CSS_UNCLOSED;
            break;
        }
        if (is_ws(c)) {
            while (is_ws(peek(t, 0)))
                t->pos++;
            if (peek(t, 0) == ')' || peek(t, 0) == -1) {
                if (peek(t, 0) == ')')
                    t->pos++;
                else
                    tok->flags |= TC_CSS_UNCLOSED;
                break;
            }
            consume_bad_url_remnants(t);
            tok->type = TC_CSS_BAD_URL;
            break;
        }
        if (c == '"' || c == '\'' || c == '(' || is_non_printable(c)) {
            consume_bad_url_remnants(t);
            tok->type = TC_CSS_BAD_URL;
            break;
        }
        if (c == '\\') {
            if (!valid_escape_at(t, 0)) {
                consume_bad_url_remnants(t);
                tok->type = TC_CSS_BAD_URL;
                break;
            }
            t->pos++;
            consume_escape(t, &t->scratch);
            continue;
        }
        tc_buf_append_char(&t->scratch, (char)c);
        t->pos++;
    }
    tok->text = tok->type == TC_CSS_URL ? keep(t, &t->scratch) : "";
}

static void consume_ident_like(tokenizer *t, tc_css_value *tok)
{
    tc_buf_clear(&t->scratch);
    consume_ident_sequence(t, &t->scratch);
    if (tc_css_ascii_ieq(tc_buf_cstr(&t->scratch), "url") && peek(t, 0) == '(') {
        t->pos++;
        while (is_ws(peek(t, 0)) && is_ws(peek(t, 1)))
            t->pos++;
        if (peek(t, 0) == '"' || peek(t, 0) == '\'' ||
            (is_ws(peek(t, 0)) && (peek(t, 1) == '"' || peek(t, 1) == '\''))) {
            tok->type = TC_CSS_FUNCTION_TOKEN;
            tok->text = keep(t, &t->scratch);
            return;
        }
        while (is_ws(peek(t, 0)))
            t->pos++;
        consume_url(t, tok);
        return;
    }
    tok->text = keep(t, &t->scratch);
    if (peek(t, 0) == '(') {
        t->pos++;
        tok->type = TC_CSS_FUNCTION_TOKEN;
    } else {
        tok->type = TC_CSS_IDENT;
    }
}

static void consume_string(tokenizer *t, int quote, tc_css_value *tok)
{
    tc_buf_clear(&t->scratch);
    tok->type = TC_CSS_STRING;
    for (;;) {
        int c = peek(t, 0);
        if (c == quote) {
            t->pos++;
            break;
        }
        if (c == -1) {
            tok->flags |= TC_CSS_UNCLOSED;
            break;
        }
        if (c == '\n') {
            tok->type = TC_CSS_BAD_STRING; /* the newline isn't consumed */
            break;
        }
        if (c == '\\') {
            if (peek(t, 1) == -1) {
                t->pos++;
            } else if (peek(t, 1) == '\n') {
                t->pos += 2;
            } else {
                t->pos++;
                consume_escape(t, &t->scratch);
            }
            continue;
        }
        tc_buf_append_char(&t->scratch, (char)c);
        t->pos++;
    }
    tok->text = tok->type == TC_CSS_STRING ? keep(t, &t->scratch) : "";
}

static void next_token(tokenizer *t, tc_css_value *tok)
{
    int c;

    memset(tok, 0, sizeof *tok);
    tok->text = "";
    /* Comments. */
    while (peek(t, 0) == '/' && peek(t, 1) == '*') {
        const char *end = NULL;
        size_t i;
        for (i = t->pos + 2; i + 1 < t->len; i++)
            if (t->s[i] == '*' && t->s[i + 1] == '/') {
                end = t->s + i;
                break;
            }
        t->pos = end ? (size_t)(end - t->s) + 2 : t->len;
    }
    c = peek(t, 0);
    if (c == -1) {
        tok->type = TC_CSS_EOF;
        return;
    }
    if (is_ws(c)) {
        while (is_ws(peek(t, 0)))
            t->pos++;
        tok->type = TC_CSS_WHITESPACE;
        return;
    }
    if (c == '"' || c == '\'') {
        t->pos++;
        consume_string(t, c, tok);
        return;
    }
    if (is_digit(c)) {
        consume_numeric(t, tok);
        return;
    }
    if (is_ident_start(c)) {
        consume_ident_like(t, tok);
        return;
    }
    switch (c) {
    case '#':
        if ((peek(t, 1) >= 0 && is_ident_char(peek(t, 1))) || valid_escape_at(t, 1)) {
            t->pos++;
            tok->type = TC_CSS_HASH;
            if (starts_ident_at(t, 0))
                tok->flags = TC_CSS_HASH_ID;
            tc_buf_clear(&t->scratch);
            consume_ident_sequence(t, &t->scratch);
            tok->text = keep(t, &t->scratch);
            return;
        }
        break;
    case '(': t->pos++; tok->type = TC_CSS_LPAREN; return;
    case ')': t->pos++; tok->type = TC_CSS_RPAREN; return;
    case '[': t->pos++; tok->type = TC_CSS_LBRACKET; return;
    case ']': t->pos++; tok->type = TC_CSS_RBRACKET; return;
    case '{': t->pos++; tok->type = TC_CSS_LBRACE; return;
    case '}': t->pos++; tok->type = TC_CSS_RBRACE; return;
    case ',': t->pos++; tok->type = TC_CSS_COMMA; return;
    case ':': t->pos++; tok->type = TC_CSS_COLON; return;
    case ';': t->pos++; tok->type = TC_CSS_SEMICOLON; return;
    case '+':
    case '.':
        if (starts_number_at(t, 0)) {
            consume_numeric(t, tok);
            return;
        }
        break;
    case '-':
        if (starts_number_at(t, 0)) {
            consume_numeric(t, tok);
            return;
        }
        if (peek(t, 1) == '-' && peek(t, 2) == '>') {
            t->pos += 3;
            tok->type = TC_CSS_CDC;
            return;
        }
        if (starts_ident_at(t, 0)) {
            consume_ident_like(t, tok);
            return;
        }
        break;
    case '<':
        if (peek(t, 1) == '!' && peek(t, 2) == '-' && peek(t, 3) == '-') {
            t->pos += 4;
            tok->type = TC_CSS_CDO;
            return;
        }
        break;
    case '@':
        if (starts_ident_at(t, 1)) {
            t->pos++;
            tc_buf_clear(&t->scratch);
            consume_ident_sequence(t, &t->scratch);
            tok->type = TC_CSS_AT_KEYWORD;
            tok->text = keep(t, &t->scratch);
            return;
        }
        break;
    case '\\':
        if (valid_escape_at(t, 0)) {
            consume_ident_like(t, tok);
            return;
        }
        break;
    }
    /* A delim: the code point itself (ASCII here; non-ASCII starts an ident). */
    t->pos++;
    tok->type = TC_CSS_DELIM;
    tok->cp = (uint32_t)c;
}

/* ---- Component values, built without recursion ---- */

typedef struct vec {
    tc_css_value *items;
    size_t len, cap;
} vec;

static int vec_push(vec *v, const tc_css_value *item)
{
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 8;
        tc_css_value *grown = realloc(v->items, cap * sizeof *grown);
        if (!grown)
            return TC_ERR_NOMEM;
        v->items = grown;
        v->cap = cap;
    }
    v->items[v->len++] = *item;
    return TC_OK;
}

/* Move a vector's items into the arena. */
static int vec_finish(tc_arena *a, vec *v, tc_css_value **out, uint32_t *count)
{
    *out = NULL;
    *count = (uint32_t)v->len;
    if (v->len) {
        *out = tc_arena_alloc(a, v->len * sizeof **out);
        if (!*out) {
            free(v->items);
            return TC_ERR_NOMEM;
        }
        memcpy(*out, v->items, v->len * sizeof **out);
    }
    free(v->items);
    v->items = NULL;
    v->len = v->cap = 0;
    return TC_OK;
}

typedef struct frame {
    tc_css_value head;  /* the function or block being filled */
    uint8_t close;      /* the token type that ends it */
    vec items;
} frame;

static int closing_type(int type)
{
    return type == TC_CSS_LBRACE ? TC_CSS_RBRACE : type == TC_CSS_LBRACKET ? TC_CSS_RBRACKET : TC_CSS_RPAREN;
}

int tc_css_parse_values(tc_arena *a, const char *text, size_t len, tc_css_value **out, uint32_t *count)
{
    tokenizer t;
    tc_buf clean;
    frame *stack = NULL;
    size_t depth = 0, cap = 0;
    vec top = { NULL, 0, 0 };
    int err = TC_OK;

    *out = NULL;
    *count = 0;
    tc_buf_init(&clean);
    if (preprocess(text, len, &clean) != TC_OK) {
        tc_buf_free(&clean);
        return TC_ERR_NOMEM;
    }
    memset(&t, 0, sizeof t);
    t.s = tc_buf_cstr(&clean);
    t.len = clean.len;
    t.arena = a;
    tc_buf_init(&t.scratch);

    for (;;) {
        tc_css_value tok;
        vec *dest = depth ? &stack[depth - 1].items : &top;
        next_token(&t, &tok);
        if (t.nomem) {
            err = TC_ERR_NOMEM;
            break;
        }
        if (tok.type == TC_CSS_EOF)
            break;
        if (tok.type == TC_CSS_LBRACE || tok.type == TC_CSS_LBRACKET || tok.type == TC_CSS_LPAREN ||
            tok.type == TC_CSS_FUNCTION_TOKEN) {
            frame *f;
            if (depth == cap) {
                size_t ncap = cap ? cap * 2 : 16;
                frame *grown = realloc(stack, ncap * sizeof *grown);
                if (!grown) {
                    err = TC_ERR_NOMEM;
                    break;
                }
                stack = grown;
                cap = ncap;
            }
            f = &stack[depth++];
            memset(f, 0, sizeof *f);
            f->head = tok;
            if (tok.type == TC_CSS_FUNCTION_TOKEN) {
                f->head.type = TC_CSS_FUNCTION;
                f->close = TC_CSS_RPAREN;
            } else {
                f->head.type = TC_CSS_BLOCK;
                f->head.cp = tok.type == TC_CSS_LBRACE ? '{' : tok.type == TC_CSS_LBRACKET ? '[' : '(';
                f->close = (uint8_t)closing_type(tok.type);
            }
            continue;
        }
        if (depth && tok.type == stack[depth - 1].close) {
            frame *f = &stack[--depth];
            if ((err = vec_finish(a, &f->items, &f->head.items, &f->head.nitems)) != TC_OK)
                break;
            dest = depth ? &stack[depth - 1].items : &top;
            if ((err = vec_push(dest, &f->head)) != TC_OK)
                break;
            continue;
        }
        if ((err = vec_push(dest, &tok)) != TC_OK)
            break;
    }
    /* Close whatever the end of the input left open. */
    while (depth) {
        frame *f = &stack[--depth];
        vec *dest = depth ? &stack[depth - 1].items : &top;
        f->head.flags |= TC_CSS_UNCLOSED;
        if (err == TC_OK && (err = vec_finish(a, &f->items, &f->head.items, &f->head.nitems)) == TC_OK)
            err = vec_push(dest, &f->head);
        free(f->items.items);
    }
    free(stack);
    tc_buf_free(&t.scratch);
    tc_buf_free(&clean);
    if (err == TC_OK)
        return vec_finish(a, &top, out, count);
    free(top.items);
    return err;
}

/* ---- Rules and declarations (CSS Syntax 5.4), over component values ---- */

typedef struct cursor {
    const tc_css_value *v;
    uint32_t n, i;
} cursor;

static const tc_css_value *cur(const cursor *c)
{
    return c->i < c->n ? &c->v[c->i] : NULL;
}

static int cur_is(const cursor *c, int type)
{
    return c->i < c->n && c->v[c->i].type == type;
}

static void skip_ws(cursor *c)
{
    while (cur_is(c, TC_CSS_WHITESPACE))
        c->i++;
}

static int is_lbrace_block(const tc_css_value *v)
{
    return v && v->type == TC_CSS_BLOCK && v->cp == '{';
}

static int copy_values(tc_arena *a, const tc_css_value *v, uint32_t n, tc_css_value **out, uint32_t *count)
{
    *out = NULL;
    *count = n;
    if (n) {
        *out = tc_arena_alloc(a, n * sizeof **out);
        if (!*out)
            return TC_ERR_NOMEM;
        memcpy(*out, v, n * sizeof **out);
    }
    return TC_OK;
}

static void bad_declaration_remnants(cursor *c, int nested)
{
    while (c->i < c->n) {
        if (cur_is(c, TC_CSS_SEMICOLON)) {
            c->i++;
            return;
        }
        if (nested && cur_is(c, TC_CSS_RBRACE))
            return;
        c->i++;
    }
}

/* "Consume an at-rule". Returns 1 if a rule was made. */
static int consume_at_rule(tc_arena *a, cursor *c, int nested, tc_css_rule *rule, int *nomem)
{
    uint32_t start;
    memset(rule, 0, sizeof *rule);
    rule->type = TC_CSS_RULE_AT;
    rule->name = c->v[c->i++].text;
    start = c->i;
    for (;;) {
        const tc_css_value *v = cur(c);
        uint32_t end = c->i;
        if (!v || v->type == TC_CSS_SEMICOLON || (nested && v->type == TC_CSS_RBRACE) || is_lbrace_block(v)) {
            if (v && v->type == TC_CSS_SEMICOLON)
                c->i++;
            if (is_lbrace_block(v)) {
                rule->block = v;
                c->i++;
            }
            if (copy_values(a, c->v + start, end - start, &rule->prelude, &rule->nprelude) != TC_OK)
                *nomem = 1;
            return 1;
        }
        c->i++;
    }
}

/* "--foo:" at the start of a prelude: a custom property, not a selector. */
static int starts_like_custom_property(const tc_css_value *v, uint32_t n)
{
    uint32_t i = 0;
    while (i < n && v[i].type == TC_CSS_WHITESPACE)
        i++;
    if (i >= n || v[i].type != TC_CSS_IDENT || strncmp(v[i].text, "--", 2) != 0)
        return 0;
    for (i++; i < n && v[i].type == TC_CSS_WHITESPACE; i++)
        ;
    return i < n && v[i].type == TC_CSS_COLON;
}

/* "Consume a qualified rule": 1 if a rule was made, 0 if nothing (an error). */
static int consume_qualified_rule(tc_arena *a, cursor *c, int stop_type, int nested, tc_css_rule *rule, int *nomem)
{
    uint32_t start = c->i;
    memset(rule, 0, sizeof *rule);
    rule->type = TC_CSS_RULE_QUALIFIED;
    for (;;) {
        const tc_css_value *v = cur(c);
        if (!v || (stop_type >= 0 && v->type == stop_type))
            return 0;
        if (v->type == TC_CSS_RBRACE && nested)
            return 0;
        if (is_lbrace_block(v)) {
            uint32_t end = c->i;
            c->i++;
            if (starts_like_custom_property(c->v + start, end - start))
                return 0;
            rule->block = v;
            if (copy_values(a, c->v + start, end - start, &rule->prelude, &rule->nprelude) != TC_OK)
                *nomem = 1;
            return 1;
        }
        c->i++;
    }
}

int tc_css_parse_rules(tc_arena *a, const tc_css_value *values, uint32_t count, int top_level, int keep_errors,
                       tc_css_rule **out, uint32_t *nrules)
{
    cursor c = { values, count, 0 };
    tc_css_rule *rules = NULL;
    size_t len = 0, cap = 0;
    int nomem = 0;

    *out = NULL;
    *nrules = 0;
    while (c.i < c.n) {
        tc_css_rule rule;
        int made;
        if (cur_is(&c, TC_CSS_WHITESPACE) || (top_level && (cur_is(&c, TC_CSS_CDO) || cur_is(&c, TC_CSS_CDC)))) {
            c.i++;
            continue;
        }
        if (cur_is(&c, TC_CSS_AT_KEYWORD))
            made = consume_at_rule(a, &c, 0, &rule, &nomem);
        else
            made = consume_qualified_rule(a, &c, -1, 0, &rule, &nomem);
        if (!made && !keep_errors)
            continue;
        if (!made) {
            memset(&rule, 0, sizeof rule);
            rule.type = TC_CSS_RULE_ERROR;
        }
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            tc_css_rule *grown = realloc(rules, ncap * sizeof *grown);
            if (!grown) {
                nomem = 1;
                break;
            }
            rules = grown;
            cap = ncap;
        }
        rules[len++] = rule;
    }
    if (!nomem && len) {
        *out = tc_arena_alloc(a, len * sizeof **out);
        if (*out)
            memcpy(*out, rules, len * sizeof **out);
        else
            nomem = 1;
    }
    *nrules = nomem ? 0 : (uint32_t)len;
    free(rules);
    return nomem ? TC_ERR_NOMEM : TC_OK;
}

/* "Consume a declaration". Returns 1 if one was made. */
static int consume_declaration(tc_arena *a, cursor *c, int nested, tc_css_decl *decl, int *nomem)
{
    uint32_t start, end, i, last, prev, nonws_blocks = 0, nonws = 0;

    memset(decl, 0, sizeof *decl);
    if (!cur_is(c, TC_CSS_IDENT)) {
        bad_declaration_remnants(c, nested);
        return 0;
    }
    decl->name = c->v[c->i++].text;
    skip_ws(c);
    if (!cur_is(c, TC_CSS_COLON)) {
        bad_declaration_remnants(c, nested);
        return 0;
    }
    c->i++;
    skip_ws(c);
    start = c->i;
    while (c->i < c->n && !cur_is(c, TC_CSS_SEMICOLON) && !(nested && cur_is(c, TC_CSS_RBRACE)))
        c->i++;
    end = c->i;

    /* !important: the last two non-whitespace values. */
    for (last = end; last > start && c->v[last - 1].type == TC_CSS_WHITESPACE; last--)
        ;
    if (last > start && tc_css_is_ident(&c->v[last - 1], "important")) {
        for (prev = last - 1; prev > start && c->v[prev - 1].type == TC_CSS_WHITESPACE; prev--)
            ;
        if (prev > start && tc_css_is_delim(&c->v[prev - 1], '!')) {
            decl->important = 1;
            end = prev - 1;
        }
    }
    while (end > start && c->v[end - 1].type == TC_CSS_WHITESPACE)
        end--;

    /* A {} block is only allowed as the whole value (custom properties aside). */
    if (strncmp(decl->name, "--", 2) != 0) {
        for (i = start; i < end; i++) {
            if (c->v[i].type == TC_CSS_WHITESPACE)
                continue;
            nonws++;
            if (is_lbrace_block(&c->v[i]))
                nonws_blocks++;
        }
        if (nonws_blocks && nonws > 1)
            return 0;
    }
    if (copy_values(a, c->v + start, end - start, &decl->value, &decl->nvalue) != TC_OK)
        *nomem = 1;
    return 1;
}

int tc_css_parse_block(tc_arena *a, const tc_css_value *values, uint32_t count, tc_css_item **out,
                       uint32_t *nitems)
{
    cursor c = { values, count, 0 };
    tc_css_item *items = NULL;
    size_t len = 0, cap = 0;
    int nomem = 0;

    *out = NULL;
    *nitems = 0;
    while (c.i < c.n && !nomem) {
        tc_css_item item;
        int made;
        if (cur_is(&c, TC_CSS_WHITESPACE) || cur_is(&c, TC_CSS_SEMICOLON)) {
            c.i++;
            continue;
        }
        memset(&item, 0, sizeof item);
        if (cur_is(&c, TC_CSS_AT_KEYWORD)) {
            made = consume_at_rule(a, &c, 1, &item.rule, &nomem);
            item.type = TC_CSS_ITEM_RULE;
        } else {
            uint32_t mark = c.i;
            made = consume_declaration(a, &c, 1, &item.decl, &nomem);
            item.type = TC_CSS_ITEM_DECL;
            if (!made) {
                c.i = mark;
                made = consume_qualified_rule(a, &c, TC_CSS_SEMICOLON, 1, &item.rule, &nomem);
                item.type = TC_CSS_ITEM_RULE;
            }
        }
        if (!made) {
            item.type = TC_CSS_ITEM_ERROR;
            /* An unmatched '}' can't be consumed by the nested rule above. */
            if (cur_is(&c, TC_CSS_RBRACE))
                c.i++;
        }
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            tc_css_item *grown = realloc(items, ncap * sizeof *grown);
            if (!grown) {
                nomem = 1;
                break;
            }
            items = grown;
            cap = ncap;
        }
        items[len++] = item;
    }
    if (!nomem && len) {
        *out = tc_arena_alloc(a, len * sizeof **out);
        if (*out)
            memcpy(*out, items, len * sizeof **out);
        else
            nomem = 1;
    }
    *nitems = nomem ? 0 : (uint32_t)len;
    free(items);
    return nomem ? TC_ERR_NOMEM : TC_OK;
}

int tc_css_parse_one_declaration(tc_arena *a, const tc_css_value *values, uint32_t count, tc_css_decl *out)
{
    cursor c = { values, count, 0 };
    int nomem = 0;
    skip_ws(&c);
    if (!cur_is(&c, TC_CSS_IDENT) || !consume_declaration(a, &c, 0, out, &nomem))
        return TC_ERR_INVALID;
    return nomem ? TC_ERR_NOMEM : TC_OK;
}

int tc_css_parse_one_rule(tc_arena *a, const tc_css_value *values, uint32_t count, tc_css_rule *out,
                          const char **error)
{
    cursor c = { values, count, 0 };
    int nomem = 0, made;

    *error = NULL;
    skip_ws(&c);
    if (c.i == c.n) {
        *error = "empty";
        return TC_ERR_INVALID;
    }
    if (cur_is(&c, TC_CSS_AT_KEYWORD))
        made = consume_at_rule(a, &c, 0, out, &nomem);
    else
        made = consume_qualified_rule(a, &c, -1, 0, out, &nomem);
    if (!made) {
        *error = "invalid";
        return TC_ERR_INVALID;
    }
    skip_ws(&c);
    if (c.i != c.n) {
        *error = "extra-input";
        return TC_ERR_INVALID;
    }
    return nomem ? TC_ERR_NOMEM : TC_OK;
}

int tc_css_parse_one_value(const tc_css_value *values, uint32_t count, const tc_css_value **out,
                           const char **error)
{
    cursor c = { values, count, 0 };
    *error = NULL;
    skip_ws(&c);
    if (c.i == c.n) {
        *error = "empty";
        return TC_ERR_INVALID;
    }
    *out = &c.v[c.i++];
    skip_ws(&c);
    if (c.i != c.n) {
        *error = "extra-input";
        return TC_ERR_INVALID;
    }
    return TC_OK;
}

/* ---- An+B (CSS Syntax 6) ---- */

static int is_integer(const tc_css_value *v)
{
    return v && v->type == TC_CSS_NUMBER && (v->flags & TC_CSS_INTEGER);
}

static int is_signed(const tc_css_value *v)
{
    return v->repr[0] == '+' || v->repr[0] == '-';
}

/* "n-123" style text: 'n' (already checked) then '-' and digits. Sets b. */
static int dash_digits(const char *s, int *b)
{
    long v = 0;
    if (*s != '-' || !is_digit(s[1]))
        return 0;
    for (s++; *s; s++) {
        if (!is_digit(*s))
            return 0;
        if (v < 100000000)
            v = v * 10 + (*s - '0');
    }
    *b = -(int)v;
    return 1;
}

static int clamp_int(double d)
{
    return d > 2147483647.0 ? 2147483647 : d < -2147483648.0 ? (int)-2147483647 - 1 : (int)d;
}

int tc_css_parse_anb(const tc_css_value *values, uint32_t count, int *a, int *b)
{
    cursor c = { values, count, 0 };
    const tc_css_value *v;
    const char *n_text; /* the text starting at 'n' */
    int plus = 0, sign = 0;

    skip_ws(&c);
    v = cur(&c);
    if (!v)
        return TC_ERR_INVALID;
    *a = 0;
    *b = 0;
    if (tc_css_is_ident(v, "odd") || tc_css_is_ident(v, "even")) {
        *a = 2;
        *b = tc_css_is_ident(v, "odd") ? 1 : 0;
        c.i++;
        goto done;
    }
    if (is_integer(v)) {
        *b = clamp_int(v->number);
        c.i++;
        goto done;
    }
    if (v->type == TC_CSS_DIMENSION && (v->flags & TC_CSS_INTEGER) && (v->text[0] | 0x20) == 'n') {
        *a = clamp_int(v->number);
        n_text = v->text;
        c.i++;
    } else {
        if (tc_css_is_delim(v, '+')) {
            /* '+' must touch the ident that follows. */
            if (c.i + 1 >= c.n || c.v[c.i + 1].type != TC_CSS_IDENT)
                return TC_ERR_INVALID;
            plus = 1;
            v = &c.v[++c.i];
        }
        if (v->type != TC_CSS_IDENT)
            return TC_ERR_INVALID;
        if ((v->text[0] | 0x20) == 'n') {
            *a = 1;
            n_text = v->text;
        } else if (!plus && v->text[0] == '-' && (v->text[1] | 0x20) == 'n') {
            *a = -1;
            n_text = v->text + 1;
        } else {
            return TC_ERR_INVALID;
        }
        c.i++;
    }
    /* After the 'n': nothing, "-", or "-digits" in the same token. */
    if (n_text[1] == '\0') {
        skip_ws(&c);
        v = cur(&c);
        if (v && is_integer(v) && is_signed(v)) {
            *b = clamp_int(v->number);
            c.i++;
        } else if (v && (tc_css_is_delim(v, '+') || tc_css_is_delim(v, '-'))) {
            sign = v->cp == '+' ? 1 : -1;
            c.i++;
            skip_ws(&c);
            v = cur(&c);
            if (!v || !is_integer(v) || is_signed(v))
                return TC_ERR_INVALID;
            *b = sign * clamp_int(v->number);
            c.i++;
        }
    } else if (n_text[1] == '-' && n_text[2] == '\0') {
        skip_ws(&c);
        v = cur(&c);
        if (!v || !is_integer(v) || is_signed(v))
            return TC_ERR_INVALID;
        *b = -clamp_int(v->number);
        c.i++;
    } else if (!dash_digits(n_text + 1, b)) {
        return TC_ERR_INVALID;
    }
done:
    skip_ws(&c);
    return c.i == c.n ? TC_OK : TC_ERR_INVALID;
}

/* ---- Decoding a style sheet (CSS Syntax 3.2) ---- */

tc_encoding tc_css_decode(const void *bytes, size_t len, const char *protocol_label, const char *environment_label,
                          tc_buf *out)
{
    const unsigned char *p = bytes;
    size_t bom;
    tc_encoding enc = tc_encoding_sniff_bom(bytes, len, &bom);

    if (enc == TC_ENC_NONE && protocol_label)
        enc = tc_encoding_from_label(protocol_label, strlen(protocol_label));
    if (enc == TC_ENC_NONE && len >= 10 && memcmp(p, "@charset \"", 10) == 0) {
        size_t i;
        for (i = 10; i < len && i < 1024; i++) {
            if (p[i] == '"') {
                if (i + 1 < len && p[i + 1] == ';') {
                    enc = tc_encoding_from_label((const char *)p + 10, i - 10);
                    /* A UTF-16 label in an ASCII-compatible file is a mistake. */
                    if (enc == TC_ENC_UTF_16BE || enc == TC_ENC_UTF_16LE)
                        enc = TC_ENC_UTF_8;
                }
                break;
            }
        }
    }
    if (enc == TC_ENC_NONE && environment_label)
        enc = tc_encoding_from_label(environment_label, strlen(environment_label));
    if (enc == TC_ENC_NONE)
        enc = TC_ENC_UTF_8;
    tc_decode(enc, bytes, len, out);
    return enc;
}
