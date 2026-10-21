#include "tc_html_tokenizer.h"

#include <stdlib.h>
#include <string.h>

#include "tc_entities.h"

#define EOF_CP 0xFFFFFFFFu

/* States; the first few are the public tc_tok_state values. */
enum {
    S_DATA = TC_TOK_DATA,
    S_RCDATA = TC_TOK_RCDATA,
    S_RAWTEXT = TC_TOK_RAWTEXT,
    S_SCRIPT_DATA = TC_TOK_SCRIPT_DATA,
    S_PLAINTEXT = TC_TOK_PLAINTEXT,
    S_CDATA_SECTION = TC_TOK_CDATA_SECTION,
    S_TAG_OPEN,
    S_END_TAG_OPEN,
    S_TAG_NAME,
    S_RCDATA_LT,
    S_RCDATA_END_TAG_OPEN,
    S_RCDATA_END_TAG_NAME,
    S_RAWTEXT_LT,
    S_RAWTEXT_END_TAG_OPEN,
    S_RAWTEXT_END_TAG_NAME,
    S_SCRIPT_LT,
    S_SCRIPT_END_TAG_OPEN,
    S_SCRIPT_END_TAG_NAME,
    S_SCRIPT_ESCAPE_START,
    S_SCRIPT_ESCAPE_START_DASH,
    S_SCRIPT_ESCAPED,
    S_SCRIPT_ESCAPED_DASH,
    S_SCRIPT_ESCAPED_DASH_DASH,
    S_SCRIPT_ESCAPED_LT,
    S_SCRIPT_ESCAPED_END_TAG_OPEN,
    S_SCRIPT_ESCAPED_END_TAG_NAME,
    S_SCRIPT_DOUBLE_ESCAPE_START,
    S_SCRIPT_DOUBLE_ESCAPED,
    S_SCRIPT_DOUBLE_ESCAPED_DASH,
    S_SCRIPT_DOUBLE_ESCAPED_DASH_DASH,
    S_SCRIPT_DOUBLE_ESCAPED_LT,
    S_SCRIPT_DOUBLE_ESCAPE_END,
    S_BEFORE_ATTR_NAME,
    S_ATTR_NAME,
    S_AFTER_ATTR_NAME,
    S_BEFORE_ATTR_VALUE,
    S_ATTR_VALUE_DQ,
    S_ATTR_VALUE_SQ,
    S_ATTR_VALUE_UNQ,
    S_AFTER_ATTR_VALUE_QUOTED,
    S_SELF_CLOSING_START_TAG,
    S_BOGUS_COMMENT,
    S_MARKUP_DECL_OPEN,
    S_COMMENT_START,
    S_COMMENT_START_DASH,
    S_COMMENT,
    S_COMMENT_LT,
    S_COMMENT_LT_BANG,
    S_COMMENT_LT_BANG_DASH,
    S_COMMENT_LT_BANG_DASH_DASH,
    S_COMMENT_END_DASH,
    S_COMMENT_END,
    S_COMMENT_END_BANG,
    S_DOCTYPE,
    S_BEFORE_DOCTYPE_NAME,
    S_DOCTYPE_NAME,
    S_AFTER_DOCTYPE_NAME,
    S_AFTER_DOCTYPE_PUBLIC_KW,
    S_BEFORE_DOCTYPE_PUBLIC_ID,
    S_DOCTYPE_PUBLIC_ID_DQ,
    S_DOCTYPE_PUBLIC_ID_SQ,
    S_AFTER_DOCTYPE_PUBLIC_ID,
    S_BETWEEN_DOCTYPE_IDS,
    S_AFTER_DOCTYPE_SYSTEM_KW,
    S_BEFORE_DOCTYPE_SYSTEM_ID,
    S_DOCTYPE_SYSTEM_ID_DQ,
    S_DOCTYPE_SYSTEM_ID_SQ,
    S_AFTER_DOCTYPE_SYSTEM_ID,
    S_BOGUS_DOCTYPE,
    S_CDATA_SECTION_BRACKET,
    S_CDATA_SECTION_END,
    S_PI_OPEN,
    S_PI_TARGET,
    S_AFTER_PI_TARGET,
    S_PI_DATA,
    S_PI_QUESTIONABLE,
    S_CHAR_REF,
    S_NAMED_CHAR_REF,
    S_AMBIGUOUS_AMPERSAND,
    S_NUMERIC_CHAR_REF,
    S_HEX_CHAR_REF_START,
    S_HEX_CHAR_REF,
    S_DEC_CHAR_REF,
    S_NUMERIC_CHAR_REF_END
};

static const char *const error_names[] = {
    "",
    "abrupt-closing-of-empty-comment",
    "abrupt-doctype-public-identifier",
    "abrupt-doctype-system-identifier",
    "absence-of-digits-in-numeric-character-reference",
    "cdata-in-html-content",
    "character-reference-outside-unicode-range",
    "control-character-in-input-stream",
    "control-character-reference",
    "disallowed-processing-instruction-target",
    "duplicate-attribute",
    "end-tag-with-attributes",
    "end-tag-with-trailing-solidus",
    "eof-before-tag-name",
    "eof-in-cdata",
    "eof-in-comment",
    "eof-in-doctype",
    "eof-in-processing-instruction",
    "eof-in-script-html-comment-like-text",
    "eof-in-tag",
    "incorrectly-closed-comment",
    "incorrectly-opened-comment",
    "invalid-character-sequence-after-doctype-name",
    "invalid-first-character-of-processing-instruction-target",
    "invalid-first-character-of-tag-name",
    "invalid-processing-instruction-target",
    "missing-attribute-value",
    "missing-doctype-name",
    "missing-doctype-public-identifier",
    "missing-doctype-system-identifier",
    "missing-end-tag-name",
    "missing-quote-before-doctype-public-identifier",
    "missing-quote-before-doctype-system-identifier",
    "missing-semicolon-after-character-reference",
    "missing-whitespace-after-doctype-public-keyword",
    "missing-whitespace-after-doctype-system-keyword",
    "missing-whitespace-before-doctype-name",
    "missing-whitespace-between-attributes",
    "missing-whitespace-between-doctype-public-and-system-identifiers",
    "nested-comment",
    "noncharacter-character-reference",
    "noncharacter-in-input-stream",
    "non-void-html-element-start-tag-with-trailing-solidus",
    "null-character-reference",
    "surrogate-character-reference",
    "surrogate-in-input-stream",
    "unexpected-character-after-doctype-system-identifier",
    "unexpected-character-in-attribute-name",
    "unexpected-character-in-unquoted-attribute-value",
    "unexpected-equals-sign-before-attribute-name",
    "unexpected-null-character",
    "unexpected-solidus-in-tag",
    "unknown-named-character-reference",
    "tree-construction"
};

const char *tc_html_error_name(int code)
{
    if (code <= 0 || code >= (int)TC_ARRAY_LEN(error_names))
        return "";
    return error_names[code];
}

/* --- Characters ------------------------------------------------------- */

static int is_ws(uint32_t c)
{
    return c == '\t' || c == '\n' || c == '\f' || c == ' ';
}

static int is_upper(uint32_t c)
{
    return c >= 'A' && c <= 'Z';
}

static int is_lower(uint32_t c)
{
    return c >= 'a' && c <= 'z';
}

static int is_alpha(uint32_t c)
{
    return is_upper(c) || is_lower(c);
}

static int is_digit(uint32_t c)
{
    return c >= '0' && c <= '9';
}

static int is_alnum(uint32_t c)
{
    return is_alpha(c) || is_digit(c);
}

