/* Fonts: faces from TrueType files, CSS font matching, and text measured
 * and drawn through a cache of rasterised glyphs. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_truetype.h"
#include "tc_font.h"
#include "tc_font_check.h"
#include "tc_woff.h"
#include "tc_platform.h"
#include "tc_utf8.h"

#define GLYPH_BUDGET (2u << 20) /* bytes of glyph bitmaps before the cache is emptied */
#define ITALIC_SHEAR 0.2f       /* synthesised italics lean like this */
#define BIG_GLYPH (128 * 128)   /* pixels: bigger glyphs aren't cached, and are drawn a visible piece at a time */

enum { FACE_UNLOADED, FACE_LOADED, FACE_FAILED };
enum { COVERAGE_UNKNOWN, COVERAGE_READY, COVERAGE_FAILED };

#define MAX_FONT_FILE (64L << 20)
#define MAX_COLLECTION 16       /* faces read from one collection */
#define MAX_TABLES 256
#define MAX_RANGES (1u << 17)   /* character ranges in a cmap */
#define FALLBACK_CACHE 64
#define MAX_PREFER 4            /* web families a font remembers from its font-family */

typedef struct face {
    char *path;                 /* NULL: added from memory, data holds it */
    char *family;
    uint16_t weight, weight_max; /* what it matches: a range for a variable web font */
    uint8_t italic;
    uint16_t own_weight;        /* what it looks like, as the file says: synthesis goes by this */
    uint8_t own_italic;
    uint32_t *urange;           /* @font-face's unicode-range: first, last... sorted; NULL for all */
    uint32_t nurange;
    uint8_t state;
    uint8_t coverage;           /* COVERAGE_* */
    int index;                  /* in a collection */
    unsigned char *data;
    size_t size;
    stbtt_fontinfo info;
    uint32_t *ranges;           /* the characters it has: first, last, first, last... sorted */
    uint32_t nranges;
    /* In font units. */
    int upem, ascent, descent, line_gap, x_height, cap_height;
    int underline_pos, underline_thickness, strikeout_pos, strikeout_size;
} face;

typedef struct glyph {
    int32_t id;                 /* -1: an empty slot */
    int16_t w, h, xoff, yoff;
    uint8_t *bitmap;            /* NULL until rasterised; see no_bitmap and big_glyph */
    float advance;
} glyph;

/* A glyph's bitmap when it has nothing to draw, or is too big to cache. */
static uint8_t no_bitmap, big_glyph;

struct tc_font {
    tc_font_set *set;
    face *face;                 /* NULL: the box font */
    tc_font *base;              /* with preferences: the same font without, whose glyphs it uses */
    const char *prefer[MAX_PREFER]; /* web families to look in first for a character it lacks */
    uint32_t nprefer;
    float size, scale;
    uint8_t fake_bold, fake_italic;
    tc_font_metrics metrics;
    int32_t ascii[128];         /* glyph ids, -1 until looked up */
    glyph *glyphs;              /* hashed by id */
    uint32_t glyphs_cap, nglyphs;
    struct {
        uint32_t cp;            /* 0: empty */
        tc_font *font;          /* NULL: no font has it */
    } fallback[FALLBACK_CACHE];  /* fonts for characters this one lacks */
};

#define MATCH_CACHE 64

struct tc_font_set {
    tc_mutex *lock;             /* a child shares its parent's */
    tc_font_set *parent;        /* a page's web fonts: the set behind them */
    face **faces;               /* each allocated alone: fonts point to them */
    uint32_t nfaces, faces_cap;
    tc_font **fonts;
    uint32_t nfonts, fonts_cap;
    size_t glyph_bytes;
    struct {
        char *families;         /* the names, each with its NUL */
        size_t len;
        int weight, italic;
        float size;
        const tc_font *font;
    } cache[MATCH_CACHE];
};

/* A character's glyph, 0 if none. A glyph number a broken cmap gives past
   the font's glyphs counts as none: stb_truetype would read its metrics
   from beyond the hmtx table. */
static int glyph_index(const stbtt_fontinfo *info, uint32_t cp)
{
    int g = stbtt_FindGlyphIndex(info, (int)cp);
    return g > 0 && g < info->numGlyphs ? g : 0;
}

static uint16_t u16(const unsigned char *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static int16_t s16(const unsigned char *p)
{
    return (int16_t)u16(p);
}

static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

static char *copy_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *out = malloc(n);
    if (out)
        memcpy(out, s, n);
    return out;
}

/* ---- Reading font files ---- */

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data = NULL;
    long len;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 12 && len < MAX_FONT_FILE && fseek(f, 0, SEEK_SET) == 0 &&
        (data = malloc((size_t)len)) != NULL) {
        if (fread(data, 1, (size_t)len, f) != (size_t)len) {
            free(data);
            data = NULL;
        } else {
            *size = (size_t)len;
        }
    }
    fclose(f);
    return data;
}

/* A table's offset and length, or 0. */
static uint32_t find_table(const unsigned char *data, size_t size, uint32_t start, const char *tag, uint32_t *len)
{
    uint32_t n, i;
    if (start + 12 > size)
        return 0;
    n = u16(data + start + 4);
    for (i = 0; i < n && start + 12 + 16 * (i + 1) <= size; i++) {
        const unsigned char *rec = data + start + 12 + 16 * i;
        if (memcmp(rec, tag, 4) == 0) {
            uint32_t off = u32(rec + 8), l = u32(rec + 12);
            if (off > size || l > size - off)
                return 0;
            *len = l;
            return off;
        }
    }
    return 0;
}

/* A font file's bytes: in memory, or read from disk a piece at a time, so
   that finding a face's names or characters doesn't read all of a 10 MB
   collection. */
typedef struct font_source {
    FILE *file;
    const unsigned char *data;
    size_t size;
} font_source;

static int source_open(font_source *src, const char *path)
{
    long len;
    memset(src, 0, sizeof *src);
    if (!(src->file = fopen(path, "rb")))
        return 0;
    if (fseek(src->file, 0, SEEK_END) != 0 || (len = ftell(src->file)) <= 12 || len >= MAX_FONT_FILE) {
        fclose(src->file);
        src->file = NULL;
        return 0;
    }
    src->size = (size_t)len;
    return 1;
}

static void source_close(font_source *src)
{
    if (src->file)
        fclose(src->file);
    src->file = NULL;
}

/* len bytes at off, copied, or NULL if they aren't all there. */
static unsigned char *source_read(font_source *src, uint32_t off, uint32_t len)
{
    unsigned char *out;
    if (off > src->size || len > src->size - off || !(out = malloc(len ? len : 1)))
        return NULL;
    if (src->data) {
        memcpy(out, src->data + off, len);
    } else if (fseek(src->file, (long)off, SEEK_SET) != 0 || fread(out, 1, len, src->file) != len) {
        free(out);
        return NULL;
    }
    return out;
}

/* How many faces the file has (a collection has several), and where face
   index starts; -1 if it has no such face. */
static long face_start(font_source *src, int index, int *count)
{
    unsigned char *head = source_read(src, 0, 12), *off;
    long start = -1;
    *count = 0;
    if (!head)
        return -1;
    if (memcmp(head, "ttcf", 4) == 0) {
        *count = (int)TC_MIN(u32(head + 8), MAX_COLLECTION);
        if (index >= 0 && index < *count && (off = source_read(src, 12 + 4 * (uint32_t)index, 4)) != NULL) {
            start = (long)u32(off);
            free(off);
        }
    } else {
        *count = 1;
        start = index == 0 ? 0 : -1;
    }
    free(head);
    return start;
}

