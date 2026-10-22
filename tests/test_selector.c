#include "tc_arena.h"
#include "tc_buf.h"
#include "tc_css_syntax.h"
#include "tc_dom.h"
#include "tc_html_parser.h"
#include "tc_selector.h"
#include "tc_test.h"

void tc_suite_selector(void);

static const char *page =
    "<!DOCTYPE html><html lang=en-GB><body>"
    "<div id=main class='a b'><p class=intro>One</p><p>Two <a href=x>link</a></p>"
    "<ul><li>1<li class=x>2<li>3<li>4</ul><input type=CheckBox checked disabled></div>"
    "<section><h2>Hi</h2></section><svg><foreignObject></foreignObject></svg></body></html>";

static int parse_selector(tc_arena *a, const char *text, tc_selector_list *list)
{
    tc_css_value *v;
    uint32_t n;
    if (tc_css_parse_values(a, text, strlen(text), &v, &n) != TC_OK)
        return TC_ERR_NOMEM;
    return tc_selectors_parse(a, v, n, 0, list);
}

/* The tag names of the elements the selector matches, in document order. */
static const char *select(const tc_document *doc, const char *text, tc_buf *out)
{
    tc_arena a;
    tc_selector_list list;
    tc_match_context ctx = { 0, "main" };
    const tc_node *n;

    tc_buf_clear(out);
    tc_arena_init(&a);
    if (parse_selector(&a, text, &list) != TC_OK) {
        tc_arena_free(&a);
        tc_buf_append_str(out, "invalid");
        return tc_buf_cstr(out);
    }
    for (n = tc_dom_next(&doc->node, &doc->node); n; n = tc_dom_next(&doc->node, n)) {
        uint32_t i;
        if (n->type != TC_NODE_ELEMENT)
            continue;
        for (i = 0; i < list.count; i++)
            if (tc_selector_matches(&list.items[i], (const tc_element *)n, &ctx)) {
                if (out->len)
                    tc_buf_append_char(out, ' ');
                tc_buf_append_str(out, ((const tc_element *)n)->name);
                break;
            }
    }
    tc_arena_free(&a);
    return tc_buf_cstr(out);
}

static void test_matching(void)
{
    static const struct {
        const char *selector, *expected;
    } cases[] = {
        { "p", "p p" },
        { "div > p", "p p" },
        { "div p a", "a" },
        { "#main .intro", "p" },
        { ".a.b", "div" },
        { "DIV.A", "" },                 /* classes are case-sensitive */
        { "DIV", "div" },                /* HTML tag names aren't */
        { "li:nth-child(2n)", "li li" },
        { "li:nth-child(odd)", "li li" },
        { "li:nth-child(-n+2)", "li li" },
        { "li:first-child + li", "li" },
        { "li ~ li", "li li li" },
        { "li:last-child", "li" },
        { "li:nth-last-child(1)", "li" },
        { "li:nth-child(1 of .x)", "li" },
        { "li:nth-child(2 of .x)", "" },
        { "body :not(p, li, a)", "div ul input section h2 svg foreignObject" },
        { "[type=checkbox]", "input" },  /* type is compared case-insensitively */
        { "[type=checkbox s]", "" },
        { "[type=CheckBox s]", "input" },
        { "[class~=b]", "div" },
        { "[lang|=en]", "html" },
        { "[href^=x][href$=x][href*=x]", "a" },
        { "[href^='']", "" },
        { "input:checked:disabled", "input" },
        { ":is(h2, a)", "a h2" },
        { ":where(#main) > ul", "ul" },
        { "div:has(> ul)", "div" },
        { "body:has(> ul)", "" },
        { "section:has(h2)", "section" },
        { "p:lang(en)", "p p" },
        { "p:lang(fr)", "" },
        { "a:link", "a" },
        { "a:any-link", "a" },
        { "a:visited, p:hover", "" },
        { "h2:only-child", "h2" },
        { "p:first-of-type", "p" },
        { "li:nth-of-type(3)", "li" },
        { ":root", "html" },
        { ":heading", "h2" },
        { ":heading(1, 2)", "h2" },
        { ":heading(3)", "" },
        { "div:target", "div" },
        { "foreignObject", "foreignObject" },
        { "foreignobject", "" },         /* SVG names are case-sensitive */
        { "*|p", "p p" },
        { "p:empty", "" },
        { "ul > li:not(.x):nth-child(even)", "li" },
    };
    tc_document *doc = NULL;
    tc_buf out;
    size_t i;

    tc_html_parse(page, strlen(page), NULL, &doc);
    tc_buf_init(&out);
    for (i = 0; i < TC_ARRAY_LEN(cases); i++) {
        const char *got = select(doc, cases[i].selector, &out);
        tc_test_checks++;
        if (strcmp(got, cases[i].expected) != 0) {
            tc_test_failures++;
            printf("FAIL %s: expected \"%s\", got \"%s\"\n", cases[i].selector, cases[i].expected, got);
        }
    }
    tc_buf_free(&out);
    tc_document_free(doc);
}

