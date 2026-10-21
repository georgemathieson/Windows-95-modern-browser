#include "tc_html_sniff.h"

#include <string.h>

/* Attribute names and values longer than this can't matter: the prescan
   only looks at 1024 bytes. */
#define ATTR_MAX 1024

typedef struct scan {
    const uint8_t *p;
    size_t len, pos;
} scan;

typedef struct attr {
    char name[ATTR_MAX], value[ATTR_MAX];
    size_t name_len, value_len;
} attr;

static int is_space(uint8_t b)
{
    return b == 0x09 || b == 0x0A || b == 0x0C || b == 0x0D || b == 0x20;
}

static uint8_t lower(uint8_t b)
{
    return (uint8_t)(b >= 'A' && b <= 'Z' ? b + 0x20 : b);
}

static void append(char *buf, size_t *len, uint8_t b)
{
    if (*len < ATTR_MAX - 1)
        buf[(*len)++] = (char)lower(b);
}

/* "Get an attribute". 1: got one; 0: there isn't one; -1: ran out of bytes,
   which aborts the whole prescan. */
static int get_attribute(scan *s, attr *a)
{
    const uint8_t *p = s->p;

    a->name_len = a->value_len = 0;
    for (;;) {
        if (s->pos >= s->len)
            return -1;
        if (!is_space(p[s->pos]) && p[s->pos] != '/')
            break;
        s->pos++;
    }
    if (p[s->pos] == '>')
        return 0;
    for (;;) {
        uint8_t b;
        if (s->pos >= s->len)
            return -1;
        b = p[s->pos];
        if (b == '=' && a->name_len > 0) {
            s->pos++;
            goto value;
        }
        if (is_space(b))
            break;
        if (b == '/' || b == '>')
            return 1;
        append(a->name, &a->name_len, b);
        s->pos++;
    }
    /* Spaces */
    for (;;) {
        if (s->pos >= s->len)
            return -1;
        if (!is_space(p[s->pos]))
            break;
        s->pos++;
    }
    if (p[s->pos] != '=')
        return 1;
    s->pos++;
value:
    for (;;) {
        if (s->pos >= s->len)
            return -1;
        if (!is_space(p[s->pos]))
            break;
        s->pos++;
    }
    if (p[s->pos] == '"' || p[s->pos] == '\'') {
        uint8_t quote = p[s->pos];
        for (;;) {
            s->pos++;
            if (s->pos >= s->len)
                return -1;
            if (p[s->pos] == quote) {
                s->pos++;
                return 1;
            }
            append(a->value, &a->value_len, p[s->pos]);
        }
    }
    if (p[s->pos] == '>')
        return 1;
    append(a->value, &a->value_len, p[s->pos]);
    s->pos++;
    for (;;) {
        uint8_t b;
        if (s->pos >= s->len)
            return -1;
        b = p[s->pos];
        if (is_space(b) || b == '>')
            return 1;
        append(a->value, &a->value_len, b);
        s->pos++;
    }
}

static int find(const uint8_t *p, size_t from, size_t to, const char *what, size_t *at)
{
    size_t n = strlen(what), i;
    for (i = from; i + n <= to; i++) {
        if (memcmp(p + i, what, n) == 0) {
            *at = i;
            return 1;
        }
    }
    return 0;
}

/* "Get an XML encoding". */
static tc_encoding xml_encoding(const uint8_t *p, size_t len)
{
    size_t end, pos, start, close;
    tc_encoding enc;

    if (len < 5 || memcmp(p, "<?xml", 5) != 0)
        return TC_ENC_NONE;
    for (end = 0; end < len && p[end] != '>'; end++)
        ;
    if (end == len || !find(p, 0, end, "encoding", &pos))
        return TC_ENC_NONE;
    pos += 8;
    while (pos < len && p[pos] <= 0x20)
        pos++;
    if (pos >= len || p[pos] != '=')
        return TC_ENC_NONE;
    pos++;
    while (pos < len && p[pos] <= 0x20)
        pos++;
    if (pos >= len || (p[pos] != '"' && p[pos] != '\''))
        return TC_ENC_NONE;
    start = ++pos;
    for (close = start; close < len && p[close] != p[start - 1]; close++)
        if (p[close] <= 0x20)
            return TC_ENC_NONE;
    if (close >= len)
        return TC_ENC_NONE;
    enc = tc_encoding_from_label((const char *)p + start, close - start);
    if (enc == TC_ENC_UTF_16BE || enc == TC_ENC_UTF_16LE)
        enc = TC_ENC_UTF_8;
    return enc;
}

/* Handle one <meta>: 1 and *out if it declares an encoding, 0 if not, -1 if
   the bytes ran out. */