/* A table of the face starting at start: its offset and length, or 0. */
static uint32_t source_table(font_source *src, uint32_t start, const char *tag, uint32_t *len)
{
    unsigned char *dir = source_read(src, start, 12), *recs;
    uint32_t n, i, found = 0;
    if (!dir)
        return 0;
    n = TC_MIN(u16(dir + 4), MAX_TABLES);
    free(dir);
    recs = source_read(src, start + 12, 16 * n);
    for (i = 0; recs && i < n; i++)
        if (memcmp(recs + 16 * i, tag, 4) == 0) {
            uint32_t off = u32(recs + 16 * i + 8), l = u32(recs + 16 * i + 12);
            if (off <= src->size && l <= src->size - off) {
                found = off;
                *len = l;
            }
            break;
        }
    free(recs);
    return found;
}

/* The family from a 'name' table, as UTF-8: the typographic family (16)
   rather than the legacy one (1), in English if there's one. */
static char *family_name(const unsigned char *t, uint32_t len)
{
    uint32_t count, strings, i, best = 0;
    int best_score = 0;
    char *out, *p;
    if (len < 6)
        return NULL;
    count = u16(t + 2);
    strings = u16(t + 4);
    for (i = 0; i < count && 6 + 12 * (i + 1) <= len; i++) {
        const unsigned char *r = t + 6 + 12 * i;
        int platform = u16(r), encoding = u16(r + 2), lang = u16(r + 4), id = u16(r + 6), score;
        if (id != 1 && id != 16)
            continue;
        if (platform == 3 && encoding == 1 && lang == 0x409)
            score = 4;
        else if (platform == 3 && encoding == 0 && lang == 0x409)
            score = 3;
        else if (platform == 1 && encoding == 0 && lang == 0)
            score = 2;
        else if (platform == 3 && (encoding == 1 || encoding == 10))
            score = 1;
        else
            continue;
        score += id == 16 ? 10 : 0;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    if (!best_score) {
        return NULL;
    } else {
        const unsigned char *r = t + 6 + 12 * best, *str;
        uint32_t n = u16(r + 8), off = strings + u16(r + 10), k;
        if (off > len || n > len - off || !(out = p = malloc((size_t)n * 3 + 1)))
            return NULL;
        str = t + off;
        if (u16(r) == 3) {
            for (k = 0; k + 1 < n; k += 2) {
                uint32_t cp = u16(str + k);
                if (cp >= 0xD800 && cp <= 0xDFFF)
                    cp = TC_REPLACEMENT_CHAR;
                p += tc_utf8_encode(cp, p);
            }
        } else {
            for (k = 0; k < n; k++)
                *p++ = str[k] & 0x80 ? '?' : (char)str[k];
        }
        *p = '\0';
        if (!*out) {
            free(out);
            return NULL;
        }
        return out;
    }
}

static int compare_ranges(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* The characters a cmap maps, as sorted, merged ranges: from its Unicode
   subtable, the full repertoire (format 12) if there is one, else the
   Basic Multilingual Plane's (format 4). A format 4 segment counts as a
   whole; the glyph lookup has the last word. */
static int cmap_ranges(const unsigned char *t, uint32_t len, uint32_t **out, uint32_t *nout)
{
    uint32_t i, n, best = 0, *r = NULL, nr = 0, k;
    int best_rank = 0;
    *out = NULL;
    *nout = 0;
    if (len < 4)
        return 0;
    n = u16(t + 2);
    for (i = 0; i < n && 4 + 8 * (i + 1) <= len; i++) {
        const unsigned char *rec = t + 4 + 8 * i;
        int platform = u16(rec), encoding = u16(rec + 2), rank = 0, format;
        uint32_t off = u32(rec + 4);
        if (off > len - 2)
            continue;
        format = u16(t + off);
        if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10)))
            rank = format == 12 ? 2 : format == 4 ? 1 : 0;
        if (rank > best_rank) {
            best_rank = rank;
            best = off;
        }
    }
    if (best_rank == 2 && best + 16 <= len) {
        uint32_t groups = u32(t + best + 12);
        if (groups > MAX_RANGES || groups > (len - best - 16) / 12)
            return 0;
        r = malloc(((size_t)groups * 2 + 2) * sizeof *r);
        for (k = 0; r && k < groups; k++) {
            const unsigned char *g = t + best + 16 + 12 * k;
            uint32_t first = u32(g), last = u32(g + 4);
            if (first <= last && last <= 0x10FFFF) {
                r[2 * nr] = first;
                r[2 * nr + 1] = last;
                nr++;
            }
        }
    } else if (best_rank == 1 && best + 14 <= len) {
        uint32_t segs = u16(t + best + 6) / 2;
        const unsigned char *ends = t + best + 14, *starts = ends + 2 * segs + 2;
        if (best + 16 + 4 * (size_t)segs > len)
            return 0;
        r = malloc(((size_t)segs * 2 + 2) * sizeof *r);
        for (k = 0; r && k < segs; k++) {
            uint32_t first = u16(starts + 2 * k), last = u16(ends + 2 * k);
            if (first <= last && first != 0xFFFF) {
                r[2 * nr] = first;
                r[2 * nr + 1] = last;
                nr++;
            }
        }
    } else {
        return 0;
    }
    if (!r)
        return 0;
    qsort(r, nr, 2 * sizeof *r, compare_ranges);
    for (i = 0, k = 0; i < nr; i++) {
        if (k && r[2 * i] <= r[2 * k - 1] + 1) {
            if (r[2 * i + 1] > r[2 * k - 1])
                r[2 * k - 1] = r[2 * i + 1];
        } else {
            r[2 * k] = r[2 * i];
            r[2 * k + 1] = r[2 * i + 1];
            k++;
        }
    }
    *out = r;
    *nout = k;
    return 1;
}

static int add_face(tc_font_set *set, const char *path, const char *family, int weight, int italic, int index)
{
    face *f;
    if (set->nfaces == set->faces_cap) {
        uint32_t cap = set->faces_cap ? set->faces_cap * 2 : 32;
        face **grown = realloc(set->faces, cap * sizeof *grown);
        if (!grown)
            return TC_ERR_NOMEM;
        set->faces = grown;
        set->faces_cap = cap;
    }
    f = calloc(1, sizeof *f);
    if (!f)
        return TC_ERR_NOMEM;
    f->path = path ? copy_str(path) : NULL;
    f->family = copy_str(family);
    if ((path && !f->path) || !f->family) {
        free(f->path);
        free(f->family);
        free(f);
        return TC_ERR_NOMEM;
    }
    set->faces[set->nfaces] = f;
    if (weight < 10)
        weight *= 100; /* some old fonts give 1 to 9 */
    f->weight = f->weight_max = f->own_weight = (uint16_t)TC_CLAMP(weight, 1, 1000);
    f->italic = f->own_italic = (uint8_t)(italic != 0);
    f->index = index;
    set->nfaces++;
    return TC_OK;
}

