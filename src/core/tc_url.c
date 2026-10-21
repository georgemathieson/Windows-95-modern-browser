/* The WHATWG URL Standard's basic URL parser (without state override,
 * which only URL setters use), followed state by state. The comments name
 * the standard's states so the two can be read side by side.
 *
 * It works on UTF-8 bytes rather than code points: percent-encoding a code
 * point means encoding each byte of its UTF-8 form, so the result is the
 * same, and only hosts need real decoding (tc_url_host.c). */
#include "tc_url_internal.h"

#include <stdlib.h>
#include <string.h>

/* ---- Shared helpers ---- */

int url_is_ascii_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int url_is_ascii_digit(int c)
{
    return c >= '0' && c <= '9';
}

int url_is_ascii_hex(int c)
{
    return url_is_ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int url_hex_value(int c)
{
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

int url_in_encode_set(unsigned char c, enum url_encode_set set)
{
    if (c < 0x20 || c > 0x7E)
        return 1;
    switch (set) {
    case SET_C0:
        return 0;
    case SET_FRAGMENT:
        return c == ' ' || c == '"' || c == '<' || c == '>' || c == '`';
    case SET_USERINFO:
        if (c == '/' || c == ':' || c == ';' || c == '=' || c == '@' || c == '[' || c == '\\' ||
            c == ']' || c == '|')
            return 1;
        /* fall through */
    case SET_PATH:
        if (c == '?' || c == '^' || c == '`' || c == '{' || c == '}')
            return 1;
        /* fall through */
    case SET_QUERY:
        return c == ' ' || c == '"' || c == '#' || c == '<' || c == '>';
    case SET_SPECIAL_QUERY:
        return c == '\'' || url_in_encode_set(c, SET_QUERY);
    }
    return 0;
}

int url_append_encoded(tc_buf *out, unsigned char c, enum url_encode_set set)
{
    static const char hex[] = "0123456789ABCDEF";
    char enc[3];
    if (!url_in_encode_set(c, set))
        return tc_buf_append_char(out, (char)c);
    enc[0] = '%';
    enc[1] = hex[c >> 4];
    enc[2] = hex[c & 15];
    return tc_buf_append(out, enc, 3);
}

int url_percent_decode(const char *s, size_t len, tc_buf *out)
{
    size_t i;
    int err = TC_OK;
    for (i = 0; i < len && !err; i++) {
        if (s[i] == '%' && i + 2 < len && url_is_ascii_hex(s[i + 1]) && url_is_ascii_hex(s[i + 2])) {
            err = tc_buf_append_char(out, (char)(url_hex_value(s[i + 1]) * 16 + url_hex_value(s[i + 2])));
            i += 2;
        } else {
            err = tc_buf_append_char(out, s[i]);
        }
    }
    return err;
}

/* ---- Schemes ---- */

static const struct {
    const char *scheme;
    int port;
} special_schemes[] = {
    { "ftp", 21 }, { "file", -1 }, { "http", 80 }, { "https", 443 }, { "ws", 80 }, { "wss", 443 },
};

static int scheme_is_special(const char *scheme)
{
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(special_schemes); i++)
        if (strcmp(scheme, special_schemes[i].scheme) == 0)
            return 1;
    return 0;
}

int tc_url_default_port(const char *scheme)
{
    size_t i;
    for (i = 0; i < TC_ARRAY_LEN(special_schemes); i++)
        if (strcmp(scheme, special_schemes[i].scheme) == 0)
            return special_schemes[i].port;
    return -1;
}

/* ---- Path segment lists ---- */

typedef struct seglist {
    char **items;
    size_t len, cap;
} seglist;

static int seg_push(seglist *l, const char *s, size_t n)
{
    char *copy;
    if (l->len == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 8;
        char **p = realloc(l->items, cap * sizeof *p);
        if (!p)
            return TC_ERR_NOMEM;
        l->items = p;
        l->cap = cap;
    }
    copy = malloc(n + 1);
    if (!copy)
        return TC_ERR_NOMEM;
    memcpy(copy, s, n);
    copy[n] = '\0';
    l->items[l->len++] = copy;
    return TC_OK;
}

static void seg_pop(seglist *l)
{
    if (l->len)
        free(l->items[--l->len]);
}

static void seg_clear(seglist *l)
{
    while (l->len)
        seg_pop(l);
}

static void seg_free(seglist *l)
{
    seg_clear(l);
    free(l->items);
    l->items = NULL;
    l->cap = 0;
}

/* Split a serialized (non-opaque) path back into segments. Segments never
   contain '/', so this is exact. */
static int seg_from_path(seglist *l, const char *path)
{
    const char *start;
    int err = TC_OK;
    seg_clear(l);
    if (!path || !*path)
        return TC_OK;
    for (start = path + 1; !err; start++) {
        const char *end = strchr(start, '/');
        if (!end) {
            err = seg_push(l, start, strlen(start));
            break;
        }
        err = seg_push(l, start, (size_t)(end - start));
        start = end;
    }
    return err;
}

/* ---- Windows drive letters and dot segments ---- */

static int is_drive_letter(const char *s, size_t len)
{
    return len == 2 && url_is_ascii_alpha(s[0]) && (s[1] == ':' || s[1] == '|');
}

static int is_normalized_drive_letter(const char *s)
{
    return strlen(s) == 2 && url_is_ascii_alpha(s[0]) && s[1] == ':';
}

static int starts_with_drive_letter(const char *s, size_t len)
{
    return len >= 2 && is_drive_letter(s, 2) &&
           (len == 2 || s[2] == '/' || s[2] == '\\' || s[2] == '?' || s[2] == '#');
}

static int equals_nocase(const char *s, size_t len, const char *lower)
{
    size_t i;
    if (len != strlen(lower))
        return 0;
    for (i = 0; i < len; i++)
        if ((s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]) != lower[i])
            return 0;
    return 1;
}

