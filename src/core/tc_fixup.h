/* Address bar "fixup": turn what someone typed into a URL to load.
 *
 *   "example.com"         -> "https://example.com"
 *   "  https://a.org/x "  -> "https://a.org/x"
 *   "ABOUT:Tachyon"       -> "about:tachyon"
 *
 * This is deliberately simple. Real URL parsing (the WHATWG URL standard)
 * arrives in Sprint 1 and will validate the result. */
#ifndef TC_FIXUP_H
#define TC_FIXUP_H

#include "tc_buf.h"

/* Writes the URL to `out` (replacing its contents). TC_ERR_INVALID if the
   input is empty or contains spaces (searching comes later). */
int tc_fixup_address(const char *input, tc_buf *out);

#endif