static int is_hex(uint32_t c)
{
    return is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static int is_noncharacter(uint32_t c)
{
    return (c >= 0xFDD0 && c <= 0xFDEF) || ((c & 0xFFFE) == 0xFFFE && c <= 0x10FFFF);
}

static int is_control(uint32_t c)
{
    return c <= 0x1F || (c >= 0x7F && c <= 0x9F);
}

static int is_cont(char b)
{
    return ((unsigned char)b & 0xC0) == 0x80;
}

/* Decode one code point. Surrogates are allowed (as WTF-8 sequences), since
   tests and, later, document.write() can put them in the input stream. */
static uint32_t decode_at(const char *s, size_t avail, size_t *n)
{
    const unsigned char *u = (const unsigned char *)s;
    uint32_t cp;

    if (u[0] < 0x80) {
        *n = 1;
        return u[0];
    }
    if (u[0] >= 0xC2 && u[0] <= 0xDF && avail >= 2 && is_cont(s[1])) {
        *n = 2;
        return ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    }
    if (u[0] >= 0xE0 && u[0] <= 0xEF && avail >= 3 && is_cont(s[1]) && is_cont(s[2])) {
        cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        if (cp >= 0x800) {
            *n = 3;
            return cp;
        }
    }
    if (u[0] >= 0xF0 && u[0] <= 0xF4 && avail >= 4 && is_cont(s[1]) && is_cont(s[2]) && is_cont(s[3])) {
        cp = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) | ((uint32_t)(u[2] & 0x3F) << 6) |
             (u[3] & 0x3F);
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            *n = 4;
            return cp;
        }
    }
    *n = 1;
    return 0xFFFD;
}

