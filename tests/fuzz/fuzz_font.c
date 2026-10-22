/* A libFuzzer target for fonts from the web: WOFF and WOFF2 unpacked, the
 * checker (decision 38), then stb_truetype reading what it let through, measuring and drawing
 * text with characters from the input, at small and huge sizes, bold and
 * italic. Built with AddressSanitizer and UBSan. */
#include <stdlib.h>
#include <string.h>

#include "tc_font.h"
#include "tc_utf8.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const char *const family[] = { "Fuzz" };
    static const float sizes[] = { 9, 24, 300 };
    tc_font_set *set = tc_font_set_new();
    char text[96];
    size_t n = 0, i;
    tc_surface s;

    if (!set)
        return 0;
    if (tc_font_set_add_data(set, data, size, "Fuzz") != TC_OK || tc_surface_init(&s, 64, 48) != TC_OK) {
        tc_font_set_free(set);
        return 0;
    }
    /* Some ASCII, then code points from the input's last bytes. */
    memcpy(text, "Ag W\xC3\xA9", 7);
    n = 7;
    for (i = 0; i + 3 <= size && i < 3 * 12 && n + 4 < sizeof text; i += 3) {
        uint32_t cp = (uint32_t)data[size - 1 - i] | (uint32_t)data[size - 2 - i] << 8 |
                      (uint32_t)(data[size - 3 - i] & 1) << 16;
        if (cp >= 0x20 && (cp < 0xD800 || cp > 0xDFFF))
            n += tc_utf8_encode(cp, text + n);
    }
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        const tc_font *f = tc_font_match(set, family, 1, i == 1 ? 700 : 400, i == 2, sizes[i]);
        tc_font_measure(f, text, n, 0);
        tc_font_draw(f, &s, tc_rect_make(0, 0, 64, 48), 2, 40, text, n, 0, 0xFF000000u);
        tc_font_for_char(f, 0x4E2D);
    }
    tc_surface_free(&s);
    tc_font_set_free(set);
    return 0;
}