static int is_single_dot(const tc_buf *b)
{
    return equals_nocase(tc_buf_cstr(b), b->len, ".") || equals_nocase(tc_buf_cstr(b), b->len, "%2e");
}

static int is_double_dot(const tc_buf *b)
{
    const char *s = tc_buf_cstr(b);
    return equals_nocase(s, b->len, "..") || equals_nocase(s, b->len, ".%2e") ||
           equals_nocase(s, b->len, "%2e.") || equals_nocase(s, b->len, "%2e%2e");
}

/* ---- The parser ---- */

enum state {
    SCHEME_START, SCHEME, NO_SCHEME, SPECIAL_RELATIVE_OR_AUTHORITY, PATH_OR_AUTHORITY,
    RELATIVE, RELATIVE_SLASH, SPECIAL_AUTHORITY_SLASHES, SPECIAL_AUTHORITY_IGNORE_SLASHES,
    AUTHORITY, HOST, PORT, FILE_STATE, FILE_SLASH, FILE_HOST, PATH_START, PATH, OPAQUE_PATH,
    QUERY, FRAGMENT
};

/* The URL being built. */
typedef struct builder {
    tc_buf scheme, username, password, host, opaque, query, fragment;
    int special, has_host, port, opaque_path, has_query, has_fragment;
    seglist path;
} builder;

static void shorten_path(builder *u)
{
    if (strcmp(tc_buf_cstr(&u->scheme), "file") == 0 && u->path.len == 1 &&
        is_normalized_drive_letter(u->path.items[0]))
        return;
    seg_pop(&u->path);
}

static int set_str(tc_buf *b, const char *s)
{
    tc_buf_clear(b);
    return s ? tc_buf_append_str(b, s) : TC_OK;
}

static int copy_authority(builder *u, const tc_url *base)
{
    int err = set_str(&u->username, base->username);
    err = err ? err : set_str(&u->password, base->password);
    err = err ? err : set_str(&u->host, base->host);
    u->has_host = base->host != NULL;
    u->port = base->port;
    return err;
}