static void put(tc_tokenizer *t, tc_buf *b, uint32_t c)
{
    char u[4];
    size_t n;

    if (c < 0x80) {
        if (tc_buf_append_char(b, (char)c))
            t->nomem = 1;
        return;
    }
    if (c < 0x800) {
        u[0] = (char)(0xC0 | (c >> 6));
        u[1] = (char)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        u[0] = (char)(0xE0 | (c >> 12));
        u[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        u[2] = (char)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        u[0] = (char)(0xF0 | (c >> 18));
        u[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        u[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        u[3] = (char)(0x80 | (c & 0x3F));
        n = 4;
    }
    if (tc_buf_append(b, u, n))
        t->nomem = 1;
}

static void put_bytes(tc_tokenizer *t, tc_buf *b, const char *s, size_t len)
{
    if (tc_buf_append(b, s, len))
        t->nomem = 1;
}

static void err(tc_tokenizer *t, int code)
{
    if (t->on_error)
        t->on_error(t->ctx, code);
}

/* The next input character without consuming it, or EOF_CP. Input stream
   errors are reported the first time a character is looked at. */
static uint32_t peek(tc_tokenizer *t, size_t *n)
{
    uint32_t c;

    if (t->pos >= t->len) {
        *n = 0;
        return EOF_CP;
    }
    c = decode_at(t->in + t->pos, t->len - t->pos, n);
    if (t->on_error && t->pos >= t->checked) {
        t->checked = t->pos + *n;
        if (c >= 0xD800 && c <= 0xDFFF)
            err(t, TC_HTML_ERR_SURROGATE_IN_INPUT_STREAM);
        else if (is_noncharacter(c))
            err(t, TC_HTML_ERR_NONCHARACTER_IN_INPUT_STREAM);
        else if (c != 0 && is_control(c) && !is_ws(c) && c != '\r')
            err(t, TC_HTML_ERR_CONTROL_CHARACTER_IN_INPUT_STREAM);
    }
    return c;
}

/* The length of the run of input from pos that needs no special handling:
   it stops at NUL, at the given ASCII bytes (a 0 means none) and, when
   errors are being reported, at anything the error checks must see. */
static size_t plain_run(const tc_tokenizer *t, char s1, char s2, char s3)
{
    size_t i = t->pos;
    int check = t->on_error != NULL;

    while (i < t->len) {
        unsigned char b = (unsigned char)t->in[i];
        if (b == 0 || b == (unsigned char)s1 || b == (unsigned char)s2 || b == (unsigned char)s3)
            break;
        if (check && (b >= 0x7F || (b < 0x20 && b != '\t' && b != '\n' && b != '\f' && b != '\r')))
            break;
        i++;
    }
    return i - t->pos;
}

/* Copy a plain run into b; returns whether there was one. */
static int copy_run(tc_tokenizer *t, tc_buf *b, char s1, char s2, char s3)
{
    size_t n = plain_run(t, s1, s2, s3);

    if (n == 0)
        return 0;
    put_bytes(t, b, t->in + t->pos, n);
    t->pos += n;
    return 1;
}

/* Whether the input at pos starts with word, ignoring ASCII case if asked. */
static int lookahead(const tc_tokenizer *t, const char *word, int nocase)
{
    size_t i, n = strlen(word);

    if (t->len - t->pos < n)
        return 0;
    for (i = 0; i < n; i++) {
        char c = t->in[t->pos + i];
        if (nocase && c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != word[i])
            return 0;
    }
    return 1;
}

/* --- Tokens ----------------------------------------------------------- */

static void emit_char(tc_tokenizer *t, uint32_t c)
{
    put(t, &t->chars, c);
}

static void emit_str(tc_tokenizer *t, const char *s)
{
    put_bytes(t, &t->chars, s, strlen(s));
}

static void new_tag(tc_tokenizer *t, int is_end)
{
    t->is_end_tag = is_end;
    t->self_closing = 0;
    tc_buf_clear(&t->tag_name);
    tc_buf_clear(&t->attr_text);
    t->attr_count = 0;
}

enum { ATTR_NONE, ATTR_IN_NAME, ATTR_IN_VALUE };
static int attr_phase(const tc_tokenizer *t);

static void start_attr(tc_tokenizer *t);
static void finish_attr_name(tc_tokenizer *t);
static void close_attr(tc_tokenizer *t);

/* Where the current attribute is: tracked in the last record's lengths.
   name_len == (size_t)-1 while the name is being built; value_len likewise
   while the value is. */
#define OPEN ((size_t)-1)

static int attr_phase(const tc_tokenizer *t)
{
    const tc_tok_attr_rec *a;

    if (t->attr_count == 0)
        return ATTR_NONE;
    a = &t->attrs[t->attr_count - 1];
    if (a->name_len == OPEN)
        return ATTR_IN_NAME;
    if (a->value_len == OPEN)
        return ATTR_IN_VALUE;
    return ATTR_NONE;
}

static void start_attr(tc_tokenizer *t)
{
    tc_tok_attr_rec *a;

    close_attr(t);
    if (t->attr_count == t->attr_cap) {
        size_t cap = t->attr_cap ? t->attr_cap * 2 : 8;
        tc_tok_attr_rec *grown = realloc(t->attrs, cap * sizeof *grown);
        if (!grown) {
            t->nomem = 1;
            return;
        }
        t->attrs = grown;
        t->attr_cap = cap;
    }
    a = &t->attrs[t->attr_count++];
    a->name_off = t->attr_text.len;
    a->name_len = OPEN;
    a->value_off = 0;
    a->value_len = 0;
    a->duplicate = 0;
}

/* On leaving the attribute name state: drop the attribute if its name is
   already on the tag. */
static void finish_attr_name(tc_tokenizer *t)
{
    tc_tok_attr_rec *a;
    size_t i;

    if (attr_phase(t) != ATTR_IN_NAME)
        return;
    a = &t->attrs[t->attr_count - 1];
    a->name_len = t->attr_text.len - a->name_off;
    put_bytes(t, &t->attr_text, "", 1);
    a->value_off = t->attr_text.len;
    a->value_len = OPEN;
    for (i = 0; i + 1 < t->attr_count; i++) {
        const tc_tok_attr_rec *b = &t->attrs[i];
        if (!b->duplicate && b->name_len == a->name_len && t->attr_text.data &&
            memcmp(t->attr_text.data + b->name_off, t->attr_text.data + a->name_off, a->name_len) == 0) {
            err(t, TC_HTML_ERR_DUPLICATE_ATTRIBUTE);
            a->duplicate = 1;
            break;
        }
    }
}

static void close_attr(tc_tokenizer *t)
{
    tc_tok_attr_rec *a;

    finish_attr_name(t);
    if (attr_phase(t) != ATTR_IN_VALUE)
        return;
    a = &t->attrs[t->attr_count - 1];
    a->value_len = t->attr_text.len - a->value_off;
    put_bytes(t, &t->attr_text, "", 1);
}

static void put_attr_value(tc_tokenizer *t, uint32_t c)
{
    put(t, &t->attr_text, c);
}

static void emit_tag(tc_tokenizer *t)
{
    close_attr(t);
    if (t->is_end_tag) {
        size_t i, real = 0;
        for (i = 0; i < t->attr_count; i++)
            real += !t->attrs[i].duplicate;
        if (real)
            err(t, TC_HTML_ERR_END_TAG_WITH_ATTRIBUTES);
        if (t->self_closing)
            err(t, TC_HTML_ERR_END_TAG_WITH_TRAILING_SOLIDUS);
        t->pending = TC_TOKEN_END_TAG;
    } else {
        tc_buf_clear(&t->last_start_tag);
        put_bytes(t, &t->last_start_tag, tc_buf_cstr(&t->tag_name), t->tag_name.len);
        t->has_last_start_tag = 1;
        t->pending = TC_TOKEN_START_TAG;
    }
}

static int appropriate_end_tag(const tc_tokenizer *t)
{
    return t->has_last_start_tag && t->tag_name.len == t->last_start_tag.len &&
           memcmp(tc_buf_cstr(&t->tag_name), tc_buf_cstr(&t->last_start_tag), t->tag_name.len) == 0;
}

static void new_comment(tc_tokenizer *t)
{
    tc_buf_clear(&t->comment);
}

static void emit_comment(tc_tokenizer *t)
{
    t->pending = TC_TOKEN_COMMENT;
}

static void new_doctype(tc_tokenizer *t)
{
    tc_buf_clear(&t->doctype_name);
    tc_buf_clear(&t->public_id);
    tc_buf_clear(&t->system_id);
    t->has_doctype_name = t->has_public_id = t->has_system_id = 0;
    t->force_quirks = 0;
}

static void emit_doctype(tc_tokenizer *t)
{
    t->pending = TC_TOKEN_DOCTYPE;
}

static void emit_eof(tc_tokenizer *t)
{
    t->pending = TC_TOKEN_EOF;
}

/* "Convert the temporary buffer to a comment": "?" and the buffer. */
static void temp_to_comment(tc_tokenizer *t)
{
    new_comment(t);
    put_bytes(t, &t->comment, "?", 1);
    put_bytes(t, &t->comment, tc_buf_cstr(&t->temp), t->temp.len);
}

static int in_attribute(const tc_tokenizer *t)
{
    return t->return_state == S_ATTR_VALUE_DQ || t->return_state == S_ATTR_VALUE_SQ ||
           t->return_state == S_ATTR_VALUE_UNQ;
}

/* "Flush code points consumed as a character reference". */
static void flush_temp(tc_tokenizer *t)
{
    put_bytes(t, in_attribute(t) ? &t->attr_text : &t->chars, tc_buf_cstr(&t->temp), t->temp.len);
}

/* The longest named character reference at the start of s, or -1. */
static int match_entity(const char *s, size_t avail, size_t *match_len)
{
    size_t lo = 0, hi = tc_entity_count, k;
    int best = -1;

    for (k = 0; k < avail && k < tc_entity_max_len; k++) {
        unsigned char c = (unsigned char)s[k];
        size_t a = lo, b = hi, first;
        if (c == 0)
            break;
        /* Within [lo, hi) all names share s[0..k); find those with s[k] next. */
        while (a < b) {
            size_t m = a + (b - a) / 2;
            if ((unsigned char)tc_entities[m].name[k] < c)
                a = m + 1;
            else
                b = m;
        }
        first = a;
        b = hi;
        while (a < b) {
            size_t m = a + (b - a) / 2;
            if ((unsigned char)tc_entities[m].name[k] <= c)
                a = m + 1;
            else
                b = m;
        }
        if (first == a)
            break;
        lo = first;
        hi = a;
        if (tc_entities[lo].name[k + 1] == '\0') {
            best = (int)lo;
            *match_len = k + 1;
        }
    }
    return best;
}

static const uint16_t c1_replacements[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

/* --- The state machine ------------------------------------------------ */

/* Run states until a token is ready (or the characters before one must be
   delivered first). */
static void run(tc_tokenizer *t)
{
    uint32_t c;
    size_t n;

    while (t->pending < 0 && !t->flush_first && !t->nomem) {
        switch (t->state) {
        case S_DATA:
            if (copy_run(t, &t->chars, '<', '&', 0))
                break;
            c = peek(t, &n);
            if (c == '&') {
                t->pos += n;
                t->return_state = S_DATA;
                t->state = S_CHAR_REF;
            } else if (c == '<') {
                t->pos += n;
                t->state = S_TAG_OPEN;
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                emit_char(t, 0);
            } else if (c == EOF_CP) {
                emit_eof(t);
            } else {
                t->pos += n;
                emit_char(t, c);
            }
            break;

        case S_RCDATA:
        case S_RAWTEXT:
        case S_SCRIPT_DATA:
        case S_PLAINTEXT: {
            int st = t->state;
            if (copy_run(t, &t->chars, st == S_PLAINTEXT ? 0 : '<', st == S_RCDATA ? '&' : 0, 0))
                break;
            c = peek(t, &n);
            if (c == '&' && st == S_RCDATA) {
                t->pos += n;
                t->return_state = S_RCDATA;
                t->state = S_CHAR_REF;
            } else if (c == '<' && st != S_PLAINTEXT) {
                t->pos += n;
                t->state = st == S_RCDATA ? S_RCDATA_LT : st == S_RAWTEXT ? S_RAWTEXT_LT : S_SCRIPT_LT;
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                emit_char(t, 0xFFFD);
            } else if (c == EOF_CP) {
                emit_eof(t);
            } else {
                t->pos += n;
                emit_char(t, c);
            }
            break;
        }

        case S_TAG_OPEN:
            c = peek(t, &n);
            if (c == '!') {
                t->pos += n;
                t->state = S_MARKUP_DECL_OPEN;
            } else if (c == '/') {
                t->pos += n;
                t->state = S_END_TAG_OPEN;
            } else if (is_alpha(c)) {
                new_tag(t, 0);
                t->state = S_TAG_NAME;
            } else if (c == '?') {
                t->pos += n;
                tc_buf_clear(&t->temp);
                t->state = S_PI_OPEN;
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_BEFORE_TAG_NAME);
                emit_char(t, '<');
                emit_eof(t);
            } else {
                err(t, TC_HTML_ERR_INVALID_FIRST_CHARACTER_OF_TAG_NAME);
                emit_char(t, '<');
                t->state = S_DATA;
            }
            break;

        case S_END_TAG_OPEN:
            c = peek(t, &n);
            if (is_alpha(c)) {
                new_tag(t, 1);
                t->state = S_TAG_NAME;
            } else if (c == '>') {
                t->pos += n;
                err(t, TC_HTML_ERR_MISSING_END_TAG_NAME);
                t->state = S_DATA;
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_BEFORE_TAG_NAME);
                emit_str(t, "</");
                emit_eof(t);
            } else {
                err(t, TC_HTML_ERR_INVALID_FIRST_CHARACTER_OF_TAG_NAME);
                new_comment(t);
                t->state = S_BOGUS_COMMENT;
            }
            break;

        case S_TAG_NAME:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
                t->state = S_BEFORE_ATTR_NAME;
            } else if (c == '/') {
                t->pos += n;
                t->state = S_SELF_CLOSING_START_TAG;
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_tag(t);
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                put(t, &t->tag_name, 0xFFFD);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_TAG);
                emit_eof(t);
            } else {
                t->pos += n;
                put(t, &t->tag_name, is_upper(c) ? c + 0x20 : c);
            }
            break;

        case S_RCDATA_LT:
        case S_RAWTEXT_LT:
            c = peek(t, &n);
            if (c == '/') {
                t->pos += n;
                tc_buf_clear(&t->temp);
                t->state = t->state == S_RCDATA_LT ? S_RCDATA_END_TAG_OPEN : S_RAWTEXT_END_TAG_OPEN;
            } else {
                emit_char(t, '<');
                t->state = t->state == S_RCDATA_LT ? S_RCDATA : S_RAWTEXT;
            }
            break;

        case S_RCDATA_END_TAG_OPEN:
        case S_RAWTEXT_END_TAG_OPEN:
        case S_SCRIPT_END_TAG_OPEN:
        case S_SCRIPT_ESCAPED_END_TAG_OPEN: {
            int st = t->state;
            c = peek(t, &n);
            if (is_alpha(c)) {
                new_tag(t, 1);
                t->state = st == S_RCDATA_END_TAG_OPEN    ? S_RCDATA_END_TAG_NAME
                           : st == S_RAWTEXT_END_TAG_OPEN ? S_RAWTEXT_END_TAG_NAME
                           : st == S_SCRIPT_END_TAG_OPEN  ? S_SCRIPT_END_TAG_NAME
                                                          : S_SCRIPT_ESCAPED_END_TAG_NAME;
            } else {
                emit_str(t, "</");
                t->state = st == S_RCDATA_END_TAG_OPEN    ? S_RCDATA
                           : st == S_RAWTEXT_END_TAG_OPEN ? S_RAWTEXT
                           : st == S_SCRIPT_END_TAG_OPEN  ? S_SCRIPT_DATA
                                                          : S_SCRIPT_ESCAPED;
            }
            break;
        }

        case S_RCDATA_END_TAG_NAME:
        case S_RAWTEXT_END_TAG_NAME:
        case S_SCRIPT_END_TAG_NAME:
        case S_SCRIPT_ESCAPED_END_TAG_NAME: {
            int st = t->state;
            c = peek(t, &n);
            if ((is_ws(c) || c == '/' || c == '>') && appropriate_end_tag(t)) {
                t->pos += n;
                if (c == '>') {
                    t->state = S_DATA;
                    emit_tag(t);
                } else {
                    t->state = c == '/' ? S_SELF_CLOSING_START_TAG : S_BEFORE_ATTR_NAME;
                }
            } else if (is_alpha(c)) {
                t->pos += n;
                put(t, &t->tag_name, is_upper(c) ? c + 0x20 : c);
                put(t, &t->temp, c);
            } else {
                emit_str(t, "</");
                put_bytes(t, &t->chars, tc_buf_cstr(&t->temp), t->temp.len);
                t->state = st == S_RCDATA_END_TAG_NAME    ? S_RCDATA
                           : st == S_RAWTEXT_END_TAG_NAME ? S_RAWTEXT
                           : st == S_SCRIPT_END_TAG_NAME  ? S_SCRIPT_DATA
                                                          : S_SCRIPT_ESCAPED;
            }
            break;
        }

        case S_SCRIPT_LT:
            c = peek(t, &n);
            if (c == '/') {
                t->pos += n;
                tc_buf_clear(&t->temp);
                t->state = S_SCRIPT_END_TAG_OPEN;
            } else if (c == '!') {
                t->pos += n;
                t->state = S_SCRIPT_ESCAPE_START;
                emit_str(t, "<!");
            } else {
                emit_char(t, '<');
                t->state = S_SCRIPT_DATA;
            }
            break;

        case S_SCRIPT_ESCAPE_START:
        case S_SCRIPT_ESCAPE_START_DASH:
            c = peek(t, &n);
            if (c == '-') {
                t->pos += n;
                t->state = t->state == S_SCRIPT_ESCAPE_START ? S_SCRIPT_ESCAPE_START_DASH : S_SCRIPT_ESCAPED_DASH_DASH;
                emit_char(t, '-');
            } else {
                t->state = S_SCRIPT_DATA;
            }
            break;

        case S_SCRIPT_ESCAPED:
        case S_SCRIPT_ESCAPED_DASH:
        case S_SCRIPT_ESCAPED_DASH_DASH: {
            int st = t->state;
            if (st == S_SCRIPT_ESCAPED && copy_run(t, &t->chars, '-', '<', 0))
                break;
            c = peek(t, &n);
            if (c == '-') {
                t->pos += n;
                t->state = st == S_SCRIPT_ESCAPED ? S_SCRIPT_ESCAPED_DASH : S_SCRIPT_ESCAPED_DASH_DASH;
                emit_char(t, '-');
            } else if (c == '<') {
                t->pos += n;
                t->state = S_SCRIPT_ESCAPED_LT;
            } else if (c == '>' && st == S_SCRIPT_ESCAPED_DASH_DASH) {
                t->pos += n;
                t->state = S_SCRIPT_DATA;
                emit_char(t, '>');
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                t->state = S_SCRIPT_ESCAPED;
                emit_char(t, 0xFFFD);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_SCRIPT_HTML_COMMENT_LIKE_TEXT);
                emit_eof(t);
            } else {
                t->pos += n;
                t->state = S_SCRIPT_ESCAPED;
                emit_char(t, c);
            }
            break;
        }

        case S_SCRIPT_ESCAPED_LT:
            c = peek(t, &n);
            if (c == '/') {
                t->pos += n;
                tc_buf_clear(&t->temp);
                t->state = S_SCRIPT_ESCAPED_END_TAG_OPEN;
            } else if (is_alpha(c)) {
                tc_buf_clear(&t->temp);
                emit_char(t, '<');
                t->state = S_SCRIPT_DOUBLE_ESCAPE_START;
            } else {
                emit_char(t, '<');
                t->state = S_SCRIPT_ESCAPED;
            }
            break;

        case S_SCRIPT_DOUBLE_ESCAPE_START:
        case S_SCRIPT_DOUBLE_ESCAPE_END: {
            int st = t->state;
            c = peek(t, &n);
            if (is_ws(c) || c == '/' || c == '>') {
                int is_script = t->temp.len == 6 && memcmp(t->temp.data, "script", 6) == 0;
                t->pos += n;
                if (st == S_SCRIPT_DOUBLE_ESCAPE_START)
                    t->state = is_script ? S_SCRIPT_DOUBLE_ESCAPED : S_SCRIPT_ESCAPED;
                else
                    t->state = is_script ? S_SCRIPT_ESCAPED : S_SCRIPT_DOUBLE_ESCAPED;
                emit_char(t, c);
            } else if (is_alpha(c)) {
                t->pos += n;
                put(t, &t->temp, is_upper(c) ? c + 0x20 : c);
                emit_char(t, c);
            } else {
                t->state = st == S_SCRIPT_DOUBLE_ESCAPE_START ? S_SCRIPT_ESCAPED : S_SCRIPT_DOUBLE_ESCAPED;
            }
            break;
        }

        case S_SCRIPT_DOUBLE_ESCAPED:
        case S_SCRIPT_DOUBLE_ESCAPED_DASH:
        case S_SCRIPT_DOUBLE_ESCAPED_DASH_DASH: {
            int st = t->state;
            if (st == S_SCRIPT_DOUBLE_ESCAPED && copy_run(t, &t->chars, '-', '<', 0))
                break;
            c = peek(t, &n);
            if (c == '-') {
                t->pos += n;
                t->state = st == S_SCRIPT_DOUBLE_ESCAPED ? S_SCRIPT_DOUBLE_ESCAPED_DASH
                                                         : S_SCRIPT_DOUBLE_ESCAPED_DASH_DASH;
                emit_char(t, '-');
            } else if (c == '<') {
                t->pos += n;
                t->state = S_SCRIPT_DOUBLE_ESCAPED_LT;
                emit_char(t, '<');
            } else if (c == '>' && st == S_SCRIPT_DOUBLE_ESCAPED_DASH_DASH) {
                t->pos += n;
                t->state = S_SCRIPT_DATA;
                emit_char(t, '>');
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                t->state = S_SCRIPT_DOUBLE_ESCAPED;
                emit_char(t, 0xFFFD);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_SCRIPT_HTML_COMMENT_LIKE_TEXT);
                emit_eof(t);
            } else {
                t->pos += n;
                t->state = S_SCRIPT_DOUBLE_ESCAPED;
                emit_char(t, c);
            }
            break;
        }

        case S_SCRIPT_DOUBLE_ESCAPED_LT:
            c = peek(t, &n);
            if (c == '/') {
                t->pos += n;
                tc_buf_clear(&t->temp);
                t->state = S_SCRIPT_DOUBLE_ESCAPE_END;
                emit_char(t, '/');
            } else {
                t->state = S_SCRIPT_DOUBLE_ESCAPED;
            }
            break;

        case S_BEFORE_ATTR_NAME:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
            } else if (c == '/' || c == '>' || c == EOF_CP) {
                t->state = S_AFTER_ATTR_NAME;
            } else if (c == '=') {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_EQUALS_SIGN_BEFORE_ATTRIBUTE_NAME);
                start_attr(t);
                put(t, &t->attr_text, c);
                t->state = S_ATTR_NAME;
            } else {
                start_attr(t);
                t->state = S_ATTR_NAME;
            }
            break;

        case S_ATTR_NAME:
            c = peek(t, &n);
            if (is_ws(c) || c == '/' || c == '>' || c == EOF_CP) {
                finish_attr_name(t);
                t->state = S_AFTER_ATTR_NAME;
            } else if (c == '=') {
                t->pos += n;
                finish_attr_name(t);
                t->state = S_BEFORE_ATTR_VALUE;
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                put(t, &t->attr_text, 0xFFFD);
            } else {
                t->pos += n;
                if (c == '"' || c == '\'' || c == '<')
                    err(t, TC_HTML_ERR_UNEXPECTED_CHARACTER_IN_ATTRIBUTE_NAME);
                put(t, &t->attr_text, is_upper(c) ? c + 0x20 : c);
            }
            break;

        case S_AFTER_ATTR_NAME:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
            } else if (c == '/') {
                t->pos += n;
                t->state = S_SELF_CLOSING_START_TAG;
            } else if (c == '=') {
                t->pos += n;
                t->state = S_BEFORE_ATTR_VALUE;
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_tag(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_TAG);
                emit_eof(t);
            } else {
                start_attr(t);
                t->state = S_ATTR_NAME;
            }
            break;

        case S_BEFORE_ATTR_VALUE:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
            } else if (c == '"') {
                t->pos += n;
                t->state = S_ATTR_VALUE_DQ;
            } else if (c == '\'') {
                t->pos += n;
                t->state = S_ATTR_VALUE_SQ;
            } else if (c == '>') {
                t->pos += n;
                err(t, TC_HTML_ERR_MISSING_ATTRIBUTE_VALUE);
                t->state = S_DATA;
                emit_tag(t);
            } else {
                t->state = S_ATTR_VALUE_UNQ;
            }
            break;

        case S_ATTR_VALUE_DQ:
        case S_ATTR_VALUE_SQ: {
            char quote = t->state == S_ATTR_VALUE_DQ ? '"' : '\'';
            if (copy_run(t, &t->attr_text, quote, '&', 0))
                break;
            c = peek(t, &n);
            if (c == (uint32_t)quote) {
                t->pos += n;
                t->state = S_AFTER_ATTR_VALUE_QUOTED;
            } else if (c == '&') {
                t->pos += n;
                t->return_state = t->state;
                t->state = S_CHAR_REF;
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                put_attr_value(t, 0xFFFD);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_TAG);
                emit_eof(t);
            } else {
                t->pos += n;
                put_attr_value(t, c);
            }
            break;
        }

        case S_ATTR_VALUE_UNQ:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
                t->state = S_BEFORE_ATTR_NAME;
            } else if (c == '&') {
                t->pos += n;
                t->return_state = S_ATTR_VALUE_UNQ;
                t->state = S_CHAR_REF;
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_tag(t);
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                put_attr_value(t, 0xFFFD);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_TAG);
                emit_eof(t);
            } else {
                t->pos += n;
                if (c == '"' || c == '\'' || c == '<' || c == '=' || c == '`')
                    err(t, TC_HTML_ERR_UNEXPECTED_CHARACTER_IN_UNQUOTED_ATTRIBUTE_VALUE);
                put_attr_value(t, c);
            }
            break;

        case S_AFTER_ATTR_VALUE_QUOTED:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
                t->state = S_BEFORE_ATTR_NAME;
            } else if (c == '/') {
                t->pos += n;
                t->state = S_SELF_CLOSING_START_TAG;
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_tag(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_TAG);
                emit_eof(t);
            } else {
                err(t, TC_HTML_ERR_MISSING_WHITESPACE_BETWEEN_ATTRIBUTES);
                t->state = S_BEFORE_ATTR_NAME;
            }
            break;

        case S_SELF_CLOSING_START_TAG:
            c = peek(t, &n);
            if (c == '>') {
                t->pos += n;
                t->self_closing = 1;
                t->state = S_DATA;
                emit_tag(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_TAG);
                emit_eof(t);
            } else {
                err(t, TC_HTML_ERR_UNEXPECTED_SOLIDUS_IN_TAG);
                t->state = S_BEFORE_ATTR_NAME;
            }
            break;

        case S_BOGUS_COMMENT:
            if (copy_run(t, &t->comment, '>', 0, 0))
                break;
            c = peek(t, &n);
            if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_comment(t);
            } else if (c == EOF_CP) {
                emit_comment(t);
                t->state = S_DATA; /* the EOF follows from the data state */
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                put(t, &t->comment, 0xFFFD);
            } else {
                t->pos += n;
                put(t, &t->comment, c);
            }
            break;

        case S_MARKUP_DECL_OPEN:
            if (lookahead(t, "--", 0)) {
                t->pos += 2;
                new_comment(t);
                t->state = S_COMMENT_START;
            } else if (lookahead(t, "doctype", 1)) {
                t->pos += 7;
                t->state = S_DOCTYPE;
            } else if (lookahead(t, "[CDATA[", 0)) {
                /* The answer depends on the tree, which must have seen the
                   characters before this. */
                if (t->chars.len && !t->chars_delivered) {
                    t->flush_first = 1;
                    break;
                }
                t->pos += 7;
                if (t->in_foreign_content && t->in_foreign_content(t->ctx)) {
                    t->state = S_CDATA_SECTION;
                } else {
                    err(t, TC_HTML_ERR_CDATA_IN_HTML_CONTENT);
                    new_comment(t);
                    put_bytes(t, &t->comment, "[CDATA[", 7);
                    t->state = S_BOGUS_COMMENT;
                }
            } else {
                err(t, TC_HTML_ERR_INCORRECTLY_OPENED_COMMENT);
                new_comment(t);
                t->state = S_BOGUS_COMMENT;
            }
            break;

        case S_COMMENT_START:
            c = peek(t, &n);
            if (c == '-') {
                t->pos += n;
                t->state = S_COMMENT_START_DASH;
            } else if (c == '>') {
                t->pos += n;
                err(t, TC_HTML_ERR_ABRUPT_CLOSING_OF_EMPTY_COMMENT);
                t->state = S_DATA;
                emit_comment(t);
            } else {
                t->state = S_COMMENT;
            }
            break;

        case S_COMMENT_START_DASH:
            c = peek(t, &n);
            if (c == '-') {
                t->pos += n;
                t->state = S_COMMENT_END;
            } else if (c == '>') {
                t->pos += n;
                err(t, TC_HTML_ERR_ABRUPT_CLOSING_OF_EMPTY_COMMENT);
                t->state = S_DATA;
                emit_comment(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_COMMENT);
                emit_comment(t);
                t->state = S_DATA;
            } else {
                put_bytes(t, &t->comment, "-", 1);
                t->state = S_COMMENT;
            }
            break;

        case S_COMMENT:
            if (copy_run(t, &t->comment, '<', '-', 0))
                break;
            c = peek(t, &n);
            if (c == '<') {
                t->pos += n;
                put_bytes(t, &t->comment, "<", 1);
                t->state = S_COMMENT_LT;
            } else if (c == '-') {
                t->pos += n;
                t->state = S_COMMENT_END_DASH;
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                put(t, &t->comment, 0xFFFD);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_COMMENT);
                emit_comment(t);
                t->state = S_DATA;
            } else {
                t->pos += n;
                put(t, &t->comment, c);
            }
            break;

        case S_COMMENT_LT:
            c = peek(t, &n);
            if (c == '!') {
                t->pos += n;
                put_bytes(t, &t->comment, "!", 1);
                t->state = S_COMMENT_LT_BANG;
            } else if (c == '<') {
                t->pos += n;
                put_bytes(t, &t->comment, "<", 1);
            } else {
                t->state = S_COMMENT;
            }
            break;

        case S_COMMENT_LT_BANG:
            c = peek(t, &n);
            if (c == '-') {
                t->pos += n;
                t->state = S_COMMENT_LT_BANG_DASH;
            } else {
                t->state = S_COMMENT;
            }
            break;

        case S_COMMENT_LT_BANG_DASH:
            c = peek(t, &n);
            if (c == '-') {
                t->pos += n;
                t->state = S_COMMENT_LT_BANG_DASH_DASH;
            } else {
                t->state = S_COMMENT_END_DASH;
            }
            break;

        case S_COMMENT_LT_BANG_DASH_DASH:
            c = peek(t, &n);
            if (c != '>' && c != EOF_CP)
                err(t, TC_HTML_ERR_NESTED_COMMENT);
            t->state = S_COMMENT_END;
            break;

        case S_COMMENT_END_DASH:
            c = peek(t, &n);
            if (c == '-') {
                t->pos += n;
                t->state = S_COMMENT_END;
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_COMMENT);
                emit_comment(t);
                t->state = S_DATA;
            } else {
                put_bytes(t, &t->comment, "-", 1);
                t->state = S_COMMENT;
            }
            break;

        case S_COMMENT_END:
            c = peek(t, &n);
            if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_comment(t);
            } else if (c == '!') {
                t->pos += n;
                t->state = S_COMMENT_END_BANG;
            } else if (c == '-') {
                t->pos += n;
                put_bytes(t, &t->comment, "-", 1);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_COMMENT);
                emit_comment(t);
                t->state = S_DATA;
            } else {
                put_bytes(t, &t->comment, "--", 2);
                t->state = S_COMMENT;
            }
            break;

        case S_COMMENT_END_BANG:
            c = peek(t, &n);
            if (c == '-') {
                t->pos += n;
                put_bytes(t, &t->comment, "--!", 3);
                t->state = S_COMMENT_END_DASH;
            } else if (c == '>') {
                t->pos += n;
                err(t, TC_HTML_ERR_INCORRECTLY_CLOSED_COMMENT);
                t->state = S_DATA;
                emit_comment(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_COMMENT);
                emit_comment(t);
                t->state = S_DATA;
            } else {
                put_bytes(t, &t->comment, "--!", 3);
                t->state = S_COMMENT;
            }
            break;

        case S_DOCTYPE:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
                t->state = S_BEFORE_DOCTYPE_NAME;
            } else if (c == '>') {
                t->state = S_BEFORE_DOCTYPE_NAME;
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_DOCTYPE);
                new_doctype(t);
                t->force_quirks = 1;
                emit_doctype(t);
                t->state = S_DATA;
            } else {
                err(t, TC_HTML_ERR_MISSING_WHITESPACE_BEFORE_DOCTYPE_NAME);
                t->state = S_BEFORE_DOCTYPE_NAME;
            }
            break;

        case S_BEFORE_DOCTYPE_NAME:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
            } else if (c == '>') {
                t->pos += n;
                err(t, TC_HTML_ERR_MISSING_DOCTYPE_NAME);
                new_doctype(t);
                t->force_quirks = 1;
                t->state = S_DATA;
                emit_doctype(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_DOCTYPE);
                new_doctype(t);
                t->force_quirks = 1;
                emit_doctype(t);
                t->state = S_DATA;
            } else {
                t->pos += n;
                new_doctype(t);
                t->has_doctype_name = 1;
                if (c == 0) {
                    err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                    c = 0xFFFD;
                }
                put(t, &t->doctype_name, is_upper(c) ? c + 0x20 : c);
                t->state = S_DOCTYPE_NAME;
            }
            break;

        case S_DOCTYPE_NAME:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
                t->state = S_AFTER_DOCTYPE_NAME;
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_doctype(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_DOCTYPE);
                t->force_quirks = 1;
                emit_doctype(t);
                t->state = S_DATA;
            } else {
                t->pos += n;
                if (c == 0) {
                    err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                    c = 0xFFFD;
                }
                put(t, &t->doctype_name, is_upper(c) ? c + 0x20 : c);
            }
            break;

        case S_AFTER_DOCTYPE_NAME:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_doctype(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_DOCTYPE);
                t->force_quirks = 1;
                emit_doctype(t);
                t->state = S_DATA;
            } else if (lookahead(t, "public", 1)) {
                t->pos += 6;
                t->state = S_AFTER_DOCTYPE_PUBLIC_KW;
            } else if (lookahead(t, "system", 1)) {
                t->pos += 6;
                t->state = S_AFTER_DOCTYPE_SYSTEM_KW;
            } else {
                err(t, TC_HTML_ERR_INVALID_CHARACTER_SEQUENCE_AFTER_DOCTYPE_NAME);
                t->force_quirks = 1;
                t->state = S_BOGUS_DOCTYPE;
            }
            break;

        case S_AFTER_DOCTYPE_PUBLIC_KW:
        case S_AFTER_DOCTYPE_SYSTEM_KW:
        case S_BEFORE_DOCTYPE_PUBLIC_ID:
        case S_BEFORE_DOCTYPE_SYSTEM_ID: {
            int st = t->state;
            int is_public = st == S_AFTER_DOCTYPE_PUBLIC_KW || st == S_BEFORE_DOCTYPE_PUBLIC_ID;
            int after_kw = st == S_AFTER_DOCTYPE_PUBLIC_KW || st == S_AFTER_DOCTYPE_SYSTEM_KW;
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
                if (after_kw)
                    t->state = is_public ? S_BEFORE_DOCTYPE_PUBLIC_ID : S_BEFORE_DOCTYPE_SYSTEM_ID;
            } else if (c == '"' || c == '\'') {
                t->pos += n;
                if (after_kw)
                    err(t, is_public ? TC_HTML_ERR_MISSING_WHITESPACE_AFTER_DOCTYPE_PUBLIC_KEYWORD
                                     : TC_HTML_ERR_MISSING_WHITESPACE_AFTER_DOCTYPE_SYSTEM_KEYWORD);
                if (is_public) {
                    t->has_public_id = 1;
                    tc_buf_clear(&t->public_id);
                    t->state = c == '"' ? S_DOCTYPE_PUBLIC_ID_DQ : S_DOCTYPE_PUBLIC_ID_SQ;
                } else {
                    t->has_system_id = 1;
                    tc_buf_clear(&t->system_id);
                    t->state = c == '"' ? S_DOCTYPE_SYSTEM_ID_DQ : S_DOCTYPE_SYSTEM_ID_SQ;
                }
            } else if (c == '>') {
                t->pos += n;
                err(t, is_public ? TC_HTML_ERR_MISSING_DOCTYPE_PUBLIC_IDENTIFIER
                                 : TC_HTML_ERR_MISSING_DOCTYPE_SYSTEM_IDENTIFIER);
                t->force_quirks = 1;
                t->state = S_DATA;
                emit_doctype(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_DOCTYPE);
                t->force_quirks = 1;
                emit_doctype(t);
                t->state = S_DATA;
            } else {
                err(t, is_public ? TC_HTML_ERR_MISSING_QUOTE_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER
                                 : TC_HTML_ERR_MISSING_QUOTE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER);
                t->force_quirks = 1;
                t->state = S_BOGUS_DOCTYPE;
            }
            break;
        }

        case S_DOCTYPE_PUBLIC_ID_DQ:
        case S_DOCTYPE_PUBLIC_ID_SQ:
        case S_DOCTYPE_SYSTEM_ID_DQ:
        case S_DOCTYPE_SYSTEM_ID_SQ: {
            int st = t->state;
            int is_public = st == S_DOCTYPE_PUBLIC_ID_DQ || st == S_DOCTYPE_PUBLIC_ID_SQ;
            uint32_t quote = st == S_DOCTYPE_PUBLIC_ID_DQ || st == S_DOCTYPE_SYSTEM_ID_DQ ? '"' : '\'';
            tc_buf *id = is_public ? &t->public_id : &t->system_id;
            c = peek(t, &n);
            if (c == quote) {
                t->pos += n;
                t->state = is_public ? S_AFTER_DOCTYPE_PUBLIC_ID : S_AFTER_DOCTYPE_SYSTEM_ID;
            } else if (c == 0) {
                t->pos += n;
                err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
                put(t, id, 0xFFFD);
            } else if (c == '>') {
                t->pos += n;
                err(t, is_public ? TC_HTML_ERR_ABRUPT_DOCTYPE_PUBLIC_IDENTIFIER
                                 : TC_HTML_ERR_ABRUPT_DOCTYPE_SYSTEM_IDENTIFIER);
                t->force_quirks = 1;
                t->state = S_DATA;
                emit_doctype(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_DOCTYPE);
                t->force_quirks = 1;
                emit_doctype(t);
                t->state = S_DATA;
            } else {
                t->pos += n;
                put(t, id, c);
            }
            break;
        }

        case S_AFTER_DOCTYPE_PUBLIC_ID:
        case S_BETWEEN_DOCTYPE_IDS: {
            int st = t->state;
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
                t->state = S_BETWEEN_DOCTYPE_IDS;
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_doctype(t);
            } else if (c == '"' || c == '\'') {
                t->pos += n;
                if (st == S_AFTER_DOCTYPE_PUBLIC_ID)
                    err(t, TC_HTML_ERR_MISSING_WHITESPACE_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS);
                t->has_system_id = 1;
                tc_buf_clear(&t->system_id);
                t->state = c == '"' ? S_DOCTYPE_SYSTEM_ID_DQ : S_DOCTYPE_SYSTEM_ID_SQ;
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_DOCTYPE);
                t->force_quirks = 1;
                emit_doctype(t);
                t->state = S_DATA;
            } else {
                err(t, TC_HTML_ERR_MISSING_QUOTE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER);
                t->force_quirks = 1;
                t->state = S_BOGUS_DOCTYPE;
            }
            break;
        }

        case S_AFTER_DOCTYPE_SYSTEM_ID:
            c = peek(t, &n);
            if (is_ws(c)) {
                t->pos += n;
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_doctype(t);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_DOCTYPE);
                t->force_quirks = 1;
                emit_doctype(t);
                t->state = S_DATA;
            } else {
                err(t, TC_HTML_ERR_UNEXPECTED_CHARACTER_AFTER_DOCTYPE_SYSTEM_IDENTIFIER);
                t->state = S_BOGUS_DOCTYPE;
            }
            break;

        case S_BOGUS_DOCTYPE:
            c = peek(t, &n);
            if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                emit_doctype(t);
            } else if (c == EOF_CP) {
                emit_doctype(t);
                t->state = S_DATA;
            } else {
                t->pos += n;
                if (c == 0)
                    err(t, TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER);
            }
            break;

        case S_CDATA_SECTION:
            if (copy_run(t, &t->chars, ']', 0, 0))
                break;
            c = peek(t, &n);
            if (c == ']') {
                t->pos += n;
                t->state = S_CDATA_SECTION_BRACKET;
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_CDATA);
                emit_eof(t);
            } else {
                /* U+0000 included: foreign content deals with it. */
                t->pos += n;
                emit_char(t, c);
            }
            break;

        case S_CDATA_SECTION_BRACKET:
            c = peek(t, &n);
            if (c == ']') {
                t->pos += n;
                t->state = S_CDATA_SECTION_END;
            } else {
                emit_char(t, ']');
                t->state = S_CDATA_SECTION;
            }
            break;

        case S_CDATA_SECTION_END:
            c = peek(t, &n);
            if (c == ']') {
                t->pos += n;
                emit_char(t, ']');
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
            } else {
                emit_str(t, "]]");
                t->state = S_CDATA_SECTION;
            }
            break;

        case S_PI_OPEN:
            c = peek(t, &n);
            if (is_alpha(c) || c == '_') {
                t->state = S_PI_TARGET;
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_PROCESSING_INSTRUCTION);
                emit_eof(t);
            } else {
                err(t, TC_HTML_ERR_INVALID_FIRST_CHARACTER_OF_PROCESSING_INSTRUCTION_TARGET);
                temp_to_comment(t);
                t->state = S_BOGUS_COMMENT;
            }
            break;

        case S_PI_TARGET:
            c = peek(t, &n);
            if (is_ws(c) || c == '?' || c == '>') {
                const char *target = tc_buf_cstr(&t->temp);
                char lower[16];
                size_t i;
                for (i = 0; i < t->temp.len && i < sizeof lower - 1; i++)
                    lower[i] = (char)(is_upper((unsigned char)target[i]) ? target[i] + 32 : target[i]);
                lower[i] = '\0';
                if (t->temp.len < sizeof lower && (strcmp(lower, "xml") == 0 || strcmp(lower, "xml-stylesheet") == 0)) {
                    err(t, TC_HTML_ERR_DISALLOWED_PROCESSING_INSTRUCTION_TARGET);
                    temp_to_comment(t);
                    t->state = S_BOGUS_COMMENT;
                } else {
                    new_comment(t); /* the data */
                    t->state = S_AFTER_PI_TARGET;
                }
            } else if (is_alnum(c) || c == '-' || c == '_') {
                t->pos += n;
                put(t, &t->temp, c);
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_PROCESSING_INSTRUCTION);
                emit_eof(t);
            } else {
                err(t, TC_HTML_ERR_INVALID_PROCESSING_INSTRUCTION_TARGET);
                temp_to_comment(t);
                t->state = S_BOGUS_COMMENT;
            }
            break;

        case S_AFTER_PI_TARGET:
            c = peek(t, &n);
            if (is_ws(c))
                t->pos += n;
            else
                t->state = S_PI_DATA;
            break;

        case S_PI_DATA:
            if (copy_run(t, &t->comment, '?', '>', 0))
                break;
            c = peek(t, &n);
            if (c == '?') {
                t->pos += n;
                t->state = S_PI_QUESTIONABLE;
            } else if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                t->pending = TC_TOKEN_PI;
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_PROCESSING_INSTRUCTION);
                emit_eof(t);
            } else {
                t->pos += n;
                put(t, &t->comment, c);
            }
            break;

        case S_PI_QUESTIONABLE:
            c = peek(t, &n);
            if (c == '>') {
                t->pos += n;
                t->state = S_DATA;
                t->pending = TC_TOKEN_PI;
            } else if (c == EOF_CP) {
                err(t, TC_HTML_ERR_EOF_IN_PROCESSING_INSTRUCTION);
                emit_eof(t);
            } else {
                put_bytes(t, &t->comment, "?", 1);
                t->state = S_PI_DATA;
            }
            break;

        case S_CHAR_REF:
            tc_buf_clear(&t->temp);
            put_bytes(t, &t->temp, "&", 1);
            c = peek(t, &n);
            if (is_alnum(c)) {
                t->state = S_NAMED_CHAR_REF;
            } else if (c == '#') {
                t->pos += n;
                put_bytes(t, &t->temp, "#", 1);
                t->state = S_NUMERIC_CHAR_REF;
            } else {
                flush_temp(t);
                t->state = t->return_state;
            }
            break;

        case S_NAMED_CHAR_REF: {
            size_t mlen = 0;
            int e = match_entity(t->in + t->pos, t->len - t->pos, &mlen);
            if (e >= 0) {
                const tc_entity *ent = &tc_entities[e];
                int semicolon = ent->name[mlen - 1] == ';';
                uint32_t next = t->pos + mlen < t->len ? (unsigned char)t->in[t->pos + mlen] : EOF_CP;
                put_bytes(t, &t->temp, t->in + t->pos, mlen);
                t->pos += mlen;
                if (in_attribute(t) && !semicolon && (next == '=' || is_alnum(next))) {
                    flush_temp(t);
                    t->state = t->return_state;
                    break;
                }
                if (!semicolon)
                    err(t, TC_HTML_ERR_MISSING_SEMICOLON_AFTER_CHARACTER_REFERENCE);
                tc_buf_clear(&t->temp);
                put(t, &t->temp, ent->cp1);
                if (ent->cp2)
                    put(t, &t->temp, ent->cp2);
                flush_temp(t);
                t->state = t->return_state;
            } else {
                flush_temp(t);
                t->state = S_AMBIGUOUS_AMPERSAND;
            }
            break;
        }

        case S_AMBIGUOUS_AMPERSAND:
            c = peek(t, &n);
            if (is_alnum(c)) {
                t->pos += n;
                if (in_attribute(t))
                    put_attr_value(t, c);
                else
                    emit_char(t, c);
            } else {
                if (c == ';')
                    err(t, TC_HTML_ERR_UNKNOWN_NAMED_CHARACTER_REFERENCE);
                t->state = t->return_state;
            }
            break;

        case S_NUMERIC_CHAR_REF:
            t->char_ref_code = 0;
            c = peek(t, &n);
            if (c == 'x' || c == 'X') {
                t->pos += n;
                put(t, &t->temp, c);
                t->state = S_HEX_CHAR_REF_START;
            } else if (is_digit(c)) {
                t->state = S_DEC_CHAR_REF;
            } else {
                err(t, TC_HTML_ERR_ABSENCE_OF_DIGITS_IN_NUMERIC_CHARACTER_REFERENCE);
                flush_temp(t);
                t->state = t->return_state;
            }
            break;

        case S_HEX_CHAR_REF_START:
            c = peek(t, &n);
            if (is_hex(c)) {
                t->state = S_HEX_CHAR_REF;
            } else {
                err(t, TC_HTML_ERR_ABSENCE_OF_DIGITS_IN_NUMERIC_CHARACTER_REFERENCE);
                flush_temp(t);
                t->state = t->return_state;
            }
            break;

        case S_HEX_CHAR_REF:
        case S_DEC_CHAR_REF: {
            int hex = t->state == S_HEX_CHAR_REF;
            c = peek(t, &n);
            if (hex ? is_hex(c) : is_digit(c)) {
                uint32_t v = is_digit(c) ? c - '0' : (c | 0x20) - 'a' + 10;
                t->pos += n;
                /* Anything past 0x10FFFF is an error either way; saturate. */
                if (t->char_ref_code <= 0x10FFFF)
                    t->char_ref_code = t->char_ref_code * (hex ? 16 : 10) + v;
            } else if (c == ';') {
                t->pos += n;
                t->state = S_NUMERIC_CHAR_REF_END;
            } else {
                err(t, TC_HTML_ERR_MISSING_SEMICOLON_AFTER_CHARACTER_REFERENCE);
                t->state = S_NUMERIC_CHAR_REF_END;
            }
            break;
        }

        case S_NUMERIC_CHAR_REF_END: {
            uint32_t code = t->char_ref_code;
            if (code == 0) {
                err(t, TC_HTML_ERR_NULL_CHARACTER_REFERENCE);
                code = 0xFFFD;
            } else if (code > 0x10FFFF) {
                err(t, TC_HTML_ERR_CHARACTER_REFERENCE_OUTSIDE_UNICODE_RANGE);
                code = 0xFFFD;
            } else if (code >= 0xD800 && code <= 0xDFFF) {
                err(t, TC_HTML_ERR_SURROGATE_CHARACTER_REFERENCE);
                code = 0xFFFD;
            } else if (is_noncharacter(code)) {
                err(t, TC_HTML_ERR_NONCHARACTER_CHARACTER_REFERENCE);
            } else if (code == 0x0D || (is_control(code) && !is_ws(code))) {
                err(t, TC_HTML_ERR_CONTROL_CHARACTER_REFERENCE);
                if (code >= 0x80 && code <= 0x9F)
                    code = c1_replacements[code - 0x80];
            }
            tc_buf_clear(&t->temp);
            put(t, &t->temp, code);
            flush_temp(t);
            t->state = t->return_state;
            break;
        }

        default:
            t->state = S_DATA;
            break;
        }
    }
}

