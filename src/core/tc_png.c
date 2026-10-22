/* A PNG encoder (RFC 2083) with a small deflate compressor (RFC 1951,
 * fixed Huffman codes only). */
#include <stdlib.h>
#include <string.h>

#include "tc_png.h"
#include "zlib.h"

typedef struct bits {
    tc_buf *out;
    uint32_t acc;
    int n;
    int err;
} bits;

static void put_bits(bits *b, uint32_t value, int count)
{
    b->acc |= value << b->n;
    b->n += count;
    while (b->n >= 8) {
        if (tc_buf_append_char(b->out, (char)(b->acc & 255)) != TC_OK)
            b->err = 1;
        b->acc >>= 8;
        b->n -= 8;
    }
}

/* Huffman codes go most significant bit first. */
static void put_code(bits *b, uint32_t code, int len)
{
    uint32_t r = 0;
    int i;
    for (i = 0; i < len; i++)
        r |= ((code >> i) & 1) << (len - 1 - i);
    put_bits(b, r, len);
}

static void literal(bits *b, unsigned v)
{
    if (v < 144)
        put_code(b, 0x30 + v, 8);
    else if (v < 256)
        put_code(b, 0x190 + v - 144, 9);
    else if (v < 280)
        put_code(b, v - 256, 7);
    else
        put_code(b, 0xC0 + v - 280, 8);
}

static void match(bits *b, unsigned len, unsigned dist)
{
    static const uint16_t len_base[] = { 3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const uint8_t len_extra[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const uint16_t dist_base[] = { 1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,   97,   129,
                                          193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const uint8_t dist_extra[] = { 0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                          6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    unsigned i = 28, d = 29;
    while (len < len_base[i])
        i--;
    literal(b, 257 + i);
    put_bits(b, len - len_base[i], len_extra[i]);
    while (dist < dist_base[d])
        d--;
    put_code(b, d, 5);
    put_bits(b, dist - dist_base[d], dist_extra[d]);
}

#define HASH_BITS 15
#define WINDOW 32768

static int deflate_fixed(const unsigned char *in, size_t len, tc_buf *out)
{
    int32_t *head = malloc(sizeof *head << HASH_BITS);
    bits b = { out, 0, 0, 0 };
    size_t i = 0;
    if (!head)
        return TC_ERR_NOMEM;
    memset(head, 0xFF, sizeof *head << HASH_BITS);
    put_bits(&b, 1, 1); /* the final block */
    put_bits(&b, 1, 2); /* fixed Huffman codes */
    while (i < len && !b.err) {
        unsigned best = 0, dist = 0;
        if (i + 3 <= len) {
            uint32_t h = ((uint32_t)in[i] << 10 ^ (uint32_t)in[i + 1] << 5 ^ in[i + 2]) & ((1u << HASH_BITS) - 1);
            int32_t cand = head[h];
            head[h] = (int32_t)i;
            if (cand >= 0 && i - (size_t)cand <= WINDOW && i - (size_t)cand > 0) {
                size_t max = TC_MIN(258, len - i), k = 0;
                while (k < max && in[cand + k] == in[i + k])
                    k++;
                if (k >= 3) {
                    best = (unsigned)k;
                    dist = (unsigned)(i - (size_t)cand);
                }
            }
        }
        if (best) {
            match(&b, best, dist);
            i += best;
        } else {
            literal(&b, in[i++]);
        }
    }
    literal(&b, 256);
    if (b.n)
        put_bits(&b, 0, 8 - b.n);
    free(head);
    return b.err ? TC_ERR_NOMEM : TC_OK;
}

static void be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int chunk(tc_buf *out, const char *type, const unsigned char *data, size_t len)
{
    unsigned char head[8], tail[4];
    uLong crc = crc32(0, (const Bytef *)type, 4);
    if (len)
        crc = crc32(crc, data, (uInt)len);
    be32(head, (uint32_t)len);
    memcpy(head + 4, type, 4);
    be32(tail, (uint32_t)crc);
    if (tc_buf_append(out, head, 8) != TC_OK || (len && tc_buf_append(out, data, len) != TC_OK) ||
        tc_buf_append(out, tail, 4) != TC_OK)
        return TC_ERR_NOMEM;
    return TC_OK;
}

int tc_png_encode(const tc_surface *s, tc_buf *out)
{
    static const unsigned char signature[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    size_t row = (size_t)s->width * 3 + 1, raw_len = row * (size_t)s->height;
    unsigned char *raw = malloc(raw_len ? raw_len : 1), ihdr[13], adler[4];
    tc_buf z;
    int x, y, err;

    if (!raw)
        return TC_ERR_NOMEM;
    /* Each row filtered with "Up": unchanged rows become runs of zeros. */
    for (y = 0; y < s->height; y++) {
        unsigned char *r = raw + row * (size_t)y;
        r[0] = y ? 2 : 0;
        for (x = 0; x < s->width; x++) {
            uint32_t p = s->pixels[(size_t)y * (size_t)s->stride + (size_t)x];
            uint32_t q = y ? s->pixels[(size_t)(y - 1) * (size_t)s->stride + (size_t)x] : 0;
            r[1 + x * 3] = (unsigned char)(((p >> 16) - (q >> 16)) & 255);
            r[2 + x * 3] = (unsigned char)(((p >> 8) - (q >> 8)) & 255);
            r[3 + x * 3] = (unsigned char)((p - q) & 255);
        }
    }
    tc_buf_init(&z);
    err = tc_buf_append(&z, "\x78\x01", 2);
    if (err == TC_OK)
        err = deflate_fixed(raw, raw_len, &z);
    be32(adler, (uint32_t)adler32(adler32(0, NULL, 0), raw, (uInt)raw_len));
    if (err == TC_OK)
        err = tc_buf_append(&z, adler, 4);
    free(raw);
    be32(ihdr, (uint32_t)s->width);
    be32(ihdr + 4, (uint32_t)s->height);
    ihdr[8] = 8;  /* bits per channel */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    if (err == TC_OK)
        err = tc_buf_append(out, signature, 8);
    if (err == TC_OK)
        err = chunk(out, "IHDR", ihdr, 13);
    if (err == TC_OK)
        err = chunk(out, "IDAT", (const unsigned char *)z.data, z.len);
    if (err == TC_OK)
        err = chunk(out, "IEND", NULL, 0);
    tc_buf_free(&z);
    return err;
}
