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

#endif
