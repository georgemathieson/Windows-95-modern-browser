/* Image decoding with Wuffs, and ICO's container and bitmaps. */
#include <stdlib.h>
#include <string.h>

#include "tc_image.h"
#include "tc_wuffs.h"

#define MAX_FRAMES  10000             /* frames counted in an animation */
#define MAX_WORKBUF (64u << 20)       /* bytes of decoder scratch memory */
#define MIN_DELAY   20                /* ms: browsers slow faster frames... */
#define SLOW_DELAY  100               /* ...down to this */

struct tc_image {
    tc_image_type type;
    uint8_t *data;
    size_t len;
    /* What's decoded: the whole file, or the chosen image in an ICO. */
    const uint8_t *src;
    size_t src_len;
    tc_image_type codec;              /* Wuffs' decoder for src; NONE for an ICO bitmap */
    int w, h;
    uint32_t frames, loops;
    /* Decoding. */
    wuffs_base__image_decoder *dec;
    wuffs_base__io_buffer io;
    wuffs_base__image_config config;
    uint8_t *workbuf;
    size_t workbuf_len;
    uint32_t *pixels, *backup;
    tc_surface surface;
    int decoded;                      /* pixels hold frame `frame` */
    uint32_t frame, loops_done, delay;
    uint8_t disposal;                 /* the current frame's */
    wuffs_base__rect_ie_u32 bounds;
};

static uint32_t u16le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t u32le(const uint8_t *p)
{
    return u16le(p) | u16le(p + 2) << 16;
}

