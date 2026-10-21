#include "tc_outline.h"

#include <stdlib.h>
#include <string.h>

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

static int hidden(const tc_node *n)
{
    const tc_chardata *t = (const tc_chardata *)n;
    size_t i;

    if (n->type != TC_NODE_TEXT)
        return 0;
    for (i = 0; i < t->len; i++)
        if (!is_ws(t->data[i]))
            return 0;
    return 1;
}

static const tc_node *skip_hidden(const tc_node *n)
{
    while (n && hidden(n))
        n = n->next;
    return n;
}

/* The outline's tree: a template's children are its contents (shown as a
   fragment row) followed by any real children. */
static const tc_node *first_child(const tc_node *n)
{
    if (n->type == TC_NODE_ELEMENT && ((const tc_element *)n)->content)
        return ((const tc_element *)n)->content;
    return skip_hidden(n->first_child);
}

static const tc_node *next_sibling(const tc_node *n)
{
    const tc_element *host = tc_dom_fragment_host(n);
    if (host)
        return skip_hidden(host->node.first_child);
    return skip_hidden(n->next);
}

static const tc_node *outline_parent(const tc_node *n)
{
    const tc_element *host = tc_dom_fragment_host(n);
    return host ? &host->node : n->parent;
}

static long collapsed_index(const tc_outline *o, const tc_node *n, int *found)
{
    size_t lo = 0, hi = o->ncollapsed;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((uintptr_t)o->collapsed[mid] < (uintptr_t)n)
            lo = mid + 1;
        else
            hi = mid;
    }
    *found = lo < o->ncollapsed && o->collapsed[lo] == n;
    return (long)lo;
}

static int is_collapsed(const tc_outline *o, const tc_node *n)
{
    int found;
    collapsed_index(o, n, &found);
    return found;
}

static int set_collapsed(tc_outline *o, const tc_node *n, int collapsed)
{
    int found;
    long i = collapsed_index(o, n, &found);

    if (collapsed == found)
        return TC_OK;
    if (!collapsed) {
        memmove(o->collapsed + i, o->collapsed + i + 1, (o->ncollapsed - (size_t)i - 1) * sizeof *o->collapsed);
        o->ncollapsed--;
        return TC_OK;
    }
    if (o->ncollapsed == o->collapsed_cap) {
        size_t cap = o->collapsed_cap ? o->collapsed_cap * 2 : 16;
        const tc_node **grown = realloc((void *)o->collapsed, cap * sizeof *grown);
        if (!grown)
            return TC_ERR_NOMEM;
        o->collapsed = grown;
        o->collapsed_cap = cap;
    }
    memmove(o->collapsed + i + 1, o->collapsed + i, (o->ncollapsed - (size_t)i) * sizeof *o->collapsed);
    o->collapsed[i] = n;
    o->ncollapsed++;
    return TC_OK;
}

static int add_row(tc_outline *o, const tc_node *n, uint32_t depth)
{
    tc_outline_row *row;

    if (o->count == o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 256;
        tc_outline_row *grown = realloc(o->rows, cap * sizeof *grown);
        if (!grown)
            return TC_ERR_NOMEM;
        o->rows = grown;
        o->cap = cap;
    }
    row = &o->rows[o->count++];
    row->node = n;
    row->depth = depth;
    row->expandable = first_child(n) != NULL;
    row->expanded = row->expandable && !is_collapsed(o, n);
    return TC_OK;
}

/* Walk the tree in order, without recursion, adding a row per visible node. */
static int build(tc_outline *o)
{
    const tc_node *n = first_child(o->root);
    uint32_t depth = 0;

    o->count = 0;
    while (n) {
        if (add_row(o, n, depth))
            return TC_ERR_NOMEM;
        if (o->rows[o->count - 1].expanded) {
            n = first_child(n);
            depth++;
            continue;
        }
        for (;;) {
            const tc_node *next = next_sibling(n);
            if (next) {
                n = next;
                break;
            }
            n = outline_parent(n);
            if (!n || n == o->root || depth == 0) {
                n = NULL;
                break;
            }
            depth--;
        }
    }
    return TC_OK;
}

