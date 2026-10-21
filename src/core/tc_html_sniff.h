/* Finding a page's character encoding before parsing it (HTML Standard,
 * "determining the character encoding"): a BOM, then the HTTP charset, then
 * a cheap scan of the first 1024 bytes for <meta charset>, then a default. */
#ifndef TC_HTML_SNIFF_H
#define TC_HTML_SNIFF_H

#include "tc_encoding.h"

/* How sure the parser may be about the encoding. With a tentative one, a
   <meta charset> found while parsing can still change it. */
typedef enum tc_confidence {
    TC_CONFIDENCE_TENTATIVE,
    TC_CONFIDENCE_CERTAIN,
    TC_CONFIDENCE_IRRELEVANT
} tc_confidence;

/* Where the encoding came from, for the user interface. */
typedef enum tc_encoding_source {
    TC_ENCSRC_BOM,
    TC_ENCSRC_HTTP,
    TC_ENCSRC_PRESCAN,   /* <meta> or an XML declaration in the first 1024 bytes */
    TC_ENCSRC_DEFAULT,
    TC_ENCSRC_META       /* <meta> found while parsing, so the page was parsed again */
} tc_encoding_source;

#define TC_PRESCAN_BYTES 1024

/* "Prescan a byte stream to determine its encoding" over all len bytes.
   TC_ENC_NONE if it finds nothing. */
tc_encoding tc_html_prescan(const void *data, size_t len);

/* "The algorithm for extracting a character encoding from a meta element",
   given a content attribute's value such as "text/html; charset=utf-8". */
tc_encoding tc_html_meta_charset(const char *content, size_t len);

/* "The encoding sniffing algorithm". http_charset is the Content-Type's
   charset parameter (NULL or "" if none). */
tc_encoding tc_html_sniff(const void *data, size_t len, const char *http_charset, tc_confidence *confidence,
                          tc_encoding_source *source);

#endif
