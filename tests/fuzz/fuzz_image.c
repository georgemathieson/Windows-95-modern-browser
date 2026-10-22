/* A libFuzzer target for image decoding: the sniffer, Wuffs' decoders as
 * Tachyon configures them, ICO's container and bitmaps, animation
 * (stepping through frames, dropping them and decoding again), and SVG
 * (NanoSVG's parser and rasteriser, at sizes from the input's last bytes).
 * Built with AddressSanitizer and UBSan. */
#include <stdlib.h>

#include "tc_image.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tc_image *img;
    uint32_t delay;
    int i;

    tc_image_sniff(data, size);
    if (tc_image_new(data, size, &img) != TC_OK)
        return 0;
    /* Big enough for real images, small enough to stay fast. */
    if (tc_image_decode(img, 1u << 22) == TC_OK || tc_image_pixels(img)) {
        for (i = 0; i < 50 && tc_image_next_frame(img, &delay); i++)
            ;
        tc_image_discard(img);
        tc_image_decode(img, 1u << 22);
        tc_image_next_frame(img, &delay);
    }
    if (tc_image_scalable(img) && size >= 2) {
        tc_image_want_size(img, data[size - 1] * 3 + 1, data[size - 2] * 3 + 1);
        tc_image_decode(img, 1u << 16); /* smaller than wanted, sometimes */
    }
    tc_image_free(img);
    return 0;
}
