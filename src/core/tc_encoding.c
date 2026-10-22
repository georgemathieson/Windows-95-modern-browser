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
    return enc >= 0 && enc < TC_ENC_COUNT;
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

/* ---- Chinese, Japanese and Korean ----
 *
 * Each follows its decoder in the Encoding Standard step by step. Where the
 * standard restores bytes to the input, they're always the last ones read,
 * so the decoder steps back over them. */

#define NO_POINTER 0xFFFFFFFFu

static uint32_t gb18030_ranges_code_point(uint32_t pointer)
{
    const uint32_t *r = tc_encoding_gb18030_ranges;
    size_t lo = 0, hi = TC_ARRAY_LEN(tc_encoding_gb18030_ranges) / 2;
    if ((pointer > 39419 && pointer < 189000) || pointer > 1237575)
        return 0;
    if (pointer == 7457)
        return 0xE7C7;
    while (hi - lo > 1) { /* the last range starting at or before pointer */
        size_t mid = lo + (hi - lo) / 2;
        if (r[2 * mid] <= pointer)
            lo = mid;
        else
            hi = mid;
    }
    return r[2 * lo + 1] + pointer - r[2 * lo];
}

/* GBK's decoder is gb18030's. */
static int decode_gb18030(const uint8_t *p, size_t len, tc_buf *out)
{
    uint32_t first = 0, second = 0, third = 0, cp;
    size_t i = 0;
    int err = TC_OK;

    while (i < len && err == TC_OK) {
        uint32_t b = p[i++];
        if (third) {
            if (b < 0x30 || b > 0x39) {
                i -= 3; /* second, third and b again */
                first = second = third = 0;
                err = put_cp(out, TC_REPLACEMENT_CHAR);
                continue;
            }
            cp = gb18030_ranges_code_point((first - 0x81) * 12600 + (second - 0x30) * 1260 + (third - 0x81) * 10 +
                                           b - 0x30);
            first = second = third = 0;
            err = put_cp(out, cp ? cp : TC_REPLACEMENT_CHAR);
        } else if (second) {
            if (b >= 0x81 && b <= 0xFE) {
                third = b;
                continue;
            }
            i -= 2; /* second and b again */
            first = second = 0;
            err = put_cp(out, TC_REPLACEMENT_CHAR);
        } else if (first) {
            uint32_t lead = first, pointer = NO_POINTER;
            if (b >= 0x30 && b <= 0x39) {
                second = b;
                continue;
            }
            first = 0;
            if ((b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFE))
                pointer = (lead - 0x81) * 190 + b - (b < 0x7F ? 0x40 : 0x41);
            cp = pointer != NO_POINTER ? tc_encoding_gb18030[pointer] : 0;
            if (!cp && b < 0x80)
                i--;
            err = put_cp(out, cp ? cp : TC_REPLACEMENT_CHAR);
        } else if (b < 0x80) {
            i--;
            err = put_ascii_run(out, p, len, &i);
        } else if (b == 0x80) {
            err = put_cp(out, 0x20AC);
        } else if (b <= 0xFE) {
            first = b;
        } else {
            err = put_cp(out, TC_REPLACEMENT_CHAR);
        }
    }
    if (err == TC_OK && (first || second || third))
        err = put_cp(out, TC_REPLACEMENT_CHAR);
    return err;
}

static uint32_t big5_code_point(uint32_t pointer)
{
    uint32_t cp = tc_encoding_big5[pointer];
    return tc_encoding_big5_plane2[pointer / 8] >> (pointer % 8) & 1 ? 0x20000 | cp : cp;
}

static int decode_big5(const uint8_t *p, size_t len, tc_buf *out)
{
    uint32_t lead = 0, cp;
    size_t i = 0;
    int err = TC_OK;

    while (i < len && err == TC_OK) {
        uint32_t b = p[i++];
        if (lead) {
            uint32_t pointer = NO_POINTER;
            if ((b >= 0x40 && b <= 0x7E) || (b >= 0xA1 && b <= 0xFE))
                pointer = (lead - 0x81) * 157 + b - (b < 0x7F ? 0x40 : 0x62);
            lead = 0;
            if (pointer == 1133 || pointer == 1135 || pointer == 1164 || pointer == 1166) {
                /* Two code points each: Ê or ê with a macron or caron. */
                err = put_cp(out, pointer < 1164 ? 0xCA : 0xEA);
                if (err == TC_OK)
                    err = put_cp(out, pointer == 1133 || pointer == 1164 ? 0x304 : 0x30C);
                continue;
            }
            cp = pointer != NO_POINTER ? big5_code_point(pointer) : 0;
            if (!cp && b < 0x80)
                i--;
            err = put_cp(out, cp ? cp : TC_REPLACEMENT_CHAR);
        } else if (b < 0x80) {
            i--;
            err = put_ascii_run(out, p, len, &i);
        } else if (b >= 0x81 && b <= 0xFE) {
            lead = b;
        } else {
            err = put_cp(out, TC_REPLACEMENT_CHAR);
        }
    }
    if (err == TC_OK && lead)
        err = put_cp(out, TC_REPLACEMENT_CHAR);
    return err;
}