/* Matches and fallbacks found before a face was added may be wrong now. */
static void forget_matches(tc_font_set *set)
{
    uint32_t i;
    for (i = 0; i < MATCH_CACHE; i++)
        set->cache[i].font = NULL;
    for (i = 0; i < set->nfonts; i++)
        memset(set->fonts[i]->fallback, 0, sizeof set->fonts[i]->fallback);
}

/* What @font-face says about a web font. */
typedef struct web_face {
    int weight_min, weight_max, italic;
    const uint32_t *ranges;
    uint32_t nranges;
} web_face;

static int compare_pairs(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* Give a face its rule's descriptors; the ones the file gave become its own. */
static int set_web(face *f, const web_face *web)
{
    uint32_t i, k = 0;
    f->own_weight = f->weight;
    f->own_italic = f->italic;
    f->weight = (uint16_t)TC_CLAMP(web->weight_min, 1, 1000);
    f->weight_max = (uint16_t)TC_CLAMP(web->weight_max, f->weight, 1000);
    f->italic = (uint8_t)(web->italic != 0);
    if (!web->ranges || !web->nranges)
        return TC_OK;
    if (!(f->urange = malloc(web->nranges * 2 * sizeof *f->urange)))
        return TC_ERR_NOMEM;
    memcpy(f->urange, web->ranges, web->nranges * 2 * sizeof *f->urange);
    qsort(f->urange, web->nranges, 2 * sizeof *f->urange, compare_pairs);
    /* Merged where they overlap, for the binary search. */
    for (i = 0; i < web->nranges; i++) {
        uint32_t first = f->urange[2 * i], last = f->urange[2 * i + 1];
        if (k && first <= f->urange[2 * k - 1] + 1) {
            f->urange[2 * k - 1] = TC_MAX(f->urange[2 * k - 1], last);
        } else {
            f->urange[2 * k] = first;
            f->urange[2 * k + 1] = last;
            k++;
        }
    }
    f->nurange = k;
    return TC_OK;
}

/* Add each face of a font file or collection, named as it names itself
   (or family), reading only the tables that say so. A face from memory
   keeps a copy of the bytes. */
static int add_faces(tc_font_set *set, font_source *src, const char *path, const char *family, const web_face *web)
{
    int count = 1, i, err = TC_OK, added = 0;
    for (i = 0; i < count && err == TC_OK && !(web && added); i++) {
        long start = face_start(src, i, &count);
        uint32_t len = 0, table;
        unsigned char *t, *copy = NULL;
        char *name = NULL;
        int w = 400, it = 0;
        if (start < 0)
            continue;
        if ((table = source_table(src, (uint32_t)start, "name", &len)) != 0 &&
            (t = source_read(src, table, TC_MIN(len, 1u << 20))) != NULL) {
            name = family_name(t, TC_MIN(len, 1u << 20));
            free(t);
        }
        if (!name && !family)
            continue;
        if ((table = source_table(src, (uint32_t)start, "OS/2", &len)) != 0 && len >= 64 &&
            (t = source_read(src, table, 64)) != NULL) {
            w = u16(t + 4);
            it = u16(t + 62) & 1;
            free(t);
        } else if ((table = source_table(src, (uint32_t)start, "head", &len)) != 0 && len >= 54 &&
                   (t = source_read(src, table, 54)) != NULL) {
            int mac = u16(t + 44);
            w = mac & 1 ? 700 : 400;
            it = (mac & 2) != 0;
            free(t);
        }
        if (src->data) {
            /* From memory, so from the web: checked first (decision 38). */
            if (!(copy = malloc(src->size))) {
                free(name);
                return TC_ERR_NOMEM;
            }
            memcpy(copy, src->data, src->size);
            if (tc_font_check(copy, src->size, (uint32_t)start) != TC_OK) {
                free(copy);
                free(name);
                continue;
            }
        }
        tc_mutex_lock(set->lock);
        err = add_face(set, path, family ? family : name, w, it, i);
        if (!err && copy) {
            face *f = set->faces[set->nfaces - 1];
            f->data = copy;
            f->size = src->size;
            copy = NULL;
            if (web && (err = set_web(f, web)) != TC_OK) {
                set->nfaces--;
                free(f->data);
                free(f->family);
                free(f);
            }
        }
        if (!err)
            forget_matches(set);
        tc_mutex_unlock(set->lock);
        free(copy);
        free(name);
        added += !err;
    }
    return err ? err : added ? TC_OK : TC_ERR_INVALID;
}

int tc_font_set_add_file(tc_font_set *set, const char *path, const char *family, int weight, int italic)
{
    font_source src;
    uint32_t k;
    int err;

    for (k = 0; k < set->nfaces; k++)
        if (set->faces[k]->path && strcmp(set->faces[k]->path, path) == 0)
            return TC_OK;
    if (family) {
        tc_mutex_lock(set->lock);
        err = add_face(set, path, family, weight, italic, 0);
        tc_mutex_unlock(set->lock);
        return err;
    }
    if (!source_open(&src, path))
        return TC_ERR_INVALID;
    err = add_faces(set, &src, path, NULL, NULL);
    source_close(&src);
    return err;
}

static int add_data(tc_font_set *set, const void *data, size_t len, const char *family, const web_face *web)
{
    font_source src;
    tc_buf sfnt;
    int err;
    memset(&src, 0, sizeof src);
    src.data = data;
    src.size = len;
    if (!data || len <= 12 || len >= (size_t)MAX_FONT_FILE)
        return TC_ERR_INVALID;
    if (!tc_woff_sniff(data, len))
        return add_faces(set, &src, NULL, family, web);
    /* WOFF or WOFF2: the TrueType font inside first. */
    tc_buf_init(&sfnt);
    err = tc_woff_decode(data, len, &sfnt);
    if (!err) {
        src.data = (const unsigned char *)sfnt.data;
        src.size = sfnt.len;
        err = add_faces(set, &src, NULL, family, web);
    }
    tc_buf_free(&sfnt);
    return err;
}

int tc_font_set_add_data(tc_font_set *set, const void *data, size_t len, const char *family)
{
    return add_data(set, data, len, family, NULL);
}

int tc_font_set_add_web(tc_font_set *set, const void *data, size_t len, const char *family, int weight_min,
                        int weight_max, int italic, const uint32_t *ranges, uint32_t nranges)
{
    web_face web;
    web.weight_min = weight_min;
    web.weight_max = weight_max;
    web.italic = italic;
    web.ranges = ranges;
    web.nranges = nranges;
    return family ? add_data(set, data, len, family, &web) : TC_ERR_INVALID;
}

struct add_ctx {
    tc_font_set *set;
    size_t added;
};

static void add_dir_file(void *ctx, const char *path)
{
    struct add_ctx *c = ctx;
    uint32_t before = c->set->nfaces;
    tc_font_set_add_file(c->set, path, NULL, 0, 0);
    c->added += c->set->nfaces - before;
}

size_t tc_font_set_add_dir(tc_font_set *set, const char *dir)
{
    struct add_ctx c;
    c.set = set;
    c.added = 0;
    tc_dir_list(dir, ".ttf", add_dir_file, &c);
    tc_dir_list(dir, ".ttc", add_dir_file, &c);
    tc_dir_list(dir, ".otf", add_dir_file, &c);
    return c.added;
}

static void add_system_font(void *ctx, const char *path, const char *family, int weight, int italic)
{
    struct add_ctx *c = ctx;
    uint32_t before = c->set->nfaces;
    tc_font_set_add_file(c->set, path, family, weight, italic);
    c->added += c->set->nfaces - before;
}

size_t tc_font_set_add_system(tc_font_set *set)
{
    struct add_ctx c;
    c.set = set;
    c.added = 0;
    tc_system_fonts(add_system_font, &c);
    return c.added;
}

size_t tc_font_set_count(const tc_font_set *set)
{
    return set->nfaces;
}

const char *tc_font_set_face(const tc_font_set *set, size_t i, int *weight, int *italic)
{
    if (i >= set->nfaces)
        return NULL;
    *weight = set->faces[i]->weight;
    *italic = set->faces[i]->italic;
    return set->faces[i]->family;
}

/* Read the whole file and the metrics a face needs. */
static int load_face(face *f)
{
    size_t size = 0;
    uint32_t len = 0, os2, post;
    int start, a, d, g, x0, y0, x1, y1;

    if (f->state != FACE_UNLOADED)
        return f->state == FACE_LOADED;
    f->state = FACE_FAILED;
    if (f->data)
        size = f->size; /* from memory */
    else if ((f->data = read_file(f->path, &size)) != NULL)
        f->size = size;
    if (!f->data)
        return 0;
    start = stbtt_GetFontOffsetForIndex(f->data, f->index);
    if (start < 0 || (size_t)start >= size || !stbtt_InitFont(&f->info, f->data, start) || !f->info.head ||
        (size_t)f->info.head + 54 > size) {
        free(f->data);
        f->data = NULL;
        return 0;
    }
    f->upem = u16(f->data + f->info.head + 18);
    if (f->upem < 16)
        f->upem = 2048;
    stbtt_GetFontVMetrics(&f->info, &a, &d, &g);
    f->ascent = a;
    f->descent = -d;
    f->line_gap = g;
    f->underline_pos = f->upem / 10;
    f->underline_thickness = f->upem / 20;
    f->strikeout_pos = f->upem * 3 / 10;
    f->strikeout_size = f->underline_thickness;
    f->x_height = f->upem / 2;
    f->cap_height = f->upem * 7 / 10;
    /* Windows' own ascent and descent, which other browsers use there. */
    os2 = find_table(f->data, size, (uint32_t)start, "OS/2", &len);
    if (os2 && len >= 78) {
        f->ascent = u16(f->data + os2 + 74);
        f->descent = u16(f->data + os2 + 76);
        f->strikeout_size = s16(f->data + os2 + 26);
        f->strikeout_pos = s16(f->data + os2 + 28);
        if (u16(f->data + os2) >= 2 && len >= 90) {
            f->x_height = s16(f->data + os2 + 86);
            f->cap_height = s16(f->data + os2 + 88);
        } else {
            int gx = glyph_index(&f->info, 'x'), gh = glyph_index(&f->info, 'H');
            if (gx && stbtt_GetGlyphBox(&f->info, gx, &x0, &y0, &x1, &y1))
                f->x_height = y1;
            if (gh && stbtt_GetGlyphBox(&f->info, gh, &x0, &y0, &x1, &y1))
                f->cap_height = y1;
        }
    }
    post = find_table(f->data, size, (uint32_t)start, "post", &len);
    if (post && len >= 12) {
        f->underline_pos = -s16(f->data + post + 8);
        f->underline_thickness = s16(f->data + post + 10);
    }
    if (f->underline_thickness <= 0)
        f->underline_thickness = f->upem / 20;
    if (f->strikeout_size <= 0)
        f->strikeout_size = f->underline_thickness;
    f->state = FACE_LOADED;
    return 1;
}

/* ---- The font set ---- */

tc_font_set *tc_font_set_new(void)
{
    tc_font_set *set = calloc(1, sizeof *set);
    if (!set)
        return NULL;
    set->lock = tc_mutex_create();
    if (!set->lock) {
        free(set);
        return NULL;
    }
    return set;
}

tc_font_set *tc_font_set_new_child(tc_font_set *parent)
{
    tc_font_set *set = calloc(1, sizeof *set);
    if (!set)
        return NULL;
    set->lock = parent->lock; /* one lock, so a font may draw with faces from both */
    set->parent = parent;
    return set;
}

static void free_glyphs(tc_font *f)
{
    uint32_t i;
    for (i = 0; i < f->glyphs_cap; i++)
        if (f->glyphs[i].id >= 0 && f->glyphs[i].bitmap && f->glyphs[i].bitmap != &big_glyph && f->glyphs[i].w)
            free(f->glyphs[i].bitmap);
    free(f->glyphs);
    f->glyphs = NULL;
    f->glyphs_cap = f->nglyphs = 0;
}

void tc_font_set_free(tc_font_set *set)
{
    uint32_t i;
    if (!set)
        return;
    for (i = 0; i < set->nfonts; i++) {
        free_glyphs(set->fonts[i]);
        free(set->fonts[i]);
    }
    free(set->fonts);
    for (i = 0; i < set->nfaces; i++) {
        free(set->faces[i]->path);
        free(set->faces[i]->family);
        free(set->faces[i]->data);
        free(set->faces[i]->ranges);
        free(set->faces[i]->urange);
        free(set->faces[i]);
    }
    free(set->faces);
    for (i = 0; i < MATCH_CACHE; i++)
        free(set->cache[i].families);
    if (!set->parent)
        tc_mutex_destroy(set->lock);
    free(set);
}

/* ---- Matching ---- */

/* Generic families and well-known substitutes, in the order to try. The
   Liberation fonts have Arial's, Times New Roman's and Courier New's
   metrics, and stand in for them on the host. */
static const char *const serif_fonts[] = { "Times New Roman", "Liberation Serif", "Tinos", NULL };
static const char *const sans_fonts[] = { "Arial", "Liberation Sans", "Arimo", NULL };
static const char *const mono_fonts[] = { "Courier New", "Liberation Mono", "Cousine", NULL };
static const char *const cursive_fonts[] = { "Comic Sans MS", "Times New Roman", "Liberation Serif", NULL };
static const char *const fantasy_fonts[] = { "Impact", "Comic Sans MS", "Arial", "Liberation Sans", NULL };

static const struct {
    const char *name;
    const char *const *fonts;
} aliases[] = {
    { "serif", serif_fonts }, { "sans-serif", sans_fonts }, { "monospace", mono_fonts },
    { "cursive", cursive_fonts }, { "fantasy", fantasy_fonts }, { "system-ui", sans_fonts },
    { "ui-sans-serif", sans_fonts }, { "ui-serif", serif_fonts }, { "ui-monospace", mono_fonts },
    { "ui-rounded", sans_fonts }, { "math", serif_fonts }, { "fangsong", serif_fonts },
    { "arial", sans_fonts }, { "helvetica", sans_fonts }, { "liberation sans", sans_fonts },
    { "times new roman", serif_fonts }, { "times", serif_fonts }, { "liberation serif", serif_fonts },
    { "courier new", mono_fonts }, { "courier", mono_fonts }, { "liberation mono", mono_fonts },
};

static int weight_penalty(int have, int want)
{
    if (have == want)
        return 0;
    if (want >= 400 && want <= 500) {
        if (have > want && have <= 500)
            return have - want;
        if (have < want)
            return 1000 + want - have;
        return 2000 + have - want;
    }
    if (want < 400)
        return have < want ? want - have : 1000 + have - want;
    return have > want ? have - want : 1000 + want - have;
}

int tc_font_weight_distance(int min, int max, int want)
{
    if (want > max)
        return weight_penalty(max, want);
    return weight_penalty(want > min ? want : min, want);
}

static int in_urange(const face *f, uint32_t cp)
{
    uint32_t lo = 0, hi = f->nurange;
    if (!f->urange)
        return 1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (cp < f->urange[2 * mid])
            hi = mid;
        else if (cp > f->urange[2 * mid + 1])
            lo = mid + 1;
        else
            return 1;
    }
    return 0;
}

