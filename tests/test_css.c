/* The CSS parser against css-parsing-tests. Every case must pass unless
 * tests/data/css_known_failures.txt lists it (with a reason); a listed case
 * that passes is also a failure, so the list stays honest. */
#include <stdlib.h>

#include "css_cases.h"
#include "tc_buf.h"
#include "tc_css_syntax.h"
#include "tc_css_color.h"
#include "tc_test.h"

void tc_suite_css(void);

#define MAX_REPORTS 20

/* ---- Canonical JSON, as tools/gen_css_tests.py writes it ---- */

static void j_str(tc_buf *out, const char *s)
{
    tc_buf_append_char(out, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"')
            tc_buf_append_str(out, "\\\"");
        else if (c == '\\')
            tc_buf_append_str(out, "\\\\");
        else if (c < 0x20 || c == 0x7F)
            tc_buf_printf(out, "\\u%04x", c);
        else
            tc_buf_append_char(out, (char)c);
    }
    tc_buf_append_char(out, '"');
}

static void j_num(tc_buf *out, double d)
{
    if (d == 0)
        tc_buf_append_char(out, '0');
    else
        tc_buf_printf(out, "%.17g", d);
}

static void j_values(tc_buf *out, const tc_css_value *v, uint32_t n, int brackets);

static void j_numeric(tc_buf *out, const char *kind, const tc_css_value *v)
{
    tc_buf_append_str(out, "[");
    j_str(out, kind);
    tc_buf_append_char(out, ',');
    j_str(out, v->repr);
    tc_buf_append_char(out, ',');
    j_num(out, v->number);
    tc_buf_append_str(out, (v->flags & TC_CSS_INTEGER) ? ",\"integer\"" : ",\"number\"");
    if (v->type == TC_CSS_DIMENSION) {
        tc_buf_append_char(out, ',');
        j_str(out, v->text);
    }
    tc_buf_append_char(out, ']');
}

/* One value; strings and urls cut short by the end add an error after them. */
static void j_value(tc_buf *out, const tc_css_value *v)
{
    char delim[2];
    switch (v->type) {
    case TC_CSS_IDENT: tc_buf_append_str(out, "[\"ident\","); j_str(out, v->text); tc_buf_append_char(out, ']'); break;
    case TC_CSS_AT_KEYWORD: tc_buf_append_str(out, "[\"at-keyword\","); j_str(out, v->text); tc_buf_append_char(out, ']'); break;
    case TC_CSS_HASH:
        tc_buf_append_str(out, "[\"hash\",");
        j_str(out, v->text);
        tc_buf_append_str(out, (v->flags & TC_CSS_HASH_ID) ? ",\"id\"]" : ",\"unrestricted\"]");
        break;
    case TC_CSS_STRING:
        tc_buf_append_str(out, "[\"string\",");
        j_str(out, v->text);
        tc_buf_append_char(out, ']');
        if (v->flags & TC_CSS_UNCLOSED)
            tc_buf_append_str(out, ",[\"error\",\"eof-in-string\"]");
        break;
    case TC_CSS_BAD_STRING: tc_buf_append_str(out, "[\"error\",\"bad-string\"]"); break;
    case TC_CSS_URL:
        tc_buf_append_str(out, "[\"url\",");
        j_str(out, v->text);
        tc_buf_append_char(out, ']');
        if (v->flags & TC_CSS_UNCLOSED)
            tc_buf_append_str(out, ",[\"error\",\"eof-in-url\"]");
        break;
    case TC_CSS_BAD_URL: tc_buf_append_str(out, "[\"error\",\"bad-url\"]"); break;
    case TC_CSS_DELIM:
        delim[0] = (char)v->cp;
        delim[1] = '\0';
        j_str(out, delim);
        break;
    case TC_CSS_NUMBER: j_numeric(out, "number", v); break;
    case TC_CSS_PERCENTAGE: j_numeric(out, "percentage", v); break;
    case TC_CSS_DIMENSION: j_numeric(out, "dimension", v); break;
    case TC_CSS_WHITESPACE: tc_buf_append_str(out, "\" \""); break;
    case TC_CSS_CDO: tc_buf_append_str(out, "\"<!--\""); break;
    case TC_CSS_CDC: tc_buf_append_str(out, "\"-->\""); break;
    case TC_CSS_COLON: tc_buf_append_str(out, "\":\""); break;
    case TC_CSS_SEMICOLON: tc_buf_append_str(out, "\";\""); break;
    case TC_CSS_COMMA: tc_buf_append_str(out, "\",\""); break;
    case TC_CSS_RBRACKET: tc_buf_append_str(out, "[\"error\",\"]\"]"); break;
    case TC_CSS_RPAREN: tc_buf_append_str(out, "[\"error\",\")\"]"); break;
    case TC_CSS_RBRACE: tc_buf_append_str(out, "[\"error\",\"}\"]"); break;
    case TC_CSS_FUNCTION:
        tc_buf_append_str(out, "[\"function\",");
        j_str(out, v->text);
        if (v->nitems)
            tc_buf_append_char(out, ',');
        j_values(out, v->items, v->nitems, 0);
        tc_buf_append_char(out, ']');
        break;
    case TC_CSS_BLOCK:
        tc_buf_append_str(out, v->cp == '{' ? "[\"{}\"" : v->cp == '[' ? "[\"[]\"" : "[\"()\"");
        if (v->nitems)
            tc_buf_append_char(out, ',');
        j_values(out, v->items, v->nitems, 0);
        tc_buf_append_char(out, ']');
        break;
    default:
        tc_buf_printf(out, "\"?%d\"", v->type);
        break;
    }
}

