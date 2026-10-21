#include "tc_encoding.h"

#include <string.h>

#include "tc_encoding_data.h"
#include "tc_utf8.h"

const char *tc_encoding_name(tc_encoding enc)
{
    return enc >= 0 && enc < TC_ENC_COUNT ? tc_encoding_names[enc] : "";
}

int tc_encoding_supported(tc_encoding enc)
{
    return enc >= 0 && enc < TC_ENC_COUNT && (enc < TC_ENC_GBK || enc > TC_ENC_EUC_KR);
}

static int is_ascii_ws(char c)
{
    return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

tc_encoding tc_encoding_from_label(const char *label, size_t len)
{
    char key[32];
    size_t i, lo = 0, hi = tc_encoding_label_count;

    while (len && is_ascii_ws(*label)) {
        label++;
        len--;
    }
    while (len && is_ascii_ws(label[len - 1]))
        len--;
    if (len == 0 || len >= sizeof key)
        return TC_ENC_NONE;
    for (i = 0; i < len; i++) {
        char c = label[i];
        if (c == '\0')
            return TC_ENC_NONE;
        key[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    key[len] = '\0';
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(key, tc_encoding_labels[mid].label);
        if (cmp == 0)
            return tc_encoding_labels[mid].encoding;
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return TC_ENC_NONE;
}

tc_encoding tc_encoding_sniff_bom(const void *data, size_t len, size_t *bom_len)
{
    const uint8_t *p = data;

    if (len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        *bom_len = 3;
        return TC_ENC_UTF_8;
    }
    if (len >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        *bom_len = 2;
        return TC_ENC_UTF_16BE;
    }
    if (len >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        *bom_len = 2;
        return TC_ENC_UTF_16LE;
    }
    *bom_len = 0;
    return TC_ENC_NONE;
}

static int put_cp(tc_buf *out, uint32_t cp)
{
    char bytes[4];
    return tc_buf_append(out, bytes, tc_utf8_encode(cp, bytes));
}

/* Copy the run of ASCII bytes starting at p[*i]; most of a page is ASCII. */
static int put_ascii_run(tc_buf *out, const uint8_t *p, size_t len, size_t *i)
{
    size_t end = *i;
    int err;

    while (end < len && p[end] < 0x80)
        end++;
    err = tc_buf_append(out, p + *i, end - *i);
    *i = end;
    return err;
}

static int decode_utf8(const uint8_t *p, size_t len, tc_buf *out)
{
    size_t i = 0;
    int err = TC_OK;

    while (i < len && err == TC_OK) {
        if (p[i] < 0x80) {
            err = put_ascii_run(out, p, len, &i);
        } else {
            uint32_t cp;
            i += tc_utf8_decode((const char *)p + i, len - i, &cp);
            err = put_cp(out, cp);
        }
    }
    return err;
}

static int decode_single_byte(const uint16_t *table, const uint8_t *p, size_t len, tc_buf *out)
{
    size_t i = 0;
    int err = TC_OK;

    while (i < len && err == TC_OK) {
        if (p[i] < 0x80) {
            err = put_ascii_run(out, p, len, &i);
        } else {
            uint16_t cp = table[p[i++] - 0x80];
            err = put_cp(out, cp ? cp : TC_REPLACEMENT_CHAR);
        }
    }
    return err;
}

static int decode_x_user_defined(const uint8_t *p, size_t len, tc_buf *out)
{
    size_t i = 0;
    int err = TC_OK;

    while (i < len && err == TC_OK) {
        if (p[i] < 0x80)
            err = put_ascii_run(out, p, len, &i);
        else
            err = put_cp(out, 0xF780u + p[i++] - 0x80);
    }
    return err;
}

static int decode_utf16(int big_endian, const uint8_t *p, size_t len, tc_buf *out)
{
    uint32_t lead = 0; /* a pending lead surrogate, or 0 */
    size_t i;
    int err = TC_OK;

    for (i = 0; i + 1 < len && err == TC_OK; i += 2) {
        uint32_t unit = big_endian ? (uint32_t)(p[i] << 8 | p[i + 1]) : (uint32_t)(p[i + 1] << 8 | p[i]);
        if (lead) {
            if (unit >= 0xDC00 && unit <= 0xDFFF) {
                err = put_cp(out, 0x10000 + ((lead - 0xD800) << 10) + (unit - 0xDC00));
                lead = 0;
                continue;
            }
            /* The lead surrogate was unpaired; this unit starts afresh. */
            lead = 0;
            err = put_cp(out, TC_REPLACEMENT_CHAR);
            if (err)
                break;
        }
        if (unit >= 0xD800 && unit <= 0xDBFF)
            lead = unit;
        else if (unit >= 0xDC00 && unit <= 0xDFFF)
            err = put_cp(out, TC_REPLACEMENT_CHAR);
        else
            err = put_cp(out, unit);
    }
    /* A dangling byte or lead surrogate at the end is one error. */
    if (err == TC_OK && (lead || (len & 1)))
        err = put_cp(out, TC_REPLACEMENT_CHAR);
    return err;
}

int tc_decode_no_bom(tc_encoding enc, const void *data, size_t len, tc_buf *out)
{
    const uint8_t *p = data;
    int err = tc_buf_reserve(out, len);

    if (err)
        return err;
    if (!tc_encoding_supported(enc))
        enc = TC_ENC_WINDOWS_1252;
    switch (enc) {
    case TC_ENC_UTF_8:
        return decode_utf8(p, len, out);
    case TC_ENC_UTF_16BE:
        return decode_utf16(1, p, len, out);
    case TC_ENC_UTF_16LE:
        return decode_utf16(0, p, len, out);
    case TC_ENC_X_USER_DEFINED:
        return decode_x_user_defined(p, len, out);
    case TC_ENC_REPLACEMENT:
        return len ? put_cp(out, TC_REPLACEMENT_CHAR) : TC_OK;
    default:
        return decode_single_byte(tc_encoding_single_byte[enc], p, len, out);
    }
}

int tc_decode(tc_encoding enc, const void *data, size_t len, tc_buf *out)
{
    size_t bom_len;
    tc_encoding bom = tc_encoding_sniff_bom(data, len, &bom_len);

    if (bom != TC_ENC_NONE)
        return tc_decode_no_bom(bom, (const uint8_t *)data + bom_len, len - bom_len, out);
    return tc_decode_no_bom(enc, data, len, out);
}
