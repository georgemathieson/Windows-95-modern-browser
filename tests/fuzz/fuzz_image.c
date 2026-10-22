/* A libFuzzer target for image decoding: the sniffer, Wuffs' decoders as
 * Tachyon configures them, ICO's container and bitmaps, and animation
 * (stepping through frames, dropping them and decoding again). Built with
 * AddressSanitizer and UBSan. */
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
    tc_image_free(img);
    return 0;
}
