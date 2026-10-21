/* The HTML parser: the tree builder of the HTML Standard's "Tree
 * construction" section, driving the tokenizer, plus the entry points for
 * parsing a page (with encoding detection) or a fragment.
 *
 * Scripts don't run yet (Sprint 5), so <script> contents are kept as text and
 * document.write() can't feed the parser. */
#ifndef TC_HTML_PARSER_H
#define TC_HTML_PARSER_H

#include "tc_dom.h"
#include "tc_html_sniff.h"

/* Deeper elements are attached to the element at this depth instead, as
   browsers do, so later stages never meet an absurdly deep tree. */
#define TC_HTML_MAX_DEPTH 400

typedef struct tc_html_options {
    int scripting;  /* the scripting flag: <noscript> content is raw text when set */
    /* Parse errors (tc_html_error codes), if wanted. */
    void (*on_error)(void *ctx, int code);
    void *ctx;
} tc_html_options;

/* Parse text (UTF-8; newlines needn't be normalized) into a new document.
   *out is NULL only if memory ran out. */
int tc_html_parse(const char *text, size_t len, const tc_html_options *opt, tc_document **out);

typedef struct tc_html_page {
    tc_document *doc;
    tc_buf text;                 /* the decoded page, newlines normalized */
    tc_encoding encoding;
    tc_encoding_source encoding_source;
} tc_html_page;

/* Parse a page's bytes: find the encoding (BOM, http_charset, <meta>),
   decode and parse, parsing again if a <meta> changes the encoding.
   Free with tc_html_page_free(). */
int tc_html_parse_page(const void *bytes, size_t len, const char *http_charset, const tc_html_options *opt,
                       tc_html_page *page);
void tc_html_page_free(tc_html_page *page);

/* The HTML fragment parsing algorithm (as innerHTML uses): parse text as if
   inside context, into a new DocumentFragment of doc. */
int tc_html_parse_fragment(tc_document *doc, const tc_element *context, const char *text, size_t len,
                           const tc_html_options *opt, tc_node **out);

#endif
