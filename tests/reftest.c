/* The reference tests: each line of reftest.list is "== test.html
 * ref.html" (they must render the same, pixel for pixel) or "!= a.html
 * b.html" (they mustn't). Pages are rendered at 400 x 300 with the
 * Liberation fonts, natively and under Wine alike.
 *
 *   tachyon_reftests LIST [FAILURES_DIR]
 *
 * Paths in the list are relative to its directory. When a test fails, its
 * two renderings are written to FAILURES_DIR as PNGs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render_page.h"
#include "tc_png.h"

static void save(const char *dir, const char *name, const char *suffix, const tc_surface *s)
{
    char path[1024];
    tc_buf png;
    FILE *f;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s%s.png", dir, name, suffix);
    tc_buf_init(&png);
    if (tc_png_encode(s, &png) == TC_OK && (f = fopen(path, "wb")) != NULL) {
        fwrite(png.data, 1, png.len, f);
        fclose(f);
        printf("     wrote %s\n", path);
    }
    tc_buf_free(&png);
}

/* How many pixels differ; -1 if the sizes do. */
static long compare(const tc_surface *a, const tc_surface *b)
{
    long diff = 0;
    int x, y;
    if (a->width != b->width || a->height != b->height)
        return -1;
    for (y = 0; y < a->height; y++)
        for (x = 0; x < a->width; x++)
            diff += tc_surface_get(a, x, y) != tc_surface_get(b, x, y);
    return diff;
}

int main(int argc, char **argv)
{
    char line[1024], dir[512], test_path[1024], ref_path[1024];
    const char *slash, *out_dir = argc > 2 ? argv[2] : NULL;
    tc_font_set *fonts;
    FILE *list;
    int passed = 0, failed = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: tachyon_reftests LIST [FAILURES_DIR]\n");
        return 2;
    }
    list = fopen(argv[1], "r");
    if (!list) {
        fprintf(stderr, "tachyon_reftests: can't open %s\n", argv[1]);
        return 2;
    }
    slash = strrchr(argv[1], '/');
    if (!slash)
        slash = strrchr(argv[1], '\\');
    snprintf(dir, sizeof dir, "%.*s", slash && slash - argv[1] < 500 ? (int)(slash - argv[1] + 1) : 0, argv[1]);
    tc_style_init();
    fonts = render_fonts(NULL);
    if (!fonts || !tc_font_set_count(fonts))
        printf("     no fonts found: install fonts-liberation\n");
    while (fgets(line, sizeof line, list)) {
        char op[8], test[256], ref[256];
        rendered a, b;
        long diff;
        int ok, ea, eb;
        if (line[0] == '#' || sscanf(line, "%7s %255s %255s", op, test, ref) != 3)
            continue;
        snprintf(test_path, sizeof test_path, "%s%s", dir, test);
        snprintf(ref_path, sizeof ref_path, "%s%s", dir, ref);
        ea = render_file(test_path, fonts, 400, 300, 0, 0, &a);
        eb = render_file(ref_path, fonts, 400, 300, 0, 0, &b);
        if (ea || eb) {
            printf("FAIL %s %s %s: can't render (%s)\n", test, op, ref, tc_err_text(ea ? ea : eb));
            failed++;
            continue;
        }
        diff = compare(&a.surface, &b.surface);
        ok = strcmp(op, "==") == 0 ? diff == 0 : diff != 0;
        if (ok) {
            printf("ok   %s %s %s\n", test, op, ref);
            passed++;
        } else {
            char name[256], *dot;
            printf("FAIL %s %s %s: %ld pixels differ\n", test, op, ref, diff);
            snprintf(name, sizeof name, "%s", test);
            if ((dot = strrchr(name, '.')) != NULL)
                *dot = '\0';
            save(out_dir, name, "", &a.surface);
            save(out_dir, name, "-ref", &b.surface);
            failed++;
        }
        rendered_free(&a);
        rendered_free(&b);
    }
    fclose(list);
    tc_font_set_free(fonts);
    printf("%d reference tests passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
