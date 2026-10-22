/* PNG files from surfaces, for tachyon-render, the reference tests and
 * screenshots. zlib in Tachyon only decompresses (see its README), so this
 * has its own small compressor: LZ77 with deflate's fixed Huffman codes,
 * which does well enough on rendered pages. */
#ifndef TC_PNG_H
#define TC_PNG_H

#include "tc_buf.h"
#include "tc_surface.h"

/* Encode the surface as an 8-bit RGB PNG, appended to out. */
int tc_png_encode(const tc_surface *s, tc_buf *out);

#endif