static int decode_euc_jp(const uint8_t *p, size_t len, tc_buf *out)
{
    uint32_t lead = 0, cp;
    int jis0212 = 0;
    size_t i = 0;
    int err = TC_OK;

    while (i < len && err == TC_OK) {
        uint32_t b = p[i++];
        if (lead == 0x8E && b >= 0xA1 && b <= 0xDF) {
            lead = 0;
            err = put_cp(out, 0xFF61 - 0xA1 + b);
        } else if (lead == 0x8F && b >= 0xA1 && b <= 0xFE) {
            jis0212 = 1;
            lead = b;
        } else if (lead) {
            cp = 0;
            if (lead >= 0xA1 && lead <= 0xFE && b >= 0xA1 && b <= 0xFE) {
                uint32_t pointer = (lead - 0xA1) * 94 + b - 0xA1;
                if (!jis0212)
                    cp = tc_encoding_jis0208[pointer];
                else if (pointer < TC_ARRAY_LEN(tc_encoding_jis0212))
                    cp = tc_encoding_jis0212[pointer];
            }
            lead = 0;
            jis0212 = 0;
            if (!cp && b < 0x80)
                i--;
            err = put_cp(out, cp ? cp : TC_REPLACEMENT_CHAR);
        } else if (b < 0x80) {
            i--;
            err = put_ascii_run(out, p, len, &i);
        } else if (b == 0x8E || b == 0x8F || (b >= 0xA1 && b <= 0xFE)) {
            lead = b;
        } else {
            err = put_cp(out, TC_REPLACEMENT_CHAR);
        }
    }
    if (err == TC_OK && lead)
        err = put_cp(out, TC_REPLACEMENT_CHAR);
    return err;
}

enum { JIS_ASCII, JIS_ROMAN, JIS_KATAKANA, JIS_LEAD, JIS_TRAIL, JIS_ESCAPE_START, JIS_ESCAPE };

static int decode_iso_2022_jp(const uint8_t *p, size_t len, tc_buf *out)
{
    int state = JIS_ASCII, output_state = JIS_ASCII, output = 0, err = TC_OK;
    uint32_t lead = 0;
    size_t i = 0;

    while (err == TC_OK) {
        int b = i < len ? p[i++] : -1; /* -1: the end of the input */
        int error = 0;
        switch (state) {
        case JIS_ASCII:
        case JIS_ROMAN:
            if (b == 0x1B) {
                state = JIS_ESCAPE_START;
                continue;
            }
            if (b < 0)
                return TC_OK;
            output = 0;
            if (state == JIS_ROMAN && b == 0x5C)
                err = put_cp(out, 0xA5);
            else if (state == JIS_ROMAN && b == 0x7E)
                err = put_cp(out, 0x203E);
            else if (b <= 0x7F && b != 0x0E && b != 0x0F)
                err = put_cp(out, (uint32_t)b);
            else
                error = 1;
            break;
        case JIS_KATAKANA:
            if (b == 0x1B) {
                state = JIS_ESCAPE_START;
                continue;
            }
            if (b < 0)
                return TC_OK;
            output = 0;
            if (b >= 0x21 && b <= 0x5F)
                err = put_cp(out, 0xFF61 - 0x21 + (uint32_t)b);
            else
                error = 1;
            break;
        case JIS_LEAD:
            if (b == 0x1B) {
                state = JIS_ESCAPE_START;
                continue;
            }
            if (b < 0)
                return TC_OK;
            output = 0;
            if (b >= 0x21 && b <= 0x7E) {
                lead = (uint32_t)b;
                state = JIS_TRAIL;
            } else {
                error = 1;
            }
            break;
        case JIS_TRAIL:
            if (b == 0x1B) {
                state = JIS_ESCAPE_START;
                error = 1;
            } else if (b >= 0x21 && b <= 0x7E) {
                uint32_t cp = tc_encoding_jis0208[(lead - 0x21) * 94 + (uint32_t)b - 0x21];
                state = JIS_LEAD;
                if (cp)
                    err = put_cp(out, cp);
                else
                    error = 1;
            } else {
                state = JIS_LEAD; /* the end of the input too: an error, then finished */
                error = 1;
            }
            break;
        case JIS_ESCAPE_START:
            if (b == 0x24 || b == 0x28) {
                lead = (uint32_t)b;
                state = JIS_ESCAPE;
                continue;
            }
            if (b >= 0)
                i--;
            output = 0;
            state = output_state;
            error = 1;
            break;
        case JIS_ESCAPE: {
            int next = -1;
            if (lead == 0x28 && b == 0x42)
                next = JIS_ASCII;
            else if (lead == 0x28 && b == 0x4A)
                next = JIS_ROMAN;
            else if (lead == 0x28 && b == 0x49)
                next = JIS_KATAKANA;
            else if (lead == 0x24 && (b == 0x40 || b == 0x42))
                next = JIS_LEAD;
            lead = 0;
            if (next >= 0) {
                /* Two escapes in a row with nothing between are an error. */
                state = output_state = next;
                error = output;
                output = 1;
                break;
            }
            i -= b < 0 ? 1 : 2; /* the escape's second byte, and b, again */
            output = 0;
            state = output_state;
            error = 1;
            break;
        }
        }
        if (error && err == TC_OK)
            err = put_cp(out, TC_REPLACEMENT_CHAR);
    }
    return err;
}