static void j_values(tc_buf *out, const tc_css_value *v, uint32_t n, int brackets)
{
    uint32_t i;
    if (brackets)
        tc_buf_append_char(out, '[');
    for (i = 0; i < n; i++) {
        if (i)
            tc_buf_append_char(out, ',');
        j_value(out, &v[i]);
    }
    if (brackets)
        tc_buf_append_char(out, ']');
}

static void j_decl(tc_buf *out, const tc_css_decl *d)
{
    tc_buf_append_str(out, "[\"declaration\",");
    j_str(out, d->name);
    tc_buf_append_char(out, ',');
    j_values(out, d->value, d->nvalue, 1);
    tc_buf_append_str(out, d->important ? ",true]" : ",false]");
}

static void j_rule(tc_buf *out, const tc_css_rule *r)
{
    if (r->type == TC_CSS_RULE_ERROR) {
        tc_buf_append_str(out, "[\"error\",\"invalid\"]");
    } else if (r->type == TC_CSS_RULE_AT) {
        tc_buf_append_str(out, "[\"at-rule\",");
        j_str(out, r->name);
        tc_buf_append_char(out, ',');
        j_values(out, r->prelude, r->nprelude, 1);
        tc_buf_append_char(out, ',');
        if (r->block)
            j_values(out, r->block->items, r->block->nitems, 1);
        else
            tc_buf_append_str(out, "null");
        tc_buf_append_char(out, ']');
    } else {
        tc_buf_append_str(out, "[\"qualified rule\",");
        j_values(out, r->prelude, r->nprelude, 1);
        tc_buf_append_char(out, ',');
        j_values(out, r->block->items, r->block->nitems, 1);
        tc_buf_append_char(out, ']');
    }
}

static void j_rules(tc_buf *out, const tc_css_rule *r, uint32_t n)
{
    uint32_t i;
    tc_buf_append_char(out, '[');
    for (i = 0; i < n; i++) {
        if (i)
            tc_buf_append_char(out, ',');
        j_rule(out, &r[i]);
    }
    tc_buf_append_char(out, ']');
}

static void j_error(tc_buf *out, const char *what)
{
    tc_buf_append_str(out, "[\"error\",");
    j_str(out, what);
    tc_buf_append_char(out, ']');
}

/* ---- Running a case ---- */

