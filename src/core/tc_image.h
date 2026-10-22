/* Images: telling formats apart by their bytes, decoding them with Wuffs
 * (decision 35) into premultiplied ARGB, and stepping through animations.
 *
 * Decoded frames are tc_surfaces whose pixels are 0xAARRGGBB with the
 * colour already multiplied by alpha. An animation keeps one frame at a
 * time and decodes the next onto it, so a long GIF costs no more memory
 * than a still one. ICO's container and the bitmaps inside it are read
 * here; Wuffs decodes the PNGs inside it and everything else. */
#ifndef TC_IMAGE_H
#define TC_IMAGE_H

#include "tc_surface.h"

typedef enum tc_image_type {
    TC_IMAGE_NONE,
    TC_IMAGE_PNG,
    TC_IMAGE_JPEG,
    TC_IMAGE_GIF,
    TC_IMAGE_BMP,
    TC_IMAGE_WEBP,
    TC_IMAGE_ICO
} tc_image_type;

/* The largest width or height Tachyon accepts. */
#define TC_IMAGE_MAX_SIDE 32767

/* An image's type from its first bytes (the MIME Sniffing Standard's image
   type patterns), or TC_IMAGE_NONE. */
tc_image_type tc_image_sniff(const void *data, size_t len);
/* Its MIME type, e.g. "image/png" ("" for none). */
const char *tc_image_mime(tc_image_type type);

typedef struct tc_image tc_image;

/* Read an image's header from its bytes (copied): its type, size and
   frames. TC_ERR_DECODE if it isn't an image Tachyon can read,
   TC_ERR_TOO_LARGE if a side is over TC_IMAGE_MAX_SIDE. */
int tc_image_new(const void *data, size_t len, tc_image **out);
void tc_image_free(tc_image *img);

tc_image_type tc_image_type_of(const tc_image *img);
int tc_image_width(const tc_image *img);
int tc_image_height(const tc_image *img);
/* Frames in an animation (1 for a still image), and how many times it
   plays (0: forever). */
uint32_t tc_image_frames(const tc_image *img);
uint32_t tc_image_loops(const tc_image *img);

/* Decode the current frame if it isn't decoded already. An image with
   more than max_pixels pixels is refused (TC_ERR_TOO_LARGE). A frame cut
   short keeps what was decoded, and gives TC_ERR_DECODE. */
int tc_image_decode(tc_image *img, uint32_t max_pixels);
/* The decoded frame (premultiplied 0xAARRGGBB), or NULL. */
const tc_surface *tc_image_pixels(const tc_image *img);

/* Move an animation to its next frame and decode it, starting again at
   the end while it has loops left. *delay_ms: how long to show it.
   Returns 0 when there's no next frame: a still image, or the animation
   has finished (its last frame stays). */
int tc_image_next_frame(tc_image *img, uint32_t *delay_ms);
/* How long to show the current frame, in milliseconds. */
uint32_t tc_image_delay(const tc_image *img);

/* Bytes of decoded pixels held, and dropping them; they're decoded again,
   from the first frame, when next needed. */
size_t tc_image_memory(const tc_image *img);
void tc_image_discard(tc_image *img);

#endif
