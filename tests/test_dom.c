#include <stdlib.h>

#include "tc_arena.h"
#include "tc_dom.h"
#include "tc_html_parser.h"
#include "tc_outline.h"
#include "tc_test.h"

void tc_suite_dom(void);

static tc_document *parse(const char *html)
{
    tc_document *doc = NULL;
    tc_html_parse(html, strlen(html), NULL, &doc);
    return doc;
}

static void test_tags_are_sorted(void)
{
    int i;
    for (i = 2; i < TC_TAG_COUNT; i++) {
        if (strcmp(tc_tag_name((tc_tag)(i - 1)), tc_tag_name((tc_tag)i)) >= 0) {
            printf("     tags out of order: %s, %s\n", tc_tag_name((tc_tag)(i - 1)), tc_tag_name((tc_tag)i));
            TC_CHECK(0);
        }
        TC_CHECK_INT(tc_tag_lookup(tc_tag_name((tc_tag)i), strlen(tc_tag_name((tc_tag)i))), i);
    }
    TC_CHECK_INT(tc_tag_lookup("div", 3), TC_TAG_DIV);
    TC_CHECK_INT(tc_tag_lookup("foreignObject", 13), TC_TAG_FOREIGNOBJECT);
    TC_CHECK_INT(tc_tag_lookup("foreignobject", 13), TC_TAG_UNKNOWN);
    TC_CHECK_INT(tc_tag_lookup("di", 2), TC_TAG_UNKNOWN);
    TC_CHECK_INT(tc_tag_lookup("divx", 4), TC_TAG_UNKNOWN);
    TC_CHECK_INT(tc_tag_lookup("", 0), TC_TAG_UNKNOWN);
}

static void test_arena(void)
{
    tc_arena a;
    char *p, *big;
    int i;

    tc_arena_init(&a);
    for (i = 0; i < 10000; i++) {
        p = tc_arena_alloc(&a, (size_t)(i % 100) + 1);
        TC_CHECK(p != NULL && ((uintptr_t)p & 7) == 0);
        if (!p)
            break;
        memset(p, 0xAB, (size_t)(i % 100) + 1);
    }
    big = tc_arena_alloc(&a, 1000000);
    TC_CHECK(big != NULL);
    p = tc_arena_strndup(&a, "hello world", 5);
    TC_CHECK_STR(p, "hello");
    tc_arena_free(&a);
}

static void test_tree_operations(void)
{
    tc_document *doc = tc_document_new();
    tc_element *div = tc_dom_create_element(doc, TC_NS_HTML, "div", 3);
    tc_element *custom = tc_dom_create_element(doc, TC_NS_HTML, "my-widget", 9);
    tc_chardata *a = tc_dom_create_text(doc, "a", 1), *b = tc_dom_create_text(doc, "b", 1);
    tc_buf out;

    TC_CHECK_INT(div->node.tag, TC_TAG_DIV);
    TC_CHECK_INT(custom->node.tag, TC_TAG_UNKNOWN);
    TC_CHECK_STR(custom->name, "my-widget");
    tc_dom_append(&doc->node, &div->node);
    tc_dom_append(&div->node, &b->node);
    tc_dom_insert_before(&div->node, &a->node, &b->node);
    tc_dom_append(&div->node, &custom->node);
    TC_CHECK_INT(tc_dom_add_attr(doc, div, TC_NS_NONE, NULL, "id", 2, "main", 4), TC_OK);
    TC_CHECK_INT(tc_dom_add_attr(doc, div, TC_NS_XLINK, "xlink", "href", 4, "#x", 2), TC_OK);
    TC_CHECK_INT(tc_dom_append_data(doc, a, "aa", 2), TC_OK);
    TC_CHECK_STR(tc_dom_get_attr(div, "id"), "main");
    TC_CHECK(tc_dom_get_attr(div, "href") == NULL); /* namespaced */
    tc_buf_init(&out);
    tc_dom_dump(&doc->node, &out);
    TC_CHECK_STR(tc_buf_cstr(&out), "| <div>\n|   id=\"main\"\n|   xlink href=\"#x\"\n|   \"aaa\"\n|   \"b\"\n|   <my-widget>");
    tc_dom_remove(&b->node);
    TC_CHECK(a->node.next == &custom->node && custom->node.prev == &a->node && b->node.parent == NULL);
    tc_buf_free(&out);
    tc_document_free(doc);
}

static void test_title(void)
{
    tc_document *doc = parse("<title>  The\n  <b>modern</b>   web </title><p>x");
    tc_buf t;

    tc_buf_init(&t);
    tc_document_title(doc, &t);
    TC_CHECK_STR(tc_buf_cstr(&t), "The <b>modern</b> web");
    tc_document_free(doc);
    doc = parse("<p>no title here</p><svg><title>not this</title></svg>");
    tc_buf_clear(&t);
    tc_document_title(doc, &t);
    TC_CHECK_STR(tc_buf_cstr(&t), "");
    tc_document_free(doc);
    tc_buf_free(&t);
}