static void run_case(const css_case *c, tc_buf *out)
{
    tc_arena a;
    tc_css_value *values = NULL;
    uint32_t n = 0;
    const char *k = c->kind;

    tc_arena_init(&a);
    if (strcmp(k, "stylesheet_bytes") == 0) {
        tc_buf text;
        tc_css_rule *rules;
        uint32_t nrules;
        tc_encoding enc;
        tc_buf_init(&text);
        enc = tc_css_decode(c->input, c->input_len, c->protocol, c->environment, &text);
        tc_css_parse_values(&a, tc_buf_cstr(&text), text.len, &values, &n);
        tc_css_parse_rules(&a, values, n, 1, 1, &rules, &nrules);
        tc_buf_append_char(out, '[');
        j_rules(out, rules, nrules);
        {
            /* The tests write encoding names in lower case. */
            char name[32];
            size_t i;
            snprintf(name, sizeof name, "%s", tc_encoding_name(enc));
            for (i = 0; name[i]; i++)
                if (name[i] >= 'A' && name[i] <= 'Z')
                    name[i] = (char)(name[i] + 32);
            tc_buf_append_char(out, ',');
            j_str(out, name);
        }
        tc_buf_append_char(out, ']');
        tc_buf_free(&text);
        tc_arena_free(&a);
        return;
    }
    tc_css_parse_values(&a, c->input, c->input_len, &values, &n);
    if (strcmp(k, "component_value_list") == 0) {
        j_values(out, values, n, 1);
    } else if (strcmp(k, "one_component_value") == 0) {
        const tc_css_value *v;
        const char *error;
        if (tc_css_parse_one_value(values, n, &v, &error) == TC_OK)
            j_value(out, v);
        else
            j_error(out, error);
    } else if (strcmp(k, "declaration_list") == 0 || strcmp(k, "blocks_contents") == 0) {
        tc_css_item *items;
        uint32_t nitems, i;
        tc_css_parse_block(&a, values, n, &items, &nitems);
        tc_buf_append_char(out, '[');
        for (i = 0; i < nitems; i++) {
            if (i)
                tc_buf_append_char(out, ',');
            if (items[i].type == TC_CSS_ITEM_DECL)
                j_decl(out, &items[i].decl);
            else if (items[i].type == TC_CSS_ITEM_RULE)
                j_rule(out, &items[i].rule);
            else
                j_error(out, "invalid");
        }
        tc_buf_append_char(out, ']');
    } else if (strcmp(k, "one_declaration") == 0) {
        tc_css_decl d;
        if (tc_css_parse_one_declaration(&a, values, n, &d) == TC_OK)
            j_decl(out, &d);
        else
            j_error(out, "invalid");
    } else if (strcmp(k, "one_rule") == 0) {
        tc_css_rule r;
        const char *error;
        if (tc_css_parse_one_rule(&a, values, n, &r, &error) == TC_OK)
            j_rule(out, &r);
        else
            j_error(out, error);
    } else if (strcmp(k, "rule_list") == 0 || strcmp(k, "stylesheet") == 0) {
        tc_css_rule *rules;
        uint32_t nrules;
        tc_css_parse_rules(&a, values, n, strcmp(k, "stylesheet") == 0, 1, &rules, &nrules);
        j_rules(out, rules, nrules);
    } else if (strcmp(k, "An+B") == 0) {
        int anb_a, anb_b;
        if (tc_css_parse_anb(values, n, &anb_a, &anb_b) == TC_OK)
            tc_buf_printf(out, "[%d,%d]", anb_a, anb_b);
        else
            tc_buf_append_str(out, "null");
    } else if (strncmp(k, "color_", 6) == 0) {
        tc_css_color color;
        char text[64];
        if (tc_css_parse_color(values, n, &color) == TC_OK) {
            tc_css_color_serialize(&color, text, sizeof text);
            j_str(out, text);
        } else {
            tc_buf_append_str(out, "null");
        }
    } else {
        tc_buf_printf(out, "\"unknown test kind %s\"", k);
    }
    tc_arena_free(&a);
}

static void test_css_parsing_tests(void)
{
    tc_buf out;
    size_t i, passed = 0, known = 0;
    int reported = 0;

    tc_buf_init(&out);
    for (i = 0; i < css_case_count; i++) {
        const css_case *c = &css_cases[i];
        int ok;
        tc_buf_clear(&out);
        run_case(c, &out);
        ok = strcmp(tc_buf_cstr(&out), c->expected) == 0;
        tc_test_checks++;
        if (ok && !c->known_failure) {
            passed++;
        } else if (!ok && c->known_failure) {
            known++;
        } else if (ok) {
            tc_test_failures++;
            printf("FAIL %s passes now: remove it from css_known_failures.txt\n", c->id);
        } else {
            tc_test_failures++;
            if (++reported <= (getenv("TC_ALL_FAILURES") ? 100000 : MAX_REPORTS))
                printf("FAIL %s\n  input:    %.300s\n  expected: %.600s\n  actual:   %.600s\n", c->id, c->input,
                       c->expected, tc_buf_cstr(&out));
        }
    }
    printf("     css-parsing-tests: %u of %u pass, %u known failures\n", (unsigned)passed, (unsigned)css_case_count,
           (unsigned)known);
    tc_buf_free(&out);
}

void tc_suite_css(void)
{
    TC_RUN(test_css_parsing_tests);
}
