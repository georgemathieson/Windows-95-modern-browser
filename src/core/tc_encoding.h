/* Text encodings from the WHATWG Encoding Standard: names, labels and
 * decoders to UTF-8.
 *
 * Tachyon keeps text as UTF-8, so every page is decoded once, up front.
 * Decoding follows the standard's error handling exactly (each error becomes
 * U+FFFD), because differences there have caused XSS bugs in browsers.
 *
 * The Chinese, Japanese and Korean multi-byte encodings are recognised but
 * not decoded yet: tc_encoding_supported() is false for them. */
#ifndef TC_ENCODING_H
#define TC_ENCODING_H

#include "tc_buf.h"

/* In the order of the standard's encodings.json; tools/gen_encodings.py
   checks that at build time. */
typedef enum tc_encoding {
    TC_ENC_UTF_8,
    TC_ENC_IBM866,
    TC_ENC_ISO_8859_2,
    TC_ENC_ISO_8859_3,
    TC_ENC_ISO_8859_4,
    TC_ENC_ISO_8859_5,
    TC_ENC_ISO_8859_6,
    TC_ENC_ISO_8859_7,
    TC_ENC_ISO_8859_8,
    TC_ENC_ISO_8859_8_I,
    TC_ENC_ISO_8859_10,
    TC_ENC_ISO_8859_13,
    TC_ENC_ISO_8859_14,
    TC_ENC_ISO_8859_15,
    TC_ENC_ISO_8859_16,
    TC_ENC_KOI8_R,
    TC_ENC_KOI8_U,
    TC_ENC_MACINTOSH,
    TC_ENC_WINDOWS_874,
    TC_ENC_WINDOWS_1250,
    TC_ENC_WINDOWS_1251,
    TC_ENC_WINDOWS_1252,
    TC_ENC_WINDOWS_1253,
    TC_ENC_WINDOWS_1254,
    TC_ENC_WINDOWS_1255,
    TC_ENC_WINDOWS_1256,
    TC_ENC_WINDOWS_1257,
    TC_ENC_WINDOWS_1258,
    TC_ENC_X_MAC_CYRILLIC,
    TC_ENC_GBK,
    TC_ENC_GB18030,
    TC_ENC_BIG5,
    TC_ENC_EUC_JP,
    TC_ENC_ISO_2022_JP,
    TC_ENC_SHIFT_JIS,
    TC_ENC_EUC_KR,
    TC_ENC_REPLACEMENT,
    TC_ENC_UTF_16BE,
    TC_ENC_UTF_16LE,
    TC_ENC_X_USER_DEFINED,
    TC_ENC_COUNT,
    TC_ENC_NONE = -1   /* "failure" in the standard */
} tc_encoding;

/* The encoding's name as the standard writes it, e.g. "windows-1252". */
const char *tc_encoding_name(tc_encoding enc);

/* Whether Tachyon can decode it. */
int tc_encoding_supported(tc_encoding enc);

/* "Get an encoding": look up a label such as " Latin1 " (surrounding ASCII
   whitespace and case are ignored). TC_ENC_NONE if it isn't one. */
tc_encoding tc_encoding_from_label(const char *label, size_t len);

/* "BOM sniff": TC_ENC_UTF_8, TC_ENC_UTF_16BE or TC_ENC_UTF_16LE and the
   BOM's length, or TC_ENC_NONE. */
tc_encoding tc_encoding_sniff_bom(const void *data, size_t len, size_t *bom_len);

/* "Decode": a BOM overrides enc and is skipped; then decode with enc,
   appending UTF-8 to out. An unsupported enc decodes as windows-1252. */
int tc_decode(tc_encoding enc, const void *data, size_t len, tc_buf *out);

/* The same without looking for a BOM. */
int tc_decode_no_bom(tc_encoding enc, const void *data, size_t len, tc_buf *out);

#endif
