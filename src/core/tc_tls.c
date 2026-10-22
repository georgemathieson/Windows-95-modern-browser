#include "tc_tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/platform.h"
#include "mbedtls/ssl.h"
#include "mbedtls/threading.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include "tc_cacerts.h"
#include "tc_entropy.h"

struct tc_tls {
    mbedtls_ssl_context ssl;
    tc_socket *sock;
    uint32_t timeout_ms;
    const volatile int *cancel;
    int io_err; /* the socket error behind a failed TLS call */
};

static struct {
    int ready;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt roots;
    int root_count;
} tls;

/* ---- Hooks named in third_party/mbedtls/tachyon/tachyon_crypto_config.h ---- */

int mbedtls_platform_get_entropy(psa_driver_get_entropy_flags_t flags, size_t *estimate_bits,
                                 unsigned char *output, size_t output_size)
{
    if (flags != 0)
        return PSA_ERROR_NOT_SUPPORTED;
    if (tc_entropy_read(output, output_size) != TC_OK)
        return PSA_ERROR_INSUFFICIENT_ENTROPY;
    *estimate_bits = 8 * output_size;
    return 0;
}

long long tc_tls_time(long long *t)
{
    long long now = tc_time_unix();
    if (t)
        *t = now;
    return now;
}

/* Seconds since 1970 to a UTC date, without the C runtime (Howard Hinnant's
   days-to-civil algorithm). */
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt, struct tm *tm)
{
    long long secs = *tt, days = secs / 86400, rem = secs % 86400, z, era, doe, yoe, doy, mp;

    if (rem < 0) {
        rem += 86400;
        days--;
    }
    z = days + 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    memset(tm, 0, sizeof *tm);
    tm->tm_mday = (int)(doy - (153 * mp + 2) / 5 + 1);
    tm->tm_mon = (int)(mp < 10 ? mp + 2 : mp - 10);
    tm->tm_year = (int)(yoe + era * 400 + (tm->tm_mon <= 1) - 1900);
    tm->tm_hour = (int)(rem / 3600);
    tm->tm_min = (int)(rem % 3600 / 60);
    tm->tm_sec = (int)(rem % 60);
    return tm;
}

/* ---- Threading for MBEDTLS_THREADING_ALT ---- */

static int mutex_init(mbedtls_platform_mutex_t *m)
{
    m->mutex = tc_mutex_create();
    return m->mutex ? 0 : MBEDTLS_ERR_THREADING_USAGE_ERROR;
}

static void mutex_destroy(mbedtls_platform_mutex_t *m)
{
    tc_mutex_destroy(m->mutex);
    m->mutex = NULL;
}

static int mutex_lock(mbedtls_platform_mutex_t *m)
{
    tc_mutex_lock(m->mutex);
    return 0;
}

static int mutex_unlock(mbedtls_platform_mutex_t *m)
{
    tc_mutex_unlock(m->mutex);
    return 0;
}

/* A condition variable from a mutex and two semaphores (the classic
   algorithm SDL uses), since Windows 95 has none. Mbed TLS 4.1 doesn't use
   them, but the threading interface requires them. */
struct tc_cond {
    tc_mutex *lock;
    tc_sem *wait_sem, *wait_done;
    int waiting, signals;
};

static void cond_destroy(mbedtls_platform_condition_variable_t *c)
{
    if (c->cond) {
        tc_sem_destroy(c->cond->wait_sem);
        tc_sem_destroy(c->cond->wait_done);
        tc_mutex_destroy(c->cond->lock);
        free(c->cond);
        c->cond = NULL;
    }
}

static int cond_init(mbedtls_platform_condition_variable_t *c)
{
    c->cond = calloc(1, sizeof *c->cond);
    if (!c->cond)
        return MBEDTLS_ERR_THREADING_USAGE_ERROR;
    c->cond->lock = tc_mutex_create();
    c->cond->wait_sem = tc_sem_create(0);
    c->cond->wait_done = tc_sem_create(0);
    if (!c->cond->lock || !c->cond->wait_sem || !c->cond->wait_done) {
        cond_destroy(c);
        return MBEDTLS_ERR_THREADING_USAGE_ERROR;
    }
    return 0;
}

