/* The box tree (CSS 2.1 section 9.2, CSS Display 3): boxes for elements,
 * text and generated content, with anonymous block boxes so that a block
 * container holds either only block-level or only inline-level boxes, and
 * inline boxes split around the blocks inside them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_layout_internal.h"
#include "tc_utf8.h"

static tc_box *new_box(tc_layout *l, int type, const tc_style *s, const tc_element *el)
{
    tc_box *b = tc_arena_alloc(&l->arena, sizeof *b);
    if (!b) {
        l->nomem = 1;
        return NULL;
    }
    memset(b, 0, sizeof *b);
    b->type = (uint8_t)type;
    b->style = s;
    b->element = el;
    b->edges = 3;
    b->baseline = -1;
    l->stats.boxes++;
    return b;
}

static void append(tc_box *parent, tc_box *child)
{
    child->parent = parent;
    child->next = NULL;
    if (parent->last)
        parent->last->next = child;
    else
        parent->first = child;
    parent->last = child;
}

const tc_style *tc_layout_anonymous_style(tc_layout *l, const tc_style *parent, int display)
{
    tc_style *s = tc_arena_alloc(&l->arena, sizeof *s);
    if (!s) {
        l->nomem = 1;
        return parent;
    }
    tc_style_anonymous(s, parent, display);
    return s;
}

static int is_block_level(const tc_box *b)
{
    return b->type == TC_BOX_BLOCK || (b->type == TC_BOX_REPLACED && !b->inline_level);
}

static int is_inline_display(int d)
{
    return d == TC_DISPLAY_INLINE || d == TC_DISPLAY_INLINE_BLOCK || d == TC_DISPLAY_INLINE_TABLE ||
           d == TC_DISPLAY_INLINE_FLEX || d == TC_DISPLAY_INLINE_GRID || d == TC_DISPLAY_RUBY ||
           d == TC_DISPLAY_RUBY_TEXT;
}

static tc_box *text_box(tc_layout *l, tc_box *parent, const tc_style *s, const char *text, size_t len)
{
    tc_box *b;
    if (!len)
        return NULL;
    b = new_box(l, TC_BOX_TEXT, s, NULL);
    if (b) {
        b->text = text;
        b->len = len;
        append(parent, b);
    }
    return b;
}

/* ---- List markers ---- */

static size_t alpha_text(int32_t n, int upper, char *out, size_t size)
{
    char buf[16];
    size_t len = 0, i;
    if (n <= 0 || size < 2)
        return 0;
    while (n > 0 && len < sizeof buf) {
        n--;
        buf[len++] = (char)((upper ? 'A' : 'a') + n % 26);
        n /= 26;
    }
    for (i = 0; i < len && i + 1 < size; i++)
        out[i] = buf[len - 1 - i];
    return i;
}

static size_t roman_text(int32_t n, int upper, char *out, size_t size)
{
    static const struct {
        int value;
        const char *text;
    } digits[] = { { 1000, "m" }, { 900, "cm" }, { 500, "d" }, { 400, "cd" }, { 100, "c" }, { 90, "xc" },
                   { 50, "l" },   { 40, "xl" },  { 10, "x" },  { 9, "ix" },   { 5, "v" },   { 4, "iv" },
                   { 1, "i" } };
    size_t len = 0, i, k;
    if (n <= 0 || n >= 4000)
        return 0;
    for (i = 0; i < TC_ARRAY_LEN(digits); i++)
        while (n >= digits[i].value) {
            for (k = 0; digits[i].text[k] && len + 1 < size; k++)
                out[len++] = (char)(upper ? digits[i].text[k] - 32 : digits[i].text[k]);
            n -= digits[i].value;
        }
    return len;
}

