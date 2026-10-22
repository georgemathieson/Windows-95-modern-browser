#include "tc_font_check.h"

#include <stdlib.h>
#include <string.h>

#define MAX_TABLES 256
#define MAX_DEPTH 8        /* composite glyphs inside composite glyphs */
#define MAX_COMPONENTS 256 /* in one composite glyph */

typedef struct table {
    uint32_t off, len;
    uint8_t *tag;          /* in the directory, to hide the table */
} table;

typedef struct font {
    uint8_t *d;
    size_t len;
    uint8_t *dir;          /* the table directory's records */
    uint32_t ntables;
    table head, hhea, hmtx, maxp, cmap, loca, glyf, kern, gpos, cff;
    uint32_t nglyphs;
    int long_loca;
    int8_t *depth;         /* composite nesting, by glyph: 0 unknown, -1 being measured */
} font;

static uint32_t u16(const uint8_t *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int16_t s16(const uint8_t *p)
{
    return (int16_t)u16(p);
}

/* The tables, as stbtt__find_table finds them: the first with each tag. */
static int read_directory(font *f, uint32_t start)
{
    static const char *const tags[] = { "head", "hhea", "hmtx", "maxp", "cmap", "loca", "glyf", "kern", "GPOS", "CFF " };
    table *slots[10];
    uint32_t n, i, k, sfnt;
    slots[0] = &f->head, slots[1] = &f->hhea, slots[2] = &f->hmtx, slots[3] = &f->maxp, slots[4] = &f->cmap;
    slots[5] = &f->loca, slots[6] = &f->glyf, slots[7] = &f->kern, slots[8] = &f->gpos, slots[9] = &f->cff;
    if (start > f->len || f->len - start < 12)
        return 0;
    sfnt = u32(f->d + start);
    if (sfnt != 0x00010000u && sfnt != 0x74727565u) /* TrueType outlines ("true" is Apple's) */
        return 0;
    n = u16(f->d + start + 4);
    if (!n || n > MAX_TABLES || (size_t)12 + 16 * n > f->len - start)
        return 0;
    f->dir = f->d + start + 12;
    f->ntables = n;
    for (i = 0; i < n; i++) {
        uint8_t *rec = f->d + start + 12 + 16 * i;
        uint32_t off = u32(rec + 8), len = u32(rec + 12);
        if (off > f->len || len > f->len - off)
            return 0;
        for (k = 0; k < TC_ARRAY_LEN(tags); k++)
            if (memcmp(rec, tags[k], 4) == 0 && !slots[k]->tag) {
                slots[k]->off = off;
                slots[k]->len = len;
                slots[k]->tag = rec;
            }
    }
    return f->head.tag && f->hhea.tag && f->hmtx.tag && f->maxp.tag && f->cmap.tag && f->loca.tag && f->glyf.tag;
}

/* head, maxp, hhea and hmtx: the glyph count and metrics. */
static int check_metrics(font *f)
{
    uint32_t upem, nlong;
    if (f->head.len < 54 || f->maxp.len < 6 || f->hhea.len < 36)
        return 0;
    upem = u16(f->d + f->head.off + 18);
    if (upem < 16 || upem > 16384)
        return 0;
    if (s16(f->d + f->head.off + 50) != 0 && s16(f->d + f->head.off + 50) != 1)
        return 0;
    f->long_loca = s16(f->d + f->head.off + 50) == 1;
    f->nglyphs = u16(f->d + f->maxp.off + 4);
    nlong = u16(f->d + f->hhea.off + 34);
    if (!f->nglyphs || !nlong || nlong > f->nglyphs)
        return 0;
    return (uint64_t)4 * nlong + (uint64_t)2 * (f->nglyphs - nlong) <= f->hmtx.len;
}

/* Where glyph g's data is in glyf (empty for none). */
static void glyph_range(const font *f, uint32_t g, uint32_t *from, uint32_t *to)
{
    const uint8_t *loca = f->d + f->loca.off;
    if (f->long_loca) {
        *from = u32(loca + 4 * g);
        *to = u32(loca + 4 * g + 4);
    } else {
        *from = u16(loca + 2 * g) * 2;
        *to = u16(loca + 2 * g + 2) * 2;
    }
}

static int check_loca(font *f)
{
    uint32_t g, from, to, prev = 0;
    if ((uint64_t)(f->nglyphs + 1) * (f->long_loca ? 4 : 2) > f->loca.len)
        return 0;
    for (g = 0; g < f->nglyphs; g++) {
        glyph_range(f, g, &from, &to);
        if (from < prev || to < from || to > f->glyf.len)
            return 0;
        prev = from;
    }
    return 1;
}

/* A simple glyph, read as stbtt_GetGlyphShape reads it. */
static int check_simple(const uint8_t *p, uint32_t len, uint32_t contours)
{
    uint32_t i, last = 0, before = 0, points, ins, at, flag = 0, repeat = 0, coords = 0;
    if ((uint64_t)12 + 2 * contours > len)
        return 0;
    for (i = 0; i < contours; i++) {
        uint32_t end = u16(p + 10 + 2 * i);
        if (i && end <= last)
            return 0; /* contours must end in order */
        before = i ? last + 1 : 0;
        last = end;
    }
    points = last + 1;
    ins = u16(p + 10 + 2 * contours);
    at = 12 + 2 * contours + ins;
    if (at > len)
        return 0;
    for (i = 0; i < points; i++) {
        if (repeat) {
            repeat--;
        } else {
            if (at >= len)
                return 0;
            flag = p[at++];
            if (flag & 8) {
                if (at >= len)
                    return 0;
                repeat = p[at++];
            }
        }
        coords += (flag & 2) ? 1 : (flag & 16) ? 0 : 2;
        coords += (flag & 4) ? 1 : (flag & 32) ? 0 : 2;
    }
    /* A last contour of one point off the curve: stb_truetype looks at the
       point after it, past the end. */
    if (before == last && !(flag & 1))
        return 0;
    return coords <= len - at;
}

/* A composite glyph's parts. children: where to note them, or NULL. */
static int check_composite(const font *f, const uint8_t *p, uint32_t len, uint32_t *children, uint32_t *n)
{
    uint32_t at = 10, count = 0, flags;
    do {
        uint32_t g;
        if (count == MAX_COMPONENTS || at + 4 > len)
            return 0;
        flags = u16(p + at);
        g = u16(p + at + 2);
        at += 4;
        if (!(flags & 2))
            return 0; /* matching points: stb_truetype can't, and would read the wrong bytes */
        at += (flags & 1) ? 4 : 2;
        if (flags & 8)
            at += 2;
        else if (flags & 0x40)
            at += 4;
        else if (flags & 0x80)
            at += 8;
        if (at > len)
            return 0;
        if (children && g < f->nglyphs)
            children[count] = g;
        else if (children)
            children[count] = 0xFFFFFFFFu;
        count++;
    } while (flags & 0x20);
    *n = count;
    return 1;
}

/* How deep glyph g's composites go; -1 for a loop or too deep. */
static int glyph_depth(font *f, uint32_t g, int level)
{
    uint32_t from, to, children[MAX_COMPONENTS], n, i;
    int deepest = 0;
    if (f->depth[g] > 0)
        return f->depth[g];
    if (f->depth[g] < 0 || level > MAX_DEPTH)
        return -1;
    glyph_range(f, g, &from, &to);
    if (to - from >= 10 && s16(f->d + f->glyf.off + from) < 0) {
        f->depth[g] = -1;
        if (!check_composite(f, f->d + f->glyf.off + from, to - from, children, &n))
            return -1;
        for (i = 0; i < n; i++) {
            int d = children[i] == 0xFFFFFFFFu ? 0 : glyph_depth(f, children[i], level + 1);
            if (d < 0)
                return -1;
            if (d > deepest)
                deepest = d;
        }
    }
    f->depth[g] = (int8_t)(deepest + 1);
    return f->depth[g] > MAX_DEPTH ? -1 : f->depth[g];
}

static int check_glyphs(font *f)
{
    uint32_t g, from, to, n;
    for (g = 0; g < f->nglyphs; g++) {
        const uint8_t *p;
        int contours;
        glyph_range(f, g, &from, &to);
        if (from == to)
            continue;
        if (to - from < 10)
            return 0;
        p = f->d + f->glyf.off + from;
        contours = s16(p);
        if (contours > 0 && !check_simple(p, to - from, (uint32_t)contours))
            return 0;
        if (contours < 0 && !check_composite(f, p, to - from, NULL, &n))
            return 0;
    }
    f->depth = calloc(f->nglyphs, 1);
    if (!f->depth)
        return 0;
    for (g = 0; g < f->nglyphs; g++)
        if (glyph_depth(f, g, 0) < 0)
            return 0;
    return 1;
}

/* The cmap subtable stbtt_InitFont chooses: the last Unicode one. */
static int check_cmap(font *f)
{
    uint8_t *cmap = f->d + f->cmap.off;
    uint32_t n, i, sub = 0, found = 0, avail;
    if (f->cmap.len < 4)
        return 0;
    n = u16(cmap + 2);
    if ((uint64_t)4 + 8 * n > f->cmap.len)
        return 0;
    for (i = 0; i < n; i++) {
        uint32_t platform = u16(cmap + 4 + 8 * i), encoding = u16(cmap + 6 + 8 * i);
        if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) {
            sub = u32(cmap + 8 + 8 * i);
            found = 1;
        }
    }
    if (!found || sub > f->cmap.len || f->cmap.len - sub < 14)
        return 0;
    avail = f->cmap.len - sub;
    cmap += sub;
    switch (u16(cmap)) {
    case 0: /* stb_truetype trusts the subtable's own length */
        return u16(cmap + 2) >= 6 && u16(cmap + 2) <= avail;
    case 6:
        return (uint64_t)10 + 2 * u16(cmap + 8) <= avail;
    case 12:
    case 13:
        return avail >= 16 && u32(cmap + 12) <= (avail - 16) / 12;
    case 4: {
        uint32_t segs = u16(cmap + 6) / 2, range = 1, selector = 0, k;
        if (!segs || (uint64_t)16 + 8 * segs > avail)
            return 0;
        /* The binary search goes where these fields say: make them right. */
        while (range * 2 <= segs)
            range *= 2, selector++;
        cmap[8] = (uint8_t)(range * 2 >> 8), cmap[9] = (uint8_t)(range * 2);
        cmap[10] = (uint8_t)(selector >> 8), cmap[11] = (uint8_t)selector;
        cmap[12] = (uint8_t)((segs - range) * 2 >> 8), cmap[13] = (uint8_t)((segs - range) * 2);
        /* Each segment's glyph array lies inside the font. */
        for (k = 0; k < segs; k++) {
            uint32_t end = u16(cmap + 14 + 2 * k), start = u16(cmap + 16 + 2 * segs + 2 * k);
            uint32_t ro = u16(cmap + 16 + 6 * segs + 2 * k);
            uint64_t last;
            if (!ro || start > end)
                continue;
            last = (uint64_t)f->cmap.off + sub + 16 + 6 * segs + 2 * k + ro + 2 * (uint64_t)(end - start);
            if (last + 2 > f->len)
                return 0;
        }
        return 1;
    }
    default: /* format 2, and anything stb_truetype doesn't know */
        return 0;
    }
}

/* Every table with t's tag: stb_truetype would find the next one. */
static void hide(font *f, table *t)
{
    uint8_t tag[4];
    uint32_t i;
    if (t->tag) {
        memcpy(tag, t->tag, 4);
        for (i = 0; i < f->ntables; i++)
            if (memcmp(f->dir + 16 * i, tag, 4) == 0)
                memcpy(f->dir + 16 * i, "xxxx", 4);
    }
    memset(t, 0, sizeof *t);
}

int tc_font_check(uint8_t *data, size_t len, uint32_t start)
{
    font f;
    int ok;
    memset(&f, 0, sizeof f);
    f.d = data;
    f.len = len;
    ok = read_directory(&f, start) && check_metrics(&f) && check_loca(&f) && check_glyphs(&f) && check_cmap(&f);
    free(f.depth);
    if (!ok)
        return TC_ERR_DECODE;
    /* Kerning: GPOS is too intricate to check, and kern must hold its pairs. */
    hide(&f, &f.gpos);
    if (f.kern.tag && (f.kern.len < 18 || (uint64_t)18 + 6 * u16(f.d + f.kern.off + 10) > f.kern.len))
        hide(&f, &f.kern);
    return TC_OK;
}
