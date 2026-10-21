/* SHA-256 (FIPS 180-4).
 *
 * Tachyon's own small implementation, used by the entropy pool. It can't use
 * Mbed TLS's, because Mbed TLS asks the pool for entropy while starting up. */
#ifndef TC_SHA256_H
#define TC_SHA256_H

#include "tc_base.h"

#define TC_SHA256_SIZE 32

typedef struct tc_sha256 {
    uint32_t state[8];
    uint64_t length;      /* bytes hashed so far */
    uint8_t block[64];
    size_t used;          /* bytes waiting in block */
} tc_sha256;

void tc_sha256_init(tc_sha256 *h);
void tc_sha256_update(tc_sha256 *h, const void *data, size_t len);
void tc_sha256_final(tc_sha256 *h, uint8_t out[TC_SHA256_SIZE]);

/* One-shot convenience. */
void tc_sha256_digest(const void *data, size_t len, uint8_t out[TC_SHA256_SIZE]);

#endif
