#include "tc_utf8.h"

size_t tc_utf8_decode(const char *s, size_t len, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char lead = p[0], lower = 0x80, upper = 0xBF;
    size_t needed, i;
    uint32_t value;

    if (lead < 0x80) {
        *cp = lead;
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        needed = 1;
        value = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        needed = 2;
        value = lead & 0x0F;
        if (lead == 0xE0)
            lower = 0xA0; /* overlong */
        if (lead == 0xED)
            upper = 0x9F; /* surrogates */
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        needed = 3;
        value = lead & 0x07;
        if (lead == 0xF0)
            lower = 0x90; /* overlong */
        if (lead == 0xF4)
            upper = 0x8F; /* above U+10FFFF */
    } else {
        *cp = TC_REPLACEMENT_CHAR;
        return 1;
    }
    for (i = 1; i <= needed; i++) {
        if (i >= len || p[i] < lower || p[i] > upper) {
            /* The valid prefix so far becomes one replacement character. */
            *cp = TC_REPLACEMENT_CHAR;
            return i;
        }
        value = (value << 6) | (p[i] & 0x3F);
        lower = 0x80;
        upper = 0xBF;
    }
    *cp = value;
    return needed + 1;
}

size_t tc_utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
