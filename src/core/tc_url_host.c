/* Host parsing from the URL Standard: domains, IPv4, IPv6 and opaque hosts.
 *
 * Domain names go through "domain to ASCII", which the standard defines as
 * Unicode's UTS #46 processing. This file implements it in part: ASCII,
 * fullwidth forms, the various dots, common Latin/Greek/Cyrillic case mapping
 * and Punycode. Full UTS #46 (its mapping table, NFC and the bidi and joiner
 * rules) is on the backlog. */
#include "tc_url_internal.h"

#include <stdlib.h>
#include <string.h>

#include "tc_utf8.h"

/* ---- Code point arrays ---- */

typedef struct cplist {
    uint32_t *cp;
    size_t len, cap;
} cplist;

static int cp_push(cplist *l, uint32_t c)
{
    if (l->len == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 32;
        uint32_t *p = realloc(l->cp, cap * sizeof *p);
        if (!p)
            return TC_ERR_NOMEM;
        l->cp = p;
        l->cap = cap;
    }
    l->cp[l->len++] = c;
    return TC_OK;
}

static int cp_insert(cplist *l, size_t at, uint32_t c)
{
    int err = cp_push(l, 0);
    if (err)
        return err;
    memmove(l->cp + at + 1, l->cp + at, (l->len - 1 - at) * sizeof *l->cp);
    l->cp[at] = c;
    return TC_OK;
}

static void cp_free(cplist *l)
{
    free(l->cp);
    l->cp = NULL;
    l->len = l->cap = 0;
}

static int is_forbidden_host_cp(uint32_t c)
{
    switch (c) {
    case 0x00: case 0x09: case 0x0A: case 0x0D: case ' ': case '#': case '/': case ':':
    case '<': case '>': case '?': case '@': case '[': case '\\': case ']': case '^': case '|':
        return 1;
    }
    return 0;
}

static int is_forbidden_domain_cp(uint32_t c)
{
    return is_forbidden_host_cp(c) || c <= 0x1F || c == '%' || c == 0x7F;
}

/* ---- IPv4 ---- */

#define IPV4_SATURATE 0xFFFFFFFFFULL /* anything this big fails anyway */

/* One part of an IPv4 address: decimal, 0x-hex or 0-octal. 0 or -1. */
static int ipv4_number(const char *s, size_t len, uint64_t *value)
{
    uint64_t v = 0;
    int radix = 10;
    size_t i;

    if (len == 0)
        return -1;
    if (len >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        len -= 2;
        radix = 16;
    } else if (len >= 2 && s[0] == '0') {
        s++;
        len--;
        radix = 8;
    }
    for (i = 0; i < len; i++) {
        int d;
        if (radix == 16 && url_is_ascii_hex(s[i]))
            d = url_hex_value(s[i]);
        else if (url_is_ascii_digit(s[i]) && s[i] - '0' < radix)
            d = s[i] - '0';
        else
            return -1;
        v = v * (uint64_t)radix + (uint64_t)d;
        if (v > IPV4_SATURATE)
            v = IPV4_SATURATE;
    }
    *value = v;
    return 0;
}

/* The standard's "ends in a number" checker: does the last label look like
   an IPv4 number, making the whole host an IPv4 address? */
static int ends_in_number(const char *s, size_t len)
{
    size_t end = len, start, i;
    uint64_t v;
    int all_digits = 1;

    if (len == 0)
        return 0;
    if (s[len - 1] == '.')
        end = len - 1; /* the trailing empty label doesn't count */
    start = end;
    while (start > 0 && s[start - 1] != '.')
        start--;
    if (start == end)
        return 0;
    for (i = start; i < end; i++)
        if (!url_is_ascii_digit(s[i]))
            all_digits = 0;
    return all_digits || ipv4_number(s + start, end - start, &v) == 0;
}

