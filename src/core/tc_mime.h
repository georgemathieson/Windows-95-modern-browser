/* MIME types, as the WHATWG MIME Sniffing Standard parses them: enough to
 * tell HTML from other content and to find a Content-Type's charset. */
#ifndef TC_MIME_H
#define TC_MIME_H

#include "tc_base.h"

typedef struct tc_mime {
    char essence[80];  /* "type/subtype", lowercase */
    char charset[48];  /* the charset parameter's value, or "" */
} tc_mime;

/* "Parse a MIME type". TC_ERR_INVALID if s isn't one (or is too long). */
int tc_mime_parse(const char *s, tc_mime *out);

/* Whether the essence is text/html. */
int tc_mime_is_html(const tc_mime *m);

/* Whether a body without a usable Content-Type looks like HTML, by the MIME
   Sniffing Standard's HTML patterns (<!DOCTYPE HTML, <html, <p, <!--, ...). */
int tc_mime_sniff_html(const void *data, size_t len);

/* Whether to parse a response as HTML: its Content-Type (may be NULL) says
   text/html or application/xhtml+xml, or there's none and the body sniffs
   as HTML. Fills m (an empty essence if the type was missing or invalid). */
int tc_mime_wants_html(const char *content_type, const void *body, size_t len, tc_mime *m);

#endif