static int face_has(face *f, uint32_t cp);

/* The family's face nearest the weight and style, or NULL. cp: one that
   has this character, or 0 for the family's first choice, which is one
   whose unicode-range has a space if the best faces differ only in that. */
static face *pick_face(tc_font_set *set, const char *family, int weight, int italic, uint32_t cp)
{
    face *best = NULL;
    int best_score = 0;
    uint32_t i;
    for (i = 0; i < set->nfaces; i++) {
        face *f = set->faces[i];
        int score;
        if (f->state == FACE_FAILED || !ieq(f->family, family) || (cp && !face_has(f, cp)))
            continue;
        score = tc_font_weight_distance(f->weight, f->weight_max, weight) + (f->italic != (italic != 0)) * 100000;
        score = score * 2 + !in_urange(f, ' ');
        if (!best || score < best_score) {
            best = f;
            best_score = score;
        }
    }
    return best;
}

static face *find_family(tc_font_set *set, const char *family, int weight, int italic)
{
    face *f = pick_face(set, family, weight, italic, 0);
    size_t i;
    const char *const *p;
    if (f)
        return f;
    for (i = 0; i < TC_ARRAY_LEN(aliases); i++)
        if (ieq(family, aliases[i].name))
            for (p = aliases[i].fonts; *p; p++)
                if ((f = pick_face(set, *p, weight, italic, 0)) != NULL)
                    return f;
    return NULL;
}