size_t tc_marker_text(const tc_style *s, int32_t number, char *out, size_t size)
{
    size_t len = 0;
    if (size < 8)
        return 0;
    switch (s->list_style_type) {
    case TC_LIST_STRING:
        len = strlen(s->list_style_string);
        if (len >= size)
            len = size - 1;
        memcpy(out, s->list_style_string, len);
        out[len] = '\0';
        return len;
    case TC_LIST_DECIMAL:
    case TC_LIST_DECIMAL_LEADING_ZERO:
        len = (size_t)snprintf(out, size - 3,
                               s->list_style_type == TC_LIST_DECIMAL_LEADING_ZERO && number >= 0 && number < 10 ? "0%ld"
                                                                                                                : "%ld",
                               (long)number);
        break;
    case TC_LIST_LOWER_ALPHA:
    case TC_LIST_UPPER_ALPHA:
        len = alpha_text(number, s->list_style_type == TC_LIST_UPPER_ALPHA, out, size - 3);
        break;
    case TC_LIST_LOWER_ROMAN:
    case TC_LIST_UPPER_ROMAN:
        len = roman_text(number, s->list_style_type == TC_LIST_UPPER_ROMAN, out, size - 3);
        break;
    case TC_LIST_LOWER_GREEK:
        if (number >= 1 && number <= 24) {
            /* U+03B1 to U+03C9, skipping the final sigma. */
            uint32_t cp = 0x3B1 + (uint32_t)number - 1;
            if (cp >= 0x3C2)
                cp++;
            len = tc_utf8_encode(cp, out);
        } else {
            len = (size_t)snprintf(out, size - 3, "%ld", (long)number);
        }
        break;
    default:
        out[0] = '\0';
        return 0;
    }
    if (len == 0)
        len = (size_t)snprintf(out, size - 3, "%ld", (long)number);
    out[len++] = '.';
    out[len++] = ' ';
    out[len] = '\0';
    return len;
}

/* The bullets as characters, for inside markers. */
static const char *bullet_text(int type)
{
    switch (type) {
    case TC_LIST_DISC: return "\xE2\x80\xA2 ";              /* U+2022 */
    case TC_LIST_CIRCLE: return "\xE2\x97\xA6 ";            /* U+25E6 */
    case TC_LIST_SQUARE: return "\xE2\x96\xAA ";            /* U+25AA */
    case TC_LIST_DISCLOSURE_OPEN: return "\xE2\x96\xBE ";   /* U+25BE */
    case TC_LIST_DISCLOSURE_CLOSED: return "\xE2\x96\xB8 "; /* U+25B8 */
    default: return NULL;
    }
}

static void add_marker(tc_layout *l, tc_box *li, const tc_element_styles *es)
{
    const tc_style *ms = es->marker ? es->marker : li->style;
    char buf[64];
    const char *text;
    size_t len;
    if (li->style->list_style_type == TC_LIST_NONE && (!es->marker || es->marker->content_kind != TC_GENERATED_ITEMS))
        return;
    if (li->style->list_style_position == TC_LIST_OUTSIDE && (!es->marker || es->marker->content_kind != TC_GENERATED_ITEMS)) {
        li->has_marker = 1;
        return;
    }
    /* Inside: text at the start of the first line. */
    if (es->marker && es->marker->content_kind == TC_GENERATED_ITEMS) {
        uint32_t i;
        tc_box *b = new_box(l, TC_BOX_INLINE, ms, NULL);
        if (!b)
            return;
        append(li, b);
        for (i = 0; i < es->marker->content->count; i++)
            if (es->marker->content->items[i].type == TC_CONTENT_STRING)
                text_box(l, b, ms, es->marker->content->items[i].text, strlen(es->marker->content->items[i].text));
        return;
    }
    text = bullet_text(li->style->list_style_type);
    if (!text) {
        len = tc_marker_text(li->style, li->number, buf, sizeof buf);
        text = tc_arena_strndup(&l->arena, buf, len);
        if (!text)
            return;
    }
    text_box(l, li, ms, text, strlen(text));
}

/* ---- Building ---- */

typedef struct counter {
    int32_t next;
    int step;
} counter;

static int parse_int(const char *s, int32_t *out)
{
    long v = 0;
    int neg = 0;
    if (!s)
        return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9' && v < 100000000)
        v = v * 10 + (*s++ - '0');
    *out = (int32_t)(neg ? -v : v);
    return 1;
}

/* A list's numbering, from an ol's start and reversed attributes. */
static void list_counter(const tc_element *el, counter *c)
{
    int32_t start;
    c->next = 1;
    c->step = 1;
    if (!el || !tc_dom_is_html(&el->node, TC_TAG_OL))
        return;
    if (tc_dom_get_attr(el, "reversed")) {
        const tc_node *n;
        int32_t count = 0;
        for (n = el->node.first_child; n; n = n->next)
            count += tc_dom_is_html(n, TC_TAG_LI);
        c->next = count;
        c->step = -1;
    }
    if (parse_int(tc_dom_get_attr(el, "start"), &start))
        c->next = start;
}