/* --- The interface ---------------------------------------------------- */

void tc_tokenizer_init(tc_tokenizer *t, const char *in, size_t len)
{
    memset(t, 0, sizeof *t);
    t->in = in;
    t->len = len;
    t->state = S_DATA;
    t->pending = -1;
    tc_buf_init(&t->chars);
    tc_buf_init(&t->tag_name);
    tc_buf_init(&t->attr_text);
    tc_buf_init(&t->comment);
    tc_buf_init(&t->doctype_name);
    tc_buf_init(&t->public_id);
    tc_buf_init(&t->system_id);
    tc_buf_init(&t->temp);
    tc_buf_init(&t->last_start_tag);
}

void tc_tokenizer_free(tc_tokenizer *t)
{
    tc_buf_free(&t->chars);
    tc_buf_free(&t->tag_name);
    tc_buf_free(&t->attr_text);
    tc_buf_free(&t->comment);
    tc_buf_free(&t->doctype_name);
    tc_buf_free(&t->public_id);
    tc_buf_free(&t->system_id);
    tc_buf_free(&t->temp);
    tc_buf_free(&t->last_start_tag);
    free(t->attrs);
    free(t->out_attrs);
    t->attrs = NULL;
    t->out_attrs = NULL;
}

void tc_tokenizer_set_state(tc_tokenizer *t, int state)
{
    t->state = state;
}

