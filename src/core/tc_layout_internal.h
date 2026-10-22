/* Layout's insides, shared by tc_box.c (the box tree), tc_layout.c (block
 * and inline layout) and tc_paint.c (painting and hit testing). */
#ifndef TC_LAYOUT_INTERNAL_H
#define TC_LAYOUT_INTERNAL_H

#include "tc_layout.h"

#define TC_LAYOUT_MAX_DEPTH 200 /* deeper elements flow into their ancestor at this depth */

struct tc_layout {
    tc_arena arena;
    const tc_document *doc;
    const tc_styled *styled;
    tc_font_set *fonts;
    tc_image_source images;    /* zeroed when there are none */
    float vw, vh;              /* the viewport */
    tc_box *root;              /* the initial containing block */
    float doc_w, doc_h;
    int nomem;
    int quote_depth;
    tc_layout_stats stats;
};

/* Build l->root from the styled document. */
int tc_box_build(tc_layout *l);

/* An anonymous box's style (in the layout's arena). */
const tc_style *tc_layout_anonymous_style(tc_layout *l, const tc_style *parent, int display);

/* The font for a style's font properties. */
const tc_font *tc_layout_font(tc_layout *l, const tc_style *s);

/* A list item's marker text ("1. ", "iv. "...), or "" for bullets, which are
   drawn as shapes. */
size_t tc_marker_text(const tc_style *s, int32_t number, char *out, size_t size);

#endif
