/* Content-Encoding decoding: gzip and deflate, using zlib.
 *
 * Handles the real-world quirks: "deflate" is meant to be zlib-wrapped but
 * some servers send raw deflate, and gzip bodies may hold several members.
 * Output is capped, so a small "zip bomb" can't exhaust memory. */
#ifndef TC_INFLATE_H
#define TC_INFLATE_H

#include "tc_buf.h"

typedef enum tc_encoding {
    TC_ENCODING_IDENTITY,
    TC_ENCODING_GZIP,
    TC_ENCODING_DEFLATE,
    TC_ENCODING_UNKNOWN
} tc_encoding;

/* Map a Content-Encoding header value (may be NULL) to an encoding. */
tc_encoding tc_encoding_from_header(const char *value);

/* Decode all of in into out (appending). TC_ERR_DECODE for corrupt data,
   TC_ERR_TOO_LARGE if the output would pass max_out bytes. */
int tc_inflate(tc_encoding encoding, const void *in, size_t len, size_t max_out, tc_buf *out);

#endif
