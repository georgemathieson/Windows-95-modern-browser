/* CSS Syntax Level 3: turning a style sheet's text into component values,
 * rules and declarations, which the selector and property parsers then read.
 *
 * Text is UTF-8. Everything returned lives in the caller's arena. The parser
 * builds nested blocks and functions without recursion, so a hostile style
 * sheet can't overflow the stack. */
#ifndef TC_CSS_SYNTAX_H
#define TC_CSS_SYNTAX_H

#include "tc_arena.h"
#include "tc_buf.h"
#include "tc_encoding.h"

typedef enum tc_css_type {
    TC_CSS_EOF,
    TC_CSS_IDENT,
    TC_CSS_AT_KEYWORD,
    TC_CSS_HASH,
    TC_CSS_STRING,
    TC_CSS_BAD_STRING,
    TC_CSS_URL,
    TC_CSS_BAD_URL,
    TC_CSS_DELIM,
    TC_CSS_NUMBER,
    TC_CSS_PERCENTAGE,
    TC_CSS_DIMENSION,
    TC_CSS_WHITESPACE,
    TC_CSS_CDO,
    TC_CSS_CDC,
    TC_CSS_COLON,
    TC_CSS_SEMICOLON,
    TC_CSS_COMMA,
    TC_CSS_LBRACKET,
    TC_CSS_RBRACKET,
    TC_CSS_LPAREN,
    TC_CSS_RPAREN,
    TC_CSS_LBRACE,
    TC_CSS_RBRACE,
    TC_CSS_FUNCTION_TOKEN, /* only inside the parser: becomes TC_CSS_FUNCTION */
    /* Made by the parser from tokens. */
    TC_CSS_FUNCTION,       /* text: the name; items: the arguments */
    TC_CSS_BLOCK           /* cp: '{', '[' or '('; items: the contents */
} tc_css_type;

/* Value flags. */
#define TC_CSS_HASH_ID   1u /* a hash that is a valid identifier (#foo, not #1) */
#define TC_CSS_INTEGER   2u /* a number written without '.' or an exponent */
#define TC_CSS_UNCLOSED  4u /* a string, url, block or function cut short by the end */

typedef struct tc_css_value tc_css_value;
struct tc_css_value {
    uint8_t type;           /* tc_css_type */
    uint8_t flags;
    uint32_t cp;            /* delim: its code point; block: its opening bracket */
    const char *text;       /* ident, at-keyword, hash, string, url, function: the value;
                               dimension: the unit. NUL-terminated, never NULL */
    const char *repr;       /* number, percentage, dimension: as written */
    double number;
    tc_css_value *items;    /* function arguments, block contents */
    uint32_t nitems;
};

typedef struct tc_css_decl {
    const char *name;
    tc_css_value *value;    /* without the !important and surrounding whitespace */
    uint32_t nvalue;
    int important;
} tc_css_decl;

typedef enum tc_css_rule_type {
    TC_CSS_RULE_QUALIFIED,
    TC_CSS_RULE_AT,
    TC_CSS_RULE_ERROR       /* only from the tests' entry points: something invalid */
} tc_css_rule_type;

typedef struct tc_css_rule {
    uint8_t type;           /* tc_css_rule_type */
    const char *name;       /* at-rules: the name without '@' */
    tc_css_value *prelude;
    uint32_t nprelude;
    const tc_css_value *block; /* its {} block, or NULL (at-rules ended by ';') */
} tc_css_rule;

/* One item of a block's contents. */
typedef enum tc_css_item_type {
    TC_CSS_ITEM_DECL,
    TC_CSS_ITEM_RULE,
    TC_CSS_ITEM_ERROR       /* something invalid was skipped */
} tc_css_item_type;

typedef struct tc_css_item {
    uint8_t type;           /* tc_css_item_type */
    tc_css_decl decl;
    tc_css_rule rule;
} tc_css_item;

/* "Parse a list of component values" from text (already decoded). */
int tc_css_parse_values(tc_arena *a, const char *text, size_t len, tc_css_value **out, uint32_t *count);

/* "Parse a stylesheet": its rules. Invalid ones are left out, unless
   keep_errors (for the tests), which adds TC_CSS_RULE_ERROR in their place.
   top_level false gives the older "parse a list of rules", where <!-- and
   --> start a rule instead of being skipped. */
int tc_css_parse_rules(tc_arena *a, const tc_css_value *values, uint32_t count, int top_level, int keep_errors,
                       tc_css_rule **out, uint32_t *nrules);

/* "Parse a block's contents": declarations and nested rules, in order. Used
   for a style rule's block (pass its items) and a style attribute. */
int tc_css_parse_block(tc_arena *a, const tc_css_value *values, uint32_t count, tc_css_item **out,
                       uint32_t *nitems);

/* "Parse a declaration", "parse a rule" and "parse a component value".
   They return TC_ERR_INVALID if there isn't exactly one. */
int tc_css_parse_one_declaration(tc_arena *a, const tc_css_value *values, uint32_t count, tc_css_decl *out);
int tc_css_parse_one_rule(tc_arena *a, const tc_css_value *values, uint32_t count, tc_css_rule *out,
                          const char **error);
int tc_css_parse_one_value(const tc_css_value *values, uint32_t count, const tc_css_value **out,
                           const char **error);

/* The An+B microsyntax (for :nth-child()). TC_OK with a and b set, or
   TC_ERR_INVALID. */
int tc_css_parse_anb(const tc_css_value *values, uint32_t count, int *a, int *b);

/* Decode a style sheet's bytes: a BOM, then the protocol's charset label,
   then @charset, then the environment's encoding (a document's), then UTF-8.
   Appends UTF-8 to out and returns the encoding used. Labels may be NULL. */
tc_encoding tc_css_decode(const void *bytes, size_t len, const char *protocol_label, const char *environment_label,
                          tc_buf *out);

/* Helpers for the property and selector parsers. */
int tc_css_is_ident(const tc_css_value *v, const char *name);      /* ASCII case-insensitive */
int tc_css_is_delim(const tc_css_value *v, uint32_t cp);
int tc_css_ascii_ieq(const char *a, const char *b);

#endif
