/* Mutex and condition variable types for Mbed TLS (MBEDTLS_THREADING_ALT).
 * They wrap Tachyon's own primitives (src/core/tc_thread.h), so Mbed TLS
 * never includes an OS header; src/core/tc_tls.c installs the functions. */
#ifndef TACHYON_THREADING_ALT_H
#define TACHYON_THREADING_ALT_H

struct tc_mutex;
struct tc_cond;

typedef struct {
    struct tc_mutex *mutex;
} mbedtls_platform_mutex_t;

typedef struct {
    struct tc_cond *cond;
} mbedtls_platform_condition_variable_t;

#endif