int tc_tokenizer_set_last_start_tag(tc_tokenizer *t, const char *name, size_t len)
{
    tc_buf_clear(&t->last_start_tag);
    t->has_last_start_tag = 1;
    return tc_buf_append(&t->last_start_tag, name, len);
}

static int fill_tag(tc_tokenizer *t, tc_token *tok)
{
    size_t i, n = 0;

    if (t->attr_count > t->out_attr_cap) {
        tc_token_attr *grown = realloc(t->out_attrs, t->attr_count * sizeof *grown);
        if (!grown)
            return TC_ERR_NOMEM;
        t->out_attrs = grown;
        t->out_attr_cap = t->attr_count;
    }
    for (i = 0; i < t->attr_count; i++) {
        const tc_tok_attr_rec *a = &t->attrs[i];
        if (a->duplicate)
            continue;
        t->out_attrs[n].name = t->attr_text.data + a->name_off;
        t->out_attrs[n].name_len = a->name_len;
        t->out_attrs[n].value = t->attr_text.data + a->value_off;
        t->out_attrs[n].value_len = a->value_len;
        n++;
    }
    tok->name = tc_buf_cstr(&t->tag_name);
    tok->name_len = t->tag_name.len;
    tok->attrs = t->out_attrs;
    tok->attr_count = n;
    tok->self_closing = t->self_closing;
    return TC_OK;
}

