#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/sha256.h"
#include "mbedtls/aes.h"
#include "mbedtls/gcm.h"
#include "mbedtls/ecp.h"
#include "mbedtls/error.h"
#include "psa/crypto.h"

/* SPIKE ONLY: timing-jitter entropy. Sprint 1 replaces this with a proper
   collector (CryptGenRandom when present, input-event timings, RDTSC jitter). */
int mbedtls_hardware_poll(void *data, unsigned char *out, size_t len, size_t *olen) {
  size_t i; (void)data;
  for (i = 0; i < len; i++) {
    LARGE_INTEGER a; unsigned char acc = 0; int k;
    for (k = 0; k < 8; k++) { QueryPerformanceCounter(&a); acc = (unsigned char)((acc << 1 | acc >> 7) ^ (unsigned char)a.LowPart); Sleep(0); }
    out[i] = acc ^ (unsigned char)GetTickCount();
  }
  *olen = len; return 0;
}

long long tachyon_time(long long *t) {
  FILETIME ft; unsigned long long v; long long secs;
  GetSystemTimeAsFileTime(&ft);
  v = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  secs = (long long)((v - 116444736000000000ULL) / 10000000ULL);
  if (t) *t = secs;
  return secs;
}

/* Days since 1970-01-01 to a civil date (Howard Hinnant's algorithm). */
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *buf) {
  long long secs = *tt, days = secs / 86400, rem = secs % 86400, z, era, doe, yoe, doy, mp;
  if (rem < 0) { rem += 86400; days--; }
  z = days + 719468; era = (z >= 0 ? z : z - 146096) / 146097;
  doe = z - era * 146097; yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100); mp = (5 * doy + 2) / 153;
  memset(buf, 0, sizeof *buf);
  buf->tm_mday = (int)(doy - (153 * mp + 2) / 5 + 1);
  buf->tm_mon = (int)(mp < 10 ? mp + 2 : mp - 10);
  buf->tm_year = (int)(yoe + era * 400 + (buf->tm_mon <= 1) - 1900);
  buf->tm_hour = (int)(rem / 3600); buf->tm_min = (int)(rem % 3600 / 60); buf->tm_sec = (int)(rem % 60);
  return buf;
}

static int bio_send(void *ctx, const unsigned char *b, size_t n) {
  int r = send(*(SOCKET *)ctx, (const char *)b, (int)n, 0);
  return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : r;
}
static int bio_recv(void *ctx, unsigned char *b, size_t n) {
  int r = recv(*(SOCKET *)ctx, (char *)b, (int)n, 0);
  return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : r;
}

static int handshake(const char *cafile, int port, int force12) {
  WSADATA wsa; SOCKET s; struct sockaddr_in sa; int ret; char err[128];
  mbedtls_ssl_context ssl; mbedtls_ssl_config conf; mbedtls_x509_crt ca;
  mbedtls_entropy_context ent; mbedtls_ctr_drbg_context drbg;
  DWORD t0;
  WSAStartup(MAKEWORD(1, 1), &wsa);
  s = socket(AF_INET, SOCK_STREAM, 0);
  memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_port = htons((u_short)port);
  sa.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) { printf("connect failed\n"); return 1; }
  mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf); mbedtls_x509_crt_init(&ca);
  mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&drbg);
  if ((ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, (const unsigned char *)"tachyon", 7))) goto fail;
  {
    FILE *f = fopen(cafile, "rb"); static unsigned char pem[65536]; size_t n;
    if (!f) { printf("no CA file\n"); return 1; }
    n = fread(pem, 1, sizeof pem - 1, f); fclose(f); pem[n] = 0;
    if ((ret = mbedtls_x509_crt_parse(&ca, pem, n + 1))) goto fail;
  }
  mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (force12) mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&conf, &ca, NULL);
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
  if ((ret = mbedtls_ssl_setup(&ssl, &conf))) goto fail;
  mbedtls_ssl_set_hostname(&ssl, "localhost");
  mbedtls_ssl_set_bio(&ssl, &s, bio_send, bio_recv, NULL);
  t0 = GetTickCount();
  while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) goto fail;
  printf("handshake ok: %s %s (%lu ms)\n", mbedtls_ssl_get_version(&ssl), mbedtls_ssl_get_ciphersuite(&ssl), (unsigned long)(GetTickCount() - t0));
  {
    const char *req = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n"; unsigned char buf[512];
    mbedtls_ssl_write(&ssl, (const unsigned char *)req, strlen(req));
    do { ret = mbedtls_ssl_read(&ssl, buf, sizeof buf - 1); } while (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
    if (ret > 0) { buf[ret] = 0; printf("read %d bytes: %.40s...\n", ret, buf); }
  }
  mbedtls_ssl_close_notify(&ssl); closesocket(s);
  return 0;
fail:
  mbedtls_strerror(ret, err, sizeof err); printf("FAILED: -0x%04x %s\n", (unsigned)-ret, err); return 1;
}

int main(int argc, char **argv) {
  int fails = 0;
  if (psa_crypto_init() != PSA_SUCCESS) { printf("psa_crypto_init failed\n"); return 1; }
  fails += mbedtls_sha256_self_test(0) != 0;
  fails += mbedtls_aes_self_test(0) != 0;
  fails += mbedtls_gcm_self_test(0) != 0;
  fails += mbedtls_ecp_self_test(0) != 0;
  printf("self-tests: %s\n", fails ? "FAILED" : "passed");
  { long long now = tachyon_time(NULL); struct tm tm; mbedtls_platform_gmtime_r(&now, &tm);
    printf("clock: %04d-%02d-%02d %02d:%02d UTC\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min); }
  if (argc >= 3) { fails += handshake(argv[1], atoi(argv[2]), 0); fails += handshake(argv[1], atoi(argv[2]), 1); }
  return fails;
}
