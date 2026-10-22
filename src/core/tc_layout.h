/* Layout (CSS 2.1's visual formatting model, with CSS Display 3's boxes):
 * the box tree built from a styled document, block layout with margin
 * collapsing, inline layout with line breaking, and painting and hit
 * testing over the result.
 *
 * Tables, floats and positioned boxes are laid out as blocks until Sprint 7
 * (decision 29); flex and grid containers too. Each box's position is its
 * border box's, relative to its parent's border box, so painting walks the
 * tree adding offsets. Lengths are CSS pixels. Nothing recurses deeper than
 * the box tree, whose depth is capped. */
#ifndef TC_LAYOUT_H
#define TC_LAYOUT_H

#include "tc_font.h"
#include "tc_image.h"
#include "tc_style.h"

typedef enum tc_box_type {
    TC_BOX_BLOCK,        /* a block container */
    TC_BOX_INLINE,       /* an inline box */
    TC_BOX_INLINE_BLOCK, /* an atomic inline: inline-block, inline-table, inline-flex... */
    TC_BOX_TEXT,
    TC_BOX_BREAK,        /* <br> */
    TC_BOX_REPLACED      /* <img> and friends; those that aren't pictures are drawn as placeholders */
} tc_box_type;

/* What a replaced box stands for. */
enum {
    TC_REPLACED_IMAGE, TC_REPLACED_FRAME, TC_REPLACED_MEDIA, TC_REPLACED_TEXT_FIELD, TC_REPLACED_CHECKBOX,
    TC_REPLACED_RADIO
};

typedef struct tc_frag tc_frag;
typedef struct tc_box tc_box;

struct tc_box {
    uint8_t type;            /* tc_box_type */
    uint8_t inline_level;    /* replaced boxes: inline-level, not block-level */
    uint8_t has_lines;       /* a block container of inline content: its lines are in frags */
    uint8_t replaced;        /* TC_REPLACED_* */
    uint8_t edges;           /* inline boxes split around blocks: bit 0 left edge, bit 1 right */
    uint8_t checked;         /* checkboxes and radio buttons */
    const tc_style *style;
    const tc_element *element; /* NULL for anonymous boxes, text and generated content */
    tc_box *parent, *first, *last, *next;
    const char *text;        /* text: as in the document; replaced: alt text or the field's value */
    size_t len;
    int32_t number;          /* list items: their number */
    /* Layout: the border box, relative to the parent's border box... */
    float x, y, w, h;
    /* ...the used box model (top, right, bottom, left)... */
    float margin[4], border[4], padding[4];
    /* ...the baseline it sits on in a line (an inline-block's last line's),
       from the border box top, or -1... */
    float baseline;
    /* ...the area its contents paint in, relative to its border box... */
    float ink[4];            /* x0, y0, x1, y1 */
    /* ...and a block container's lines. */
    tc_frag *frags;
    uint32_t nfrags;
    /* An outside list marker, relative to the border box. */
    float marker_x, marker_y;
    uint8_t has_marker;
};

typedef enum tc_frag_type {
    TC_FRAG_TEXT,            /* text: x and y are the start of its baseline */
    TC_FRAG_BOX,             /* an inline box's piece on one line: its border box */
    TC_FRAG_ATOMIC,          /* an inline-block or replaced box (box->x and y are set too) */
    TC_FRAG_MARKER           /* an inside list marker */
} tc_frag_type;

/* A piece of a line, relative to its block container's border box. */
struct tc_frag {
    uint8_t type;            /* tc_frag_type */
    uint8_t edges;           /* TC_FRAG_BOX: bit 0 has the box's left edge, bit 1 its right */
    const tc_box *box;       /* the text box, inline box, atomic box or list item */
    float x, y, w, h;
    const char *text;        /* TC_FRAG_TEXT: the text after white-space processing */
    uint32_t len;
    const tc_font *font;
    float word_spacing;      /* added to each space (text-align: justify too) */
};

typedef struct tc_layout tc_layout;

/* Where layout and painting get images: tc_page provides them. Any
   callback may be NULL. */
typedef struct tc_image_source {
    /* The image an element shows (<img>, <input type=image>), or NULL while
       it loads, when it failed, or when there's none. *density: its pixels
       per CSS pixel (srcset). sizing: layout is asking for its size, so a
       NULL answer means it must lay out again when the image arrives. */
    tc_image *(*element)(void *ctx, const tc_element *el, int sizing, float *density);
    /* The image a style's url() names, or NULL. */
    tc_image *(*url)(void *ctx, const tc_css_image *ref);
    /* An image's pixels to draw, decoded now if they need to be (NULL if
       they can't be). */
    const tc_surface *(*pixels)(void *ctx, tc_image *img);
    void *ctx;
} tc_image_source;

/* Lay out a styled document for a viewport w x h. images may be NULL (no
   pictures: their alt text instead). */
int tc_layout_document(const tc_document *doc, const tc_styled *styled, tc_font_set *fonts,
                       const tc_image_source *images, float w, float h, tc_layout **out);
void tc_layout_free(tc_layout *l);

/* The root of the box tree (the initial containing block), and the
   document's size (at least the viewport's). */
const tc_box *tc_layout_root(const tc_layout *l);
void tc_layout_size(const tc_layout *l, float *w, float *h);

/* Paint the part of the document at (scroll_x, scroll_y) into s, inside clip
   (in s's coordinates). */
void tc_paint(const tc_layout *l, tc_surface *s, tc_rect clip, float scroll_x, float scroll_y);

/* The innermost element at a point in the document, or NULL. */
const tc_element *tc_layout_hit(const tc_layout *l, float x, float y);
/* Where an element's first box is in the document. 0 if it has none. */
int tc_layout_find(const tc_layout *l, const tc_element *el, float *x, float *y);

/* The box tree as text, one box or fragment per line, for debugging and
   the tests: type, element, border box and, for text, the text. */
int tc_layout_dump(const tc_layout *l, tc_buf *out);

/* Statistics. */
typedef struct tc_layout_stats {
    size_t boxes, lines, frags, bytes;
} tc_layout_stats;
void tc_layout_stats_of(const tc_layout *l, tc_layout_stats *out);

#endif
