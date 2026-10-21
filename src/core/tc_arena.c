#include "tc_arena.h"

#include <stdlib.h>
#include <string.h>

/* Big enough that a typical page needs a few dozen blocks, small enough not
   to waste much on a tiny one. */
#define BLOCK_SIZE 32768
#define ALIGN 8

struct tc_arena_block {
    tc_arena_block *next;
    size_t size;
    /* Data follows, aligned by the union below. */
};

typedef union block_header {
    tc_arena_block block;
    double align_d;
    void *align_p;
    long long align_ll;
} block_header;

void tc_arena_init(tc_arena *a)
{
    memset(a, 0, sizeof *a);
}

void tc_arena_free(tc_arena *a)
{
    tc_arena_block *b = a->blocks;

    while (b) {
        tc_arena_block *next = b->next;
        free(b);
        b = next;
    }
    memset(a, 0, sizeof *a);
}

void *tc_arena_alloc(tc_arena *a, size_t size)
{
    size_t data_size;
    tc_arena_block *b;
    void *p;

    size = (size + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    if (size == 0)
        size = ALIGN;
    if (size <= a->left) {
        p = a->next;
        a->next += size;
        a->left -= size;
        return p;
    }
    if (size > (size_t)-1 / 2)
        return NULL;
    /* Large requests get a block of their own, so the current block's
       free space isn't thrown away. */
    data_size = size > BLOCK_SIZE / 4 ? size : BLOCK_SIZE;
    b = malloc(sizeof(block_header) + data_size);
    if (!b)
        return NULL;
    b->size = data_size;
    a->total += sizeof(block_header) + data_size;
    p = (char *)b + sizeof(block_header);
    if (data_size == size && a->blocks) {
        /* Keep the current block current: link the big one behind it. */
        b->next = a->blocks->next;
        a->blocks->next = b;
        return p;
    }
    b->next = a->blocks;
    a->blocks = b;
    a->next = (char *)p + size;
    a->left = data_size - size;
    return p;
}

char *tc_arena_strndup(tc_arena *a, const char *s, size_t len)
{
    char *p = tc_arena_alloc(a, len + 1);

    if (p) {
        if (len)
            memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}
