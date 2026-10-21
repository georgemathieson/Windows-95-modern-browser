/* Shared definitions for the Tachyon core.
 *
 * The core is portable C99: it must build for Windows 95 (MinGW-w64) and for
 * the Linux host used for tests, so it never includes OS headers. */
#ifndef TC_BASE_H
#define TC_BASE_H

#include <stddef.h>
#include <stdint.h>

#define TC_UNUSED(x)        ((void)(x))
#define TC_ARRAY_LEN(a)     (sizeof(a) / sizeof((a)[0]))
#define TC_MIN(a, b)        ((a) < (b) ? (a) : (b))
#define TC_MAX(a, b)        ((a) > (b) ? (a) : (b))
#define TC_CLAMP(v, lo, hi) TC_MIN(TC_MAX((v), (lo)), (hi))

/* printf-style argument checking. MinGW's "printf" means the msvcrt rules,
   but Tachyon links MinGW's own C99 printf, so ask for those rules instead. */
#if defined(__MINGW32__) && defined(__MINGW_PRINTF_FORMAT)
#define TC_PRINTF(fmt, args) __attribute__((format(__MINGW_PRINTF_FORMAT, fmt, args)))
#elif defined(__GNUC__)
#define TC_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define TC_PRINTF(fmt, args)
#endif

/* Functions that can fail return 0 on success and a negative tc_err. */
enum tc_err {
    TC_OK = 0,
    TC_ERR_NOMEM = -1,
    TC_ERR_INVALID = -2,
    TC_ERR_TIMEOUT = -3,
    TC_ERR_CANCELLED = -4,
    TC_ERR_URL = -5,          /* not a valid URL */
    TC_ERR_UNSUPPORTED = -6,  /* e.g. a URL scheme Tachyon can't load */
    TC_ERR_DNS = -7,          /* host name not found */
    TC_ERR_CONNECT = -8,      /* connection refused or unreachable */
    TC_ERR_NET = -9,          /* connection failed after it was made */
    TC_ERR_CLOSED = -10,      /* the server closed the connection early */
    TC_ERR_TLS = -11,         /* TLS handshake or record failure */
    TC_ERR_CERT = -12,        /* the server's certificate isn't trusted */
    TC_ERR_PROTOCOL = -13,    /* the server broke the HTTP rules */
    TC_ERR_REDIRECTS = -14,   /* too many redirects */
    TC_ERR_TOO_LARGE = -15,   /* response bigger than Tachyon accepts */
    TC_ERR_DECODE = -16,      /* bad gzip/deflate data */
    TC_ERR_PROXY = -17        /* the proxy refused or failed */
};

/* A short English description of an error, e.g. "connection refused". */
const char *tc_err_text(int err);

#endif
