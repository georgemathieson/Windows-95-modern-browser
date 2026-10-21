#include "tc_mime.h"

#include <string.h>

static int is_http_ws(char c)
{
    return c == '\t' || c == '\n' || c == '\r' || c == ' ';
}

static int is_token_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           (c && strchr("!#$%&'*+-.^_`|~", c));
}

/* Quoted-string token code points; bytes 0x80-0xFF stand for U+0080-U+00FF
   (headers are isomorphic-decoded). */
static int is_quoted_char(char c)
{
    unsigned char u = (unsigned char)c;
    return u == '\t' || (u >= 0x20 && u != 0x7F);
}

static int all(const char *s, size_t len, int (*ok)(char))
{
    size_t i;
    for (i = 0; i < len; i++)
        if (!ok(s[i]))
            return 0;
    return 1;
}

static void lower_copy(char *dst, const char *src, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
        dst[i] = (char)(src[i] >= 'A' && src[i] <= 'Z' ? src[i] + 32 : src[i]);
    dst[len] = '\0';
}

int tc_mime_parse(const char *s, tc_mime *out)
{
    const char *end, *p, *type, *sub;
    size_t type_len, sub_len;
    int have_charset = 0;

    out->essence[0] = out->charset[0] = '\0';
    end = s + strlen(s);
    while (s < end && is_http_ws(*s))
        s++;
    while (end > s && is_http_ws(end[-1]))
        end--;

    type = p = s;
    while (p < end && *p != '/')
        p++;
    type_len = (size_t)(p - type);
    if (type_len == 0 || !all(type, type_len, is_token_char) || p >= end)
        return TC_ERR_INVALID;
    sub = ++p;
    while (p < end && *p != ';')
        p++;
    sub_len = (size_t)(p - sub);
    while (sub_len && is_http_ws(sub[sub_len - 1]))
        sub_len--;
    if (sub_len == 0 || !all(sub, sub_len, is_token_char) || type_len + 1 + sub_len >= sizeof out->essence)
        return TC_ERR_INVALID;
    lower_copy(out->essence, type, type_len);
    out->essence[type_len] = '/';
    lower_copy(out->essence + type_len + 1, sub, sub_len);

    while (p < end) {
        const char *name, *value;
        size_t name_len, value_len = 0;
        char quoted[sizeof out->charset];
        int fits = 1;

        p++; /* past ';' */
        while (p < end && is_http_ws(*p))
            p++;
        name = p;
        while (p < end && *p != ';' && *p != '=')
            p++;
        name_len = (size_t)(p - name);
        if (p < end) {
            if (*p == ';')
                continue;
            p++; /* past '=' */
        }
        if (p >= end)
            break;
        if (*p == '"') {
            /* Collect an HTTP quoted string, keeping only the value. */
            p++;
            while (p < end) {
                char c = *p++;
                if (c == '"')
                    break;
                if (c == '\\') {
                    if (p >= end) {
                        c = '\\';
                    } else {
                        c = *p++;
                    }
                }
                if (value_len + 1 < sizeof quoted)
                    quoted[value_len++] = c;
                else
                    fits = 0;
            }
            value = quoted;
            while (p < end && *p != ';')
                p++;
        } else {
            value = p;
            while (p < end && *p != ';')
                p++;
            value_len = (size_t)(p - value);
            while (value_len && is_http_ws(value[value_len - 1]))
                value_len--;
            if (value_len == 0)
                continue;
        }
        if (name_len == 7 && !have_charset && all(name, name_len, is_token_char) &&
            all(value, value_len, is_quoted_char)) {
            char lname[8];
            lower_copy(lname, name, name_len);
            if (strcmp(lname, "charset") == 0) {
                have_charset = 1;
                if (fits && value_len < sizeof out->charset) {
                    memcpy(out->charset, value, value_len);
                    out->charset[value_len] = '\0';
                }
            }
        }
    }
    return TC_OK;
}

int tc_mime_is_html(const tc_mime *m)
{
    return strcmp(m->essence, "text/html") == 0;
}

int tc_mime_sniff_html(const void *data, size_t len)
{
    static const char *const patterns[] = {
        "<!doctype html", "<html", "<head", "<script", "<iframe", "<h1", "<div", "<font", "<table", "<a",
        "<style", "<title", "<b", "<body", "<br", "<p", "<!--"
    };
    const unsigned char *p = data;
    size_t i, start = 0;

    while (start < len && (p[start] == 0x09 || p[start] == 0x0A || p[start] == 0x0C || p[start] == 0x0D ||
                           p[start] == 0x20))
        start++;
    for (i = 0; i < TC_ARRAY_LEN(patterns); i++) {
        size_t n = strlen(patterns[i]), k;
        if (len - start < n + 1)
            continue;
        for (k = 0; k < n; k++) {
            unsigned char c = p[start + k];
            if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != (unsigned char)patterns[i][k])
                break;
        }
        /* A tag-terminating byte must follow (space or '>'). */
        if (k == n && (p[start + n] == 0x20 || p[start + n] == 0x3E))
            return 1;
    }
    return 0;
}

int tc_mime_wants_html(const char *content_type, const void *body, size_t len, tc_mime *m)
{
    if (content_type && tc_mime_parse(content_type, m) == TC_OK)
        return tc_mime_is_html(m) || strcmp(m->essence, "application/xhtml+xml") == 0;
    m->essence[0] = m->charset[0] = '\0';
    return tc_mime_sniff_html(body, len);
}