static void test_quirks_modes(void)
{
    tc_document *doc = parse("<!DOCTYPE html><p>");
    TC_CHECK_INT(doc->mode, TC_NO_QUIRKS);
    tc_document_free(doc);
    doc = parse("<p>no doctype");
    TC_CHECK_INT(doc->mode, TC_QUIRKS);
    tc_document_free(doc);
    doc = parse("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" "
                "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">");
    TC_CHECK_INT(doc->mode, TC_LIMITED_QUIRKS);
    tc_document_free(doc);
}

static void test_deep_nesting_is_capped(void)
{
    char *html = malloc(3 * 5000 + 1);
    tc_document *doc;
    tc_dom_stats stats;
    int i;

    for (i = 0; i < 5000; i++)
        memcpy(html + 3 * i, "<b>", 3);
    html[3 * 5000] = '\0';
    doc = parse(html);
    tc_dom_count(&doc->node, &stats);
    TC_CHECK(stats.max_depth <= TC_HTML_MAX_DEPTH + 2);
    TC_CHECK(stats.elements > 1000);
    tc_document_free(doc);
    free(html);
}

static const char *row_text(const tc_outline *o, size_t row, tc_outline_text *t)
{
    tc_outline_row_text(o, row, t);
    return tc_buf_cstr(&t->text);
}

static void test_outline(void)
{
    tc_document *doc = parse("<!DOCTYPE html><html><head><title>T</title></head>\n<body class=\"x\">\n"
                             "<p id=a>Hello  <b>world</b></p><!-- note --><template><i>t</i></template>");
    tc_outline o;
    tc_outline_text t;
    long body, p, tpl;

    tc_outline_text_init(&t);
    TC_CHECK_INT(tc_outline_init(&o, &doc->node), TC_OK);
    /* doctype, html, head (collapsed), body, p, "Hello", b, "world", comment, template, fragment, i, "t" */
    TC_CHECK_INT((long)o.count, 13);
    TC_CHECK_STR(row_text(&o, 0, &t), "<!DOCTYPE html>");
    TC_CHECK_STR(row_text(&o, 2, &t), "<head>");
    TC_CHECK(o.rows[2].expandable && !o.rows[2].expanded);
    body = tc_outline_find(&o, o.rows[3].node);
    TC_CHECK_STR(row_text(&o, (size_t)body, &t), "<body class=\"x\">");
    TC_CHECK_INT(t.nspans, 7); /* <, body, space, class, =", x, "> */
    p = body + 1;
    TC_CHECK_STR(row_text(&o, (size_t)p, &t), "<p id=\"a\">");
    TC_CHECK_INT(o.rows[p].depth, 2);
    TC_CHECK_STR(row_text(&o, (size_t)p + 1, &t), "\"Hello\"");
    TC_CHECK_STR(row_text(&o, 8, &t), "<!-- note -->");
    tpl = 9;
    TC_CHECK_STR(row_text(&o, (size_t)tpl, &t), "<template>");
    TC_CHECK_STR(row_text(&o, (size_t)tpl + 1, &t), "#document-fragment");
    TC_CHECK_INT(o.rows[tpl + 2].depth, o.rows[tpl].depth + 2);
    TC_CHECK_INT(tc_outline_parent(&o, (size_t)tpl + 2), tpl + 1);
    TC_CHECK_INT(tc_outline_parent(&o, (size_t)p), body);
    TC_CHECK_INT(tc_outline_parent(&o, 0), -1);

    /* Collapsing the body hides its 9 descendant rows; expanding head shows 2. */
    TC_CHECK_INT(tc_outline_set_expanded(&o, (size_t)body, 0), TC_OK);
    TC_CHECK_INT((long)o.count, 4);
    TC_CHECK_INT(tc_outline_set_expanded(&o, 2, 1), TC_OK);
    TC_CHECK_INT((long)o.count, 6);
    TC_CHECK_STR(row_text(&o, 4, &t), "\"T\"");
    tc_outline_free(&o);
    tc_outline_text_free(&t);
    tc_document_free(doc);
}

static void test_outline_cuts_long_rows(void)
{
    char html[2100];
    tc_document *doc;
    tc_outline o;
    tc_outline_text t;
    size_t len;

    memcpy(html, "<p>", 3);
    memset(html + 3, 'x', 2000);
    memcpy(html + 2003, "\xC3\xA9", 3); /* a two-byte character at the end, and NUL */
    doc = parse(html);
    tc_outline_text_init(&t);
    tc_outline_init(&o, &doc->node);
    row_text(&o, o.count - 1, &t);
    len = t.text.len;
    TC_CHECK(len <= TC_OUTLINE_MAX_TEXT + 3);
    TC_CHECK(len >= 3 && memcmp(t.text.data + len - 3, "\xE2\x80\xA6", 3) == 0);
    TC_CHECK_INT(t.span_kind[t.nspans - 1], TC_SPAN_NOTE);
    tc_outline_free(&o);
    tc_outline_text_free(&t);
    tc_document_free(doc);
}

void tc_suite_dom(void)
{
    TC_RUN(test_tags_are_sorted);
    TC_RUN(test_arena);
    TC_RUN(test_tree_operations);
    TC_RUN(test_title);
    TC_RUN(test_quirks_modes);
    TC_RUN(test_deep_nesting_is_capped);
    TC_RUN(test_outline);
    TC_RUN(test_outline_cuts_long_rows);
}