static int parse_ipv4(const char *s, size_t len, tc_buf *out)
{
    uint64_t numbers[4], ipv4;
    size_t count = 0, start = 0, i;

    if (len > 0 && s[len - 1] == '.')
        len--; /* one trailing dot is allowed */
    for (i = 0; i <= len; i++) {
        if (i == len || s[i] == '.') {
            if (count == 4 || ipv4_number(s + start, i - start, &numbers[count]) != 0)
                return TC_ERR_URL;
            count++;
            start = i + 1;
        }
    }
    for (i = 0; i + 1 < count; i++)
        if (numbers[i] > 255)
            return TC_ERR_URL;
    if (numbers[count - 1] >= (1ULL << (8 * (5 - count))))
        return TC_ERR_URL;
    ipv4 = numbers[count - 1];
    for (i = 0; i + 1 < count; i++)
        ipv4 += numbers[i] << (8 * (3 - i));
    return tc_buf_printf(out, "%u.%u.%u.%u", (unsigned)(ipv4 >> 24) & 255, (unsigned)(ipv4 >> 16) & 255,
                         (unsigned)(ipv4 >> 8) & 255, (unsigned)ipv4 & 255);
}

/* ---- IPv6 ---- */

static int parse_ipv6(const char *s, size_t len, uint16_t address[8])
{
    size_t p = 0;
    int piece = 0, compress = -1;

#define C (p < len ? (unsigned char)s[p] : -1)
    memset(address, 0, 8 * sizeof address[0]);
    if (C == ':') {
        if (p + 1 >= len || s[p + 1] != ':')
            return -1;
        p += 2;
        compress = ++piece;
    }
    while (C != -1) {
        unsigned value = 0;
        int length = 0;

        if (piece == 8)
            return -1;
        if (C == ':') {
            if (compress != -1)
                return -1;
            p++;
            compress = ++piece;
            continue;
        }
        while (length < 4 && url_is_ascii_hex(C)) {
            value = value * 16 + (unsigned)url_hex_value(C);
            p++;
            length++;
        }
        if (C == '.') {
            int seen = 0;
            if (length == 0)
                return -1;
            p -= (size_t)length;
            if (piece > 6)
                return -1;
            while (C != -1) {
                int v = -1;
                if (seen > 0) {
                    if (C == '.' && seen < 4)
                        p++;
                    else
                        return -1;
                }
                if (!url_is_ascii_digit(C))
                    return -1;
                while (url_is_ascii_digit(C)) {
                    int n = C - '0';
                    if (v == -1)
                        v = n;
                    else if (v == 0)
                        return -1; /* no leading zeros */
                    else
                        v = v * 10 + n;
                    if (v > 255)
                        return -1;
                    p++;
                }
                address[piece] = (uint16_t)(address[piece] * 0x100 + v);
                seen++;
                if (seen == 2 || seen == 4)
                    piece++;
            }
            if (seen != 4)
                return -1;
            break;
        } else if (C == ':') {
            p++;
            if (C == -1)
                return -1;
        } else if (C != -1) {
            return -1;
        }
        address[piece++] = (uint16_t)value;
    }
#undef C
    if (compress != -1) {
        int swaps = piece - compress;
        piece = 7;
        while (piece != 0 && swaps > 0) {
            uint16_t t = address[piece];
            address[piece] = address[compress + swaps - 1];
            address[compress + swaps - 1] = t;
            piece--;
            swaps--;
        }
    } else if (piece != 8) {
        return -1;
    }
    return 0;
}

static int serialize_ipv6(const uint16_t address[8], tc_buf *out)
{
    int best = -1, best_len = 1, i, err = tc_buf_append_char(out, '[');
    int ignore0 = 0;

    /* Compress the first longest run of two or more zero pieces. */
    for (i = 0; i < 8;) {
        int j = i;
        while (j < 8 && address[j] == 0)
            j++;
        if (j - i > best_len) {
            best = i;
            best_len = j - i;
        }
        i = j > i ? j : i + 1;
    }
    for (i = 0; i < 8 && !err; i++) {
        if (ignore0 && address[i] == 0)
            continue;
        ignore0 = 0;
        if (i == best) {
            err = tc_buf_append_str(out, i == 0 ? "::" : ":");
            ignore0 = 1;
            continue;
        }
        err = tc_buf_printf(out, "%x", address[i]);
        if (!err && i != 7)
            err = tc_buf_append_char(out, ':');
    }
    return err ? err : tc_buf_append_char(out, ']');
}

