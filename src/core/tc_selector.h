/* Selectors Level 4: parsing selector lists from a rule's prelude and
 * matching them against elements.
 *
 * Supported: type, universal, id, class and attribute selectors (all
 * matchers, with the i and s flags); the four combinators; structural
 * pseudo-classes including :nth-child(An+B of S); :is(), :where(), :not()
 * and :has() with descendant or child arguments; link, form-state and
 * language pseudo-classes; ::before, ::after and ::marker. Interaction
 * pseudo-classes (:hover, :focus...) are parsed but never match yet. A
 * selector with anything Tachyon doesn't know is invalid, as the spec says,
 * which drops its whole rule. */
#ifndef TC_SELECTOR_H
#define TC_SELECTOR_H

#include "tc_css_syntax.h"
#include "tc_dom.h"

typedef enum tc_pseudo_element {
    TC_PSEUDO_NONE,
    TC_PSEUDO_BEFORE,
    TC_PSEUDO_AFTER,
    TC_PSEUDO_MARKER,
    TC_PSEUDO_OTHER     /* ::first-line, ::selection...: parsed, not used */
} tc_pseudo_element;

typedef struct tc_compound tc_compound;

typedef struct tc_selector {
    tc_compound *compounds;  /* rightmost first */
    uint32_t ncompounds;
    uint32_t specificity;    /* a << 20 | b << 10 | c */
    uint8_t pseudo;          /* tc_pseudo_element */
} tc_selector;

typedef struct tc_selector_list {
    tc_selector *items;
    uint32_t count;
} tc_selector_list;

/* What matching needs to know beyond the element. */
typedef struct tc_match_context {
    int quirks;              /* class and id names match case-insensitively */
    const char *target_id;   /* :target, or NULL */
} tc_match_context;

/* Parse a selector list. forgiving: drop invalid selectors instead of
   failing the whole list (as :is() and :where() do). The selectors live in
   the arena and don't point into the component values. */
int tc_selectors_parse(tc_arena *a, const tc_css_value *values, uint32_t count, int forgiving,
                       tc_selector_list *out);

/* Whether el matches (ignoring the pseudo-element part). */
int tc_selector_matches(const tc_selector *s, const tc_element *el, const tc_match_context *ctx);

/* For bucketing rules: the rightmost compound's id, first class or tag
   name, whichever comes first, or nothing. */
typedef enum tc_selector_key_type {
    TC_KEY_NONE,
    TC_KEY_ID,
    TC_KEY_CLASS,
    TC_KEY_TAG
} tc_selector_key_type;

tc_selector_key_type tc_selector_key(const tc_selector *s, const char **name);

/* For an ancestor filter: hashes of the ids, classes and tag names the
   selector needs its subject's ancestors to have (at most max), and the
   hash itself (kind: TC_KEY_*; fold: lower-case ASCII first, for quirks-mode
   ids and classes; tag names are always lower case). */
uint32_t tc_selector_ancestor_hashes(const tc_selector *s, int fold, uint32_t *out, uint32_t max);
uint32_t tc_selector_hash(int kind, const char *name, size_t len, int fold);

/* The element as selectors see it. */
const tc_element *tc_element_parent(const tc_element *el);
const tc_element *tc_element_prev(const tc_element *el);
const tc_element *tc_element_next(const tc_element *el);
/* The value of an attribute in no namespace, or NULL. HTML attribute names
   are lower case, so name must be too. */
const char *tc_element_attr(const tc_element *el, const char *name);
/* Whether the class attribute contains name (a whitespace-separated list). */
int tc_element_has_class(const tc_element *el, const char *name, size_t len, int case_insensitive);

#endif