int tc_outline_init(tc_outline *o, const tc_node *root)
{
    const tc_node *n;

    memset(o, 0, sizeof *o);
    o->root = root;
    for (n = tc_dom_next(root, root); n; n = tc_dom_next(root, n))
        if (tc_dom_is_html(n, TC_TAG_HEAD) && set_collapsed(o, n, 1))
            return TC_ERR_NOMEM;
    return build(o);
}

void tc_outline_free(tc_outline *o)
{
    free(o->rows);
    free((void *)o->collapsed);
    memset(o, 0, sizeof *o);
}

int tc_outline_set_expanded(tc_outline *o, size_t row, int expanded)
{
    if (row >= o->count || !o->rows[row].expandable)
        return TC_OK;
    if (set_collapsed(o, o->rows[row].node, !expanded))
        return TC_ERR_NOMEM;
    return build(o);
}

long tc_outline_find(const tc_outline *o, const tc_node *node)
{
    size_t i;
    for (i = 0; i < o->count; i++)
        if (o->rows[i].node == node)
            return (long)i;
    return -1;
}

long tc_outline_parent(const tc_outline *o, size_t row)
{
    size_t i;

    if (row >= o->count || o->rows[row].depth == 0)
        return -1;
    for (i = row; i-- > 0;)
        if (o->rows[i].depth < o->rows[row].depth)
            return (long)i;
    return -1;
}

/* --- Row text ------------------------------------------------------------ */

void tc_outline_text_init(tc_outline_text *t)
{
    memset(t, 0, sizeof *t);
    tc_buf_init(&t->text);
}

void tc_outline_text_free(tc_outline_text *t)
{
    tc_buf_free(&t->text);
}

typedef struct writer {
    tc_outline_text *out;
    int full;
} writer;

static void span(writer *w, int kind)
{
    tc_outline_text *t = w->out;
    if (t->nspans && t->span_kind[t->nspans - 1] == kind)
        return;
    if (t->nspans && t->span_start[t->nspans - 1] == t->text.len) {
        t->span_kind[t->nspans - 1] = (uint8_t)kind;
        return;
    }
    if (t->nspans < TC_OUTLINE_MAX_SPANS) {
        t->span_start[t->nspans] = t->text.len;
        t->span_kind[t->nspans] = (uint8_t)kind;
        t->nspans++;
    }
}

/* Append s with whitespace runs shown as one space, stopping (with an
   ellipsis) when the row is full. */
static void put(writer *w, int kind, const char *s, size_t len)
{
    tc_buf *b = &w->out->text;
    size_t i = 0;
    int last_space = b->len && b->data[b->len - 1] == ' ';

    if (w->full)
        return;
    span(w, kind);
    while (i < len) {
        if (b->len >= TC_OUTLINE_MAX_TEXT) {
            /* Don't leave half a UTF-8 sequence. */
            while (b->len && ((unsigned char)b->data[b->len - 1] & 0xC0) == 0x80)
                b->len--;
            if (b->len && (unsigned char)b->data[b->len - 1] >= 0xC0)
                b->len--;
            b->data[b->len] = '\0';
            span(w, TC_SPAN_NOTE);
            tc_buf_append_str(b, "\xE2\x80\xA6"); /* ... */
            w->full = 1;
            return;
        }
        if (is_ws(s[i])) {
            if (!last_space)
                tc_buf_append_char(b, ' ');
            last_space = 1;
        } else {
            tc_buf_append_char(b, s[i] ? s[i] : '?');
            last_space = 0;
        }
        i++;
    }
}

static void puts_kind(writer *w, int kind, const char *s)
{
    put(w, kind, s, strlen(s));
}