static int cond_wake(mbedtls_platform_condition_variable_t *c, int all)
{
    struct tc_cond *cv = c->cond;
    int n = 0, i;
    tc_mutex_lock(cv->lock);
    if (cv->waiting > cv->signals) {
        n = all ? cv->waiting - cv->signals : 1;
        cv->signals += n;
        for (i = 0; i < n; i++)
            tc_sem_post(cv->wait_sem);
    }
    tc_mutex_unlock(cv->lock);
    for (i = 0; i < n; i++)
        tc_sem_wait(cv->wait_done, TC_WAIT_FOREVER);
    return 0;
}

static int cond_signal(mbedtls_platform_condition_variable_t *c)
{
    return cond_wake(c, 0);
}

static int cond_broadcast(mbedtls_platform_condition_variable_t *c)
{
    return cond_wake(c, 1);
}

static int cond_wait(mbedtls_platform_condition_variable_t *c, mbedtls_platform_mutex_t *m)
{
    struct tc_cond *cv = c->cond;
    tc_mutex_lock(cv->lock);
    cv->waiting++;
    tc_mutex_unlock(cv->lock);
    tc_mutex_unlock(m->mutex);
    tc_sem_wait(cv->wait_sem, TC_WAIT_FOREVER);
    tc_mutex_lock(cv->lock);
    cv->signals--;
    cv->waiting--;
    tc_mutex_unlock(cv->lock);
    tc_sem_post(cv->wait_done);
    tc_mutex_lock(m->mutex);
    return 0;
}

/* ---- Setup ---- */

static const char *alpn[] = { "http/1.1", NULL };

int tc_tls_init(void)
{
    size_t i;

    if (tls.ready)
        return TC_OK;
    if (tc_entropy_init() != TC_OK)
        return TC_ERR_NOMEM;
    mbedtls_threading_set_alt(mutex_init, mutex_destroy, mutex_lock, mutex_unlock, cond_init, cond_destroy,
                              cond_signal, cond_broadcast, cond_wait);
    if (psa_crypto_init() != PSA_SUCCESS)
        return TC_ERR_TLS;

    mbedtls_x509_crt_init(&tls.roots);
    for (i = 0; i < tc_cacerts_count; i++)
        if (mbedtls_x509_crt_parse_der_nocopy(&tls.roots, tc_cacerts[i].der, tc_cacerts[i].len) == 0)
            tls.root_count++;

    mbedtls_ssl_config_init(&tls.conf);
    if (mbedtls_ssl_config_defaults(&tls.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        return TC_ERR_TLS;
    mbedtls_ssl_conf_min_tls_version(&tls.conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_authmode(&tls.conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls.conf, &tls.roots, NULL);
    mbedtls_ssl_conf_alpn_protocols(&tls.conf, alpn);
    tls.ready = 1;
    return TC_OK;
}

int tc_tls_add_trusted_pem(const char *pem, size_t len)
{
    char *copy = malloc(len + 1); /* Mbed TLS wants PEM NUL-terminated */
    int before = tls.root_count, ret;
    mbedtls_x509_crt *c;

    if (!copy)
        return TC_ERR_NOMEM;
    memcpy(copy, pem, len);
    copy[len] = '\0';
    ret = mbedtls_x509_crt_parse(&tls.roots, (const unsigned char *)copy, len + 1);
    free(copy);
    tls.root_count = 0;
    for (c = &tls.roots; c && c->raw.len; c = c->next)
        tls.root_count++;
    return ret < 0 || tls.root_count == before ? TC_ERR_INVALID : TC_OK;
}

int tc_tls_add_trusted_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *pem = NULL;
    long len;
    int err = TC_ERR_INVALID;

    if (!f)
        return TC_ERR_INVALID;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (pem = malloc((size_t)len)) != NULL && fread(pem, 1, (size_t)len, f) == (size_t)len)
        err = tc_tls_add_trusted_pem(pem, (size_t)len);
    free(pem);
    fclose(f);
    return err;
}

int tc_tls_trusted_count(void)
{
    return tls.root_count;
}

/* ---- Connections ---- */

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    tc_tls *t = ctx;
    int n = tc_net_send(t->sock, buf, len, t->timeout_ms, t->cancel);
    if (n < 0) {
        t->io_err = n;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    tc_tls *t = ctx;
    int n = tc_net_recv(t->sock, buf, len, t->timeout_ms, t->cancel);
    if (n < 0) {
        t->io_err = n;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return n;
}

void tc_tls_set_io(tc_tls *t, uint32_t timeout_ms, const volatile int *cancel)
{
    t->timeout_ms = timeout_ms;
    t->cancel = cancel;
}

int tc_tls_connect(tc_socket *sock, const char *host, uint32_t timeout_ms, const volatile int *cancel,
                   tc_tls **out, tc_tls_info *info)
{
    tc_tls *t;
    int ret;

    *out = NULL;
    memset(info, 0, sizeof *info);
    if (!tls.ready)
        return TC_ERR_TLS;
    t = calloc(1, sizeof *t);
    if (!t)
        return TC_ERR_NOMEM;
    t->sock = sock;
    t->timeout_ms = timeout_ms;
    t->cancel = cancel;
    mbedtls_ssl_init(&t->ssl);
    ret = mbedtls_ssl_setup(&t->ssl, &tls.conf);
    if (!ret)
        ret = mbedtls_ssl_set_hostname(&t->ssl, host);
    if (!ret) {
        mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);
        do
            ret = mbedtls_ssl_handshake(&t->ssl);
        while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    }
    if (ret) {
        uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
        int err = t->io_err ? t->io_err : TC_ERR_TLS;
        info->tls_error = ret;
        mbedtls_strerror(ret, info->detail, sizeof info->detail);
        if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
            info->cert_flags = flags;
            err = TC_ERR_CERT;
        }
        mbedtls_ssl_free(&t->ssl);
        free(t);
        return err;
    }
    snprintf(info->version, sizeof info->version, "%s", mbedtls_ssl_get_version(&t->ssl));
    snprintf(info->cipher, sizeof info->cipher, "%s", mbedtls_ssl_get_ciphersuite(&t->ssl));
    *out = t;
    return TC_OK;
}