static void build_children(tc_layout *l, tc_box *parent, const tc_element *el, const tc_style *s, int depth,
                           counter *list);

static void generated(tc_layout *l, tc_box *parent, const tc_style *ps)
{
    tc_box *b;
    uint32_t i;
    if (!ps || ps->content_kind != TC_GENERATED_ITEMS)
        return;
    b = new_box(l, is_inline_display(ps->display) ? (ps->display == TC_DISPLAY_INLINE ? TC_BOX_INLINE
                                                                                      : TC_BOX_INLINE_BLOCK)
                                                  : TC_BOX_BLOCK,
                ps, NULL);
    if (!b)
        return;
    append(parent, b);
    for (i = 0; i < ps->content->count; i++) {
        const tc_content_item *it = &ps->content->items[i];
        const char *text = "";
        switch (it->type) {
        case TC_CONTENT_STRING: text = it->text; break;
        case TC_CONTENT_OPEN_QUOTE:
            text = l->quote_depth++ % 2 ? "\xE2\x80\x98" : "\xE2\x80\x9C";
            break;
        case TC_CONTENT_CLOSE_QUOTE:
            if (l->quote_depth > 0)
                l->quote_depth--;
            text = l->quote_depth % 2 ? "\xE2\x80\x99" : "\xE2\x80\x9D";
            break;
        default: break; /* counters: not yet */
        }
        text_box(l, b, ps, text, strlen(text));
    }
}

static const char *input_type(const tc_element *el)
{
    const char *t = tc_dom_get_attr(el, "type");
    return t ? t : "text";
}

/* Replaced elements, and form controls drawn like them. Returns 1 if el is
   one (and b is set up). */
static int setup_replaced(tc_layout *l, tc_box *b, const tc_element *el)
{
    const char *type;
    if (el->node.ns == TC_NS_SVG && el->node.tag == TC_TAG_SVG) {
        b->replaced = TC_REPLACED_IMAGE; /* drawn like a picture (tc_page makes it one) */
        return 1;
    }
    if (el->node.ns != TC_NS_HTML)
        return 0;
    switch (el->node.tag) {
    case TC_TAG_IMG:
        b->replaced = TC_REPLACED_IMAGE;
        b->text = tc_dom_get_attr(el, "alt");
        b->len = b->text ? strlen(b->text) : 0;
        return 1;
    case TC_TAG_IFRAME:
    case TC_TAG_EMBED:
    case TC_TAG_OBJECT:
        b->replaced = TC_REPLACED_FRAME;
        return 1;
    case TC_TAG_VIDEO:
    case TC_TAG_CANVAS:
    case TC_TAG_AUDIO:
        b->replaced = TC_REPLACED_MEDIA;
        return 1;
    case TC_TAG_INPUT:
        type = input_type(el);
        if (tc_css_ascii_ieq(type, "checkbox") || tc_css_ascii_ieq(type, "radio")) {
            b->replaced = tc_css_ascii_ieq(type, "radio") ? TC_REPLACED_RADIO : TC_REPLACED_CHECKBOX;
            b->checked = tc_dom_get_attr(el, "checked") != NULL;
            return 1;
        }
        if (tc_css_ascii_ieq(type, "image")) {
            b->replaced = TC_REPLACED_IMAGE;
            b->text = tc_dom_get_attr(el, "alt");
            b->len = b->text ? strlen(b->text) : 0;
            return 1;
        }
        if (tc_css_ascii_ieq(type, "submit") || tc_css_ascii_ieq(type, "reset") || tc_css_ascii_ieq(type, "button") ||
            tc_css_ascii_ieq(type, "file"))
            return 0; /* buttons: an inline-block with a label */
        b->replaced = TC_REPLACED_TEXT_FIELD;
        b->text = tc_dom_get_attr(el, "value");
        if (!b->text || !*b->text)
            b->text = tc_dom_get_attr(el, "placeholder");
        if (tc_css_ascii_ieq(type, "password") && b->text && tc_dom_get_attr(el, "value"))
            b->text = "********";
        b->len = b->text ? strlen(b->text) : 0;
        TC_UNUSED(l);
        return 1;
    default:
        return 0;
    }
}