/* ---- Punycode (RFC 3492) ---- */

enum { BASE = 36, TMIN = 1, TMAX = 26, SKEW = 38, DAMP = 700, INITIAL_BIAS = 72, INITIAL_N = 128 };

static uint32_t adapt(uint32_t delta, uint32_t points, int first)
{
    uint32_t k = 0;
    delta = first ? delta / DAMP : delta / 2;
    delta += delta / points;
    while (delta > ((BASE - TMIN) * TMAX) / 2) {
        delta /= BASE - TMIN;
        k += BASE;
    }
    return k + (BASE - TMIN + 1) * delta / (delta + SKEW);
}

static uint32_t threshold(uint32_t k, uint32_t bias)
{
    return k <= bias ? TMIN : k >= bias + TMAX ? TMAX : k - bias;
}

static int punycode_encode(const uint32_t *in, size_t len, tc_buf *out)
{
    uint32_t n = INITIAL_N, delta = 0, bias = INITIAL_BIAS, h, b = 0;
    size_t i;
    int err = 0;

    for (i = 0; i < len; i++)
        if (in[i] < 0x80) {
            err |= tc_buf_append_char(out, (char)in[i]);
            b++;
        }
    if (b > 0)
        err |= tc_buf_append_char(out, '-');
    for (h = b; h < len && !err;) {
        uint32_t m = 0xFFFFFFFFu;
        for (i = 0; i < len; i++)
            if (in[i] >= n && in[i] < m)
                m = in[i];
        if ((uint64_t)(m - n) * (h + 1) + delta > 0xFFFFFFFFu)
            return TC_ERR_URL;
        delta += (m - n) * (h + 1);
        n = m;
        for (i = 0; i < len; i++) {
            if (in[i] < n && ++delta == 0)
                return TC_ERR_URL;
            if (in[i] == n) {
                uint32_t q = delta, k;
                for (k = BASE;; k += BASE) {
                    uint32_t t = threshold(k, bias), d;
                    if (q < t)
                        break;
                    d = t + (q - t) % (BASE - t);
                    err |= tc_buf_append_char(out, (char)(d < 26 ? 'a' + d : '0' + d - 26));
                    q = (q - t) / (BASE - t);
                }
                err |= tc_buf_append_char(out, (char)(q < 26 ? 'a' + q : '0' + q - 26));
                bias = adapt(delta, h + 1, h == b);
                delta = 0;
                h++;
            }
        }
        delta++;
        n++;
    }
    return err ? TC_ERR_NOMEM : TC_OK;
}

static int punycode_decode(const char *in, size_t len, cplist *out)
{
    uint32_t n = INITIAL_N, i = 0, bias = INITIAL_BIAS;
    size_t b = 0, p, j;

    for (j = 0; j < len; j++)
        if (in[j] == '-')
            b = j;
    for (j = 0; j < b; j++)
        if ((unsigned char)in[j] >= 0x80 || cp_push(out, (unsigned char)in[j]))
            return TC_ERR_URL;
    for (p = b > 0 ? b + 1 : 0; p < len;) {
        uint32_t oldi = i, w = 1, k;
        for (k = BASE;; k += BASE) {
            uint32_t digit, t;
            char c;
            if (p >= len)
                return TC_ERR_URL;
            c = in[p++];
            digit = c >= '0' && c <= '9' ? (uint32_t)(c - '0' + 26)
                  : c >= 'a' && c <= 'z' ? (uint32_t)(c - 'a')
                  : c >= 'A' && c <= 'Z' ? (uint32_t)(c - 'A') : BASE;
            if (digit >= BASE || digit > (0xFFFFFFFFu - i) / w)
                return TC_ERR_URL;
            i += digit * w;
            t = threshold(k, bias);
            if (digit < t)
                break;
            if (w > 0xFFFFFFFFu / (BASE - t))
                return TC_ERR_URL;
            w *= BASE - t;
        }
        bias = adapt(i - oldi, (uint32_t)out->len + 1, oldi == 0);
        if (i / (out->len + 1) > 0x10FFFF - n)
            return TC_ERR_URL;
        n += i / (uint32_t)(out->len + 1);
        i %= (uint32_t)(out->len + 1);
        if ((n >= 0xD800 && n <= 0xDFFF) || cp_insert(out, i, n))
            return TC_ERR_URL;
        i++;
    }
    return TC_OK;
}