static int copy_query(builder *u, const tc_url *base)
{
    u->has_query = base->query != NULL;
    return set_str(&u->query, base->query);
}

static int is_end_of_authority(int c, int special)
{
    return c == -1 || c == '/' || c == '?' || c == '#' || (special && c == '\\');
}

#define FAIL() do { err = TC_ERR_URL; goto done; } while (0)
#define TRY(x) do { if ((err = (x)) != TC_OK) goto done; } while (0)

static int finish(builder *u, tc_url *out);

int tc_url_parse_n(const char *input, size_t input_len, const tc_url *base, tc_url *out)
{
    builder u;
    tc_buf buf;
    char *in;
    size_t len = 0, i, start = 0, end = input_len;
    long p;
    enum state state = SCHEME_START;
    int err = TC_OK, at_seen = 0, in_brackets = 0, password_seen = 0;

    memset(out, 0, sizeof *out);
    /* Strip leading and trailing C0 controls and spaces, and every tab and
       newline. */
    while (start < end && (unsigned char)input[start] <= 0x20)
        start++;
    while (end > start && (unsigned char)input[end - 1] <= 0x20)
        end--;
    in = malloc(end - start + 1);
    if (!in)
        return TC_ERR_NOMEM;
    for (i = start; i < end; i++)
        if (input[i] != '\t' && input[i] != '\n' && input[i] != '\r')
            in[len++] = input[i];

    memset(&u, 0, sizeof u);
    tc_buf_init(&u.scheme);
    tc_buf_init(&u.username);
    tc_buf_init(&u.password);
    tc_buf_init(&u.host);
    tc_buf_init(&u.opaque);
    tc_buf_init(&u.query);
    tc_buf_init(&u.fragment);
    tc_buf_init(&buf);
    u.port = -1;

    for (p = 0; p <= (long)len; p++) {
        int c = p < (long)len ? (unsigned char)in[p] : -1;
        const char *rest = in + p;
        size_t rest_len = p < (long)len ? len - (size_t)p : 0;

        switch (state) {
        case SCHEME_START:
            if (url_is_ascii_alpha(c)) {
                TRY(tc_buf_append_char(&buf, (char)(c | 0x20)));
                state = SCHEME;
            } else {
                state = NO_SCHEME;
                p--;
            }
            break;

        case SCHEME:
            if (url_is_ascii_alpha(c) || url_is_ascii_digit(c) || c == '+' || c == '-' || c == '.') {
                TRY(tc_buf_append_char(&buf, (char)(c >= 'A' && c <= 'Z' ? c | 0x20 : c)));
            } else if (c == ':') {
                TRY(set_str(&u.scheme, tc_buf_cstr(&buf)));
                tc_buf_clear(&buf);
                u.special = scheme_is_special(tc_buf_cstr(&u.scheme));
                if (strcmp(tc_buf_cstr(&u.scheme), "file") == 0) {
                    state = FILE_STATE;
                } else if (u.special && base && strcmp(base->scheme, tc_buf_cstr(&u.scheme)) == 0) {
                    state = SPECIAL_RELATIVE_OR_AUTHORITY;
                } else if (u.special) {
                    state = SPECIAL_AUTHORITY_SLASHES;
                } else if (rest_len > 1 && rest[1] == '/') {
                    state = PATH_OR_AUTHORITY;
                    p++;
                } else {
                    u.opaque_path = 1;
                    state = OPAQUE_PATH;
                }
            } else {
                /* Not a scheme after all: start over as a relative URL. */
                tc_buf_clear(&buf);
                state = NO_SCHEME;
                p = -1;
            }
            break;

        case NO_SCHEME:
            if (!base || (base->opaque_path && c != '#')) {
                FAIL();
            } else if (base->opaque_path && c == '#') {
                TRY(set_str(&u.scheme, base->scheme));
                u.special = scheme_is_special(base->scheme);
                u.opaque_path = 1;
                TRY(set_str(&u.opaque, base->path));
                TRY(copy_query(&u, base));
                u.has_fragment = 1;
                state = FRAGMENT;
            } else {
                state = strcmp(base->scheme, "file") != 0 ? RELATIVE : FILE_STATE;
                p--;
            }
            break;

        case SPECIAL_RELATIVE_OR_AUTHORITY:
            if (c == '/' && rest_len > 1 && rest[1] == '/') {
                state = SPECIAL_AUTHORITY_IGNORE_SLASHES;
                p++;
            } else {
                state = RELATIVE;
                p--;
            }
            break;

        case PATH_OR_AUTHORITY:
            if (c == '/') {
                state = AUTHORITY;
            } else {
                state = PATH;
                p--;
            }
            break;

        case RELATIVE:
            TRY(set_str(&u.scheme, base->scheme));
            u.special = scheme_is_special(base->scheme);
            if (c == '/' || (u.special && c == '\\')) {
                state = RELATIVE_SLASH;
            } else {
                TRY(copy_authority(&u, base));
                TRY(seg_from_path(&u.path, base->path));
                TRY(copy_query(&u, base));
                if (c == '?') {
                    tc_buf_clear(&u.query);
                    u.has_query = 1;
                    state = QUERY;
                } else if (c == '#') {
                    u.has_fragment = 1;
                    state = FRAGMENT;
                } else if (c != -1) {
                    tc_buf_clear(&u.query);
                    u.has_query = 0;
                    shorten_path(&u);
                    state = PATH;
                    p--;
                }
            }
            break;

        case RELATIVE_SLASH:
            if (u.special && (c == '/' || c == '\\')) {
                state = SPECIAL_AUTHORITY_IGNORE_SLASHES;
            } else if (c == '/') {
                state = AUTHORITY;
            } else {
                TRY(copy_authority(&u, base));
                state = PATH;
                p--;
            }
            break;

        case SPECIAL_AUTHORITY_SLASHES:
            state = SPECIAL_AUTHORITY_IGNORE_SLASHES;
            if (c == '/' && rest_len > 1 && rest[1] == '/')
                p++;
            else
                p--;
            break;

        case SPECIAL_AUTHORITY_IGNORE_SLASHES:
            if (c != '/' && c != '\\') {
                state = AUTHORITY;
                p--;
            }
            break;

        case AUTHORITY:
            if (c == '@') {
                if (at_seen) {
                    tc_buf with_at;
                    tc_buf_init(&with_at);
                    err = tc_buf_append_str(&with_at, "%40");
                    err = err ? err : tc_buf_append(&with_at, buf.data, buf.len);
                    tc_buf_free(&buf);
                    buf = with_at;
                    TRY(err);
                }
                at_seen = 1;
                for (i = 0; i < buf.len; i++) {
                    unsigned char x = (unsigned char)buf.data[i];
                    if (x == ':' && !password_seen) {
                        password_seen = 1;
                        continue;
                    }
                    TRY(url_append_encoded(password_seen ? &u.password : &u.username, x, SET_USERINFO));
                }
                tc_buf_clear(&buf);
            } else if (is_end_of_authority(c, u.special)) {
                if (at_seen && buf.len == 0)
                    FAIL();
                p -= (long)buf.len + 1;
                tc_buf_clear(&buf);
                state = HOST;
            } else {
                TRY(tc_buf_append_char(&buf, (char)c));
            }
            break;

        case HOST:
            if (c == ':' && !in_brackets) {
                if (buf.len == 0)
                    FAIL();
                TRY(tc_url_parse_host(tc_buf_cstr(&buf), buf.len, !u.special, &u.host));
                u.has_host = 1;
                tc_buf_clear(&buf);
                state = PORT;
            } else if (is_end_of_authority(c, u.special)) {
                p--;
                if (u.special && buf.len == 0)
                    FAIL();
                TRY(tc_url_parse_host(tc_buf_cstr(&buf), buf.len, !u.special, &u.host));
                u.has_host = 1;
                tc_buf_clear(&buf);
                state = PATH_START;
            } else {
                if (c == '[')
                    in_brackets = 1;
                if (c == ']')
                    in_brackets = 0;
                TRY(tc_buf_append_char(&buf, (char)c));
            }
            break;

        case PORT:
            if (url_is_ascii_digit(c)) {
                TRY(tc_buf_append_char(&buf, (char)c));
            } else if (is_end_of_authority(c, u.special)) {
                if (buf.len) {
                    const char *digits = tc_buf_cstr(&buf);
                    long port = 0;
                    while (*digits == '0' && digits[1])
                        digits++;
                    if (strlen(digits) > 5)
                        FAIL();
                    port = strtol(digits, NULL, 10);
                    if (port > 65535)
                        FAIL();
                    u.port = port == tc_url_default_port(tc_buf_cstr(&u.scheme)) ? -1 : (int)port;
                    tc_buf_clear(&buf);
                }
                state = PATH_START;
                p--;
            } else {
                FAIL();
            }
            break;

        case FILE_STATE:
            TRY(set_str(&u.scheme, "file"));
            u.special = 1;
            tc_buf_clear(&u.host);
            u.has_host = 1;
            if (c == '/' || c == '\\') {
                state = FILE_SLASH;
            } else if (base && strcmp(base->scheme, "file") == 0) {
                TRY(set_str(&u.host, base->host));
                u.has_host = base->host != NULL;
                TRY(seg_from_path(&u.path, base->path));
                TRY(copy_query(&u, base));
                if (c == '?') {
                    tc_buf_clear(&u.query);
                    u.has_query = 1;
                    state = QUERY;
                } else if (c == '#') {
                    u.has_fragment = 1;
                    state = FRAGMENT;
                } else if (c != -1) {
                    tc_buf_clear(&u.query);
                    u.has_query = 0;
                    if (!starts_with_drive_letter(rest, rest_len))
                        shorten_path(&u);
                    else
                        seg_clear(&u.path);
                    state = PATH;
                    p--;
                }
            } else {
                state = PATH;
                p--;
            }
            break;

        case FILE_SLASH:
            if (c == '/' || c == '\\') {
                state = FILE_HOST;
            } else {
                if (base && strcmp(base->scheme, "file") == 0) {
                    seglist base_path = { 0 };
                    TRY(set_str(&u.host, base->host));
                    u.has_host = base->host != NULL;
                    err = seg_from_path(&base_path, base->path);
                    if (!err && !starts_with_drive_letter(rest, rest_len) && base_path.len > 0 &&
                        is_normalized_drive_letter(base_path.items[0]))
                        err = seg_push(&u.path, base_path.items[0], strlen(base_path.items[0]));
                    seg_free(&base_path);
                    TRY(err);
                }
                state = PATH;
                p--;
            }
            break;

        case FILE_HOST:
            if (c == -1 || c == '/' || c == '\\' || c == '?' || c == '#') {
                p--;
                if (is_drive_letter(tc_buf_cstr(&buf), buf.len)) {
                    state = PATH; /* the buffer becomes the first path segment */
                } else if (buf.len == 0) {
                    tc_buf_clear(&u.host);
                    u.has_host = 1;
                    state = PATH_START;
                } else {
                    TRY(tc_url_parse_host(tc_buf_cstr(&buf), buf.len, 0, &u.host));
                    if (strcmp(tc_buf_cstr(&u.host), "localhost") == 0)
                        tc_buf_clear(&u.host);
                    u.has_host = 1;
                    tc_buf_clear(&buf);
                    state = PATH_START;
                }
            } else {
                TRY(tc_buf_append_char(&buf, (char)c));
            }
            break;

        case PATH_START:
            if (u.special) {
                state = PATH;
                if (c != '/' && c != '\\')
                    p--;
            } else if (c == '?') {
                u.has_query = 1;
                state = QUERY;
            } else if (c == '#') {
                u.has_fragment = 1;
                state = FRAGMENT;
            } else if (c != -1) {
                state = PATH;
                if (c != '/')
                    p--;
            }
            break;

        case PATH:
            if (c == -1 || c == '/' || (u.special && c == '\\') || c == '?' || c == '#') {
                int slash = c == '/' || (u.special && c == '\\');
                if (is_double_dot(&buf)) {
                    shorten_path(&u);
                    if (!slash)
                        TRY(seg_push(&u.path, "", 0));
                } else if (is_single_dot(&buf) && !slash) {
                    TRY(seg_push(&u.path, "", 0));
                } else if (!is_single_dot(&buf)) {
                    if (strcmp(tc_buf_cstr(&u.scheme), "file") == 0 && u.path.len == 0 &&
                        is_drive_letter(tc_buf_cstr(&buf), buf.len))
                        buf.data[1] = ':';
                    TRY(seg_push(&u.path, tc_buf_cstr(&buf), buf.len));
                }
                tc_buf_clear(&buf);
                if (c == '?') {
                    u.has_query = 1;
                    state = QUERY;
                }
                if (c == '#') {
                    u.has_fragment = 1;
                    state = FRAGMENT;
                }
            } else {
                TRY(url_append_encoded(&buf, (unsigned char)c, SET_PATH));
            }
            break;

        case OPAQUE_PATH:
            if (c == '?') {
                u.has_query = 1;
                state = QUERY;
            } else if (c == '#') {
                u.has_fragment = 1;
                state = FRAGMENT;
            } else if (c == ' ') {
                /* A space right before the query or fragment is encoded, so
                   it can't be lost as trailing whitespace. */
                if (rest_len > 1 && (rest[1] == '?' || rest[1] == '#'))
                    TRY(tc_buf_append_str(&u.opaque, "%20"));
                else
                    TRY(tc_buf_append_char(&u.opaque, ' '));
            } else if (c != -1) {
                TRY(url_append_encoded(&u.opaque, (unsigned char)c, SET_C0));
            }
            break;

        case QUERY:
            if (c == '#' || c == -1) {
                enum url_encode_set set = u.special ? SET_SPECIAL_QUERY : SET_QUERY;
                for (i = 0; i < buf.len; i++)
                    TRY(url_append_encoded(&u.query, (unsigned char)buf.data[i], set));
                tc_buf_clear(&buf);
                if (c == '#') {
                    u.has_fragment = 1;
                    state = FRAGMENT;
                }
            } else {
                TRY(tc_buf_append_char(&buf, (char)c));
            }
            break;

        case FRAGMENT:
            if (c != -1)
                TRY(url_append_encoded(&u.fragment, (unsigned char)c, SET_FRAGMENT));
            break;
        }
    }
    err = finish(&u, out);

done:
    free(in);
    tc_buf_free(&buf);
    tc_buf_free(&u.scheme);
    tc_buf_free(&u.username);
    tc_buf_free(&u.password);
    tc_buf_free(&u.host);
    tc_buf_free(&u.opaque);
    tc_buf_free(&u.query);
    tc_buf_free(&u.fragment);
    seg_free(&u.path);
    if (err)
        tc_url_free(out);
    return err;
}

