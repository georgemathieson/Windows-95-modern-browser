/* TLS 1.2 and 1.3 client connections, using Mbed TLS.
 *
 * tc_tls_init() runs once at startup, on the main thread: it hooks Mbed TLS
 * up to Tachyon's entropy pool, clock and threads, starts its crypto, and
 * loads the root certificates compiled into Tachyon. After that, connections
 * can be made from any thread. */
#ifndef TC_TLS_H
#define TC_TLS_H

#include "tc_platform.h"

int tc_tls_init(void);

/* Trust extra roots (PEM text): a company proxy's CA, or a test CA. Call
   after tc_tls_init and before any connection. */
int tc_tls_add_trusted_pem(const char *pem, size_t len);
/* The same, from a PEM file. */
int tc_tls_add_trusted_file(const char *path);
/* How many roots are trusted. */
int tc_tls_trusted_count(void);

typedef struct tc_tls_info {
    char version[16];    /* e.g. "TLSv1.3" */
    char cipher[64];     /* e.g. "TLS1-3-AES-128-GCM-SHA256" */
    uint32_t cert_flags; /* why the certificate was refused (TC_ERR_CERT) */
    int tls_error;       /* Mbed TLS's error code, for diagnostics */
    char detail[160];    /* Mbed TLS's description of tls_error */
} tc_tls_info;

typedef struct tc_tls tc_tls;

/* Run the handshake over a connected socket and verify the server's
   certificate for host. Socket I/O uses timeout_ms and cancel throughout the
   connection's life. Errors: TC_ERR_CERT (see info->cert_flags), TC_ERR_TLS,
   or the socket's own errors. */
int tc_tls_connect(tc_socket *sock, const char *host, uint32_t timeout_ms, const volatile int *cancel,
                   tc_tls **out, tc_tls_info *info);
/* Send all of data. */
int tc_tls_send(tc_tls *t, const void *data, size_t len);
/* Bytes received, 0 at the end of the stream, or a negative tc_err. */
int tc_tls_recv(tc_tls *t, void *buf, size_t len);
/* Say goodbye and free the connection. Doesn't close the socket. */
void tc_tls_close(tc_tls *t);

/* Explain certificate verification flags in plain English. */
void tc_tls_describe_cert_error(uint32_t flags, char *buf, size_t size);

#endif