static void test_invalid(void)
{
    static const char *const cases[] = {
        "p:foo", "::-webkit-scrollbar", "a < b", "p..x", "#1", "[a~b]", "p,", "", ":not()", "a::before b",
        "svg|rect", ":nth-child(x)", "[a='b' x]", ":-moz-focusring", ":heading()", ":heading(1,)",
    };
    tc_arena a;
    tc_selector_list list;
    size_t i;

    tc_arena_init(&a);
    for (i = 0; i < TC_ARRAY_LEN(cases); i++) {
        tc_test_checks++;
        if (parse_selector(&a, cases[i], &list) != TC_ERR_INVALID) {
            tc_test_failures++;
            printf("FAIL \"%s\" should be invalid\n", cases[i]);
        }
    }
    /* :is() forgives what it can't parse. */
    TC_CHECK_INT(parse_selector(&a, ":is(p:foo, li)", &list), TC_OK);
    tc_arena_free(&a);
}

static void test_specificity(void)
{
    static const struct {
        const char *selector;
        uint32_t a, b, c;
    } cases[] = {
        { "*", 0, 0, 0 },
        { "li", 0, 0, 1 },
        { "ul li", 0, 0, 2 },
        { "#a .b c", 1, 1, 1 },
        { "a[href]:hover", 0, 2, 1 },
        { ":where(#a) p", 0, 0, 1 },
        { ":is(#a, p)", 1, 0, 0 },
        { ":not(.x, #y)", 1, 0, 0 },
        { "li:nth-child(2 of .x)", 0, 2, 1 },
        { "p::before", 0, 0, 2 },
    };
    tc_arena a;
    tc_selector_list list;
    size_t i;

    tc_arena_init(&a);
    for (i = 0; i < TC_ARRAY_LEN(cases); i++) {
        uint32_t want = cases[i].a << 20 | cases[i].b << 10 | cases[i].c;
        tc_test_checks++;
        if (parse_selector(&a, cases[i].selector, &list) != TC_OK || list.items[0].specificity != want) {
            tc_test_failures++;
            printf("FAIL specificity of %s\n", cases[i].selector);
        }
    }
    TC_CHECK_INT(parse_selector(&a, "p::before, p:after, li::marker", &list), TC_OK);
    TC_CHECK_INT(list.items[0].pseudo, TC_PSEUDO_BEFORE);
    TC_CHECK_INT(list.items[1].pseudo, TC_PSEUDO_AFTER);
    TC_CHECK_INT(list.items[2].pseudo, TC_PSEUDO_MARKER);
    tc_arena_free(&a);
}

static void test_keys(void)
{
    tc_arena a;
    tc_selector_list list;
    const char *name;

    tc_arena_init(&a);
    TC_CHECK_INT(parse_selector(&a, "div p.intro#x, ul li.y, Section, :hover", &list), TC_OK);
    TC_CHECK_INT(tc_selector_key(&list.items[0], &name), TC_KEY_ID);
    TC_CHECK_STR(name, "x");
    TC_CHECK_INT(tc_selector_key(&list.items[1], &name), TC_KEY_CLASS);
    TC_CHECK_STR(name, "y");
    TC_CHECK_INT(tc_selector_key(&list.items[2], &name), TC_KEY_TAG);
    TC_CHECK_STR(name, "section");
    TC_CHECK_INT(tc_selector_key(&list.items[3], &name), TC_KEY_NONE);
    tc_arena_free(&a);
}

void tc_suite_selector(void)
{
    TC_RUN(test_matching);
    TC_RUN(test_invalid);
    TC_RUN(test_specificity);
    TC_RUN(test_keys);
}
