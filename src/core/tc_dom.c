#include "tc_dom.h"

#include <stdlib.h>
#include <string.h>

/* A DocumentFragment. Template contents remember their template (the DOM
   Standard's "host"), which lets tree walks go from the contents back out. */
typedef struct tc_fragment {
    tc_node node;
    tc_element *host;
} tc_fragment;

tc_document *tc_document_new(void)
{
    tc_document *doc = calloc(1, sizeof *doc);

    if (!doc)
        return NULL;
    tc_arena_init(&doc->arena);
    doc->node.type = TC_NODE_DOCUMENT;
    doc->node.doc = doc;
    return doc;
}

void tc_document_free(tc_document *doc)
{
    if (!doc)
        return;
    tc_arena_free(&doc->arena);
    free(doc);
}

static void *alloc_node(tc_document *doc, size_t size, tc_node_type type)
{
    tc_node *n = tc_arena_alloc(&doc->arena, size);

    if (!n) {
        doc->nomem = 1;
        return NULL;
    }
    memset(n, 0, size);
    n->type = (uint8_t)type;
    n->doc = doc;
    return n;
}

static char *copy(tc_document *doc, const char *s, size_t len)
{
    char *p = tc_arena_strndup(&doc->arena, s ? s : "", s ? len : 0);

    if (!p)
        doc->nomem = 1;
    return p;
}

tc_node *tc_dom_create_fragment(tc_document *doc)
{
    tc_fragment *f = alloc_node(doc, sizeof *f, TC_NODE_FRAGMENT);
    return f ? &f->node : NULL;
}

tc_element *tc_dom_create_element(tc_document *doc, tc_ns ns, const char *name, size_t len)
{
    tc_element *el = alloc_node(doc, sizeof *el, TC_NODE_ELEMENT);
    tc_tag tag;

    if (!el)
        return NULL;
    tag = tc_tag_lookup(name, len);
    el->node.ns = (uint8_t)ns;
    el->node.tag = (uint16_t)tag;
    el->name = tag != TC_TAG_UNKNOWN ? tc_tag_name(tag) : copy(doc, name, len);
    if (!el->name)
        return NULL;
    if (ns == TC_NS_HTML && tag == TC_TAG_TEMPLATE) {
        tc_fragment *content = (tc_fragment *)tc_dom_create_fragment(doc);
        if (!content)
            return NULL;
        content->host = el;
        el->content = &content->node;
    }
    return el;
}

int tc_dom_add_attr(tc_document *doc, tc_element *el, tc_ns ns, const char *prefix, const char *name,
                    size_t name_len, const char *value, size_t value_len)
{
    tc_attr *a;

    if (el->attr_count == el->attr_cap) {
        uint32_t cap = el->attr_cap ? el->attr_cap * 2 : 4;
        tc_attr *grown = tc_arena_alloc(&doc->arena, cap * sizeof *grown);
        if (!grown) {
            doc->nomem = 1;
            return TC_ERR_NOMEM;
        }
        if (el->attr_count)
            memcpy(grown, el->attrs, el->attr_count * sizeof *grown);
        el->attrs = grown;
        el->attr_cap = cap;
    }
    a = &el->attrs[el->attr_count];
    a->name = copy(doc, name, name_len);
    a->value = copy(doc, value, value_len);
    if (!a->name || !a->value)
        return TC_ERR_NOMEM;
    a->value_len = value_len;
    a->ns = (uint8_t)ns;
    a->prefix = prefix;
    el->attr_count++;
    return TC_OK;
}

static tc_chardata *create_chardata(tc_document *doc, tc_node_type type, const char *data, size_t len)
{
    tc_chardata *cd = alloc_node(doc, sizeof *cd, type);

    if (!cd)
        return NULL;
    cd->data = copy(doc, data, len);
    if (!cd->data)
        return NULL;
    cd->len = cd->cap = len;
    return cd;
}