static int prescan_meta(scan *s, tc_encoding *out)
{
    attr a;
    int seen_http_equiv = 0, seen_content = 0, seen_charset = 0;
    int got_pragma = 0, need_pragma = -1; /* -1: null */
    int have_charset = 0;                  /* charset isn't null */
    tc_encoding charset = TC_ENC_NONE;

    for (;;) {
        int r = get_attribute(s, &a);
        if (r < 0)
            return -1;
        if (r == 0)
            break;
        a.name[a.name_len] = a.value[a.value_len] = '\0';
        if (strcmp(a.name, "http-equiv") == 0) {
            if (seen_http_equiv++)
                continue;
            if (strcmp(a.value, "content-type") == 0)
                got_pragma = 1;
        } else if (strcmp(a.name, "content") == 0) {
            tc_encoding enc;
            if (seen_content++)
                continue;
            enc = tc_html_meta_charset(a.value, a.value_len);
            if (enc != TC_ENC_NONE && !have_charset) {
                charset = enc;
                have_charset = 1;
                need_pragma = 1;
            }
        } else if (strcmp(a.name, "charset") == 0) {
            if (seen_charset++)
                continue;
            charset = tc_encoding_from_label(a.value, a.value_len);
            have_charset = 1;
            need_pragma = 0;
        }
    }
    if (need_pragma < 0 || (need_pragma == 1 && !got_pragma) || charset == TC_ENC_NONE)
        return 0;
    if (charset == TC_ENC_UTF_16BE || charset == TC_ENC_UTF_16LE)
        charset = TC_ENC_UTF_8;
    else if (charset == TC_ENC_X_USER_DEFINED)
        charset = TC_ENC_WINDOWS_1252;
    *out = charset;
    return 1;
}

static int is_alpha(uint8_t b)
{
    return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
}

tc_encoding tc_html_prescan(const void *data, size_t len)
{
    scan s;
    const uint8_t *p = data;

    s.p = p;
    s.len = len;
    s.pos = 0;
    if (len >= 6 && memcmp(p, "<\0?\0x\0", 6) == 0)
        return TC_ENC_UTF_16LE;
    if (len >= 6 && memcmp(p, "\0<\0?\0x", 6) == 0)
        return TC_ENC_UTF_16BE;

    for (; s.pos < len; s.pos++) {
        size_t rest = len - s.pos;
        const uint8_t *q = p + s.pos;
        if (q[0] != '<')
            continue;
        if (rest >= 4 && memcmp(q, "<!--", 4) == 0) {
            /* To the '>' of the first "-->"; its dashes may be the ones in "<!--". */
            size_t k = s.pos + 4;
            while (k < len && !(p[k] == '>' && p[k - 1] == '-' && p[k - 2] == '-'))
                k++;
            if (k >= len)
                break;
            s.pos = k;
        } else if (rest >= 6 && lower(q[1]) == 'm' && lower(q[2]) == 'e' && lower(q[3]) == 't' &&
                   lower(q[4]) == 'a' && (is_space(q[5]) || q[5] == '/')) {
            tc_encoding enc;
            int r;
            s.pos += 5;
            r = prescan_meta(&s, &enc);
            if (r < 0)
                break;
            if (r > 0)
                return enc;
        } else if ((rest >= 2 && is_alpha(q[1])) || (rest >= 3 && q[1] == '/' && is_alpha(q[2]))) {
            attr a;
            int r;
            while (s.pos < len && !is_space(p[s.pos]) && p[s.pos] != '>')
                s.pos++;
            while ((r = get_attribute(&s, &a)) > 0)
                ;
            if (r < 0)
                break;
        } else if (rest >= 2 && (q[1] == '!' || q[1] == '/' || q[1] == '?')) {
            size_t k = s.pos + 1;
            while (k < len && p[k] != '>')
                k++;
            if (k >= len)
                break;
            s.pos = k;
        }
    }
    return xml_encoding(p, len);
}

tc_encoding tc_html_meta_charset(const char *content, size_t len)
{
    size_t pos = 0;

    for (;;) {
        size_t i, start;
        for (i = pos; i + 7 <= len; i++) {
            size_t j;
            for (j = 0; j < 7 && lower((uint8_t)content[i + j]) == (uint8_t)"charset"[j]; j++)
                ;
            if (j == 7)
                break;
        }
        if (i + 7 > len)
            return TC_ENC_NONE;
        pos = i + 7;
        while (pos < len && is_space((uint8_t)content[pos]))
            pos++;
        if (pos >= len || content[pos] != '=')
            continue;
        pos++;
        while (pos < len && is_space((uint8_t)content[pos]))
            pos++;
        if (pos >= len)
            return TC_ENC_NONE;
        if (content[pos] == '"' || content[pos] == '\'') {
            const char *close = memchr(content + pos + 1, content[pos], len - pos - 1);
            if (!close)
                return TC_ENC_NONE;
            return tc_encoding_from_label(content + pos + 1, (size_t)(close - content) - pos - 1);
        }
        start = pos;
        while (pos < len && !is_space((uint8_t)content[pos]) && content[pos] != ';')
            pos++;
        return tc_encoding_from_label(content + start, pos - start);
    }
}

tc_encoding tc_html_sniff(const void *data, size_t len, const char *http_charset, tc_confidence *confidence,
                          tc_encoding_source *source)
{
    size_t bom_len;
    tc_encoding enc = tc_encoding_sniff_bom(data, len, &bom_len);

    *confidence = TC_CONFIDENCE_CERTAIN;
    if (enc != TC_ENC_NONE) {
        *source = TC_ENCSRC_BOM;
        return enc;
    }
    if (http_charset && *http_charset) {
        enc = tc_encoding_from_label(http_charset, strlen(http_charset));
        if (enc != TC_ENC_NONE) {
            *source = TC_ENCSRC_HTTP;
            return enc;
        }
    }
    *confidence = TC_CONFIDENCE_TENTATIVE;
    enc = tc_html_prescan(data, TC_MIN(len, (size_t)TC_PRESCAN_BYTES));
    if (enc != TC_ENC_NONE) {
        *source = TC_ENCSRC_PRESCAN;
        return enc;
    }
    *source = TC_ENCSRC_DEFAULT;
    return TC_ENC_WINDOWS_1252;
}
