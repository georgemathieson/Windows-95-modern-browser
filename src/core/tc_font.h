/* Fonts and text: TrueType fonts (Windows 95's own, decision 27) read and
 * rasterised by stb_truetype, chosen by CSS font matching, measured and
 * drawn into a tc_surface with antialiasing and a glyph cache.
 *
 * Text is UTF-8. A font set may be used from several threads at once. Faces
 * that a family lacks (bold, italic) are synthesised, as browsers do. A
 * character the font lacks comes from the first face in the set that has
 * it, at the same size and style (decision 36), so fonts added later are
 * the last resort. When no font at all is installed, text is drawn as
 * boxes. */
#ifndef TC_FONT_H
#define TC_FONT_H

#include "tc_surface.h"

typedef struct tc_font_set tc_font_set;
typedef struct tc_font tc_font;

typedef struct tc_font_metrics {
    float ascent, descent, line_gap;         /* px; descent is below the baseline, positive */
    float x_height, cap_height;
    float underline_offset, underline_thickness; /* the offset is below the baseline */
    float strikeout_offset, strikeout_thickness; /* the offset is above the baseline */
    float space_width;
} tc_font_metrics;

tc_font_set *tc_font_set_new(void);
/* A set for one page's web fonts (@font-face) in front of parent: its
   families hide the parent's of the same name, and characters its faces
   lack come from its other faces, then the parent's as usual. The two
   share a lock; the parent must outlive it. Fonts matched in it, even
   with the parent's faces, belong to it. */
tc_font_set *tc_font_set_new_child(tc_font_set *parent);
void tc_font_set_free(tc_font_set *set);

/* Add a TrueType font file (or collection). family, weight and italic: as
   the system lists the font, or family NULL to read them from the file. */
int tc_font_set_add_file(tc_font_set *set, const char *path, const char *family, int weight, int italic);
/* Add a font (or collection) from memory, copied: named family, or as
   it names itself when family is NULL. Fonts from memory come from the
   web: WOFF and WOFF2 are unpacked, and each face is checked
   (tc_font_check) and left out if it fails. */
int tc_font_set_add_data(tc_font_set *set, const void *data, size_t len, const char *family);
/* Add a web font as an @font-face rule describes it, checked as fonts from
   memory are: its first face, matched as family with the rule's weight
   (weight_min to weight_max for a variable font) and style, for the
   characters in ranges only (first, last pairs; NULL for all). Bold and
   italic are synthesised by what the file itself is. */
int tc_font_set_add_web(tc_font_set *set, const void *data, size_t len, const char *family, int weight_min,
                        int weight_max, int italic, const uint32_t *ranges, uint32_t nranges);
/* Add every font file (.ttf, .ttc, .otf) in a directory; returns how many
   faces were added. */
size_t tc_font_set_add_dir(tc_font_set *set, const char *dir);
/* Add the system's fonts (tc_system_fonts). */
size_t tc_font_set_add_system(tc_font_set *set);

/* Whether a family (or a generic family) has a face here or in a parent. */
int tc_font_set_has_family(tc_font_set *set, const char *family);

/* The faces, for about: pages and the tests. */
size_t tc_font_set_count(const tc_font_set *set);
const char *tc_font_set_face(const tc_font_set *set, size_t i, int *weight, int *italic);

/* CSS font matching: the first of the families that's installed (generic
   families included), and its face nearest the weight (1 to 1000) and
   style, at size px. Never NULL; the font lives as long as the set. */
const tc_font *tc_font_match(tc_font_set *set, const char *const *families, uint32_t count, int weight, int italic,
                             float size);

/* How far a face of weight min to max (a variable font's range) is from
   the weight wanted, as CSS font matching ranks them: 0 for a match. */
int tc_font_weight_distance(int min, int max, int want);

/* The family of the face chosen ("" for the box font), and whether bold
   and italic are synthesised. */
const char *tc_font_family(const tc_font *font, int *fake_bold, int *fake_italic);
const tc_font_metrics *tc_font_get_metrics(const tc_font *font);
/* The font a character is drawn with: font itself, or its fallback. */
const tc_font *tc_font_for_char(const tc_font *font, uint32_t cp);

/* The advance of text in px, with kerning and letter_spacing after each
   character. */
float tc_font_measure(const tc_font *font, const char *text, size_t len, float letter_spacing);

/* Draw text with its baseline's left end at (x, y), clipped to clip.
   Returns the advance, as tc_font_measure does. */
float tc_font_draw(const tc_font *font, tc_surface *s, tc_rect clip, float x, float y, const char *text, size_t len,
                   float letter_spacing, tc_color color);

#endif
