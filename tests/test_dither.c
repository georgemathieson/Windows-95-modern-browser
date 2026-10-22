#include <string.h>

#include "tc_dither.h"
#include "tc_test.h"

void tc_suite_dither(void);

#define W 16
#define H 16

static void test_palette(void)
{
    uint32_t pal[TC_DITHER_COLOURS];
    int i, j, distinct = 1;
    tc_dither_palette(pal);
    TC_CHECK_INT(pal[0], 0x000000);
    TC_CHECK_INT(pal[36 * 1 + 6 * 2 + 3], 0x336699);
    TC_CHECK_INT(pal[215], 0xFFFFFF);
    TC_CHECK_INT(pal[216], 0x0A0A0A);      /* the darkest grey between the cube's */
    TC_CHECK_INT(pal[235], 0xF5F5F5);
    for (i = 0; i < TC_DITHER_COLOURS; i++)
        for (j = i + 1; j < TC_DITHER_COLOURS; j++)
            if (pal[i] == pal[j])
                distinct = 0;
    TC_CHECK(distinct);
}

/* A W x H surface of one colour, dithered: the indexes. */
static void dither_flat(uint32_t colour, uint8_t out[H][W], int ox, int oy)
{
    uint32_t pixels[W * H];
    tc_surface s;
    int i;
    for (i = 0; i < W * H; i++)
        pixels[i] = colour;
    tc_surface_wrap(&s, pixels, W, H, W);
    memset(out, 0xEE, W * H);
    tc_dither(&s, tc_rect_make(0, 0, W, H), &out[0][0], W, ox, oy);
}

/* The palette's average over the area: near the colour dithered. */
static int average_close(uint32_t colour, int tol)
{
    uint32_t pal[TC_DITHER_COLOURS];
    uint8_t out[H][W];
    long sum[3] = { 0, 0, 0 };
    int x, y, c;
    tc_dither_palette(pal);
    dither_flat(colour, out, 0, 0);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            for (c = 0; c < 3; c++)
                sum[c] += (long)(pal[out[y][x]] >> (16 - 8 * c) & 255);
    for (c = 0; c < 3; c++) {
        long want = (long)(colour >> (16 - 8 * c) & 255), got = sum[c] / (W * H);
        if (got < want - tol || got > want + tol)
            return 0;
    }
    return 1;
}

static int all_index(uint8_t out[H][W], int index)
{
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if (out[y][x] != index)
                return 0;
    return 1;
}

static void test_flat_colours(void)
{
    uint8_t out[H][W];
    /* Colours in the palette come out flat: the cube's, and the greys. */
    dither_flat(0xFF336699u, out, 0, 0);
    TC_CHECK(all_index(out, 36 * 1 + 6 * 2 + 3));
    dither_flat(0xFFFFFFFFu, out, 0, 0);
    TC_CHECK(all_index(out, 215));
    dither_flat(0xFF0A0A0Au, out, 0, 0);
    TC_CHECK(all_index(out, 216));
    dither_flat(0xFF999999u, out, 0, 0); /* 153: one of the cube's greys */
    TC_CHECK(all_index(out, 3 * 43));
}

static void test_mixtures(void)
{
    uint8_t out[H][W];
    /* Others are mixed, and average out to about the colour. */
    TC_CHECK(average_close(0xFFC8641Eu, 4));
    TC_CHECK(average_close(0xFF808080u, 2));
    TC_CHECK(average_close(0xFFF8F9FAu, 3)); /* near grey: the greys, not the cube */
    TC_CHECK(average_close(0xFF0000F0u, 4));
    dither_flat(0xFFF8F9FAu, out, 0, 0);
    TC_CHECK(out[0][0] == 215 || out[0][0] == 235);
    /* Half way between two levels: half and half. */
    {
        int x, y, high = 0;
        dither_flat(0xFF000000u | (25 << 16), out, 0, 0); /* red 25.5 of 51 is half */
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                high += out[y][x] == 36;
        TC_CHECK(high >= W * H / 2 - 8 && high <= W * H / 2);
    }
}

static void test_pattern_and_rect(void)
{
    uint8_t a[H][W], b[H][W];
    uint32_t pixels[W * H];
    tc_surface s;
    int x, y, same = 1, i;
    /* The pattern moves with the anchor. */
    dither_flat(0xFF7F7F7Fu, a, 0, 0);
    dither_flat(0xFF7F7F7Fu, b, 3, 5);
    for (y = 0; y + 5 < H; y++)
        for (x = 0; x + 3 < W; x++)
            if (b[y][x] != a[y + 5][x + 3])
                same = 0;
    TC_CHECK(same);
    /* Only the rect is written, and a rect beyond the surface is clipped. */
    for (i = 0; i < W * H; i++)
        pixels[i] = 0xFF000000u;
    tc_surface_wrap(&s, pixels, W, H, W);
    memset(a, 0xEE, sizeof a);
    tc_dither(&s, tc_rect_make(4, 4, 100, 2), &a[0][0], W, 0, 0);
    TC_CHECK_INT(a[3][4], 0xEE);
    TC_CHECK_INT(a[4][3], 0xEE);
    TC_CHECK_INT(a[4][4], 0);
    TC_CHECK_INT(a[5][W - 1], 0);
    TC_CHECK_INT(a[6][4], 0xEE);
}

void tc_suite_dither(void)
{
    TC_RUN(test_palette);
    TC_RUN(test_flat_colours);
    TC_RUN(test_mixtures);
    TC_RUN(test_pattern_and_rect);
}