static int decode_shift_jis(const uint8_t *p, size_t len, tc_buf *out)
{
    uint32_t lead = 0, cp;
    size_t i = 0;
    int err = TC_OK;

    while (i < len && err == TC_OK) {
        uint32_t b = p[i++];
        if (lead) {
            uint32_t pointer = NO_POINTER;
            if ((b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFC))
                pointer = (lead - (lead < 0xA0 ? 0x81 : 0xC1)) * 188 + b - (b < 0x7F ? 0x40 : 0x41);
            lead = 0;
            if (pointer >= 8836 && pointer <= 10715) { /* Windows' user-defined characters */
                err = put_cp(out, 0xE000 - 8836 + pointer);
                continue;
            }
            cp = pointer < TC_ARRAY_LEN(tc_encoding_jis0208) ? tc_encoding_jis0208[pointer] : 0;
            if (!cp && b < 0x80)
                i--;
            err = put_cp(out, cp ? cp : TC_REPLACEMENT_CHAR);
        } else if (b < 0x80) {
            i--;
            err = put_ascii_run(out, p, len, &i);
        } else if (b == 0x80) {
            err = put_cp(out, 0x80);
        } else if (b >= 0xA1 && b <= 0xDF) {
            err = put_cp(out, 0xFF61 - 0xA1 + b);
        } else if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) {
            lead = b;
        } else {
            err = put_cp(out, TC_REPLACEMENT_CHAR);
        }
    }
    if (err == TC_OK && lead)
        err = put_cp(out, TC_REPLACEMENT_CHAR);
    return err;
}

static int decode_euc_kr(const uint8_t *p, size_t len, tc_buf *out)
{
    uint32_t lead = 0, cp;
    size_t i = 0;
    int err = TC_OK;

    while (i < len && err == TC_OK) {
        uint32_t b = p[i++];
        if (lead) {
            uint32_t pointer = b >= 0x41 && b <= 0xFE ? (lead - 0x81) * 190 + b - 0x41 : NO_POINTER;
            lead = 0;
            cp = pointer < TC_ARRAY_LEN(tc_encoding_euc_kr) ? tc_encoding_euc_kr[pointer] : 0;
            if (!cp && b < 0x80)
                i--;
            err = put_cp(out, cp ? cp : TC_REPLACEMENT_CHAR);
        } else if (b < 0x80) {
            i--;
            err = put_ascii_run(out, p, len, &i);
        } else if (b >= 0x81 && b <= 0xFE) {
            lead = b;
        } else {
            err = put_cp(out, TC_REPLACEMENT_CHAR);
        }
    }
    if (err == TC_OK && lead)
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
    case TC_ENC_GBK:
    case TC_ENC_GB18030:
        return decode_gb18030(p, len, out);
    case TC_ENC_BIG5:
        return decode_big5(p, len, out);
    case TC_ENC_EUC_JP:
        return decode_euc_jp(p, len, out);
    case TC_ENC_ISO_2022_JP:
        return decode_iso_2022_jp(p, len, out);
    case TC_ENC_SHIFT_JIS:
        return decode_shift_jis(p, len, out);
    case TC_ENC_EUC_KR:
        return decode_euc_kr(p, len, out);
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
