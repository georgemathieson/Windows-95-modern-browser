/* Tests for the platform layer (time and threads) and the entropy pool,
 * which run against the real platform: Linux natively, Windows under Wine. */
#include "tc_test.h"

#include "tc_entropy.h"
#include "tc_platform.h"

void tc_suite_platform(void);

static void test_clock_is_plausible(void)
{
    /* After 2026-01-01 and before 2100: catches unit and epoch mistakes. */
    int64_t now = tc_time_unix();
    TC_CHECK(now > 1767225600);
    TC_CHECK(now < 4102444800LL);
}

typedef struct worker {
    tc_mutex *lock;
    tc_sem *done;
    int counter;
} worker;

static void worker_main(void *arg)
{
    worker *w = arg;
    int i;
    for (i = 0; i < 1000; i++) {
        tc_mutex_lock(w->lock);
        w->counter++;
        tc_mutex_unlock(w->lock);
    }
    tc_sem_post(w->done);
}

static void test_threads_mutex_and_semaphore(void)
{
    worker w;
    int i;

    w.lock = tc_mutex_create();
    w.done = tc_sem_create(0);
    w.counter = 0;
    TC_CHECK(w.lock && w.done);
    for (i = 0; i < 4; i++)
        TC_CHECK_INT(tc_thread_start(worker_main, &w), TC_OK);
    for (i = 0; i < 4; i++)
        TC_CHECK_INT(tc_sem_wait(w.done, 10000), TC_OK);
    TC_CHECK_INT(w.counter, 4000);
    tc_sem_destroy(w.done);
    tc_mutex_destroy(w.lock);
}

static void test_semaphore_timeout(void)
{
    tc_sem *s = tc_sem_create(1);
    uint32_t start;
    TC_CHECK_INT(tc_sem_wait(s, 0), TC_OK);
    start = tc_time_ms();
    TC_CHECK_INT(tc_sem_wait(s, 50), TC_ERR_TIMEOUT);
    TC_CHECK(tc_time_ms() - start >= 40);
    tc_sem_destroy(s);
}

static void test_entropy_pool(void)
{
    uint8_t a[48], b[48];
    TC_CHECK_INT(tc_entropy_init(), TC_OK);
    TC_CHECK_INT(tc_entropy_read(a, sizeof a), TC_OK);
    TC_CHECK_INT(tc_entropy_read(b, sizeof b), TC_OK);
    TC_CHECK(memcmp(a, b, sizeof a) != 0);
    TC_CHECK(tc_entropy_strength() > 0);
    printf("     entropy: %d bits from %s\n", tc_entropy_strength(), tc_entropy_sources());
}

void tc_suite_platform(void)
{
    TC_RUN(test_clock_is_plausible);
    TC_RUN(test_threads_mutex_and_semaphore);
    TC_RUN(test_semaphore_timeout);
    TC_RUN(test_entropy_pool);
}
