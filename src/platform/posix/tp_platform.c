/* tc_platform.h for the Linux host (and other POSIX systems): time, threads
 * and entropy. Only used for tests and host tools. */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "tc_platform.h"

/* ---- Time ---- */

int64_t tc_time_unix(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec;
}

uint32_t tc_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

/* ---- Threads ---- */

struct tc_mutex {
    pthread_mutex_t m;
};

/* A counting semaphore from a mutex and condition variable, so timed waits
   work the same everywhere. */
struct tc_sem {
    pthread_mutex_t m;
    pthread_cond_t c;
    unsigned count;
};

tc_mutex *tc_mutex_create(void)
{
    tc_mutex *m = malloc(sizeof *m);
    if (m && pthread_mutex_init(&m->m, NULL) != 0) {
        free(m);
        m = NULL;
    }
    return m;
}

void tc_mutex_destroy(tc_mutex *m)
{
    if (m) {
        pthread_mutex_destroy(&m->m);
        free(m);
    }
}

void tc_mutex_lock(tc_mutex *m)
{
    pthread_mutex_lock(&m->m);
}

void tc_mutex_unlock(tc_mutex *m)
{
    pthread_mutex_unlock(&m->m);
}

tc_sem *tc_sem_create(unsigned initial)
{
    tc_sem *s = malloc(sizeof *s);
    if (!s)
        return NULL;
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, NULL);
    s->count = initial;
    return s;
}

void tc_sem_destroy(tc_sem *s)
{
    if (s) {
        pthread_cond_destroy(&s->c);
        pthread_mutex_destroy(&s->m);
        free(s);
    }
}

void tc_sem_post(tc_sem *s)
{
    pthread_mutex_lock(&s->m);
    s->count++;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
}

int tc_sem_wait(tc_sem *s, uint32_t timeout_ms)
{
    struct timespec deadline;
    int result = TC_OK;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&s->m);
    while (s->count == 0) {
        int err = timeout_ms == TC_WAIT_FOREVER ? pthread_cond_wait(&s->c, &s->m)
                                                : pthread_cond_timedwait(&s->c, &s->m, &deadline);
        if (err == ETIMEDOUT) {
            result = TC_ERR_TIMEOUT;
            break;
        }
    }
    if (result == TC_OK)
        s->count--;
    pthread_mutex_unlock(&s->m);
    return result;
}

typedef struct thread_start {
    void (*fn)(void *);
    void *arg;
} thread_start;

static void *thread_main(void *param)
{
    thread_start start = *(thread_start *)param;
    free(param);
    start.fn(start.arg);
    return NULL;
}

int tc_thread_start(void (*fn)(void *arg), void *arg)
{
    thread_start *start = malloc(sizeof *start);
    pthread_t thread;

    if (!start)
        return TC_ERR_NOMEM;
    start->fn = fn;
    start->arg = arg;
    if (pthread_create(&thread, NULL, thread_main, start) != 0) {
        free(start);
        return TC_ERR_NOMEM;
    }
    pthread_detach(thread);
    return TC_OK;
}

/* ---- Entropy ---- */

size_t tc_entropy_os(void *buf, size_t len)
{
    size_t done = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return 0;
    while (done < len) {
        ssize_t n = read(fd, (char *)buf + done, len - done);
        if (n <= 0)
            break;
        done += (size_t)n;
    }
    close(fd);
    return done == len ? len : 0;
}

uint32_t tc_entropy_jitter(void)
{
    static volatile uint8_t scratch[65536];
    struct timespec a, b;
    uint32_t x;
    int i;

    clock_gettime(CLOCK_MONOTONIC, &a);
    x = (uint32_t)a.tv_nsec;
    for (i = 0; i < 64; i++) {
        x = x * 1103515245u + 12345u;
        scratch[x >> 16] += (uint8_t)x;
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    return (uint32_t)(b.tv_nsec - a.tv_nsec) + (uint32_t)(b.tv_sec - a.tv_sec) * 1000000000u;
}

void tc_entropy_snapshot(void (*add)(const void *data, size_t len))
{
    struct timespec ts;
    pid_t pid = getpid();
    add(&pid, sizeof pid);
    clock_gettime(CLOCK_REALTIME, &ts);
    add(&ts, sizeof ts);
}

const char *tc_entropy_seed_path(void)
{
    return NULL; /* /dev/urandom is always there */
}

/* ---- Files and fonts ---- */

void tc_dir_list(const char *dir, const char *suffix, void (*fn)(void *ctx, const char *path), void *ctx)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t slen = strlen(suffix);
    if (!d)
        return;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        char *path;
        if (len < slen || strcasecmp(e->d_name + len - slen, suffix) != 0)
            continue;
        path = malloc(strlen(dir) + len + 2);
        if (!path)
            break;
        strcpy(path, dir);
        strcat(path, "/");
        strcat(path, e->d_name);
        fn(ctx, path);
        free(path);
    }
    closedir(d);
}

struct dir_fonts {
    void (*fn)(void *ctx, const char *path, const char *family, int weight, int italic);
    void *ctx;
};

static void font_file(void *ctx, const char *path)
{
    struct dir_fonts *d = ctx;
    d->fn(d->ctx, path, NULL, 0, 0);
}

/* The host has no Windows fonts: the Liberation fonts stand in for Arial,
   Times New Roman and Courier New, with the same metrics. */
void tc_system_fonts(void (*fn)(void *ctx, const char *path, const char *family, int weight, int italic), void *ctx)
{
    struct dir_fonts d;
    d.fn = fn;
    d.ctx = ctx;
    tc_dir_list("/usr/share/fonts/truetype/liberation", ".ttf", font_file, &d);
}