/* ---- Domain to ASCII ---- */

/* UTS #46 "ignored" code points: removed from domains. */
static int is_ignored(uint32_t c)
{
    return c == 0x00AD || c == 0x034F || (c >= 0x180B && c <= 0x180D) || c == 0x180F ||
           c == 0x200B || c == 0x2060 || c == 0x2064 || (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF ||
           (c >= 0x1BCA0 && c <= 0x1BCA3) || (c >= 0xE0100 && c <= 0xE01EF);
}

/* Case and compatibility mapping for the common cases. Returns 0 when the
   code point isn't allowed in a domain. */
static uint32_t map_cp(uint32_t c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if (c < 0x80)
        return c;
    if (c >= 0xFF01 && c <= 0xFF5E) /* fullwidth ASCII */
        return map_cp(c - 0xFEE0);
    if (c == 0x3002 || c == 0xFF61)  /* ideographic full stops */
        return '.';
    if (c == 0xA0 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000)
        return ' '; /* compatibility spaces become a (forbidden) space */
    if (c >= 0x1D400 && c <= 0x1D6A3) /* mathematical letters, e.g. bold A */
        return (c - 0x1D400) % 52 < 26 ? 'a' + (c - 0x1D400) % 52 : 'a' + (c - 0x1D400) % 52 - 26;
    if (c >= 0x1D7CE && c <= 0x1D7FF) /* mathematical digits */
        return '0' + (c - 0x1D7CE) % 10;
    if ((c >= 0xFDD0 && c <= 0xFDEF) || (c & 0xFFFE) == 0xFFFE)
        return 0; /* noncharacters */
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) ||
        (c >= 0x410 && c <= 0x42F))
        return c + 32;
    if (c >= 0x400 && c <= 0x40F)
        return c + 80;
    if (c >= 0x100 && c <= 0x17F) {
        /* Latin Extended-A alternates uppercase/lowercase, with exceptions. */
        if (c == 0x130 || c == 0x149)
            return 0; /* map to two code points; not handled here */
        if (c == 0x178)
            return 0xFF;
        if (c == 0x17F)
            return 's';
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
            return (c & 1) ? c + 1 : c;
        if (c != 0x138)
            return (c & 1) ? c : c + 1;
        return c;
    }
    if (c < 0xA0 || c == TC_REPLACEMENT_CHAR || (c >= 0xD800 && c <= 0xDFFF) ||
        c == 0x200C || c == 0x200D || (c >= 0xFFF0 && c <= 0xFFFF))
        return 0; /* controls, errors, joiners: not allowed */
    return c;
}

static int is_all_ascii(const uint32_t *cp, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
        if (cp[i] >= 0x80)
            return 0;
    return 1;
}

