#include "tc_dither.h"

#define LEVELS 6         /* cube levels per channel, 51 apart */
#define GREYS 26         /* grey levels: the cube's six and 20 between */
#define GREY_SPREAD 16   /* channels this close: grey (the cube is too coarse for the tint) */

/* An 8x8 Bayer matrix: each threshold as far as can be from the ones
   before it, so any fraction of the cells makes an even pattern. */
static const uint8_t bayer[64] = {
    0,  32, 8,  40, 2,  34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44, 4,  36, 14, 46, 6,  38,
    60, 28, 52, 20, 62, 30, 54, 22,
    3,  35, 11, 43, 1,  33, 9,  41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47, 7,  39, 13, 45, 5,  37,
    63, 31, 55, 23, 61, 29, 53, 21,
};

/* For each channel value: the level below it, and how far it is towards
   the next, 0 to 254. A pixel goes up a level where that's more than the
   cell's threshold (2 to 253), so a level itself never does. */
static struct {
    uint8_t cube_lo[256], cube_frac[256];
    uint8_t grey_lo[256], grey_frac[256];
    uint8_t grey_index[GREYS];
    uint8_t threshold[64];
    int ready;
} t;

static int grey_value(int k)
{
    return (k * 255 + (GREYS - 1) / 2) / (GREYS - 1);
}

static void make_tables(void)
{
    int i, k = 0;
    for (i = 0; i < 256; i++) {
        t.cube_lo[i] = (uint8_t)(i / 51);
        t.cube_frac[i] = (uint8_t)(i % 51 * 5);
        while (k + 1 < GREYS && grey_value(k + 1) <= i)
            k++;
        t.grey_lo[i] = (uint8_t)k;
        t.grey_frac[i] = k + 1 < GREYS ? (uint8_t)((i - grey_value(k)) * 255 / (grey_value(k + 1) - grey_value(k)))
                                       : 0;
    }
    /* Every fifth grey is one of the cube's. */
    for (k = 0; k < GREYS; k++)
        t.grey_index[k] = (uint8_t)(k % 5 ? LEVELS * LEVELS * LEVELS + k - k / 5 - 1 : k / 5 * 43);
    for (i = 0; i < 64; i++)
        t.threshold[i] = (uint8_t)((bayer[i] * 255 + 128) / 64);
    t.ready = 1;
}

void tc_dither_palette(uint32_t colours[TC_DITHER_COLOURS])
{
    int r, g, b, k, n = 0;
    for (r = 0; r < LEVELS; r++)
        for (g = 0; g < LEVELS; g++)
            for (b = 0; b < LEVELS; b++)
                colours[n++] = (uint32_t)(r * 51) << 16 | (uint32_t)(g * 51) << 8 | (uint32_t)(b * 51);
    for (k = 0; k < GREYS; k++)
        if (k % 5)
            colours[n++] = (uint32_t)grey_value(k) * 0x010101u;
}

void tc_dither(const tc_surface *src, tc_rect r, uint8_t *dst, int dst_stride, int ox, int oy)
{
    int x, y;
    if (!t.ready)
        make_tables();
    r = tc_rect_intersect(r, tc_rect_make(0, 0, src->width, src->height));
    for (y = r.y; y < r.y + r.h; y++) {
        const uint32_t *row = src->pixels + (size_t)y * (size_t)src->stride;
        const uint8_t *th = t.threshold + ((unsigned)(y + oy) & 7) * 8;
        uint8_t *out = dst + (size_t)y * (size_t)dst_stride;
        for (x = r.x; x < r.x + r.w; x++) {
            uint32_t c = row[x];
            int cr = (int)(c >> 16 & 255), cg = (int)(c >> 8 & 255), cb = (int)(c & 255);
            int hi = TC_MAX(cr, TC_MAX(cg, cb)), lo = TC_MIN(cr, TC_MIN(cg, cb));
            int cell = th[(unsigned)(x + ox) & 7];
            if (hi - lo <= GREY_SPREAD) {
                int v = (cr + 2 * cg + cb) >> 2;
                out[x] = t.grey_index[t.grey_lo[v] + (t.grey_frac[v] > cell)];
            } else {
                out[x] = (uint8_t)(36 * (t.cube_lo[cr] + (t.cube_frac[cr] > cell)) +
                                   6 * (t.cube_lo[cg] + (t.cube_frac[cg] > cell)) +
                                   (t.cube_lo[cb] + (t.cube_frac[cb] > cell)));
            }
        }
    }
}
