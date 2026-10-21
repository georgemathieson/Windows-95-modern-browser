/* The Document Object Model: the tree of elements, text and comments that
 * the HTML parser builds and that styling, layout and (from Sprint 5)
 * scripts work on.
 *
 * Every node and string belongs to its document's arena and is freed with
 * the document. Text is UTF-8. Nothing here recurses over the tree, so a
 * deeply nested page can't overflow the stack. */
#ifndef TC_DOM_H
#define TC_DOM_H

#include "tc_arena.h"
#include "tc_buf.h"
#include "tc_tags.h"

/* Node types, numbered as in the DOM Standard's nodeType. */
typedef enum tc_node_type {
    TC_NODE_ELEMENT = 1,
    TC_NODE_TEXT = 3,
    TC_NODE_PI = 7,
    TC_NODE_COMMENT = 8,
    TC_NODE_DOCUMENT = 9,
    TC_NODE_DOCTYPE = 10,
    TC_NODE_FRAGMENT = 11
} tc_node_type;

/* Namespaces of elements (HTML, SVG, MathML) and attributes (none, XLink,
   XML, XMLNS). */
typedef enum tc_ns {
    TC_NS_NONE,
    TC_NS_HTML,
    TC_NS_SVG,
    TC_NS_MATHML,
    TC_NS_XLINK,
    TC_NS_XML,
    TC_NS_XMLNS
} tc_ns;

typedef enum tc_quirks_mode {
    TC_NO_QUIRKS,
    TC_LIMITED_QUIRKS,
    TC_QUIRKS
} tc_quirks_mode;

typedef struct tc_document tc_document;
typedef struct tc_node tc_node;

struct tc_node {
    uint8_t type;   /* tc_node_type */
    uint8_t ns;     /* elements: tc_ns */
    uint16_t tag;   /* elements: tc_tag */
    tc_node *parent, *first_child, *last_child, *prev, *next;
    tc_document *doc;
};

typedef struct tc_attr {
    const char *name;    /* local name */
    const char *value;
    size_t value_len;
    uint8_t ns;          /* TC_NS_NONE, or XLINK, XML, XMLNS for foreign attributes */
    const char *prefix;  /* "xlink", "xml", "xmlns" or NULL */
} tc_attr;

/* Element flags. */
#define TC_EL_HTML_INTEGRATION_POINT 1u /* MathML annotation-xml with an HTML encoding */

typedef struct tc_element {
    tc_node node;
    const char *name;    /* local name, e.g. "div", "foreignObject" */
    tc_attr *attrs;
    uint32_t attr_count, attr_cap;
    tc_node *content;    /* template elements: their contents (a fragment) */
    uint32_t flags;
} tc_element;

/* Text and comment nodes. */
typedef struct tc_chardata {
    tc_node node;
    char *data;
    size_t len, cap;
} tc_chardata;

typedef struct tc_pi {
    tc_node node;
    const char *target;
    const char *data;
    size_t len;
} tc_pi;

typedef struct tc_doctype {
    tc_node node;
    const char *name, *public_id, *system_id;
} tc_doctype;

struct tc_document {
    tc_node node;
    tc_arena arena;
    tc_quirks_mode mode;
    int nomem;   /* an allocation failed: the tree is incomplete */
};

tc_document *tc_document_new(void);
void tc_document_free(tc_document *doc);

/* Creating nodes. They return NULL (and set doc->nomem) when out of memory.
   Strings are copied. */
tc_element *tc_dom_create_element(tc_document *doc, tc_ns ns, const char *name, size_t len);
int tc_dom_add_attr(tc_document *doc, tc_element *el, tc_ns ns, const char *prefix, const char *name,
                    size_t name_len, const char *value, size_t value_len);
tc_chardata *tc_dom_create_text(tc_document *doc, const char *data, size_t len);
tc_chardata *tc_dom_create_comment(tc_document *doc, const char *data, size_t len);
tc_pi *tc_dom_create_pi(tc_document *doc, const char *target, size_t target_len, const char *data, size_t len);
tc_doctype *tc_dom_create_doctype(tc_document *doc, const char *name, const char *public_id, const char *system_id);
tc_node *tc_dom_create_fragment(tc_document *doc);

/* Append to a text or comment node's data. */
int tc_dom_append_data(tc_document *doc, tc_chardata *cd, const char *data, size_t len);

/* Tree changes. node is removed from any old parent first. ref NULL means
   append. */
void tc_dom_insert_before(tc_node *parent, tc_node *node, tc_node *ref);
void tc_dom_append(tc_node *parent, tc_node *node);
void tc_dom_remove(tc_node *node);

/* Whether node is an element with this namespace and tag. */
#define tc_dom_is(n, ns_, tag_) ((n) && (n)->type == TC_NODE_ELEMENT && (n)->ns == (ns_) && (n)->tag == (tag_))
#define tc_dom_is_html(n, tag_) tc_dom_is(n, TC_NS_HTML, tag_)

/* An attribute's value (attributes in no namespace only), or NULL. */
const char *tc_dom_get_attr(const tc_element *el, const char *name);

/* The next node in tree order within root (template contents excluded), or
   NULL. Walks the tree without recursion. */
tc_node *tc_dom_next(const tc_node *root, const tc_node *node);

/* Write the children of node (a document, fragment or element) in the
   html5lib-tests tree format: one node per line, "| " and two spaces per
   level. */
int tc_dom_dump(const tc_node *node, tc_buf *out);

/* document.title: the first <title>'s text, whitespace collapsed. */
int tc_document_title(const tc_document *doc, tc_buf *out);

/* How many nodes of each kind, for statistics. */
typedef struct tc_dom_stats {
    size_t elements, texts, comments, others, max_depth;
} tc_dom_stats;
void tc_dom_count(const tc_node *root, tc_dom_stats *stats);

#endif