static void set_metrics(tc_font *f)
{
    tc_font_metrics *m = &f->metrics;
    const face *fc = f->face;
    float s = f->scale;
    if (!fc) {
        m->ascent = 0.8f * f->size;
        m->descent = 0.2f * f->size;
        m->line_gap = 0;
        m->x_height = 0.5f * f->size;
        m->cap_height = 0.7f * f->size;
        m->underline_offset = 0.1f * f->size;
        m->underline_thickness = f->size / 16 > 1 ? f->size / 16 : 1;
        m->strikeout_offset = 0.3f * f->size;
        m->strikeout_thickness = m->underline_thickness;
        m->space_width = 0.3f * f->size;
        return;
    }
    m->ascent = fc->ascent * s;
    m->descent = fc->descent * s;
    m->line_gap = fc->line_gap * s;
    m->x_height = fc->x_height * s;
    m->cap_height = fc->cap_height * s;
    m->underline_offset = fc->underline_pos * s;
    m->underline_thickness = fc->underline_thickness * s;
    m->strikeout_offset = fc->strikeout_pos * s;
    m->strikeout_thickness = fc->strikeout_size * s;
    if (m->underline_thickness < 1)
        m->underline_thickness = 1;
    if (m->strikeout_thickness < 1)
        m->strikeout_thickness = 1;
    {
        int adv, lsb, g = glyph_index(&fc->info, ' ');
        stbtt_GetGlyphHMetrics(&fc->info, g, &adv, &lsb);
        m->space_width = adv * s + f->fake_bold;
    }
}

static tc_font *get_font(tc_font_set *set, face *fc, float size, int fake_bold, int fake_italic)
{
    tc_font *f;
    uint32_t i;
    for (i = 0; i < set->nfonts; i++) {
        f = set->fonts[i];
        if (f->face == fc && f->size == size && f->fake_bold == fake_bold && f->fake_italic == fake_italic &&
            !f->base)
            return f;
    }
    if (set->nfonts == set->fonts_cap) {
        uint32_t cap = set->fonts_cap ? set->fonts_cap * 2 : 16;
        tc_font **grown = realloc(set->fonts, cap * sizeof *grown);
        if (!grown)
            return NULL;
        set->fonts = grown;
        set->fonts_cap = cap;
    }
    f = calloc(1, sizeof *f);
    if (!f)
        return NULL;
    f->set = set;
    f->face = fc;
    f->size = size;
    f->scale = fc ? size / fc->upem : 0;
    f->fake_bold = (uint8_t)fake_bold;
    f->fake_italic = (uint8_t)fake_italic;
    for (i = 0; i < 128; i++)
        f->ascii[i] = -1;
    set_metrics(f);
    set->fonts[set->nfonts++] = f;
    return f;
}

/* A font that looks in prefer's families first for characters it lacks.
   It shares its glyphs with the font that doesn't. */
static tc_font *get_font_preferring(tc_font_set *set, face *fc, float size, int fake_bold, int fake_italic,
                                    const char *const *prefer, uint32_t nprefer)
{
    tc_font *base = get_font(set, fc, size, fake_bold, fake_italic), *f;
    uint32_t i, k;
    if (!nprefer || !base)
        return base;
    for (i = 0; i < set->nfonts; i++) {
        f = set->fonts[i];
        if (f->base != base || f->nprefer != nprefer)
            continue;
        for (k = 0; k < nprefer && f->prefer[k] == prefer[k]; k++)
            ;
        if (k == nprefer)
            return f;
    }
    if (set->nfonts == set->fonts_cap) {
        uint32_t cap = set->fonts_cap * 2;
        tc_font **grown = realloc(set->fonts, cap * sizeof *grown);
        if (!grown)
            return base;
        set->fonts = grown;
        set->fonts_cap = cap;
    }
    if (!(f = malloc(sizeof *f)))
        return base;
    *f = *base;
    f->base = base;
    f->glyphs = NULL;
    f->glyphs_cap = f->nglyphs = 0;
    memset(f->fallback, 0, sizeof f->fallback);
    for (k = 0; k < nprefer; k++)
        f->prefer[k] = prefer[k];
    f->nprefer = nprefer;
    set->fonts[set->nfonts++] = f;
    return f;
}

static face *any_face(tc_font_set *set)
{
    uint32_t i;
    for (i = 0; i < set->nfaces; i++)
        if (set->faces[i]->state != FACE_FAILED)
            return set->faces[i];
    return NULL;
}

/* The first family that's there: a page's web fonts first, which hide
   installed families of the same name. A web family whose face has no
   space in its unicode-range is passed over, as CSS's "first available
   font" is; its characters come to it as fallbacks. */
static face *choose(tc_font_set *set, const char *const *families, uint32_t count, int weight, int italic)
{
    face *fc = NULL;
    tc_font_set *s, *root = set;
    uint32_t i;
    while (root->parent)
        root = root->parent;
    for (i = 0; i < count && !fc; i++)
        for (s = set; s && !fc; s = s->parent) {
            fc = s->parent ? pick_face(s, families[i], weight, italic, 0) : find_family(s, families[i], weight, italic);
            if (fc && !in_urange(fc, ' '))
                fc = NULL;
        }
    if (!fc)
        fc = find_family(root, "serif", weight, italic);
    return fc ? fc : any_face(root);
}