int tc_tls_send(tc_tls *t, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len) {
        int n = mbedtls_ssl_write(&t->ssl, p, len);
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (n <= 0)
            return t->io_err ? t->io_err : TC_ERR_TLS;
        p += n;
        len -= (size_t)n;
    }
    return TC_OK;
}

int tc_tls_recv(tc_tls *t, void *buf, size_t len)
{
    for (;;) {
        int n = mbedtls_ssl_read(&t->ssl, buf, len);
        if (n > 0)
            return n;
        /* Many servers just close the connection without a close_notify;
           the HTTP layer knows whether the response was complete. */
        if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == MBEDTLS_ERR_SSL_CONN_EOF)
            return 0;
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE ||
            n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            continue;
        return t->io_err ? t->io_err : TC_ERR_TLS;
    }
}

void tc_tls_close(tc_tls *t)
{
    if (!t)
        return;
    if (!t->io_err) {
        t->timeout_ms = TC_MIN(t->timeout_ms, 1000u);
        mbedtls_ssl_close_notify(&t->ssl);
    }
    mbedtls_ssl_free(&t->ssl);
    free(t);
}

void tc_tls_describe_cert_error(uint32_t flags, char *buf, size_t size)
{
    const char *why;
    if (flags & MBEDTLS_X509_BADCERT_FUTURE)
        why = "The certificate isn't valid yet. Is your computer's clock right?";
    else if (flags & MBEDTLS_X509_BADCERT_EXPIRED)
        why = "The certificate has expired (or your computer's clock is wrong).";
    else if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
        why = "The certificate belongs to a different site.";
    else if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
        why = "The certificate isn't signed by an authority Tachyon trusts.";
    else if (flags & MBEDTLS_X509_BADCERT_REVOKED)
        why = "The certificate has been revoked.";
    else if (flags & (MBEDTLS_X509_BADCERT_BAD_MD | MBEDTLS_X509_BADCERT_BAD_PK | MBEDTLS_X509_BADCERT_BAD_KEY))
        why = "The certificate uses weak or unsupported cryptography.";
    else if (flags & (MBEDTLS_X509_BADCERT_KEY_USAGE | MBEDTLS_X509_BADCERT_EXT_KEY_USAGE))
        why = "The certificate isn't meant for websites.";
    else
        why = "The certificate couldn't be verified.";
    snprintf(buf, size, "%s", why);
}
