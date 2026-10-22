/* Fonts: faces from TrueType files, CSS font matching, and text measured
 * and drawn through a cache of rasterised glyphs. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_truetype.h"
#include "tc_font.h"
#include "tc_platform.h"
#include "tc_utf8.h"

#define GLYPH_BUDGET (2u << 20) /* bytes of glyph bitmaps before the cache is emptied */
#define ITALIC_SHEAR 0.2f       /* synthesised italics lean like this */
#define BIG_GLYPH (128 * 128)   /* pixels: bigger glyphs aren't cached, and are drawn a visible piece at a time */

enum { FACE_UNLOADED, FACE_LOADED, FACE_FAILED };

typedef struct face {
    char *path;
    char *family;
    uint16_t weight;
    uint8_t italic;
    uint8_t state;
    int index;                  /* in a collection */
    unsigned char *data;
    stbtt_fontinfo info;
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
    float size, scale;
    uint8_t fake_bold, fake_italic;
    tc_font_metrics metrics;
    int32_t ascii[128];         /* glyph ids, -1 until looked up */
    glyph *glyphs;              /* hashed by id */
    uint32_t glyphs_cap, nglyphs;
};

#define MATCH_CACHE 64

struct tc_font_set {
    tc_mutex *lock;
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
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 12 && len < (64L << 20) && fseek(f, 0, SEEK_SET) == 0 &&
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

/* A name from the 'name' table as UTF-8, or NULL. */
static char *font_name(const stbtt_fontinfo *info, int id)
{
    static const int ids[][3] = { { 3, 1, 0x409 }, { 3, 0, 0x409 }, { 1, 0, 0 } };
    size_t k;
    for (k = 0; k < sizeof ids / sizeof ids[0]; k++) {
        int len, i;
        const char *s = stbtt_GetFontNameString(info, &len, ids[k][0], ids[k][1], ids[k][2], id);
        char *out, *p;
        if (!s || len <= 0)
            continue;
        out = p = malloc((size_t)len * 3 + 1);
        if (!out)
            return NULL;
        if (ids[k][0] == 3) {
            for (i = 0; i + 1 < len; i += 2) {
                uint32_t cp = u16((const unsigned char *)s + i);
                if (cp >= 0xD800 && cp <= 0xDFFF)
                    cp = TC_REPLACEMENT_CHAR;
                p += tc_utf8_encode(cp, p);
            }
        } else {
            for (i = 0; i < len; i++)
                *p++ = s[i] & 0x80 ? '?' : s[i];
        }
        *p = '\0';
        return out;
    }
    return NULL;
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
    f->path = copy_str(path);
    f->family = copy_str(family);
    if (!f->path || !f->family) {
        free(f->path);
        free(f->family);
        free(f);
        return TC_ERR_NOMEM;
    }
    set->faces[set->nfaces] = f;
    if (weight < 10)
        weight *= 100; /* some old fonts give 1 to 9 */
    f->weight = (uint16_t)TC_CLAMP(weight, 1, 1000);
    f->italic = (uint8_t)(italic != 0);
    f->index = index;
    set->nfaces++;
    return TC_OK;
}

int tc_font_set_add_file(tc_font_set *set, const char *path, const char *family, int weight, int italic)
{
    unsigned char *data;
    size_t size = 0;
    int n, i, err = TC_OK;
    uint32_t k;

    for (k = 0; k < set->nfaces; k++)
        if (strcmp(set->faces[k]->path, path) == 0)
            return TC_OK;
    if (family) {
        tc_mutex_lock(set->lock);
        err = add_face(set, path, family, weight, italic, 0);
        tc_mutex_unlock(set->lock);
        return err;
    }
    /* Read the names and style from the file itself. */
    data = read_file(path, &size);
    if (!data)
        return TC_ERR_INVALID;
    n = stbtt_GetNumberOfFonts(data);
    for (i = 0; i < n && i < 16 && err == TC_OK; i++) {
        int start = stbtt_GetFontOffsetForIndex(data, i);
        stbtt_fontinfo info;
        uint32_t os2_len = 0, os2;
        char *name;
        int w = 400, it = 0;
        if (start < 0 || (size_t)start >= size || !stbtt_InitFont(&info, data, start))
            continue;
        name = font_name(&info, 16);
        if (!name)
            name = font_name(&info, 1);
        if (!name)
            continue;
        os2 = find_table(data, size, (uint32_t)start, "OS/2", &os2_len);
        if (os2 && os2_len >= 64) {
            w = u16(data + os2 + 4);
            it = u16(data + os2 + 62) & 1;
        } else if (info.head) {
            int mac = u16(data + info.head + 44);
            w = mac & 1 ? 700 : 400;
            it = (mac & 2) != 0;
        }
        tc_mutex_lock(set->lock);
        err = add_face(set, path, name, w, it, i);
        tc_mutex_unlock(set->lock);
        free(name);
    }
    free(data);
    return err;
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
    f->data = read_file(f->path, &size);
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
            int gx = stbtt_FindGlyphIndex(&f->info, 'x'), gh = stbtt_FindGlyphIndex(&f->info, 'H');
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
        free(set->faces[i]);
    }
    free(set->faces);
    for (i = 0; i < MATCH_CACHE; i++)
        free(set->cache[i].families);
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

/* The family's face nearest the weight and style, or NULL. */
static face *pick_face(tc_font_set *set, const char *family, int weight, int italic)
{
    face *best = NULL;
    int best_score = 0;
    uint32_t i;
    for (i = 0; i < set->nfaces; i++) {
        face *f = set->faces[i];
        int score;
        if (f->state == FACE_FAILED || !ieq(f->family, family))
            continue;
        score = weight_penalty(f->weight, weight) + (f->italic != (italic != 0)) * 100000;
        if (!best || score < best_score) {
            best = f;
            best_score = score;
        }
    }
    return best;
}

static face *find_family(tc_font_set *set, const char *family, int weight, int italic)
{
    face *f = pick_face(set, family, weight, italic);
    size_t i;
    const char *const *p;
    if (f)
        return f;
    for (i = 0; i < TC_ARRAY_LEN(aliases); i++)
        if (ieq(family, aliases[i].name))
            for (p = aliases[i].fonts; *p; p++)
                if ((f = pick_face(set, *p, weight, italic)) != NULL)
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
        int adv, lsb, g = stbtt_FindGlyphIndex(&fc->info, ' ');
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
        if (f->face == fc && f->size == size && f->fake_bold == fake_bold && f->fake_italic == fake_italic)
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

static face *any_face(tc_font_set *set)
{
    uint32_t i;
    for (i = 0; i < set->nfaces; i++)
        if (set->faces[i]->state != FACE_FAILED)
            return set->faces[i];
    return NULL;
}

static face *choose(tc_font_set *set, const char *const *families, uint32_t count, int weight, int italic)
{
    face *fc = NULL;
    uint32_t i;
    for (i = 0; i < count && !fc; i++)
        fc = find_family(set, families[i], weight, italic);
    if (!fc)
        fc = find_family(set, "serif", weight, italic);
    return fc ? fc : any_face(set);
}

const tc_font *tc_font_match(tc_font_set *set, const char *const *families, uint32_t count, int weight, int italic,
                             float size)
{
    char key[256];
    size_t len = 0;
    face *fc;
    const tc_font *result;
    uint32_t i, slot, h = 2166136261u;

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
    result = get_font(set, fc, size, fc && weight >= 600 && fc->weight <= 500, fc && italic && !fc->italic);
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
    g = stbtt_FindGlyphIndex(&f->face->info, (int)cp);
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

/* A glyph's outline, leaned for synthesised italics. */
static int glyph_shape(tc_font *f, int32_t id, stbtt_vertex **v)
{
    int n = stbtt_GetGlyphShape(&f->face->info, id, v), i;
    if (f->fake_italic)
        for (i = 0; i < n; i++) {
            stbtt_vertex *p = &(*v)[i];
            p->x = (stbtt_vertex_type)(p->x + p->y * ITALIC_SHEAR);
            p->cx = (stbtt_vertex_type)(p->cx + p->cy * ITALIC_SHEAR);
            p->cx1 = (stbtt_vertex_type)(p->cx1 + p->cy1 * ITALIC_SHEAR);
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
        glyph *g;
        i += tc_utf8_decode(text + i, len - i, &cp);
        id = glyph_id(f, cp);
        if (prev >= 0 && id && prev)
            pen += stbtt_GetGlyphKernAdvance(&f->face->info, prev, id) * f->scale;
        g = find_glyph(f, id);
        if (!g)
            break;
        if (s && !(fabsf(pen) < 1e8f))
            s = NULL; /* letter-spacing took the rest far off the surface */
        if (s && TC_ALPHA(color)) {
            if (!g->bitmap)
                rasterise(f, g);
            if (g->bitmap == &big_glyph)
                draw_part(f, g, s, clip, (int)floor(pen + 0.5f) + g->xoff, baseline + g->yoff, color);
            else if (g->w)
                blit(s, clip, (int)floor(pen + 0.5f) + g->xoff, baseline + g->yoff, g, color);
        }
        pen += g->advance + letter_spacing;
        prev = id;
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
