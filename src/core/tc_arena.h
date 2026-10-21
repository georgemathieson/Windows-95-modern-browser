/* An arena allocator: many small allocations, all freed at once.
 *
 * A document's nodes and strings live in its arena, which keeps parsing
 * fast (no malloc per node) and freeing a page a single pass over a few
 * large blocks. */
#ifndef TC_ARENA_H
#define TC_ARENA_H

#include "tc_base.h"

typedef struct tc_arena_block tc_arena_block;

typedef struct tc_arena {
    tc_arena_block *blocks;
    char *next;          /* free space in the newest block */
    size_t left;
    size_t total;        /* bytes requested from malloc, for statistics */
} tc_arena;

void tc_arena_init(tc_arena *a);
void tc_arena_free(tc_arena *a);

/* size bytes aligned for any type; NULL if out of memory. */
void *tc_arena_alloc(tc_arena *a, size_t size);

/* A NUL-terminated copy of len bytes. */
char *tc_arena_strndup(tc_arena *a, const char *s, size_t len);

#endif
