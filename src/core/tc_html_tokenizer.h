/* The HTML tokenizer: the state machine of the HTML Standard's
 * "Tokenization" section, turning a page's text into tokens.
 *
 * Input is UTF-8 with newlines already normalized to LF. The tree builder
 * pulls one token at a time with tc_tokenizer_next() and may switch the
 * tokenizer's state between tokens (after <script>, <title>, ...), which is
 * why this is a pull interface. Adjacent characters arrive as one token.
 *
 * Tokens point into the tokenizer's buffers and are valid until the next
 * call. */
#ifndef TC_HTML_TOKENIZER_H
#define TC_HTML_TOKENIZER_H

#include "tc_buf.h"

/* Parse error codes, as named in the HTML Standard. */
typedef enum tc_html_error {
    TC_HTML_ERR_ABRUPT_CLOSING_OF_EMPTY_COMMENT = 1,
    TC_HTML_ERR_ABRUPT_DOCTYPE_PUBLIC_IDENTIFIER,
    TC_HTML_ERR_ABRUPT_DOCTYPE_SYSTEM_IDENTIFIER,
    TC_HTML_ERR_ABSENCE_OF_DIGITS_IN_NUMERIC_CHARACTER_REFERENCE,
    TC_HTML_ERR_CDATA_IN_HTML_CONTENT,
    TC_HTML_ERR_CHARACTER_REFERENCE_OUTSIDE_UNICODE_RANGE,
    TC_HTML_ERR_CONTROL_CHARACTER_IN_INPUT_STREAM,
    TC_HTML_ERR_CONTROL_CHARACTER_REFERENCE,
    TC_HTML_ERR_DISALLOWED_PROCESSING_INSTRUCTION_TARGET,
    TC_HTML_ERR_DUPLICATE_ATTRIBUTE,
    TC_HTML_ERR_END_TAG_WITH_ATTRIBUTES,
    TC_HTML_ERR_END_TAG_WITH_TRAILING_SOLIDUS,
    TC_HTML_ERR_EOF_BEFORE_TAG_NAME,
    TC_HTML_ERR_EOF_IN_CDATA,
    TC_HTML_ERR_EOF_IN_COMMENT,
    TC_HTML_ERR_EOF_IN_DOCTYPE,
    TC_HTML_ERR_EOF_IN_PROCESSING_INSTRUCTION,
    TC_HTML_ERR_EOF_IN_SCRIPT_HTML_COMMENT_LIKE_TEXT,
    TC_HTML_ERR_EOF_IN_TAG,
    TC_HTML_ERR_INCORRECTLY_CLOSED_COMMENT,
    TC_HTML_ERR_INCORRECTLY_OPENED_COMMENT,
    TC_HTML_ERR_INVALID_CHARACTER_SEQUENCE_AFTER_DOCTYPE_NAME,
    TC_HTML_ERR_INVALID_FIRST_CHARACTER_OF_PROCESSING_INSTRUCTION_TARGET,
    TC_HTML_ERR_INVALID_FIRST_CHARACTER_OF_TAG_NAME,
    TC_HTML_ERR_INVALID_PROCESSING_INSTRUCTION_TARGET,
    TC_HTML_ERR_MISSING_ATTRIBUTE_VALUE,
    TC_HTML_ERR_MISSING_DOCTYPE_NAME,
    TC_HTML_ERR_MISSING_DOCTYPE_PUBLIC_IDENTIFIER,
    TC_HTML_ERR_MISSING_DOCTYPE_SYSTEM_IDENTIFIER,
    TC_HTML_ERR_MISSING_END_TAG_NAME,
    TC_HTML_ERR_MISSING_QUOTE_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER,
    TC_HTML_ERR_MISSING_QUOTE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER,
    TC_HTML_ERR_MISSING_SEMICOLON_AFTER_CHARACTER_REFERENCE,
    TC_HTML_ERR_MISSING_WHITESPACE_AFTER_DOCTYPE_PUBLIC_KEYWORD,
    TC_HTML_ERR_MISSING_WHITESPACE_AFTER_DOCTYPE_SYSTEM_KEYWORD,
    TC_HTML_ERR_MISSING_WHITESPACE_BEFORE_DOCTYPE_NAME,
    TC_HTML_ERR_MISSING_WHITESPACE_BETWEEN_ATTRIBUTES,
    TC_HTML_ERR_MISSING_WHITESPACE_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS,
    TC_HTML_ERR_NESTED_COMMENT,
    TC_HTML_ERR_NONCHARACTER_CHARACTER_REFERENCE,
    TC_HTML_ERR_NONCHARACTER_IN_INPUT_STREAM,
    TC_HTML_ERR_NON_VOID_HTML_ELEMENT_START_TAG_WITH_TRAILING_SOLIDUS,
    TC_HTML_ERR_NULL_CHARACTER_REFERENCE,
    TC_HTML_ERR_SURROGATE_CHARACTER_REFERENCE,
    TC_HTML_ERR_SURROGATE_IN_INPUT_STREAM,
    TC_HTML_ERR_UNEXPECTED_CHARACTER_AFTER_DOCTYPE_SYSTEM_IDENTIFIER,
    TC_HTML_ERR_UNEXPECTED_CHARACTER_IN_ATTRIBUTE_NAME,
    TC_HTML_ERR_UNEXPECTED_CHARACTER_IN_UNQUOTED_ATTRIBUTE_VALUE,
    TC_HTML_ERR_UNEXPECTED_EQUALS_SIGN_BEFORE_ATTRIBUTE_NAME,
    TC_HTML_ERR_UNEXPECTED_NULL_CHARACTER,
    TC_HTML_ERR_UNEXPECTED_SOLIDUS_IN_TAG,
    TC_HTML_ERR_UNKNOWN_NAMED_CHARACTER_REFERENCE,
    TC_HTML_ERR_TREE_CONSTRUCTION  /* the tree builder's errors have no codes */
} tc_html_error;

