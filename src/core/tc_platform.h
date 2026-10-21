/* What the core needs from the operating system.
 *
 * The core never includes OS headers. Each platform implements this file:
 * src/platform/win32 for Windows 95, src/platform/posix for the Linux host
 * that runs the tests. Keep it small; add to it only when the core needs
 * something new. */
#ifndef TC_PLATFORM_H
#define TC_PLATFORM_H

#include "tc_base.h"

/* ---- Time ---- */

/* Seconds since 1970-01-01 UTC from the system clock, which may be wrong. */
int64_t tc_time_unix(void);
/* Milliseconds from an arbitrary start. Wraps after 49.7 days, so only
   compare differences. */
uint32_t tc_time_ms(void);

/* ---- Threads ---- */

typedef struct tc_mutex tc_mutex;
typedef struct tc_sem tc_sem;

#define TC_WAIT_FOREVER 0xFFFFFFFFu

tc_mutex *tc_mutex_create(void); /* NULL if out of memory */
void tc_mutex_destroy(tc_mutex *m);
void tc_mutex_lock(tc_mutex *m);
void tc_mutex_unlock(tc_mutex *m);

tc_sem *tc_sem_create(unsigned initial);
void tc_sem_destroy(tc_sem *s);
void tc_sem_post(tc_sem *s);
/* TC_OK, or TC_ERR_TIMEOUT after timeout_ms (TC_WAIT_FOREVER: no limit). */
int tc_sem_wait(tc_sem *s, uint32_t timeout_ms);

/* Run fn(arg) on a new thread. Nobody waits for it: it must clean up after
   itself. */
int tc_thread_start(void (*fn)(void *arg), void *arg);

/* ---- Entropy (collected into the pool in tc_entropy.c) ---- */

/* Bytes from the OS's cryptographic random generator. Returns how many were
   written: 0 when there is none (Windows 95 without CryptoAPI). */
size_t tc_entropy_os(void *buf, size_t len);
/* A high-resolution timestamp taken after a little unpredictable work. Its
   jitter is a weak but always-available entropy source. */
uint32_t tc_entropy_jitter(void);
/* Pass system state that differs between runs (process ID, clocks, memory)
   to add(). Mixed in but never counted as entropy. */
void tc_entropy_snapshot(void (*add)(const void *data, size_t len));
/* Where to keep a random seed between runs, or NULL for none. */
const char *tc_entropy_seed_path(void);

/* ---- Network: IPv4 TCP ----
 *
 * Every blocking call gives up with TC_ERR_TIMEOUT after timeout_ms and with
 * TC_ERR_CANCELLED soon after *cancel becomes non-zero (cancel may be NULL).
 * Only name lookup can't be interrupted. */

typedef struct tc_socket tc_socket;

int tc_net_init(void);
/* Look up an IPv4 address (dotted quads are parsed, not looked up). */
int tc_net_resolve(const char *host, uint8_t addr[4]);
int tc_net_connect(const uint8_t addr[4], uint16_t port, uint32_t timeout_ms,
                   const volatile int *cancel, tc_socket **out);
/* Bytes sent (at least 1), or a negative tc_err. */
int tc_net_send(tc_socket *s, const void *data, size_t len, uint32_t timeout_ms,
                const volatile int *cancel);
/* Bytes received, 0 when the peer has closed the connection, or a negative
   tc_err. */
int tc_net_recv(tc_socket *s, void *buf, size_t len, uint32_t timeout_ms,
                const volatile int *cancel);
void tc_net_close(tc_socket *s);

#endif