tc_chardata *tc_dom_create_text(tc_document *doc, const char *data, size_t len)
{
    return create_chardata(doc, TC_NODE_TEXT, data, len);
}

tc_chardata *tc_dom_create_comment(tc_document *doc, const char *data, size_t len)
{
    return create_chardata(doc, TC_NODE_COMMENT, data, len);
}

int tc_dom_append_data(tc_document *doc, tc_chardata *cd, const char *data, size_t len)
{
    if (len > cd->cap - cd->len) {
        /* Grow geometrically; the old copy stays in the arena until the
           document goes, which costs at most as much again. */
        size_t cap = cd->len + len > cd->cap * 2 ? cd->len + len : cd->cap * 2;
        char *grown = tc_arena_alloc(&doc->arena, cap + 1);
        if (!grown) {
            doc->nomem = 1;
            return TC_ERR_NOMEM;
        }
        memcpy(grown, cd->data, cd->len);
        cd->data = grown;
        cd->cap = cap;
    }
    memcpy(cd->data + cd->len, data, len);
    cd->len += len;
    cd->data[cd->len] = '\0';
    return TC_OK;
}

tc_pi *tc_dom_create_pi(tc_document *doc, const char *target, size_t target_len, const char *data, size_t len)
{
    tc_pi *pi = alloc_node(doc, sizeof *pi, TC_NODE_PI);

    if (!pi)
        return NULL;
    pi->target = copy(doc, target, target_len);
    pi->data = copy(doc, data, len);
    pi->len = len;
    return pi->target && pi->data ? pi : NULL;
}

tc_doctype *tc_dom_create_doctype(tc_document *doc, const char *name, const char *public_id, const char *system_id)
{
    tc_doctype *dt = alloc_node(doc, sizeof *dt, TC_NODE_DOCTYPE);

    if (!dt)
        return NULL;
    dt->name = copy(doc, name, name ? strlen(name) : 0);
    dt->public_id = copy(doc, public_id, public_id ? strlen(public_id) : 0);
    dt->system_id = copy(doc, system_id, system_id ? strlen(system_id) : 0);
    return dt->name && dt->public_id && dt->system_id ? dt : NULL;
}

void tc_dom_remove(tc_node *node)
{
    tc_node *parent = node->parent;

    if (!parent)
        return;
    if (node->prev)
        node->prev->next = node->next;
    else
        parent->first_child = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        parent->last_child = node->prev;
    node->parent = node->prev = node->next = NULL;
}

void tc_dom_insert_before(tc_node *parent, tc_node *node, tc_node *ref)
{
    tc_dom_remove(node);
    if (ref && ref->parent != parent)
        ref = NULL;
    node->parent = parent;
    node->next = ref;
    node->prev = ref ? ref->prev : parent->last_child;
    if (node->prev)
        node->prev->next = node;
    else
        parent->first_child = node;
    if (ref)
        ref->prev = node;
    else
        parent->last_child = node;
}

void tc_dom_append(tc_node *parent, tc_node *node)
{
    tc_dom_insert_before(parent, node, NULL);
}

const char *tc_dom_get_attr(const tc_element *el, const char *name)
{
    uint32_t i;

    for (i = 0; i < el->attr_count; i++)
        if (el->attrs[i].ns == TC_NS_NONE && strcmp(el->attrs[i].name, name) == 0)
            return el->attrs[i].value;
    return NULL;
}

tc_node *tc_dom_next(const tc_node *root, const tc_node *node)
{
    if (node->first_child)
        return node->first_child;
    while (node && node != root) {
        if (node->next)
            return node->next;
        node = node->parent;
    }
    return NULL;
}

/* --- Dumping in the html5lib-tests format -------------------------------- */

