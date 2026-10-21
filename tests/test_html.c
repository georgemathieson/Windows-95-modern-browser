/* The HTML parser against html5lib-tests and web-platform-tests. Every case
 * must pass unless tests/data/html_known_failures.txt lists it (with a
 * reason); a listed case that passes is also a failure, so the list stays
 * honest. */
#include <stdlib.h>

#include "html_cases.h"
#include "tc_buf.h"
#include "tc_encoding.h"
#include "tc_html_parser.h"
#include "tc_html_sniff.h"
#include "tc_html_tokenizer.h"
#include "tc_test.h"

void tc_suite_html(void);

/* Print at most this many failures per suite, so one bug can't bury the
   rest of the output. */
#define MAX_REPORTS 20

static void report(int *reported, const char *id, const char *what, const char *expected, const char *actual)
{
    if (++*reported > MAX_REPORTS)
        return;
    printf("FAIL %s: %s\n  expected: %.600s\n  actual:   %.600s\n", id, what, expected, actual);
}

/* --- Tokenizer -------------------------------------------------------- */

static void ser_str(tc_buf *out, const char *s, size_t len)
{
    size_t i;

    if (!s) {
        tc_buf_append_str(out, "null");
        return;
    }
    tc_buf_append_char(out, '"');
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
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

static void ser_token(tc_buf *out, const tc_token *tok)
{
    size_t i;

    if (out->len)
        tc_buf_append_char(out, '\n');
    switch (tok->type) {
    case TC_TOKEN_DOCTYPE:
        tc_buf_append_str(out, "[\"DOCTYPE\", ");
        ser_str(out, tok->name, tok->name ? tok->name_len : 0);
        tc_buf_append_str(out, ", ");
        ser_str(out, tok->public_id, tok->public_id ? strlen(tok->public_id) : 0);
        tc_buf_append_str(out, ", ");
        ser_str(out, tok->system_id, tok->system_id ? strlen(tok->system_id) : 0);
        tc_buf_append_str(out, tok->force_quirks ? ", false]" : ", true]");
        break;
    case TC_TOKEN_START_TAG:
        tc_buf_append_str(out, "[\"StartTag\", ");
        ser_str(out, tok->name, tok->name_len);
        tc_buf_append_str(out, ", {");
        for (i = 0; i < tok->attr_count; i++) {
            if (i)
                tc_buf_append_str(out, ", ");
            ser_str(out, tok->attrs[i].name, tok->attrs[i].name_len);
            tc_buf_append_str(out, ": ");
            ser_str(out, tok->attrs[i].value, tok->attrs[i].value_len);
        }
        tc_buf_append_str(out, tok->self_closing ? "}, true]" : "}]");
        break;
    case TC_TOKEN_END_TAG:
        tc_buf_append_str(out, "[\"EndTag\", ");
        ser_str(out, tok->name, tok->name_len);
        tc_buf_append_char(out, ']');
        break;
    case TC_TOKEN_COMMENT:
        tc_buf_append_str(out, "[\"Comment\", ");
        ser_str(out, tok->data, tok->data_len);
        tc_buf_append_char(out, ']');
        break;
    case TC_TOKEN_PI:
        tc_buf_append_str(out, "[\"ProcessingInstruction\", ");
        ser_str(out, tok->name, tok->name_len);
        tc_buf_append_str(out, ", ");
        ser_str(out, tok->data, tok->data_len);
        tc_buf_append_char(out, ']');
        break;
    default:
        break;
    }
}

typedef struct error_list {
    int codes[256];
    size_t count;
} error_list;

static void collect_error(void *ctx, int code)
{
    error_list *errors = ctx;
    if (errors->count < TC_ARRAY_LEN(errors->codes))
        errors->codes[errors->count++] = code;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(tc_html_error_name(*(const int *)a), tc_html_error_name(*(const int *)b));
}

/* The codes' names, sorted (as the generated cases are), one per line. */
static void error_names(error_list *errors, tc_buf *out)
{
    size_t i;

    qsort(errors->codes, errors->count, sizeof errors->codes[0], by_name);
    for (i = 0; i < errors->count; i++) {
        if (i)
            tc_buf_append_char(out, '\n');
        tc_buf_append_str(out, tc_html_error_name(errors->codes[i]));
    }
}

/* Tokenize one case into out (and its errors, if errors isn't NULL). */
static void tokenize(const tok_case *c, tc_buf *out, error_list *errors)
{
    tc_tokenizer t;
    tc_token tok;
    tc_buf chars;
    char *input = malloc(c->input_len + 1);
    size_t len;

    memcpy(input, c->input, c->input_len);
    len = tc_html_normalize_newlines(input, c->input_len);
    tc_buf_init(&chars);
    tc_tokenizer_init(&t, input, len);
    tc_tokenizer_set_state(&t, c->state);
    if (c->last_start_tag)
        tc_tokenizer_set_last_start_tag(&t, c->last_start_tag, strlen(c->last_start_tag));
    if (errors) {
        t.on_error = collect_error;
        t.ctx = errors;
    }
    for (;;) {
        if (tc_tokenizer_next(&t, &tok) != TC_OK) {
            tc_buf_append_str(out, "<out of memory>");
            break;
        }
        if (tok.type == TC_TOKEN_CHARS) {
            tc_buf_append(&chars, tok.data, tok.data_len);
            continue;
        }
        if (chars.len) {
            tc_token ct;
            memset(&ct, 0, sizeof ct);
            if (out->len)
                tc_buf_append_char(out, '\n');
            tc_buf_append_str(out, "[\"Character\", ");
            ser_str(out, chars.data, chars.len);
            tc_buf_append_char(out, ']');
            tc_buf_clear(&chars);
        }
        if (tok.type == TC_TOKEN_EOF)
            break;
        ser_token(out, &tok);
    }
    tc_tokenizer_free(&t);
    tc_buf_free(&chars);
    free(input);
}

static void test_tokenizer_suite(void)
{
    size_t i, passed = 0, errors_passed = 0, known = 0;
    int reported = 0;

    for (i = 0; i < tok_case_count; i++) {
        const tok_case *c = &tok_cases[i];
        tc_buf out, fast, errors;
        error_list codes;
        int tokens_ok, errors_ok, fast_ok;

        tc_buf_init(&out);
        tc_buf_init(&fast);
        tc_buf_init(&errors);
        codes.count = 0;
        tokenize(c, &out, &codes);
        error_names(&codes, &errors);
        tokenize(c, &fast, NULL); /* without error reporting the fast paths run */
        tokens_ok = out.len == c->expected_len && memcmp(tc_buf_cstr(&out), c->expected, out.len) == 0;
        errors_ok = strcmp(tc_buf_cstr(&errors), c->errors) == 0;
        fast_ok = fast.len == out.len && memcmp(tc_buf_cstr(&fast), tc_buf_cstr(&out), out.len) == 0;
        passed += tokens_ok && fast_ok;
        errors_passed += errors_ok;
        tc_test_checks++;
        if (c->known_failure) {
            known++;
            if (tokens_ok && errors_ok && fast_ok) {
                tc_test_failures++;
                printf("FAIL %s: listed as a known failure (%s) but passes\n", c->id, c->known_failure);
            }
        } else if (!tokens_ok) {
            tc_test_failures++;
            report(&reported, c->id, "tokens", c->expected, tc_buf_cstr(&out));
        } else if (!fast_ok) {
            tc_test_failures++;
            report(&reported, c->id, "tokens without error reporting", tc_buf_cstr(&out), tc_buf_cstr(&fast));
        } else if (!errors_ok) {
            tc_test_failures++;
            report(&reported, c->id, "errors", c->errors, tc_buf_cstr(&errors));
        }
        tc_buf_free(&out);
        tc_buf_free(&fast);
        tc_buf_free(&errors);
    }
    printf("     html5lib tokenizer: %u of %u cases give the right tokens, %u the right errors (%u known failures)\n",
           (unsigned)passed, (unsigned)tok_case_count, (unsigned)errors_passed, (unsigned)known);
}

/* --- Tree construction --------------------------------------------------- */

/* Parse one case and dump the tree (the document, or the fragment). */
static void build_tree(const tree_case *c, tc_buf *out)
{
    tc_html_options opt;
    tc_document *doc = NULL;
    int err;

    memset(&opt, 0, sizeof opt);
    opt.scripting = c->scripting;
    if (c->context) {
        const char *name = c->context;
        tc_ns ns = TC_NS_HTML;
        tc_element *context;
        tc_node *fragment = NULL;
        if (strncmp(name, "svg ", 4) == 0) {
            ns = TC_NS_SVG;
            name += 4;
        } else if (strncmp(name, "math ", 5) == 0) {
            ns = TC_NS_MATHML;
            name += 5;
        }
        doc = tc_document_new();
        context = tc_dom_create_element(doc, ns, name, strlen(name));
        err = tc_html_parse_fragment(doc, context, c->data, c->data_len, &opt, &fragment);
        if (err == TC_OK)
            tc_dom_dump(fragment, out);
    } else {
        err = tc_html_parse(c->data, c->data_len, &opt, &doc);
        if (err == TC_OK)
            tc_dom_dump(&doc->node, out);
    }
    if (err)
        tc_buf_append_str(out, "<parse failed>");
    tc_document_free(doc);
}

static void test_tree_construction_suite(void)
{
    size_t i, passed = 0, known = 0;
    int reported = 0;

    for (i = 0; i < tree_case_count; i++) {
        const tree_case *c = &tree_cases[i];
        tc_buf out;
        int ok;

        tc_buf_init(&out);
        build_tree(c, &out);
        ok = strcmp(tc_buf_cstr(&out), c->expected) == 0;
        passed += ok;
        tc_test_checks++;
        if (c->known_failure) {
            known++;
            if (ok) {
                tc_test_failures++;
                printf("FAIL %s: listed as a known failure (%s) but passes\n", c->id, c->known_failure);
            }
        } else if (!ok) {
            tc_test_failures++;
            if (++reported <= MAX_REPORTS)
                printf("FAIL %s%s%s: tree\n  input: %.300s\n  expected:\n%s\n  actual:\n%s\n", c->id,
                       c->context ? " in " : "", c->context ? c->context : "", c->data, c->expected,
                       tc_buf_cstr(&out));
        }
        tc_buf_free(&out);
    }
    printf("     web-platform-tests tree construction: %u of %u (%u known failures; %u cases need scripts)\n",
           (unsigned)passed, (unsigned)tree_case_count, (unsigned)known, (unsigned)tree_case_skipped);
}

/* --- Encoding sniffing -------------------------------------------------- */

static void test_encoding_suite(void)
{
    size_t i, passed = 0;
    int reported = 0;

    for (i = 0; i < enc_case_count; i++) {
        const enc_case *c = &enc_cases[i];
        tc_html_page page;
        tc_encoding expected = tc_encoding_from_label(c->encoding, strlen(c->encoding));
        tc_encoding actual;
        int ok;

        /* The whole page: a <meta> past the prescan's 1024 bytes still counts. */
        tc_html_parse_page(c->data, c->data_len, NULL, NULL, &page);
        actual = page.encoding;
        tc_html_page_free(&page);
        ok = actual == expected;

        passed += ok;
        tc_test_checks++;
        if (c->known_failure) {
            if (ok) {
                tc_test_failures++;
                printf("FAIL %s: listed as a known failure (%s) but passes\n", c->id, c->known_failure);
            }
        } else if (!ok) {
            tc_test_failures++;
            report(&reported, c->id, "encoding", c->encoding, tc_encoding_name(actual));
        }
    }
    printf("     html5lib encoding: %u of %u\n", (unsigned)passed, (unsigned)enc_case_count);
}

void tc_suite_html(void)
{
    TC_RUN(test_tokenizer_suite);
    TC_RUN(test_tree_construction_suite);
    TC_RUN(test_encoding_suite);
}
