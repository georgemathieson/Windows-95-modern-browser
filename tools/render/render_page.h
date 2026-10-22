/* Rendering a page from a file: shared by tachyon-render and the reference
 * tests. Style sheets linked from the page, and their @imports, are read
 * from files beside it. */
#ifndef RENDER_PAGE_H
#define RENDER_PAGE_H

#include "tc_font.h"
#include "tc_layout.h"

typedef struct rendered {
    tc_surface surface;
    float doc_w, doc_h;
    unsigned parse_ms, style_ms, layout_ms, paint_ms;
    tc_style_stats style;
    tc_layout_stats layout;
    tc_buf boxes;              /* the box tree, if asked for */
} rendered;

/* Render path at a viewport of w x h. full: the whole document's height
   rather than the viewport's. */
int render_file(const char *path, tc_font_set *fonts, int w, int h, int full, int dump, rendered *out);
void rendered_free(rendered *r);

/* The fonts in dirs, in that order (characters missing from the first come
   from the later ones); with none, the test fonts: the Liberation fonts
   (natively, or through Wine's Z: drive), else the system's. */
tc_font_set *render_fonts(const char *const *dirs, size_t ndirs);

#endif
