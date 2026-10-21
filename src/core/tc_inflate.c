#include "tc_inflate.h"

#include <string.h>

#include "zlib.h"

tc_content_coding tc_content_coding_from_header(const char *value)
{
    char token[16];
    size_t n = 0;

    if (!value)
        return TC_CODING_IDENTITY;
    while (*value == ' ' || *value == '\t')
        value++;
    while (value[n] && value[n] != ' ' && value[n] != '\t' && value[n] != ',' && n < sizeof token - 1) {
        token[n] = (char)(value[n] >= 'A' && value[n] <= 'Z' ? value[n] + 32 : value[n]);
        n++;
    }
    token[n] = '\0';
    if (n == 0 || strcmp(token, "identity") == 0)
        return TC_CODING_IDENTITY;
    if (strcmp(token, "gzip") == 0 || strcmp(token, "x-gzip") == 0)
        return TC_CODING_GZIP;
    if (strcmp(token, "deflate") == 0)
        return TC_CODING_DEFLATE;
    return TC_CODING_UNKNOWN;
}

/* A zlib header: compression method 8 and a header checksum. */
static int looks_like_zlib(const unsigned char *p, size_t len)
{
    return len >= 2 && (p[0] & 0x0F) == 8 && (p[0] >> 4) <= 7 && ((p[0] << 8) | p[1]) % 31 == 0;
}

int tc_inflate(tc_content_coding coding, const void *in, size_t len, size_t max_out, tc_buf *out)
{
    const unsigned char *p = in;
    unsigned char chunk[16384];
    z_stream z;
    int window, zerr = Z_OK, err = TC_OK;

    if (coding == TC_CODING_IDENTITY)
        return len > max_out ? TC_ERR_TOO_LARGE : tc_buf_append(out, in, len);
    if (coding != TC_CODING_GZIP && coding != TC_CODING_DEFLATE)
        return TC_ERR_DECODE;

    if (coding == TC_CODING_GZIP)
        window = 16 + MAX_WBITS;
    else
        window = looks_like_zlib(p, len) ? MAX_WBITS : -MAX_WBITS;
    memset(&z, 0, sizeof z);
    if (inflateInit2(&z, window) != Z_OK)
        return TC_ERR_NOMEM;
    z.next_in = (Bytef *)p;
    z.avail_in = (uInt)len;

    while (!err) {
        z.next_out = chunk;
        z.avail_out = sizeof chunk;
        zerr = inflate(&z, Z_NO_FLUSH);
        if (zerr != Z_OK && zerr != Z_STREAM_END && zerr != Z_BUF_ERROR) {
            err = TC_ERR_DECODE;
            break;
        }
        if (out->len + (sizeof chunk - z.avail_out) > max_out) {
            err = TC_ERR_TOO_LARGE;
            break;
        }
        err = tc_buf_append(out, chunk, sizeof chunk - z.avail_out);
        if (zerr == Z_STREAM_END) {
            /* Another gzip member may follow; trailing junk is ignored, as
               browsers do. */
            if (coding == TC_CODING_GZIP && z.avail_in >= 2 && z.next_in[0] == 0x1F && z.next_in[1] == 0x8B)
                inflateReset(&z);
            else
                break;
        } else if (zerr == Z_BUF_ERROR || (z.avail_in == 0 && z.avail_out != 0)) {
            /* Truncated input. Browsers show what they have, and so do we. */
            break;
        }
    }
    inflateEnd(&z);
    return err;
}
