#include "tc_fixup.h"

#include <string.h>

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static int starts_with_nocase(const char *s, size_t len, const char *prefix)
{
    size_t i, n = strlen(prefix);
    if (len < n)
        return 0;
    for (i = 0; i < n; i++)
        if (lower(s[i]) != prefix[i])
            return 0;
    return 1;
}

/* True if s begins with "scheme://" (letters, digits, +, -, . after a letter). */
static int has_scheme(const char *s, size_t len)
{
    size_t i;
    if (len == 0 || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z')))
        return 0;
    for (i = 1; i + 2 < len; i++) {
        char c = s[i];
        if (c == ':')
            return s[i + 1] == '/' && s[i + 2] == '/';
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '+' || c == '-' || c == '.'))
            return 0;
    }
    return 0;
}

int tc_fixup_address(const char *input, tc_buf *out)
{
    const char *start = input, *end = input + strlen(input);
    size_t len, i;

    while (start < end && is_space(*start))
        start++;
    while (end > start && is_space(end[-1]))
        end--;
    len = (size_t)(end - start);
    tc_buf_clear(out);
    if (len == 0)
        return TC_ERR_INVALID;
    for (i = 0; i < len; i++)
        if (is_space(start[i]))
            return TC_ERR_INVALID;

    if (starts_with_nocase(start, len, "about:")) {
        for (i = 0; i < len; i++)
            if (tc_buf_append_char(out, lower(start[i])))
                return TC_ERR_NOMEM;
        return TC_OK;
    }
    if (!has_scheme(start, len) && tc_buf_append_str(out, "https://"))
        return TC_ERR_NOMEM;
    return tc_buf_append(out, start, len);
}