static char *dup_buf(const tc_buf *b)
{
    char *s = malloc(b->len + 1);
    if (s) {
        memcpy(s, tc_buf_cstr(b), b->len);
        s[b->len] = '\0';
    }
    return s;
}

/* Copy the builder into out and serialize the href. */
static int finish(builder *u, tc_url *out)
{
    tc_buf path, href;
    size_t i;
    int err = TC_OK;

    tc_buf_init(&path);
    tc_buf_init(&href);
    if (u->opaque_path) {
        err = tc_buf_append(&path, tc_buf_cstr(&u->opaque), u->opaque.len);
    } else {
        for (i = 0; i < u->path.len && !err; i++) {
            err = tc_buf_append_char(&path, '/');
            err = err ? err : tc_buf_append_str(&path, u->path.items[i]);
        }
    }
    err = err ? err : tc_buf_append(&href, tc_buf_cstr(&u->scheme), u->scheme.len);
    err = err ? err : tc_buf_append_char(&href, ':');
    if (!err && u->has_host) {
        err = tc_buf_append_str(&href, "//");
        if (!err && (u->username.len || u->password.len)) {
            err = tc_buf_append(&href, tc_buf_cstr(&u->username), u->username.len);
            if (!err && u->password.len) {
                err = tc_buf_append_char(&href, ':');
                err = err ? err : tc_buf_append(&href, tc_buf_cstr(&u->password), u->password.len);
            }
            err = err ? err : tc_buf_append_char(&href, '@');
        }
        err = err ? err : tc_buf_append(&href, tc_buf_cstr(&u->host), u->host.len);
        if (!err && u->port >= 0)
            err = tc_buf_printf(&href, ":%d", u->port);
    }
    /* Without a host, "//x" would read back as a host: write "/.//x". */
    if (!err && !u->has_host && !u->opaque_path && u->path.len > 1 && u->path.items[0][0] == '\0')
        err = tc_buf_append_str(&href, "/.");
    err = err ? err : tc_buf_append(&href, tc_buf_cstr(&path), path.len);
    if (!err && u->has_query) {
        err = tc_buf_append_char(&href, '?');
        err = err ? err : tc_buf_append(&href, tc_buf_cstr(&u->query), u->query.len);
    }
    if (!err && u->has_fragment) {
        err = tc_buf_append_char(&href, '#');
        err = err ? err : tc_buf_append(&href, tc_buf_cstr(&u->fragment), u->fragment.len);
    }

    if (!err) {
        out->href = dup_buf(&href);
        out->scheme = dup_buf(&u->scheme);
        out->username = dup_buf(&u->username);
        out->password = dup_buf(&u->password);
        out->host = u->has_host ? dup_buf(&u->host) : NULL;
        out->port = u->port;
        out->path = dup_buf(&path);
        out->query = u->has_query ? dup_buf(&u->query) : NULL;
        out->fragment = u->has_fragment ? dup_buf(&u->fragment) : NULL;
        out->opaque_path = u->opaque_path;
        if (!out->href || !out->scheme || !out->username || !out->password || !out->path ||
            (u->has_host && !out->host) || (u->has_query && !out->query) ||
            (u->has_fragment && !out->fragment))
            err = TC_ERR_NOMEM;
    }
    tc_buf_free(&path);
    tc_buf_free(&href);
    return err;
}

int tc_url_parse(const char *input, const tc_url *base, tc_url *out)
{
    return tc_url_parse_n(input, strlen(input), base, out);
}

void tc_url_free(tc_url *url)
{
    free(url->href);
    free(url->scheme);
    free(url->username);
    free(url->password);
    free(url->host);
    free(url->path);
    free(url->query);
    free(url->fragment);
    memset(url, 0, sizeof *url);
    url->port = -1;
}

int tc_url_copy(const tc_url *src, tc_url *dst)
{
    return tc_url_parse(src->href, NULL, dst);
}

int tc_url_is_special(const tc_url *url)
{
    return scheme_is_special(url->scheme);
}

int tc_url_effective_port(const tc_url *url)
{
    return url->port >= 0 ? url->port : tc_url_default_port(url->scheme);
}

int tc_url_request_target(const tc_url *url, tc_buf *out)
{
    int err;
    tc_buf_clear(out);
    err = tc_buf_append_str(out, url->path[0] ? url->path : "/");
    if (!err && url->query) {
        err = tc_buf_append_char(out, '?');
        err = err ? err : tc_buf_append_str(out, url->query);
    }
    return err;
}
