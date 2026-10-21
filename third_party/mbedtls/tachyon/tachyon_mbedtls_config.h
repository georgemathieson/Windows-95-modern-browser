/* Tachyon's changes to the Mbed TLS default configuration (TLS and X.509).
 * Included through MBEDTLS_USER_CONFIG_FILE, after mbedtls_config.h.
 *
 * Tachyon is a TLS 1.2/1.3 client, so the server, DTLS and certificate
 * writing code is left out, which also keeps the program small. */

/* Winsock 2 and getaddrinfo (Windows XP): Tachyon does its own socket I/O. */
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C

/* Client only. */
#undef MBEDTLS_SSL_SRV_C
#undef MBEDTLS_SSL_CACHE_C
#undef MBEDTLS_SSL_COOKIE_C
#undef MBEDTLS_SSL_TICKET_C
#undef MBEDTLS_SSL_PROTO_DTLS
#undef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#undef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#undef MBEDTLS_SSL_DTLS_CONNECTION_ID
#undef MBEDTLS_SSL_DTLS_HELLO_VERIFY

/* Certificates are only parsed and verified, never written. */
#undef MBEDTLS_X509_CREATE_C
#undef MBEDTLS_X509_CRT_WRITE_C
#undef MBEDTLS_X509_CSR_PARSE_C
#undef MBEDTLS_X509_CSR_WRITE_C
#undef MBEDTLS_X509_CRL_PARSE_C
#undef MBEDTLS_PKCS7_C
#undef MBEDTLS_DEBUG_C
