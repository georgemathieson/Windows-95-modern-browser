/* Content-Encoding decoding: gzip and deflate, using zlib, and Brotli
 * (br, decision 42), which WOFF2 fonts use too.
 *
 * Handles the real-world quirks: "deflate" is meant to be zlib-wrapped but
 * some servers send raw deflate, and gzip bodies may hold several members.
 * Output is capped, so a small "zip bomb" can't exhaust memory. */
#ifndef TC_INFLATE_H
#define TC_INFLATE_H

#include "tc_buf.h"

typedef enum tc_content_coding {
    TC_CODING_IDENTITY,
    TC_CODING_GZIP,
    TC_CODING_DEFLATE,
    TC_CODING_BR,
    TC_CODING_UNKNOWN
} tc_content_coding;

/* Map a Content-Encoding header value (may be NULL) to a coding. */
tc_content_coding tc_content_coding_from_header(const char *value);

/* Decode all of in into out (appending). TC_ERR_DECODE for corrupt data,
   TC_ERR_TOO_LARGE if the output would pass max_out bytes. */
int tc_inflate(tc_content_coding coding, const void *in, size_t len, size_t max_out, tc_buf *out);

#endif
