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
    TC_ERR_INVALID = -2
};

#endif