/* The code as the standard writes it, e.g. "eof-in-tag". */
const char *tc_html_error_name(int code);

/* The states the tree builder (and tests) can switch the tokenizer to. */
typedef enum tc_tok_state {
    TC_TOK_DATA,
    TC_TOK_RCDATA,
    TC_TOK_RAWTEXT,
    TC_TOK_SCRIPT_DATA,
    TC_TOK_PLAINTEXT,
    TC_TOK_CDATA_SECTION
} tc_tok_state;

typedef enum tc_token_type {
    TC_TOKEN_DOCTYPE,
    TC_TOKEN_START_TAG,
    TC_TOKEN_END_TAG,
    TC_TOKEN_COMMENT,
    TC_TOKEN_CHARS,
    TC_TOKEN_PI,
    TC_TOKEN_EOF
} tc_token_type;

typedef struct tc_token_attr {
    const char *name, *value;  /* NUL-terminated UTF-8 */
    size_t name_len, value_len;
} tc_token_attr;

typedef struct tc_token {
    tc_token_type type;
    /* Tag name (lowercase), DOCTYPE name or processing instruction target. */
    const char *name;
    size_t name_len;
    /* Characters (may include U+0000), comment or processing instruction data. */
    const char *data;
    size_t data_len;
    tc_token_attr *attrs;  /* start and end tags */
    size_t attr_count;
    int self_closing;
    /* DOCTYPE: a missing name or identifier is NULL, not "". */
    const char *public_id, *system_id;
    int force_quirks;
} tc_token;

typedef struct tc_tok_attr_rec {
    size_t name_off, name_len, value_off, value_len;
    int duplicate;
} tc_tok_attr_rec;

typedef struct tc_tokenizer {
    const char *in;
    size_t len, pos;
    size_t checked;          /* input before this has been checked for errors */
    int state, return_state;

    tc_buf chars;            /* characters not yet delivered */
    int chars_delivered;
    int pending;             /* a non-character token waiting behind chars, or -1 */
    int eof;                 /* the end-of-file token has been emitted */
    int flush_first;         /* deliver chars before running the current state */
    int nomem;

    /* The token being built. */
    int is_end_tag, self_closing;
    tc_buf tag_name;
    tc_buf attr_text;        /* attribute names and values */
    tc_tok_attr_rec *attrs;
    size_t attr_count, attr_cap;
    tc_token_attr *out_attrs;
    size_t out_attr_cap;
    tc_buf comment;          /* also processing instruction data */
    tc_buf doctype_name, public_id, system_id;
    int has_doctype_name, has_public_id, has_system_id, force_quirks;
    tc_buf temp;             /* the standard's "temporary buffer" */
    tc_buf last_start_tag;
    int has_last_start_tag;
    uint32_t char_ref_code;

    /* Set by the tree builder: is there an adjusted current node that isn't
       in the HTML namespace? (Decides whether <![CDATA[ starts a CDATA
       section.) NULL means no. */
    int (*in_foreign_content)(void *ctx);
    /* Parse errors, if wanted. */
    void (*on_error)(void *ctx, int code);
    void *ctx;
} tc_tokenizer;

void tc_tokenizer_init(tc_tokenizer *t, const char *in, size_t len);
void tc_tokenizer_free(tc_tokenizer *t);

/* Switch state (one of tc_tok_state). */
void tc_tokenizer_set_state(tc_tokenizer *t, int state);

/* Set "the last start tag to have been emitted" (for tests and fragments). */
int tc_tokenizer_set_last_start_tag(tc_tokenizer *t, const char *name, size_t len);

/* The next token. TC_OK, or TC_ERR_NOMEM. After TC_TOKEN_EOF, always EOF. */
int tc_tokenizer_next(tc_tokenizer *t, tc_token *tok);

/* Normalize newlines in place (CR LF and CR become LF), as the input stream
   needs. Returns the new length. */
size_t tc_html_normalize_newlines(char *s, size_t len);

#endif
