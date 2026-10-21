/* Win95 adjustments on top of the mbedTLS 3.6 default config (spike). */
#undef MBEDTLS_NET_C                 /* Winsock 2 + getaddrinfo: we bring our own BIO on wsock32 */
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C  /* no persistent PSA keys */
#undef MBEDTLS_PSA_ITS_FILE_C
#define MBEDTLS_NO_PLATFORM_ENTROPY  /* platform source is BCryptGenRandom (Vista+) */
#define MBEDTLS_ENTROPY_HARDWARE_ALT /* our collector: mbedtls_hardware_poll() */
#define MBEDTLS_PLATFORM_GMTIME_R_ALT /* msvcrt.dll (VC6) has no gmtime_s */
/* MinGW's snprintf/vsnprintf are C99-conformant and linked statically;
   mbedTLS's MinGW default calls vsnprintf_s, which VC6-era msvcrt.dll lacks. */
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO snprintf
#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO vsnprintf
/* No file access inside mbedTLS: the CA directory loader uses FindFirstFileW,
   a stub on Win95. Tachyon reads the CA bundle itself and parses from memory. */
#undef MBEDTLS_FS_IO
/* Windows 95 never enables SSE (CR4.OSFXSR) and does not save XMM registers,
   so AES-NI code would fault even on a CPU that reports it, e.g. in a VM. */
#undef MBEDTLS_AESNI_C
#undef MBEDTLS_PADLOCK_C
/* Tachyon's own clock (GetSystemTimeAsFileTime) with a 64-bit time_t, so
   certificates valid past 2038 work and no CRT time functions are needed
   (MinGW's crtdll import library has no _time32/_gmtime32). */
#define MBEDTLS_PLATFORM_TIME_TYPE_MACRO long long
#define MBEDTLS_PLATFORM_TIME_MACRO tachyon_time
long long tachyon_time(long long *t);
