/* Tachyon's changes to the TF-PSA-Crypto default configuration.
 * Included through TF_PSA_CRYPTO_USER_CONFIG_FILE, after crypto_config.h.
 * Each change was found by tools/win95check or the Sprint 0 spike. */

/* The built-in entropy source on Windows is BCryptGenRandom (Vista).
   Tachyon's entropy pool (src/core/tc_entropy.c) provides
   mbedtls_platform_get_entropy() instead. */
#undef MBEDTLS_PSA_BUILTIN_GET_ENTROPY
#define MBEDTLS_PSA_DRIVER_GET_ENTROPY

/* No persistent keys, and no file access inside the library: the CA folder
   loader uses FindFirstFileW, a stub on Windows 95. */
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_FS_IO

/* Windows 95 never enables SSE and does not save XMM registers, so AES-NI
   code would fault even on a CPU that reports it, e.g. in a VM. */
#undef MBEDTLS_AESNI_C

/* Page loads run on worker threads. The mutexes come from Tachyon's
   platform layer (src/core/tc_thread.h), see threading_alt.h. */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT

/* Tachyon's clock with a 64-bit time type, so certificates valid past 2038
   work and no C runtime time functions are needed (MinGW's crtdll import
   library has no _time32/_gmtime32). Both live in src/core/tc_tls.c. */
#define MBEDTLS_PLATFORM_TIME_TYPE_MACRO long long
#define MBEDTLS_PLATFORM_TIME_MACRO tc_tls_time
#define MBEDTLS_PLATFORM_GMTIME_R_ALT
long long tc_tls_time(long long *t);

/* MinGW's snprintf/vsnprintf are C99-conformant and linked statically;
   Mbed TLS's MinGW default calls vsnprintf_s, which old C runtimes lack. */
#if defined(__MINGW32__)
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO snprintf
#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO vsnprintf
#endif