const tc_font *tc_font_match(tc_font_set *set, const char *const *families, uint32_t count, int weight, int italic,
                             float size)
{
    char key[256];
    size_t len = 0;
    face *fc;
    const tc_font *result;
    const char *prefer[MAX_PREFER];
    uint32_t i, slot, nprefer, h = 2166136261u;

    if (!(size >= 0)) /* NaN too */
        size = 0;
    if (size > 100000)
        size = 100000;
    size = (float)floor(size * 16 + 0.5f) / 16; /* a sixteenth of a pixel is close enough */
    /* Remembered by the names, weight, style and size. */
    for (i = 0; i < count; i++) {
        size_t n = strlen(families[i]) + 1;
        if (len + n > sizeof key) {
            len = 0;
            break;
        }
        memcpy(key + len, families[i], n);
        len += n;
    }
    for (i = 0; i < len; i++)
        h = (h ^ (unsigned char)key[i]) * 16777619u;
    slot = (h ^ (uint32_t)weight * 7u ^ (uint32_t)italic ^ (uint32_t)(size * 16)) % MATCH_CACHE;
    tc_mutex_lock(set->lock);
    if (len && set->cache[slot].font && set->cache[slot].len == len && set->cache[slot].weight == weight &&
        set->cache[slot].italic == italic && set->cache[slot].size == size &&
        memcmp(set->cache[slot].families, key, len) == 0) {
        result = set->cache[slot].font;
        tc_mutex_unlock(set->lock);
        return result;
    }
    /* Faces load on first use; one that won't load is skipped from then on. */
    fc = choose(set, families, count, weight, italic);
    while (fc && !load_face(fc))
        fc = choose(set, families, count, weight, italic);
    /* The list's other web families, for the characters they have (CSS
       looks for each character family by family). */
    nprefer = 0;
    for (i = 0; set->parent && fc && i < count && nprefer < MAX_PREFER; i++) {
        face *web = pick_face(set, families[i], weight, italic, 0);
        if (web && !ieq(web->family, fc->family))
            prefer[nprefer++] = web->family;
    }
    result = get_font_preferring(set, fc, size, fc && weight >= 600 && fc->own_weight <= 500,
                                 fc && italic && !fc->own_italic, prefer, nprefer);
    if (!result)
        result = get_font(set, NULL, size, 0, 0);
    if (result && len) {
        char *copy = malloc(len);
        if (copy) {
            memcpy(copy, key, len);
            free(set->cache[slot].families);
            set->cache[slot].families = copy;
            set->cache[slot].len = len;
            set->cache[slot].weight = weight;
            set->cache[slot].italic = italic;
            set->cache[slot].size = size;
            set->cache[slot].font = result;
        }
    }
    tc_mutex_unlock(set->lock);
    if (!result) {
        /* Out of memory: a font that draws nothing rather than a crash. */
        static tc_font none;
        result = &none;
    }
    return result;
}

int tc_font_set_has_family(tc_font_set *set, const char *family)
{
    tc_font_set *s;
    int found = 0;
    tc_mutex_lock(set->lock);
    for (s = set; s && !found; s = s->parent)
        found = (s->parent ? pick_face(s, family, 400, 0, 0) : find_family(s, family, 400, 0)) != NULL;
    tc_mutex_unlock(set->lock);
    return found;
}

const char *tc_font_family(const tc_font *font, int *fake_bold, int *fake_italic)
{
    if (fake_bold)
        *fake_bold = font->fake_bold;
    if (fake_italic)
        *fake_italic = font->fake_italic;
    return font->face ? font->face->family : "";
}

const tc_font_metrics *tc_font_get_metrics(const tc_font *font)
{
    return &font->metrics;
}

/* ---- Glyphs ---- */

static int32_t glyph_id(tc_font *f, uint32_t cp)
{
    int32_t g;
    if (cp < 128 && f->ascii[cp] >= 0)
        return f->ascii[cp];
    g = in_urange(f->face, cp) ? glyph_index(&f->face->info, cp) : 0;
    if (cp < 128)
        f->ascii[cp] = g;
    return g;
}

static glyph *find_glyph(tc_font *f, int32_t id)
{
    uint32_t mask, i;
    if (f->nglyphs * 2 >= f->glyphs_cap) {
        uint32_t cap = f->glyphs_cap ? f->glyphs_cap * 2 : 128, k;
        glyph *grown = malloc(cap * sizeof *grown);
        if (!grown)
            return NULL;
        for (k = 0; k < cap; k++)
            grown[k].id = -1;
        for (k = 0; k < f->glyphs_cap; k++)
            if (f->glyphs[k].id >= 0) {
                uint32_t j = ((uint32_t)f->glyphs[k].id * 2654435761u) & (cap - 1);
                while (grown[j].id >= 0)
                    j = (j + 1) & (cap - 1);
                grown[j] = f->glyphs[k];
            }
        free(f->glyphs);
        f->glyphs = grown;
        f->glyphs_cap = cap;
    }
    mask = f->glyphs_cap - 1;
    for (i = ((uint32_t)id * 2654435761u) & mask; f->glyphs[i].id >= 0; i = (i + 1) & mask)
        if (f->glyphs[i].id == id)
            return &f->glyphs[i];
    {
        glyph *g = &f->glyphs[i];
        int adv, lsb;
        memset(g, 0, sizeof *g);
        g->id = id;
        stbtt_GetGlyphHMetrics(&f->face->info, id, &adv, &lsb);
        g->advance = adv * f->scale + f->fake_bold;
        f->nglyphs++;
        return g;
    }
}

