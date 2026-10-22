/* Web fonts: WOFF (zlib) and WOFF2 (Brotli, with glyf, loca and hmtx
 * transformed) turned back into the TrueType files they were made from,
 * for tc_font_check and stb_truetype. Single fonts only; collections in
 * WOFF2 are refused. */
#ifndef TC_WOFF_H
#define TC_WOFF_H

#include "tc_buf.h"

/* Whether data is WOFF or WOFF2. */
int tc_woff_sniff(const void *data, size_t len);

/* The font inside, appended to out. TC_ERR_DECODE if it's broken,
   TC_ERR_TOO_LARGE if it would be bigger than 32 MB. */
int tc_woff_decode(const void *data, size_t len, tc_buf *out);

#endif
