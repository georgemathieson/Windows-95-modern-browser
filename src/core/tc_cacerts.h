/* The root certificates Tachyon trusts: Mozilla's root store, compiled in
 * from third_party/cacert/cacert.pem by tools/gen_cacerts.py. */
#ifndef TC_CACERTS_H
#define TC_CACERTS_H

#include <stddef.h>

typedef struct tc_cacert {
    const char *label;        /* e.g. "ISRG Root X1" */
    const unsigned char *der;
    size_t len;
} tc_cacert;

extern const tc_cacert tc_cacerts[];
extern const size_t tc_cacerts_count;

#endif