/* x leaned by y, within a short. */
static stbtt_vertex_type lean(int x, int y)
{
    float v = x + y * ITALIC_SHEAR;
    return (stbtt_vertex_type)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* A glyph's outline, leaned for synthesised italics. */
static int glyph_shape(tc_font *f, int32_t id, stbtt_vertex **v)
{
    int n = stbtt_GetGlyphShape(&f->face->info, id, v), i;
    if (f->fake_italic)
        for (i = 0; i < n; i++) {
            stbtt_vertex *p = &(*v)[i];
            p->x = lean(p->x, p->y);
            p->cx = lean(p->cx, p->cy);
            p->cx1 = lean(p->cx1, p->cy1);
        }
    return n;
}

/* Coverage for the w x h pixels of an outline from (x0, y0), y down from
   the baseline. Synthesised bold smears each pixel one to the right, so
   the column to the left of the window is rasterised too. */
static uint8_t *rasterise_part(tc_font *f, stbtt_vertex *v, int n, int x0, int y0, int w, int h)
{
    int bold = f->fake_bold, x, y;
    stbtt__bitmap bm;

    bm.w = w + bold;
    bm.h = h;
    bm.stride = bm.w;
    bm.pixels = calloc((size_t)bm.w * (size_t)h, 1);
    if (!bm.pixels)
        return NULL;
    stbtt_Rasterize(&bm, 0.35f, v, n, f->scale, f->scale, 0, 0, x0 - bold, y0, 1, NULL);
    if (bold) {
        for (y = 0; y < h; y++)
            for (x = bm.w - 1; x > 0; x--) {
                uint8_t *p = bm.pixels + y * bm.w + x;
                if (p[-1] > p[0])
                    p[0] = p[-1];
            }
        for (y = 0; y < h; y++) /* drop the extra column */
            memmove(bm.pixels + y * w, bm.pixels + y * bm.w + 1, (size_t)w);
    }
    return bm.pixels;
}

/* Find a glyph's bitmap box, and rasterise it if it's small enough to
   cache. Bigger ones are drawn a visible piece at a time (draw_part). */
static void rasterise(tc_font *f, glyph *g)
{
    stbtt_vertex *v;
    int n = glyph_shape(f, g->id, &v), i, w, h, x0, y0, x1, y1;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f, s = f->scale;

    g->bitmap = &no_bitmap;
    g->w = g->h = 0;
    if (n <= 0) {
        stbtt_FreeShape(&f->face->info, v);
        return;
    }
    for (i = 0; i < n; i++) {
        stbtt_vertex *p = &v[i];
        minx = TC_MIN(minx, p->x), maxx = TC_MAX(maxx, p->x);
        miny = TC_MIN(miny, p->y), maxy = TC_MAX(maxy, p->y);
        if (p->type == STBTT_vcurve || p->type == STBTT_vcubic) {
            minx = TC_MIN(minx, p->cx), maxx = TC_MAX(maxx, p->cx);
            miny = TC_MIN(miny, p->cy), maxy = TC_MAX(maxy, p->cy);
        }
        if (p->type == STBTT_vcubic) {
            minx = TC_MIN(minx, p->cx1), maxx = TC_MAX(maxx, p->cx1);
            miny = TC_MIN(miny, p->cy1), maxy = TC_MAX(maxy, p->cy1);
        }
    }
    x0 = (int)floor(minx * s);
    x1 = (int)ceil(maxx * s);
    y0 = (int)floor(-maxy * s);
    y1 = (int)ceil(-miny * s);
    w = x1 - x0 + f->fake_bold;
    h = y1 - y0;
    if (w > 0 && h > 0 && w <= INT16_MAX && h <= INT16_MAX && x0 >= INT16_MIN && y0 >= INT16_MIN) {
        uint8_t *pixels = (long)w * h > BIG_GLYPH ? &big_glyph : rasterise_part(f, v, n, x0, y0, w, h);
        if (pixels) {
            g->bitmap = pixels;
            g->w = (int16_t)w;
            g->h = (int16_t)h;
            g->xoff = (int16_t)x0;
            g->yoff = (int16_t)y0;
            if (pixels != &big_glyph)
                f->set->glyph_bytes += (size_t)w * (size_t)h;
        }
    }
    f->set->glyph_bytes += sizeof *g;
    stbtt_FreeShape(&f->face->info, v);
}

/* Empty every glyph cache once they take too much memory. */
static void trim_cache(tc_font_set *set)
{
    uint32_t i;
    if (set->glyph_bytes <= GLYPH_BUDGET)
        return;
    for (i = 0; i < set->nfonts; i++)
        free_glyphs(set->fonts[i]);
    set->glyph_bytes = 0;
}

/* Coverage to alpha: a little darker than linear, as antialiased text
   looks too light otherwise. */
static unsigned boost(unsigned c)
{
    return c + c * (255 - c) / 512;
}

static void blit(tc_surface *s, tc_rect clip, int x, int y, const glyph *g, tc_color color)
{
    tc_rect r = tc_rect_intersect(tc_rect_intersect(clip, tc_rect_make(0, 0, s->width, s->height)),
                                  tc_rect_make(x, y, g->w, g->h));
    unsigned alpha = TC_ALPHA(color), cr = TC_RED(color), cg = TC_GREEN(color), cb = TC_BLUE(color);
    int row, col;
    if (tc_rect_is_empty(r))
        return;
    for (row = r.y; row < r.y + r.h; row++) {
        const uint8_t *src = g->bitmap + (row - y) * g->w + (r.x - x);
        uint32_t *dst = s->pixels + (size_t)row * (size_t)s->stride + r.x;
        for (col = 0; col < r.w; col++) {
            int a = src[col], dr, dg, db;
            uint32_t d;
            if (!a)
                continue;
            a = (int)(boost((unsigned)a) * alpha / 255);
            d = dst[col];
            dr = (int)((d >> 16) & 255), dg = (int)((d >> 8) & 255), db = (int)(d & 255);
            dr += ((int)cr - dr) * a / 255;
            dg += ((int)cg - dg) * a / 255;
            db += ((int)cb - db) * a / 255;
            dst[col] = (uint32_t)dr << 16 | (uint32_t)dg << 8 | (uint32_t)db;
        }
    }
}

/* A big glyph: rasterise only the part of it inside the clip. */
static void draw_part(tc_font *f, const glyph *g, tc_surface *s, tc_rect clip, int x, int y, tc_color color)
{
    tc_rect r = tc_rect_intersect(tc_rect_intersect(clip, tc_rect_make(0, 0, s->width, s->height)),
                                  tc_rect_make(x, y, g->w, g->h));
    stbtt_vertex *v;
    glyph part;
    int n;

    if (tc_rect_is_empty(r))
        return;
    n = glyph_shape(f, g->id, &v);
    part.bitmap = n > 0 ? rasterise_part(f, v, n, g->xoff + (r.x - x), g->yoff + (r.y - y), r.w, r.h) : NULL;
    stbtt_FreeShape(&f->face->info, v);
    if (part.bitmap) {
        part.w = (int16_t)r.w;
        part.h = (int16_t)r.h;
        blit(s, clip, r.x, r.y, &part, color);
        free(part.bitmap);
    }
}

/* The box font: an outline for each character, for when there are no fonts. */
static float box_text(const tc_font *f, tc_surface *s, tc_rect clip, float x, float y, const char *text, size_t len,
                      float letter_spacing, tc_color color)
{
    float pen = x, adv = 0.6f * f->size;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        i += tc_utf8_decode(text + i, len - i, &cp);
        if (s && !(fabsf(pen) < 1e8f))
            s = NULL; /* letter-spacing took the rest far off the surface */
        if (s && cp > ' ' && f->size >= 4) {
            int bx = (int)(pen + 1), by = (int)(y - 0.7f * f->size), bw = (int)(adv - 2), bh = (int)(0.7f * f->size);
            tc_surface_fill(s, tc_rect_intersect(clip, tc_rect_make(bx, by, bw, 1)), color);
            tc_surface_fill(s, tc_rect_intersect(clip, tc_rect_make(bx, by + bh - 1, bw, 1)), color);
            tc_surface_fill(s, tc_rect_intersect(clip, tc_rect_make(bx, by, 1, bh)), color);
            tc_surface_fill(s, tc_rect_intersect(clip, tc_rect_make(bx + bw - 1, by, 1, bh)), color);
        }
        pen += (cp == ' ' ? f->metrics.space_width : adv) + letter_spacing;
    }
    return pen - x;
}

/* ---- Fallback: characters a font lacks ---- */

/* Read which characters a face has, once. */
static int face_coverage(face *f)
{
    font_source src;
    unsigned char *t;
    uint32_t len = 0, table;
    long start;
    int count, ok = 0;
    if (f->coverage != COVERAGE_UNKNOWN)
        return f->coverage == COVERAGE_READY;
    f->coverage = COVERAGE_FAILED;
    if (f->data) {
        memset(&src, 0, sizeof src);
        src.data = f->data;
        src.size = f->size;
    } else if (!source_open(&src, f->path)) {
        return 0;
    }
    start = face_start(&src, f->index, &count);
    if (start >= 0 && (table = source_table(&src, (uint32_t)start, "cmap", &len)) != 0 && len <= (4u << 20) &&
        (t = source_read(&src, table, len)) != NULL) {
        ok = cmap_ranges(t, len, &f->ranges, &f->nranges);
        free(t);
    }
    source_close(&src);
    if (ok)
        f->coverage = COVERAGE_READY;
    return ok;
}