static void build_element(tc_layout *l, tc_box *parent, const tc_element *el, int depth, counter *list)
{
    const tc_element_styles *es = tc_styled_element(l->styled, el);
    const tc_style *s;
    tc_box *b;
    int type;

    if (!es || !es->style || l->nomem)
        return;
    s = es->style;
    if (s->display == TC_DISPLAY_NONE || s->display == TC_DISPLAY_TABLE_COLUMN ||
        s->display == TC_DISPLAY_TABLE_COLUMN_GROUP)
        return;
    if (s->display == TC_DISPLAY_CONTENTS || depth >= TC_LAYOUT_MAX_DEPTH) {
        counter c;
        list_counter(el, &c);
        generated(l, parent, es->before);
        build_children(l, parent, el, s, depth, &c);
        generated(l, parent, es->after);
        return;
    }
    if (tc_dom_is_html(&el->node, TC_TAG_BR))
        type = TC_BOX_BREAK;
    else if (s->display == TC_DISPLAY_INLINE || s->display == TC_DISPLAY_RUBY || s->display == TC_DISPLAY_RUBY_TEXT)
        type = TC_BOX_INLINE;
    else if (is_inline_display(s->display))
        type = TC_BOX_INLINE_BLOCK;
    else
        type = TC_BOX_BLOCK;
    b = new_box(l, type, s, el);
    if (!b)
        return;
    if (type != TC_BOX_BREAK && setup_replaced(l, b, el)) {
        b->type = TC_BOX_REPLACED;
        b->inline_level = is_inline_display(s->display);
        append(parent, b);
        return;
    }
    append(parent, b);
    if (type == TC_BOX_BREAK)
        return;
    if (s->display == TC_DISPLAY_LIST_ITEM) {
        int32_t v;
        if (parse_int(tc_dom_get_attr(el, "value"), &v))
            list->next = v;
        b->number = list->next;
        list->next += list->step;
        add_marker(l, b, es);
    }
    if (tc_dom_is_html(&el->node, TC_TAG_INPUT)) {
        /* A button's label. */
        const char *t = input_type(el), *label = tc_dom_get_attr(el, "value");
        if (!label)
            label = tc_css_ascii_ieq(t, "submit") ? "Submit" : tc_css_ascii_ieq(t, "reset") ? "Reset"
                    : tc_css_ascii_ieq(t, "file")  ? "Browse..."
                                                    : "";
        text_box(l, b, s, label, strlen(label));
        return;
    }
    generated(l, b, es->before);
    {
        counter c;
        list_counter(el, &c);
        build_children(l, b, el, s, depth + 1, &c);
    }
    generated(l, b, es->after);
}

static void build_children(tc_layout *l, tc_box *parent, const tc_element *el, const tc_style *s, int depth,
                           counter *list)
{
    const tc_node *n;
    for (n = el->node.first_child; n && !l->nomem; n = n->next) {
        if (n->type == TC_NODE_ELEMENT)
            build_element(l, parent, (const tc_element *)n, depth, list);
        else if (n->type == TC_NODE_TEXT)
            text_box(l, parent, s, ((const tc_chardata *)n)->data, ((const tc_chardata *)n)->len);
    }
}

/* ---- Fixing up the tree ---- */

static int has_block_child(const tc_box *b)
{
    const tc_box *c;
    for (c = b->first; c; c = c->next)
        if (is_block_level(c))
            return 1;
    return 0;
}

/* An inline box with a block inside is split in two around it (CSS 2.1
   9.2.1.1): the pieces share the style, and only the first has the left
   edge, only the last the right. */
static void split_inlines(tc_layout *l, tc_box *b, int depth)
{
    tc_box *c, *next, *prev = NULL;
    if (depth > TC_LAYOUT_MAX_DEPTH + 8)
        return;
    for (c = b->first; c; c = next) {
        tc_box *k, *knext, *piece = NULL, *first_out = NULL, *last_out = NULL;
        next = c->next;
        if (c->first && c->type != TC_BOX_REPLACED)
            split_inlines(l, c, depth + 1);
        if (c->type != TC_BOX_INLINE || !has_block_child(c)) {
            prev = c;
            continue;
        }
        /* Replace c with its pieces, in place. */
        for (k = c->first; k; k = knext) {
            tc_box *out;
            knext = k->next;
            if (is_block_level(k)) {
                piece = NULL;
                out = k;
            } else {
                if (!piece) {
                    piece = new_box(l, TC_BOX_INLINE, c->style, c->element);
                    if (!piece)
                        return;
                    piece->edges = 0;
                    piece->parent = b;
                    out = piece;
                } else {
                    out = NULL;
                }
                k->parent = piece;
                k->next = NULL;
                if (piece->last)
                    piece->last->next = k;
                else
                    piece->first = k;
                piece->last = k;
            }
            if (out) {
                out->parent = b;
                out->next = NULL;
                if (last_out)
                    last_out->next = out;
                else
                    first_out = out;
                last_out = out;
            }
        }
        if (first_out && first_out->type == TC_BOX_INLINE)
            first_out->edges |= c->edges & 1;
        if (last_out && last_out->type == TC_BOX_INLINE)
            last_out->edges |= c->edges & 2;
        if (!first_out) {
            prev = c;
            continue;
        }
        if (prev)
            prev->next = first_out;
        else
            b->first = first_out;
        last_out->next = next;
        if (b->last == c)
            b->last = last_out;
        prev = last_out;
    }
}

