/* A libFuzzer target for the whole HTML pipeline: encoding sniffing,
 * decoding, tokenizing and tree building, on arbitrary bytes, as a page or
 * as a fragment in one of several contexts. Built with AddressSanitizer and
 * UBSan, and it checks that the tree it gets back is well formed.
 *
 * The first input byte picks the mode; the rest is the page. */
#include <stdlib.h>
#include <string.h>

#include "tc_html_parser.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_tree(const tc_node *root)
{
    const tc_node *n;

    for (n = tc_dom_next(root, root); n; n = tc_dom_next(root, n)) {
        const tc_node *c, *prev = NULL;
        for (c = n->first_child; c; c = c->next) {
            if (c->parent != n || c->prev != prev)
                abort();
            prev = c;
        }
        if (n->last_child != prev)
            abort();
        if (n->type == TC_NODE_ELEMENT && ((const tc_element *)n)->content)
            check_tree(((const tc_element *)n)->content);
    }
}

static const struct {
    tc_ns ns;
    const char *name;
} contexts[] = {
    { TC_NS_HTML, "div" },      { TC_NS_HTML, "td" },        { TC_NS_HTML, "table" },
    { TC_NS_HTML, "template" }, { TC_NS_HTML, "select" },    { TC_NS_HTML, "title" },
    { TC_NS_HTML, "textarea" }, { TC_NS_HTML, "script" },    { TC_NS_HTML, "plaintext" },
    { TC_NS_HTML, "html" },     { TC_NS_HTML, "frameset" },  { TC_NS_HTML, "colgroup" },
    { TC_NS_SVG, "svg" },       { TC_NS_SVG, "foreignObject" }, { TC_NS_MATHML, "math" },
    { TC_NS_MATHML, "annotation-xml" }
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tc_html_options opt;
    tc_buf out;
    unsigned mode;

    if (size < 1)
        return 0;
    mode = data[0];
    data++;
    size--;
    memset(&opt, 0, sizeof opt);
    opt.scripting = mode & 1;
    tc_buf_init(&out);
    if ((mode >> 1) % 4 != 0) {
        tc_html_page page;
        if (tc_html_parse_page(data, size, (mode & 2) ? "utf-16le" : NULL, &opt, &page) == TC_OK) {
            check_tree(&page.doc->node);
            tc_dom_dump(&page.doc->node, &out);
            tc_document_title(page.doc, &out);
        }
        tc_html_page_free(&page);
    } else {
        tc_document *doc = tc_document_new();
        size_t which = (mode >> 3) % (sizeof contexts / sizeof contexts[0]);
        tc_element *context = tc_dom_create_element(doc, contexts[which].ns, contexts[which].name,
                                                    strlen(contexts[which].name));
        tc_node *fragment = NULL;
        if (tc_html_parse_fragment(doc, context, (const char *)data, size, &opt, &fragment) == TC_OK) {
            check_tree(fragment);
            tc_dom_dump(fragment, &out);
        }
        tc_document_free(doc);
    }
    tc_buf_free(&out);
    return 0;
}
