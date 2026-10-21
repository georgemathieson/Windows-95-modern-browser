/* The HTML named character references (&amp;, &eacute;, ...), generated
 * by tools/gen_entities.py. Sorted by name, byte by byte, so the tokenizer
 * can find the longest one that matches with binary searches. */
#ifndef TC_ENTITIES_H
#define TC_ENTITIES_H

#include "tc_base.h"

typedef struct tc_entity {
    const char *name;  /* without the '&'; may end in ';' */
    uint32_t cp1;
    uint16_t cp2;      /* a second code point, or 0 */
} tc_entity;

extern const tc_entity tc_entities[];
extern const size_t tc_entity_count;
extern const size_t tc_entity_max_len;

#endif