static const char *ns_designator(int ns)
{
    switch (ns) {
    case TC_NS_SVG:
        return "svg ";
    case TC_NS_MATHML:
        return "math ";
    case TC_NS_XLINK:
        return "xlink ";
    case TC_NS_XML:
        return "xml ";
    case TC_NS_XMLNS:
        return "xmlns ";
    default:
        return "";
    }
}

/* The i-th byte of an attribute's name string ("xlink href"), 0 past the end. */
static unsigned char name_string_at(const tc_attr *a, size_t i)
{
    const char *d = ns_designator(a->ns);
    size_t dlen = strlen(d);

    if (i < dlen)
        return (unsigned char)d[i];
    return (unsigned char)a->name[i - dlen];
}

/* Sort by name string. (Byte order is code point order, which differs from
   the tests' UTF-16 order only for names beyond U+FFFF.) */
static int compare_attrs(const void *a, const void *b)
{
    const tc_attr *x = *(const tc_attr *const *)a, *y = *(const tc_attr *const *)b;
    size_t i;

    for (i = 0;; i++) {
        unsigned char cx = name_string_at(x, i), cy = name_string_at(y, i);
        if (cx != cy)
            return cx < cy ? -1 : 1;
        if (!cx)
            return 0;
    }
}

static void line_start(tc_buf *out, size_t depth)
{
    size_t i;

    if (out->len)
        tc_buf_append_char(out, '\n');
    tc_buf_append_str(out, "| ");
    for (i = 0; i < depth; i++)
        tc_buf_append_str(out, "  ");
}

static void dump_node(const tc_node *n, size_t depth, tc_buf *out)
{
    line_start(out, depth);
    switch (n->type) {
    case TC_NODE_ELEMENT: {
        const tc_element *el = (const tc_element *)n;
        const tc_attr *stack_attrs[32];
        const tc_attr **sorted = stack_attrs;
        uint32_t i;
        tc_buf_printf(out, "<%s%s>", ns_designator(n->ns), el->name);
        if (el->attr_count > TC_ARRAY_LEN(stack_attrs)) {
            sorted = malloc(el->attr_count * sizeof *sorted);
            if (!sorted)
                break;
        }
        for (i = 0; i < el->attr_count; i++)
            sorted[i] = &el->attrs[i];
        qsort(sorted, el->attr_count, sizeof *sorted, compare_attrs);
        for (i = 0; i < el->attr_count; i++) {
            line_start(out, depth + 1);
            tc_buf_printf(out, "%s%s=\"", ns_designator(sorted[i]->ns), sorted[i]->name);
            tc_buf_append(out, sorted[i]->value, sorted[i]->value_len);
            tc_buf_append_char(out, '"');
        }
        if (sorted != stack_attrs)
            free(sorted);
        if (el->content) {
            line_start(out, depth + 1);
            tc_buf_append_str(out, "content");
        }
        break;
    }
    case TC_NODE_TEXT: {
        const tc_chardata *cd = (const tc_chardata *)n;
        tc_buf_append_char(out, '"');
        tc_buf_append(out, cd->data, cd->len);
        tc_buf_append_char(out, '"');
        break;
    }
    case TC_NODE_COMMENT: {
        const tc_chardata *cd = (const tc_chardata *)n;
        tc_buf_append_str(out, "<!-- ");
        tc_buf_append(out, cd->data, cd->len);
        tc_buf_append_str(out, " -->");
        break;
    }
    case TC_NODE_DOCTYPE: {
        const tc_doctype *dt = (const tc_doctype *)n;
        tc_buf_printf(out, "<!DOCTYPE %s", dt->name);
        if (*dt->public_id || *dt->system_id)
            tc_buf_printf(out, " \"%s\" \"%s\"", dt->public_id, dt->system_id);
        tc_buf_append_char(out, '>');
        break;
    }
    case TC_NODE_PI: {
        const tc_pi *pi = (const tc_pi *)n;
        tc_buf_printf(out, "<?%s ", pi->target);
        tc_buf_append(out, pi->data, pi->len);
        tc_buf_append_str(out, "?>");
        break;
    }
    default:
        break;
    }
}