tc_image_type tc_image_sniff(const void *data, size_t len)
{
    const uint8_t *p = data;
    if (len >= 4 && p[0] == 0 && p[1] == 0 && (p[2] == 1 || p[2] == 2) && p[3] == 0)
        return TC_IMAGE_ICO; /* icons and cursors */
    if (len >= 2 && p[0] == 'B' && p[1] == 'M')
        return TC_IMAGE_BMP;
    if (len >= 6 && (memcmp(p, "GIF87a", 6) == 0 || memcmp(p, "GIF89a", 6) == 0))
        return TC_IMAGE_GIF;
    if (len >= 14 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBPVP", 6) == 0)
        return TC_IMAGE_WEBP;
    if (len >= 8 && memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0)
        return TC_IMAGE_PNG;
    if (len >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF)
        return TC_IMAGE_JPEG;
    return TC_IMAGE_NONE;
}

const char *tc_image_mime(tc_image_type type)
{
    static const char *const names[] = { "", "image/png", "image/jpeg", "image/gif", "image/bmp", "image/webp",
                                         "image/x-icon" };
    return (unsigned)type < TC_ARRAY_LEN(names) ? names[type] : "";
}

static wuffs_base__image_decoder *new_decoder(tc_image_type codec)
{
    wuffs_base__image_decoder *dec;
    switch (codec) {
    case TC_IMAGE_PNG: return wuffs_png__decoder__alloc_as__wuffs_base__image_decoder();
    case TC_IMAGE_JPEG: return wuffs_jpeg__decoder__alloc_as__wuffs_base__image_decoder();
    case TC_IMAGE_BMP: return wuffs_bmp__decoder__alloc_as__wuffs_base__image_decoder();
    case TC_IMAGE_WEBP: return wuffs_webp__decoder__alloc_as__wuffs_base__image_decoder();
    case TC_IMAGE_GIF:
        dec = wuffs_gif__decoder__alloc_as__wuffs_base__image_decoder();
        /* Encoders that write too many pixels are common; browsers carry on. */
        if (dec)
            wuffs_base__image_decoder__set_quirk(dec, WUFFS_GIF__QUIRK_IGNORE_TOO_MUCH_PIXEL_DATA, 1);
        return dec;
    default: return NULL;
    }
}

/* A fresh decoder at the start of the image, past its header. */
static int restart(tc_image *img)
{
    wuffs_base__status st;
    free(img->dec);
    img->dec = new_decoder(img->codec);
    if (!img->dec)
        return TC_ERR_NOMEM;
    img->io = wuffs_base__ptr_u8__reader((uint8_t *)img->src, img->src_len, true);
    st = wuffs_base__image_decoder__decode_image_config(img->dec, &img->config, &img->io);
    if (st.repr)
        return TC_ERR_DECODE;
    img->frame = (uint32_t)-1;
    return TC_OK;
}

/* ---- ICO ---- */

typedef struct dib {
    const uint8_t *p;
    size_t len;
    uint32_t header, bits, colours;
    int height;              /* of the colour bitmap; negative: top down */
} dib;

/* Pick an ICO's biggest image (then its deepest), and make it src. */
static int read_ico(tc_image *img, dib *d)
{
    const uint8_t *p = img->data;
    uint32_t count, i, best = 0, best_area = 0, best_bits = 0;
    int found = 0;
    if (img->len < 6)
        return TC_ERR_DECODE;
    count = u16le(p + 4);
    if (!count || img->len < 6 + 16 * (size_t)count)
        return TC_ERR_DECODE;
    for (i = 0; i < count; i++) {
        const uint8_t *e = p + 6 + 16 * i;
        uint32_t w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256, bits = u16le(e + 6);
        uint32_t size = u32le(e + 8), offset = u32le(e + 12);
        if (offset >= img->len || size > img->len - offset || size < 8)
            continue;
        if (!found || w * h > best_area || (w * h == best_area && bits > best_bits)) {
            best = i, best_area = w * h, best_bits = bits;
            found = 1;
        }
    }
    if (!found)
        return TC_ERR_DECODE;
    img->src = p + u32le(p + 6 + 16 * best + 12);
    img->src_len = u32le(p + 6 + 16 * best + 8);
    if (tc_image_sniff(img->src, img->src_len) == TC_IMAGE_PNG) {
        img->codec = TC_IMAGE_PNG;
        return TC_OK;
    }
    /* A BITMAPINFOHEADER, uncompressed, then the colour bitmap and a 1-bit
       mask, both twice as high together as the image. */
    img->codec = TC_IMAGE_NONE;
    d->p = img->src;
    d->len = img->src_len;
    if (d->len < 40 || (d->header = u32le(d->p)) < 40 || d->header > d->len)
        return TC_ERR_DECODE;
    img->w = (int)u32le(d->p + 4);
    d->height = (int)u32le(d->p + 8) / 2;
    d->bits = u16le(d->p + 14);
    d->colours = u32le(d->p + 32);
    if (u32le(d->p + 16) != 0 || (d->bits != 1 && d->bits != 4 && d->bits != 8 && d->bits != 24 && d->bits != 32))
        return TC_ERR_DECODE;
    if (d->bits <= 8 && (!d->colours || d->colours > (1u << d->bits)))
        d->colours = 1u << d->bits;
    if (d->bits > 8)
        d->colours = 0;
    img->h = d->height < 0 ? -d->height : d->height;
    if (img->w <= 0 || img->h <= 0 || img->w > 256 * 16 || img->h > 256 * 16)
        return TC_ERR_DECODE;
    return TC_OK;
}

static uint32_t premultiply(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
    if (a == 255)
        return 0xFF000000u | r << 16 | g << 8 | b;
    return a << 24 | (r * a + 127) / 255 << 16 | (g * a + 127) / 255 << 8 | (b * a + 127) / 255;
}

static int decode_dib(tc_image *img)
{
    dib d;
    uint32_t w = (uint32_t)img->w, h = (uint32_t)img->h, x, y;
    size_t palette, stride, mask_stride, colour, mask;
    int use_alpha = 0;

    memset(&d, 0, sizeof d);
    if (read_ico(img, &d) != TC_OK)
        return TC_ERR_DECODE;
    palette = d.header;
    colour = palette + 4 * (size_t)d.colours;
    stride = ((size_t)w * d.bits + 31) / 32 * 4;
    mask_stride = ((size_t)w + 31) / 32 * 4;
    mask = colour + stride * h;
    if (mask > d.len)
        return TC_ERR_DECODE;
    if (mask + mask_stride * h > d.len)
        mask = 0; /* no mask: opaque */
    if (d.bits == 32)
        for (y = 0; y < h && !use_alpha; y++)
            for (x = 0; x < w && !use_alpha; x++)
                use_alpha = d.p[colour + y * stride + x * 4 + 3] != 0;
    for (y = 0; y < h; y++) {
        uint32_t row = d.height < 0 ? y : h - 1 - y; /* bitmaps are bottom up */
        const uint8_t *src = d.p + colour + row * stride;
        uint32_t *dst = img->pixels + (size_t)y * w;
        for (x = 0; x < w; x++) {
            uint32_t r, g, b, a = 255;
            if (d.bits <= 8) {
                uint32_t i = (src[x * d.bits / 8] >> (8 - d.bits - x * d.bits % 8)) & ((1u << d.bits) - 1);
                const uint8_t *c = i < d.colours ? d.p + palette + 4 * i : (const uint8_t *)"\0\0\0";
                b = c[0], g = c[1], r = c[2];
            } else {
                const uint8_t *c = src + x * (d.bits / 8);
                b = c[0], g = c[1], r = c[2];
                if (use_alpha)
                    a = c[3];
            }
            if (!use_alpha && mask && (d.p[mask + row * mask_stride + x / 8] >> (7 - x % 8) & 1))
                a = 0;
            dst[x] = premultiply(a, r, g, b);
        }
    }
    return TC_OK;
}

/* ---- Reading the header ---- */

/* How many frames an animation has: decode their headers, skipping the
   pixels. */
static void count_frames(tc_image *img)
{
    wuffs_base__frame_config fc;
    img->frames = 0;
    if (restart(img) != TC_OK)
        return;
    while (img->frames < MAX_FRAMES) {
        wuffs_base__status st = wuffs_base__image_decoder__decode_frame_config(img->dec, &fc, &img->io);
        if (st.repr)
            break; /* the end, or a broken frame: play what came before */
        img->frames++;
    }
    img->loops = wuffs_base__image_decoder__num_animation_loops(img->dec);
}

int tc_image_new(const void *data, size_t len, tc_image **out)
{
    tc_image *img;
    int err = TC_OK;

    *out = NULL;
    img = calloc(1, sizeof *img);
    if (!img || !(img->data = malloc(len ? len : 1))) {
        free(img);
        return TC_ERR_NOMEM;
    }
    memcpy(img->data, data, len);
    img->len = len;
    img->src = img->data;
    img->src_len = len;
    img->type = img->codec = tc_image_sniff(data, len);
    img->frames = 1;
    if (img->type == TC_IMAGE_NONE) {
        err = TC_ERR_DECODE;
    } else if (img->type == TC_IMAGE_ICO) {
        dib d;
        err = read_ico(img, &d);
    }
    if (!err && img->codec != TC_IMAGE_NONE) {
        err = restart(img);
        if (!err) {
            uint32_t w = wuffs_base__pixel_config__width(&img->config.pixcfg);
            uint32_t h = wuffs_base__pixel_config__height(&img->config.pixcfg);
            if (w > TC_IMAGE_MAX_SIDE || h > TC_IMAGE_MAX_SIDE)
                err = TC_ERR_TOO_LARGE;
            else if (!w || !h)
                err = TC_ERR_DECODE;
            img->w = (int)w;
            img->h = (int)h;
        }
        if (!err && (img->codec == TC_IMAGE_GIF || img->codec == TC_IMAGE_PNG)) {
            count_frames(img);
            if (!img->frames)
                err = TC_ERR_DECODE;
        }
        free(img->dec);
        img->dec = NULL;
    }
    if (err) {
        tc_image_free(img);
        return err;
    }
    *out = img;
    return TC_OK;
}

void tc_image_free(tc_image *img)
{
    if (!img)
        return;
    tc_image_discard(img);
    free(img->data);
    free(img);
}

tc_image_type tc_image_type_of(const tc_image *img)
{
    return img->type;
}

int tc_image_width(const tc_image *img)
{
    return img->w;
}

int tc_image_height(const tc_image *img)
{
    return img->h;
}

uint32_t tc_image_frames(const tc_image *img)
{
    return img->frames;
}

uint32_t tc_image_loops(const tc_image *img)
{
    return img->loops;
}

/* ---- Decoding ---- */

static void clear_rect(tc_image *img, wuffs_base__rect_ie_u32 r)
{
    uint32_t y;
    if (r.max_excl_x > (uint32_t)img->w)
        r.max_excl_x = (uint32_t)img->w;
    if (r.max_excl_y > (uint32_t)img->h)
        r.max_excl_y = (uint32_t)img->h;
    for (y = r.min_incl_y; y < r.max_excl_y && r.min_incl_x < r.max_excl_x; y++)
        memset(img->pixels + (size_t)y * (size_t)img->w + r.min_incl_x, 0,
               (r.max_excl_x - r.min_incl_x) * sizeof(uint32_t));
}

/* Decode the decoder's next frame onto the pixels. TC_ERR_CLOSED at the
   end of the frames. */
static int decode_next(tc_image *img)
{
    wuffs_base__frame_config fc;
    wuffs_base__pixel_buffer pb;
    wuffs_base__range_ii_u64 need;
    wuffs_base__status st;
    size_t bytes = (size_t)img->w * (size_t)img->h * 4;
    uint64_t flicks;

    st = wuffs_base__image_decoder__decode_frame_config(img->dec, &fc, &img->io);
    if (st.repr == wuffs_base__note__end_of_data)
        return TC_ERR_CLOSED;
    if (st.repr)
        return TC_ERR_DECODE;
    img->disposal = wuffs_base__frame_config__disposal(&fc);
    img->bounds = wuffs_base__frame_config__bounds(&fc);
    if (img->disposal == WUFFS_BASE__ANIMATION_DISPOSAL__RESTORE_PREVIOUS) {
        if (!img->backup && !(img->backup = malloc(bytes)))
            return TC_ERR_NOMEM;
        memcpy(img->backup, img->pixels, bytes);
    }
    need = wuffs_base__image_decoder__workbuf_len(img->dec);
    if (need.max_incl > MAX_WORKBUF)
        return TC_ERR_TOO_LARGE;
    if (img->workbuf_len < need.max_incl) {
        uint8_t *grown = realloc(img->workbuf, (size_t)need.max_incl);
        if (!grown)
            return TC_ERR_NOMEM;
        img->workbuf = grown;
        img->workbuf_len = (size_t)need.max_incl;
    }
    wuffs_base__pixel_config__set(&img->config.pixcfg, WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL,
                                  WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, (uint32_t)img->w, (uint32_t)img->h);
    st = wuffs_base__pixel_buffer__set_from_slice(&pb, &img->config.pixcfg,
                                                  wuffs_base__make_slice_u8((uint8_t *)img->pixels, bytes));
    if (st.repr)
        return TC_ERR_DECODE;
    st = wuffs_base__image_decoder__decode_frame(
        img->dec, &pb, &img->io,
        wuffs_base__frame_config__overwrite_instead_of_blend(&fc) ? WUFFS_BASE__PIXEL_BLEND__SRC
                                                                   : WUFFS_BASE__PIXEL_BLEND__SRC_OVER,
        wuffs_base__make_slice_u8(img->workbuf, img->workbuf_len), NULL);
    img->frame++;
    flicks = (uint64_t)wuffs_base__frame_config__duration(&fc);
    img->delay = (uint32_t)TC_MIN(flicks / WUFFS_BASE__FLICKS_PER_MILLISECOND, 600000u);
    if (img->delay < MIN_DELAY)
        img->delay = SLOW_DELAY;
    if (img->frames <= 1) {
        free(img->workbuf); /* a JPEG's can be big, and it's done */
        img->workbuf = NULL;
        img->workbuf_len = 0;
    }
    return st.repr ? TC_ERR_DECODE : TC_OK;
}

int tc_image_decode(tc_image *img, uint32_t max_pixels)
{
    int err;
    if (img->decoded)
        return TC_OK;
    if ((uint64_t)img->w * (uint64_t)img->h > max_pixels)
        return TC_ERR_TOO_LARGE;
    if (!img->pixels) {
        img->pixels = calloc((size_t)img->w * (size_t)img->h, sizeof(uint32_t));
        if (!img->pixels)
            return TC_ERR_NOMEM;
        tc_surface_wrap(&img->surface, img->pixels, img->w, img->h, img->w);
    }
    img->decoded = 1; /* even half a frame is worth showing */
    if (img->codec == TC_IMAGE_NONE) {
        img->frame = 0;
        return decode_dib(img);
    }
    err = restart(img);
    if (!err)
        err = decode_next(img);
    if (err == TC_ERR_NOMEM || err == TC_ERR_TOO_LARGE)
        tc_image_discard(img);
    return err == TC_ERR_CLOSED ? TC_ERR_DECODE : err;
}

const tc_surface *tc_image_pixels(const tc_image *img)
{
    return img->decoded ? &img->surface : NULL;
}

int tc_image_next_frame(tc_image *img, uint32_t *delay_ms)
{
    int err;
    *delay_ms = 0;
    if (img->frames <= 1 || !img->decoded || !img->dec)
        return 0;
    if (img->frame + 1 >= img->frames) {
        /* The end: once round more, unless that was the last loop. */
        if (img->loops && ++img->loops_done >= img->loops)
            return 0;
        memset(img->pixels, 0, (size_t)img->w * (size_t)img->h * 4);
        if (restart(img) != TC_OK)
            return 0;
    } else if (img->disposal == WUFFS_BASE__ANIMATION_DISPOSAL__RESTORE_BACKGROUND) {
        clear_rect(img, img->bounds);
    } else if (img->disposal == WUFFS_BASE__ANIMATION_DISPOSAL__RESTORE_PREVIOUS && img->backup) {
        memcpy(img->pixels, img->backup, (size_t)img->w * (size_t)img->h * 4);
    }
    err = decode_next(img);
    if (err == TC_ERR_NOMEM || err == TC_ERR_TOO_LARGE || err == TC_ERR_CLOSED) {
        img->frames = img->frame + 1; /* fewer frames than counted: stop here */
        return 0;
    }
    *delay_ms = img->delay;
    return 1;
}

uint32_t tc_image_delay(const tc_image *img)
{
    return img->delay;
}

size_t tc_image_memory(const tc_image *img)
{
    size_t frame = (size_t)img->w * (size_t)img->h * 4;
    return (img->pixels ? frame : 0) + (img->backup ? frame : 0) + img->workbuf_len;
}

void tc_image_discard(tc_image *img)
{
    free(img->dec);
    free(img->workbuf);
    free(img->pixels);
    free(img->backup);
    img->dec = NULL;
    img->workbuf = NULL;
    img->workbuf_len = 0;
    img->pixels = img->backup = NULL;
    img->decoded = 0;
    img->frame = 0;
    img->loops_done = 0;
    memset(&img->surface, 0, sizeof img->surface);
}