static int domain_to_ascii(const cplist *domain, tc_buf *out)
{
    cplist mapped = { 0 }, label = { 0 };
    size_t i, start;
    int err = TC_OK;

    if (is_all_ascii(domain->cp, domain->len)) {
        /* The standard's fast path: an ASCII domain is only lowercased, even
           if it has "xn--" labels. */
        for (i = 0; i < domain->len && !err; i++)
            err = tc_buf_append_char(out, (char)map_cp(domain->cp[i]));
        goto check;
    }
    for (i = 0; i < domain->len && !err; i++) {
        uint32_t c;
        if (is_ignored(domain->cp[i]))
            continue;
        c = map_cp(domain->cp[i]);
        err = c ? cp_push(&mapped, c) : TC_ERR_URL;
    }
    for (start = 0; !err && start <= mapped.len;) {
        size_t end = start;
        while (end < mapped.len && mapped.cp[end] != '.')
            end++;
        if (start > 0)
            err = tc_buf_append_char(out, '.');
        if (!err && end - start >= 4 && mapped.cp[start] == 'x' && mapped.cp[start + 1] == 'n' &&
            mapped.cp[start + 2] == '-' && mapped.cp[start + 3] == '-') {
            /* A Punycode label must decode to something that isn't plain
               ASCII and needs no mapping. */
            char ascii[256];
            size_t n = end - start - 4, k;
            label.len = 0;
            if (n >= sizeof ascii || !is_all_ascii(mapped.cp + start, end - start))
                err = TC_ERR_URL;
            for (k = 0; !err && k < n; k++)
                ascii[k] = (char)mapped.cp[start + 4 + k];
            if (!err)
                err = punycode_decode(ascii, n, &label);
            if (!err && (label.len == 0 || is_all_ascii(label.cp, label.len)))
                err = TC_ERR_URL;
            for (k = 0; !err && k < label.len; k++)
                if (is_ignored(label.cp[k]) || map_cp(label.cp[k]) != label.cp[k] || label.cp[k] == '.')
                    err = TC_ERR_URL;
            for (k = start; !err && k < end; k++)
                err = tc_buf_append_char(out, (char)mapped.cp[k]);
        } else if (!err && !is_all_ascii(mapped.cp + start, end - start)) {
            err = tc_buf_append_str(out, "xn--");
            if (!err)
                err = punycode_encode(mapped.cp + start, end - start, out);
        } else {
            for (i = start; !err && i < end; i++)
                err = tc_buf_append_char(out, (char)mapped.cp[i]);
        }
        start = end + 1;
    }
    cp_free(&mapped);
    cp_free(&label);
check:
    if (err)
        return err;
    if (out->len == 0)
        return TC_ERR_URL;
    for (i = 0; i < out->len; i++)
        if (is_forbidden_domain_cp((unsigned char)out->data[i]))
            return TC_ERR_URL;
    return TC_OK;
}

/* ---- Host parser ---- */

static int parse_opaque_host(const char *s, size_t len, tc_buf *out)
{
    size_t i;
    int err = TC_OK;
    for (i = 0; i < len; i++)
        if (is_forbidden_host_cp((unsigned char)s[i]))
            return TC_ERR_URL;
    for (i = 0; i < len && !err; i++)
        err = url_append_encoded(out, (unsigned char)s[i], SET_C0);
    return err;
}

int tc_url_parse_host(const char *s, size_t len, int opaque, tc_buf *out)
{
    tc_buf decoded, ascii;
    cplist domain = { 0 };
    size_t i;
    int err;

    tc_buf_clear(out);
    if (len > 0 && s[0] == '[') {
        uint16_t address[8];
        if (s[len - 1] != ']' || len < 2 || parse_ipv6(s + 1, len - 2, address) != 0)
            return TC_ERR_URL;
        return serialize_ipv6(address, out);
    }
    if (opaque)
        return parse_opaque_host(s, len, out);
    if (len == 0)
        return TC_ERR_URL;

    tc_buf_init(&decoded);
    tc_buf_init(&ascii);
    err = url_percent_decode(s, len, &decoded);
    for (i = 0; !err && i < decoded.len;) {
        uint32_t c;
        i += tc_utf8_decode(decoded.data + i, decoded.len - i, &c);
        err = cp_push(&domain, c);
    }
    if (!err)
        err = domain_to_ascii(&domain, &ascii);
    if (!err) {
        if (ends_in_number(ascii.data, ascii.len))
            err = parse_ipv4(ascii.data, ascii.len, out);
        else
            err = tc_buf_append(out, ascii.data, ascii.len);
    }
    cp_free(&domain);
    tc_buf_free(&decoded);
    tc_buf_free(&ascii);
    return err;
}