static int collapsible_space_only(const tc_box *b)
{
    size_t i;
    if (b->type != TC_BOX_TEXT)
        return 0;
    if (b->style->ws_collapse != TC_COLLAPSE_COLLAPSE)
        return 0;
    for (i = 0; i < b->len; i++)
        if (b->text[i] != ' ' && b->text[i] != '\t' && b->text[i] != '\n' && b->text[i] != '\r' && b->text[i] != '\f')
            return 0;
    return 1;
}

/* Wrap each run of inline-level boxes among block-level ones in an
   anonymous block; runs of collapsible white space are dropped. */
static void wrap_inlines(tc_layout *l, tc_box *b, int depth)
{
    tc_box *c, *next, *run = NULL, *prev = NULL;
    int blocks = 0, inlines = 0;
    if (depth > TC_LAYOUT_MAX_DEPTH + 8)
        return;
    for (c = b->first; c; c = c->next) {
        if (c->first && c->type != TC_BOX_REPLACED)
            wrap_inlines(l, c, depth + 1);
        if (is_block_level(c))
            blocks = 1;
        else
            inlines = 1;
    }
    if (b->type != TC_BOX_BLOCK && b->type != TC_BOX_INLINE_BLOCK)
        return;
    if (!blocks) {
        b->has_lines = inlines;
        return;
    }
    if (!inlines)
        return;
    for (c = b->first; c; c = next) {
        next = c->next;
        if (is_block_level(c)) {
            run = NULL;
            prev = c;
            continue;
        }
        if (!run) {
            /* Skip white space that would collapse away. */
            tc_box *k;
            int empty = 1;
            for (k = c; k && !is_block_level(k); k = k->next)
                if (!collapsible_space_only(k)) {
                    empty = 0;
                    break;
                }
            if (empty) {
                if (prev)
                    prev->next = k;
                else
                    b->first = k;
                if (!k)
                    b->last = prev;
                next = k;
                continue;
            }
            run = new_box(l, TC_BOX_BLOCK, tc_layout_anonymous_style(l, b->style, TC_DISPLAY_BLOCK), NULL);
            if (!run)
                return;
            run->has_lines = 1;
            run->parent = b;
            if (prev)
                prev->next = run;
            else
                b->first = run;
            run->next = NULL;
            prev = run;
        }
        c->parent = run;
        c->next = NULL;
        if (run->last)
            run->last->next = c;
        else
            run->first = c;
        run->last = c;
        prev->next = next;
        if (!next)
            b->last = prev;
    }
}

int tc_box_build(tc_layout *l)
{
    const tc_node *n;
    const tc_style *root_style = NULL;

    for (n = l->doc->node.first_child; n; n = n->next)
        if (n->type == TC_NODE_ELEMENT) {
            const tc_element_styles *es = tc_styled_element(l->styled, (const tc_element *)n);
            if (es)
                root_style = es->style;
            break;
        }
    if (!root_style) {
        tc_style *initial = tc_arena_alloc(&l->arena, sizeof *initial);
        if (!initial)
            return TC_ERR_NOMEM;
        tc_style_initial(initial);
        root_style = initial;
    }
    l->root = new_box(l, TC_BOX_BLOCK, tc_layout_anonymous_style(l, root_style, TC_DISPLAY_BLOCK), NULL);
    if (!l->root)
        return TC_ERR_NOMEM;
    if (n) {
        counter c = { 1, 1 };
        build_element(l, l->root, (const tc_element *)n, 0, &c);
    }
    split_inlines(l, l->root, 0);
    wrap_inlines(l, l->root, 0);
    return l->nomem ? TC_ERR_NOMEM : TC_OK;
}