int tc_dom_dump(const tc_node *root, tc_buf *out)
{
    const tc_node *n = root->first_child;
    size_t depth = 0;

    while (n) {
        const tc_element *el = n->type == TC_NODE_ELEMENT ? (const tc_element *)n : NULL;
        dump_node(n, depth, out);
        /* Down: a template's contents come first (two levels down, under
           "content"), then its children. */
        if (el && el->content && el->content->first_child) {
            n = el->content->first_child;
            depth += 2;
            continue;
        }
        if (n->first_child) {
            n = n->first_child;
            depth++;
            continue;
        }
        /* Along, or up and along. */
        for (;;) {
            const tc_node *parent;
            if (n->next) {
                n = n->next;
                break;
            }
            parent = n->parent;
            if (parent && parent->type == TC_NODE_FRAGMENT && ((const tc_fragment *)parent)->host &&
                parent != root) {
                /* Leaving a template's contents: on to the template's own children. */
                const tc_element *host = ((const tc_fragment *)parent)->host;
                depth -= 2;
                if (host->node.first_child) {
                    n = host->node.first_child;
                    depth++;
                    break;
                }
                n = &host->node;
                continue;
            }
            if (!parent || parent == root) {
                n = NULL;
                break;
            }
            n = parent;
            depth--;
        }
    }
    return out->data || !root->first_child ? TC_OK : TC_ERR_NOMEM;
}

/* --- Queries ------------------------------------------------------------ */

static int is_ascii_ws(char c)
{
    return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

/* Append the text children of el, whitespace stripped and collapsed. */
static int child_text(const tc_node *el, tc_buf *out)
{
    const tc_node *c;
    int pending_space = 0;

    for (c = el->first_child; c; c = c->next) {
        const tc_chardata *t = (const tc_chardata *)c;
        size_t i;
        if (c->type != TC_NODE_TEXT)
            continue;
        for (i = 0; i < t->len; i++) {
            if (is_ascii_ws(t->data[i])) {
                pending_space = 1;
                continue;
            }
            if (pending_space && out->len)
                tc_buf_append_char(out, ' ');
            pending_space = 0;
            if (tc_buf_append_char(out, t->data[i]))
                return TC_ERR_NOMEM;
        }
    }
    return TC_OK;
}

int tc_document_title(const tc_document *doc, tc_buf *out)
{
    const tc_node *root = &doc->node, *n, *html = NULL;

    for (n = root->first_child; n; n = n->next)
        if (n->type == TC_NODE_ELEMENT) {
            html = n;
            break;
        }
    if (tc_dom_is(html, TC_NS_SVG, TC_TAG_SVG)) {
        for (n = html->first_child; n; n = n->next)
            if (tc_dom_is(n, TC_NS_SVG, TC_TAG_TITLE))
                return child_text(n, out);
        return TC_OK;
    }
    for (n = tc_dom_next(root, root); n; n = tc_dom_next(root, n))
        if (tc_dom_is_html(n, TC_TAG_TITLE))
            return child_text(n, out);
    return TC_OK;
}

void tc_dom_count(const tc_node *root, tc_dom_stats *stats)
{
    const tc_node *n = root->first_child;
    size_t depth = 1;

    memset(stats, 0, sizeof *stats);
    while (n) {
        if (n->type == TC_NODE_ELEMENT)
            stats->elements++;
        else if (n->type == TC_NODE_TEXT)
            stats->texts++;
        else if (n->type == TC_NODE_COMMENT)
            stats->comments++;
        else
            stats->others++;
        if (depth > stats->max_depth)
            stats->max_depth = depth;
        if (n->first_child) {
            n = n->first_child;
            depth++;
            continue;
        }
        while (n && n != root && !n->next) {
            n = n->parent;
            depth--;
        }
        n = n && n != root ? n->next : NULL;
    }
}
