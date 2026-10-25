/* A libFuzzer target for the whole HTML pipeline: encoding sniffing,
 * decoding, tokenizing and tree building, on arbitrary bytes, as a page or
 * as a fragment in one of several contexts. Built with AddressSanitizer and
 * UBSan, and it checks that the tree it gets back is well formed. Every
 * input also goes through the Chinese, Japanese and Korean decoders, whose
 * output must be valid UTF-8.
 *
 * The first input byte picks the mode; the rest is the page. */
#include <stdlib.h>
#include <string.h>

#include "tc_encoding.h"
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

/* Strict UTF-8: shortest forms, no surrogates, nothing past U+10FFFF. */
static int valid_utf8(const uint8_t *p, size_t len)
{
    size_t i = 0;
    while (i < len) {
        uint32_t cp, min;
        size_t n, k;
        if (p[i] < 0x80) {
            i++;
            continue;
        }
        if (p[i] >= 0xC2 && p[i] <= 0xDF)
            n = 2, cp = p[i] & 0x1F, min = 0x80;
        else if (p[i] >= 0xE0 && p[i] <= 0xEF)
            n = 3, cp = p[i] & 0x0F, min = 0x800;
        else if (p[i] >= 0xF0 && p[i] <= 0xF4)
            n = 4, cp = p[i] & 0x07, min = 0x10000;
        else
            return 0;
        if (i + n > len)
            return 0;
        for (k = 1; k < n; k++) {
            if ((p[i + k] & 0xC0) != 0x80)
                return 0;
            cp = cp << 6 | (p[i + k] & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return 0;
        i += n;
    }
    return 1;
}

static void check_decoders(const uint8_t *data, size_t size)
{
    static const tc_encoding encodings[] = { TC_ENC_SHIFT_JIS, TC_ENC_EUC_JP, TC_ENC_ISO_2022_JP,
                                             TC_ENC_GB18030,   TC_ENC_BIG5,   TC_ENC_EUC_KR };
    size_t i;
    for (i = 0; i < sizeof encodings / sizeof encodings[0]; i++) {
        tc_buf out;
        tc_buf_init(&out);
        if (tc_decode_no_bom(encodings[i], data, size, &out) == TC_OK && !valid_utf8((const uint8_t *)out.data, out.len))
            abort();
        tc_buf_free(&out);
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
    check_decoders(data, size);
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
