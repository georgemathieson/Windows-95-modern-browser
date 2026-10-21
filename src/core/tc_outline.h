/* A document tree as an outline, the way a DOM inspector shows it: one row
 * per node, children indented under their parent, any element collapsible.
 *
 * The Win32 view only draws the rows; building them and their text is here
 * so it can be tested natively. Whitespace-only text is left out, as
 * inspectors do. */
#ifndef TC_OUTLINE_H
#define TC_OUTLINE_H

#include "tc_dom.h"

/* What a piece of a row's text is, for colouring. */
typedef enum tc_span_kind {
    TC_SPAN_PLAIN,       /* < > = " */
    TC_SPAN_TAG,
    TC_SPAN_ATTR_NAME,
    TC_SPAN_ATTR_VALUE,
    TC_SPAN_TEXT,
    TC_SPAN_COMMENT,
    TC_SPAN_DOCTYPE,     /* also processing instructions */
    TC_SPAN_NOTE         /* #document-fragment, and the ellipsis of a cut row */
} tc_span_kind;

typedef struct tc_outline_row {
    const tc_node *node;
    uint32_t depth;
    uint8_t expandable, expanded;
} tc_outline_row;

typedef struct tc_outline {
    const tc_node *root;
    tc_outline_row *rows;
    size_t count, cap;
    const tc_node **collapsed;  /* sorted by address */
    size_t ncollapsed, collapsed_cap;
} tc_outline;

/* The rows for root's children, with <head> collapsed. */
int tc_outline_init(tc_outline *o, const tc_node *root);
void tc_outline_free(tc_outline *o);

/* Expand or collapse a row's node and rebuild the rows. */
int tc_outline_set_expanded(tc_outline *o, size_t row, int expanded);

/* The row showing node, or -1. */
long tc_outline_find(const tc_outline *o, const tc_node *node);

/* The row of a row's parent, or -1 at the top level. */
long tc_outline_parent(const tc_outline *o, size_t row);

/* Rows are cut after this many bytes of text. */
#define TC_OUTLINE_MAX_TEXT 400
#define TC_OUTLINE_MAX_SPANS 64

typedef struct tc_outline_text {
    tc_buf text;  /* UTF-8; spans index into it */
    size_t span_start[TC_OUTLINE_MAX_SPANS];
    uint8_t span_kind[TC_OUTLINE_MAX_SPANS];
    size_t nspans;
} tc_outline_text;

void tc_outline_text_init(tc_outline_text *t);
void tc_outline_text_free(tc_outline_text *t);

/* A row's text, e.g. <a href="/wiki">, "Example", <!-- note -->. */
int tc_outline_row_text(const tc_outline *o, size_t row, tc_outline_text *out);

#endif
