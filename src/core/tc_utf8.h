/* UTF-8 encoding and decoding.
 *
 * Tachyon keeps all text as UTF-8 internally. Decoding follows the WHATWG
 * Encoding Standard: each invalid sequence becomes one U+FFFD. */
#ifndef TC_UTF8_H
#define TC_UTF8_H

#include "tc_base.h"

#define TC_REPLACEMENT_CHAR 0xFFFDu

/* Decode the code point at s (len bytes available, len > 0) into *cp.
   Returns how many bytes it used (at least 1). */
size_t tc_utf8_decode(const char *s, size_t len, uint32_t *cp);

/* Encode cp (a Unicode scalar value) into out. Returns 1 to 4. */
size_t tc_utf8_encode(uint32_t cp, char out[4]);

#endif