int tc_tokenizer_next(tc_tokenizer *t, tc_token *tok)
{
    memset(tok, 0, sizeof *tok);
    if (t->chars_delivered) {
        tc_buf_clear(&t->chars);
        t->chars_delivered = 0;
    }
    if (t->eof) {
        tok->type = TC_TOKEN_EOF;
        return TC_OK;
    }
    if (t->pending < 0) {
        t->flush_first = 0;
        run(t);
        if (t->nomem)
            return TC_ERR_NOMEM;
    }
    if (t->chars.len) {
        tok->type = TC_TOKEN_CHARS;
        tok->data = t->chars.data;
        tok->data_len = t->chars.len;
        t->chars_delivered = 1;
        return TC_OK;
    }
    tok->type = (tc_token_type)t->pending;
    t->pending = -1;
    switch (tok->type) {
    case TC_TOKEN_START_TAG:
    case TC_TOKEN_END_TAG:
        return fill_tag(t, tok);
    case TC_TOKEN_COMMENT:
        tok->data = tc_buf_cstr(&t->comment);
        tok->data_len = t->comment.len;
        break;
    case TC_TOKEN_PI:
        tok->name = tc_buf_cstr(&t->temp);
        tok->name_len = t->temp.len;
        tok->data = tc_buf_cstr(&t->comment);
        tok->data_len = t->comment.len;
        break;
    case TC_TOKEN_DOCTYPE:
        tok->name = t->has_doctype_name ? tc_buf_cstr(&t->doctype_name) : NULL;
        tok->name_len = t->doctype_name.len;
        tok->public_id = t->has_public_id ? tc_buf_cstr(&t->public_id) : NULL;
        tok->system_id = t->has_system_id ? tc_buf_cstr(&t->system_id) : NULL;
        tok->force_quirks = t->force_quirks;
        break;
    case TC_TOKEN_EOF:
        t->eof = 1;
        break;
    default:
        break;
    }
    return TC_OK;
}

size_t tc_html_normalize_newlines(char *s, size_t len)
{
    size_t r, w = 0;
    char *cr = memchr(s, '\r', len);

    if (!cr)
        return len;
    w = r = (size_t)(cr - s);
    while (r < len) {
        if (s[r] == '\r') {
            s[w++] = '\n';
            r += (r + 1 < len && s[r + 1] == '\n') ? 2 : 1;
        } else {
            s[w++] = s[r++];
        }
    }
    return w;
}