int tc_outline_row_text(const tc_outline *o, size_t row, tc_outline_text *out)
{
    const tc_node *n;
    writer w;

    tc_buf_clear(&out->text);
    out->nspans = 0;
    if (row >= o->count)
        return TC_ERR_INVALID;
    n = o->rows[row].node;
    w.out = out;
    w.full = 0;
    switch (n->type) {
    case TC_NODE_ELEMENT: {
        const tc_element *el = (const tc_element *)n;
        uint32_t i;
        puts_kind(&w, TC_SPAN_PLAIN, "<");
        puts_kind(&w, TC_SPAN_TAG, el->name);
        for (i = 0; i < el->attr_count; i++) {
            const tc_attr *a = &el->attrs[i];
            puts_kind(&w, TC_SPAN_PLAIN, " ");
            if (a->prefix) {
                puts_kind(&w, TC_SPAN_ATTR_NAME, a->prefix);
                puts_kind(&w, TC_SPAN_ATTR_NAME, ":");
            }
            puts_kind(&w, TC_SPAN_ATTR_NAME, a->name);
            puts_kind(&w, TC_SPAN_PLAIN, "=\"");
            put(&w, TC_SPAN_ATTR_VALUE, a->value, a->value_len);
            puts_kind(&w, TC_SPAN_PLAIN, "\"");
        }
        puts_kind(&w, TC_SPAN_PLAIN, ">");
        break;
    }
    case TC_NODE_TEXT: {
        const tc_chardata *t = (const tc_chardata *)n;
        size_t start = 0, end = t->len;
        while (start < end && is_ws(t->data[start]))
            start++;
        while (end > start && is_ws(t->data[end - 1]))
            end--;
        puts_kind(&w, TC_SPAN_TEXT, "\"");
        put(&w, TC_SPAN_TEXT, t->data + start, end - start);
        puts_kind(&w, TC_SPAN_TEXT, "\"");
        break;
    }
    case TC_NODE_COMMENT: {
        const tc_chardata *c = (const tc_chardata *)n;
        puts_kind(&w, TC_SPAN_COMMENT, "<!--");
        put(&w, TC_SPAN_COMMENT, c->data, c->len);
        puts_kind(&w, TC_SPAN_COMMENT, "-->");
        break;
    }
    case TC_NODE_DOCTYPE: {
        const tc_doctype *dt = (const tc_doctype *)n;
        puts_kind(&w, TC_SPAN_DOCTYPE, "<!DOCTYPE ");
        puts_kind(&w, TC_SPAN_DOCTYPE, dt->name);
        if (*dt->public_id) {
            puts_kind(&w, TC_SPAN_DOCTYPE, " PUBLIC \"");
            puts_kind(&w, TC_SPAN_DOCTYPE, dt->public_id);
            puts_kind(&w, TC_SPAN_DOCTYPE, "\"");
        }
        if (*dt->system_id) {
            puts_kind(&w, TC_SPAN_DOCTYPE, *dt->public_id ? " \"" : " SYSTEM \"");
            puts_kind(&w, TC_SPAN_DOCTYPE, dt->system_id);
            puts_kind(&w, TC_SPAN_DOCTYPE, "\"");
        }
        puts_kind(&w, TC_SPAN_DOCTYPE, ">");
        break;
    }
    case TC_NODE_PI: {
        const tc_pi *pi = (const tc_pi *)n;
        puts_kind(&w, TC_SPAN_DOCTYPE, "<?");
        puts_kind(&w, TC_SPAN_DOCTYPE, pi->target);
        puts_kind(&w, TC_SPAN_DOCTYPE, " ");
        put(&w, TC_SPAN_DOCTYPE, pi->data, pi->len);
        puts_kind(&w, TC_SPAN_DOCTYPE, "?>");
        break;
    }
    case TC_NODE_FRAGMENT:
        puts_kind(&w, TC_SPAN_NOTE, "#document-fragment");
        break;
    default:
        break;
    }
    return out->text.data ? TC_OK : TC_ERR_NOMEM;
}