static int covers(const face *f, uint32_t cp)
{
    uint32_t lo = 0, hi = f->nranges;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (cp < f->ranges[2 * mid])
            hi = mid;
        else if (cp > f->ranges[2 * mid + 1])
            lo = mid + 1;
        else
            return 1;
    }
    return 0;
}

/* Unicode's default ignorables (format controls, variation selectors and
   the like): drawn as nothing when a font lacks them, not as a box. */
static int ignorable(uint32_t cp)
{
    return cp == 0xAD || cp == 0x34F || cp == 0x61C || (cp >= 0x115F && cp <= 0x1160) ||
           (cp >= 0x17B4 && cp <= 0x17B5) || (cp >= 0x180B && cp <= 0x180F) || (cp >= 0x200B && cp <= 0x200F) ||
           (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F) || cp == 0x3164 ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF || cp == 0xFFA0 || (cp >= 0xFFF0 && cp <= 0xFFF8) ||
           (cp >= 0x1BCA0 && cp <= 0x1BCA3) || (cp >= 0x1D173 && cp <= 0x1D17A) || (cp >= 0xE0000 && cp <= 0xE0FFF);
}

/* Whether a face has a character (and its unicode-range lets it). */
static int face_has(face *f, uint32_t cp)
{
    return in_urange(f, cp) && face_coverage(f) && covers(f, cp);
}

/* The first face in s that has cp (only faces of family, or only faces of
   other families, if either is given), as a font like f. */
static tc_font *fallback_in(tc_font *f, tc_font_set *s, uint32_t cp, const char *family, const char *other)
{
    int weight = f->fake_bold ? 700 : f->face->own_weight, italic = f->face->own_italic || f->fake_italic;
    uint32_t i;
    for (i = 0; i < s->nfaces; i++) {
        face *fc = s->faces[i], *best;
        if (fc == f->face || fc->state == FACE_FAILED || (family && !ieq(fc->family, family)) ||
            (other && ieq(fc->family, other)) || !face_has(fc, cp))
            continue;
        /* Its family's face nearest the style that has the character. */
        best = pick_face(s, fc->family, weight, italic, cp);
        if (!best || !load_face(best) || !glyph_index(&best->info, cp))
            best = fc;
        if (!load_face(best) || !glyph_index(&best->info, cp))
            continue;
        return get_font(f->set, best, f->size, weight >= 600 && best->own_weight <= 500, italic && !best->own_italic);
    }
    return NULL;
}

/* A font for a character f lacks, at its size and style, or NULL: the
   first face in the set that has it (decision 36: Windows' fonts are added
   before Tachyon's font pack). In a page's set, its family's other web
   faces come first (unicode-range splits fonts into several), then the
   web families its font-family named, and the page's other web fonts
   last. With the set locked. */
static tc_font *fallback_font(tc_font *f, uint32_t cp)
{
    tc_font_set *s;
    uint32_t slot = cp % FALLBACK_CACHE, k;
    tc_font *found = NULL;
    if (f->fallback[slot].cp == cp)
        return f->fallback[slot].font;
    if (f->set->parent)
        found = fallback_in(f, f->set, cp, f->face->family, NULL);
    for (k = 0; k < f->nprefer && !found; k++)
        found = fallback_in(f, f->set, cp, f->prefer[k], NULL);
    for (s = f->set->parent ? f->set->parent : f->set; s && !found; s = s->parent)
        found = fallback_in(f, s, cp, NULL, NULL);
    if (!found && f->set->parent)
        found = fallback_in(f, f->set, cp, NULL, f->face->family);
    f->fallback[slot].cp = cp;
    f->fallback[slot].font = found;
    return found;
}

const tc_font *tc_font_for_char(const tc_font *font, uint32_t cp)
{
    tc_font *f = (tc_font *)font, *fb = NULL;
    if (!f->set || !f->face || cp < 0x20)
        return font;
    tc_mutex_lock(f->set->lock);
    if (!glyph_id(f, cp) && !ignorable(cp))
        fb = fallback_font(f, cp);
    tc_mutex_unlock(f->set->lock);
    return fb ? fb : font;
}

/* Measure, and draw if s isn't NULL. */
static float run(const tc_font *cf, tc_surface *s, tc_rect clip, float x, float y, const char *text, size_t len,
                 float letter_spacing, tc_color color)
{
    tc_font *f = (tc_font *)cf;
    float pen = x;
    int32_t prev = -1;
    int baseline = 0;
    size_t i = 0;

    if (!(fabsf(x) < 1e8f && fabsf(y) < 1e8f))
        s = NULL; /* far off the surface (or NaN): measure only */
    else
        baseline = (int)floor(y + 0.5f);
    if (!f->set || f->size <= 0)
        return 0;
    if (!f->face)
        return box_text(f, s, clip, x, y, text, len, letter_spacing, color);
    tc_mutex_lock(f->set->lock);
    while (i < len) {
        uint32_t cp;
        int32_t id;
        tc_font *gf = f, *store; /* the font this character comes from, and where its glyphs are */
        glyph *g;
        i += tc_utf8_decode(text + i, len - i, &cp);
        id = glyph_id(f, cp);
        if (!id && cp >= 0x20) {
            tc_font *fb;
            if (ignorable(cp)) {
                prev = -1;
                continue;
            }
            if ((fb = fallback_font(f, cp)) != NULL) {
                gf = fb;
                id = glyph_id(fb, cp);
            }
        }
        if (gf == f && prev > 0 && id)
            pen += stbtt_GetGlyphKernAdvance(&f->face->info, prev, id) * f->scale;
        store = gf->base ? gf->base : gf;
        g = find_glyph(store, id);
        if (!g)
            break;
        if (s && !(fabsf(pen) < 1e8f))
            s = NULL; /* letter-spacing took the rest far off the surface */
        if (s && TC_ALPHA(color)) {
            if (!g->bitmap)
                rasterise(store, g);
            if (g->bitmap == &big_glyph)
                draw_part(store, g, s, clip, (int)floor(pen + 0.5f) + g->xoff, baseline + g->yoff, color);
            else if (g->w)
                blit(s, clip, (int)floor(pen + 0.5f) + g->xoff, baseline + g->yoff, g, color);
        }
        pen += g->advance + letter_spacing;
        prev = gf == f ? id : -1;
    }
    if (s)
        trim_cache(f->set);
    tc_mutex_unlock(f->set->lock);
    return pen - x;
}

float tc_font_measure(const tc_font *font, const char *text, size_t len, float letter_spacing)
{
    tc_rect none = { 0, 0, 0, 0 };
    return run(font, NULL, none, 0, 0, text, len, letter_spacing, 0);
}

float tc_font_draw(const tc_font *font, tc_surface *s, tc_rect clip, float x, float y, const char *text, size_t len,
                   float letter_spacing, tc_color color)
{
    return run(font, s, clip, x, y, text, len, letter_spacing, color);
}
