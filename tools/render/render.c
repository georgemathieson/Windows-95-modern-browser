/* tachyon-render: lay out and paint a page from a file into a PNG.
 *
 *   tachyon-render [--width N] [--height N] [--viewport] [--fonts DIR] [--boxes]
 *                  page.html out.png
 *
 *   --width, --height  the viewport (800 x 600)
 *   --viewport         only the viewport, not the whole page
 *   --fonts DIR        fonts from DIR, not the Liberation or system fonts
 *   --boxes            print the box tree
 *
 * Style sheets linked from the page are read from files beside it. Timings
 * and statistics go to stderr. Built for the host (tachyon-render) and for
 * Windows 95 (RENDER.EXE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_page.h"
#include "tc_png.h"

static int usage(void)
{
    fprintf(stderr,
            "usage: tachyon-render [--width N] [--height N] [--viewport] [--fonts DIR] [--boxes] page.html out.png\n");
    return 1;
}

int main(int argc, char **argv)
{
    int i, w = 800, h = 600, full = 1, boxes = 0, err;
    const char *fonts_dir = NULL, *in = NULL, *out = NULL;
    tc_font_set *fonts;
    rendered r;
    tc_buf png;
    FILE *f;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            w = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            h = atoi(argv[++i]);
        else if (strcmp(argv[i], "--viewport") == 0)
            full = 0;
        else if (strcmp(argv[i], "--boxes") == 0)
            boxes = 1;
        else if (strcmp(argv[i], "--fonts") == 0 && i + 1 < argc)
            fonts_dir = argv[++i];
        else if (argv[i][0] == '-' && argv[i][1])
            return usage();
        else if (!in)
            in = argv[i];
        else if (!out)
            out = argv[i];
        else
            return usage();
    }
    if (!in || !out || w <= 0 || h <= 0 || w > 4096)
        return usage();
    tc_style_init();
    fonts = render_fonts(fonts_dir);
    if (!fonts)
        return 2;
    err = render_file(in, fonts, w, h, full, boxes, &r);
    if (err) {
        fprintf(stderr, "tachyon-render: %s: %s\n", in, tc_err_text(err));
        tc_font_set_free(fonts);
        return 2;
    }
    if (boxes)
        fwrite(r.boxes.data ? r.boxes.data : "", 1, r.boxes.len, stdout);
    tc_buf_init(&png);
    err = tc_png_encode(&r.surface, &png);
    f = err ? NULL : fopen(out, "wb");
    if (!f || fwrite(png.data, 1, png.len, f) != png.len) {
        fprintf(stderr, "tachyon-render: can't write %s\n", out);
        err = 1;
    }
    if (f)
        fclose(f);
    fprintf(stderr,
            "tachyon-render: %dx%.0f; parse %u ms, style %u ms (%u elements, %u styles), layout %u ms (%u boxes, %u "
            "lines), paint %u ms\n",
            w, r.doc_h, r.parse_ms, r.style_ms, (unsigned)r.style.elements, (unsigned)r.style.styles, r.layout_ms,
            (unsigned)r.layout.boxes, (unsigned)r.layout.lines, r.paint_ms);
    tc_buf_free(&png);
    rendered_free(&r);
    tc_font_set_free(fonts);
    return err ? 2 : 0;
}
