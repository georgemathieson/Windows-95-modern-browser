/* Checking fonts from the web before stb_truetype reads them (decision 38).
 *
 * stb_truetype trusts its input. Every offset it follows and every byte it
 * reads in the tables it uses (the table directory, head, hhea, hmtx, maxp,
 * cmap, loca, glyf and kern) is checked here: glyphs are parsed the way it
 * parses them, and composite glyphs may nest 8 deep, without loops. What's
 * cheap to repair is repaired in place: a cmap's binary search fields are
 * recomputed, and GPOS and a kern table that won't do are hidden (so no
 * kerning). Fonts with PostScript (CFF) outlines aren't accepted yet. */
#ifndef TC_FONT_CHECK_H
#define TC_FONT_CHECK_H

#include "tc_base.h"

/* Check the font whose table directory starts at start (0, or a face in
   a collection). TC_OK if stb_truetype may read it. */
int tc_font_check(uint8_t *data, size_t len, uint32_t start);

#endif
