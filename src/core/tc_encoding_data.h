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

#endif
