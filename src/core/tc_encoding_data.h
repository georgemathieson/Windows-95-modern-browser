/* Tables generated from the Encoding Standard by tools/gen_encodings.py;
 * internal to tc_encoding.c. */
#ifndef TC_ENCODING_DATA_H
#define TC_ENCODING_DATA_H

#include "tc_encoding.h"

typedef struct tc_encoding_label {
    const char *label;   /* lowercase */
    tc_encoding encoding;
} tc_encoding_label;

extern const char *const tc_encoding_names[TC_ENC_COUNT];

/* Sorted by label. */
extern const tc_encoding_label tc_encoding_labels[];
extern const size_t tc_encoding_label_count;

/* For single-byte encodings, the code points of bytes 0x80-0xFF (0 where the
   byte is an error); NULL for the others. */
extern const uint16_t *const tc_encoding_single_byte[TC_ENC_COUNT];

/* The Chinese, Japanese and Korean indexes, by pointer (0: none). Big5's
   code point is in plane 2 where its bit in tc_encoding_big5_plane2 is set.
   tc_encoding_gb18030_ranges: pointer, code point pairs, sorted. */
extern const uint16_t tc_encoding_jis0208[11104];
extern const uint16_t tc_encoding_jis0212[7211];
extern const uint16_t tc_encoding_gb18030[23940];
extern const uint16_t tc_encoding_euc_kr[23750];
extern const uint16_t tc_encoding_big5[19782];
extern const uint8_t tc_encoding_big5_plane2[2473];
extern const uint32_t tc_encoding_gb18030_ranges[414];

#endif
