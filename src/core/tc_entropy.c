#include "tc_entropy.h"

#include <stdio.h>
#include <string.h>

#include "tc_platform.h"
#include "tc_sha256.h"

#define FULL_STRENGTH   256
/* Credit one bit per this many timing samples that differ from the previous
   one. Deliberately conservative: a sample has a few bits of real noise. */
#define JITTER_SAMPLES_PER_BIT 4
#define JITTER_MAX_SAMPLES     (FULL_STRENGTH * JITTER_SAMPLES_PER_BIT * 4)
#define SEED_SIZE 32

static struct {
    tc_mutex *lock;
    int ready;
    uint8_t key[TC_SHA256_SIZE];
    uint32_t counter;
    int strength;
    char sources[80];
} pool;

/* key = SHA-256(key || data). Caller holds the lock (or is initialising). */
static void mix(const void *data, size_t len)
{
    tc_sha256 h;
    tc_sha256_init(&h);
    tc_sha256_update(&h, pool.key, sizeof pool.key);
    tc_sha256_update(&h, data, len);
    tc_sha256_final(&h, pool.key);
}

static void add_source(const char *name)
{
    if (pool.sources[0])
        strncat(pool.sources, ", ", sizeof pool.sources - strlen(pool.sources) - 1);
    strncat(pool.sources, name, sizeof pool.sources - strlen(pool.sources) - 1);
}

static int gather_jitter(int bits_wanted)
{
    uint32_t previous = 0, sample;
    int changes = 0, i;

    for (i = 0; i < JITTER_MAX_SAMPLES && changes < bits_wanted * JITTER_SAMPLES_PER_BIT; i++) {
        sample = tc_entropy_jitter();
        mix(&sample, sizeof sample);
        if (sample != previous)
            changes++;
        previous = sample;
    }
    return changes / JITTER_SAMPLES_PER_BIT;
}

static void read_seed(void)
{
    const char *path = tc_entropy_seed_path();
    uint8_t seed[SEED_SIZE];
    FILE *f;

    if (!path || !(f = fopen(path, "rb")))
        return;
    if (fread(seed, 1, sizeof seed, f) == sizeof seed) {
        mix(seed, sizeof seed);
        add_source("seed file");
    }
    fclose(f);
}

/* Call from the main thread before starting other threads: creating the
   lock is not itself thread-safe. */
int tc_entropy_init(void)
{
    uint8_t os[64];
    int strength = 0;

    if (pool.ready)
        return TC_OK;
    if (!pool.lock && !(pool.lock = tc_mutex_create()))
        return TC_ERR_NOMEM;
    tc_mutex_lock(pool.lock);
    if (!pool.ready) {
        tc_entropy_snapshot(mix);
        if (tc_entropy_os(os, sizeof os) == sizeof os) {
            mix(os, sizeof os);
            memset(os, 0, sizeof os);
            strength = FULL_STRENGTH;
            add_source("OS generator");
        }
        /* Jitter is always gathered: with the OS generator a little, as a
           backstop; without it, enough to reach full strength if possible. */
        strength += gather_jitter(strength ? 64 : FULL_STRENGTH);
        add_source("CPU jitter");
        read_seed();
        pool.strength = TC_MIN(strength, FULL_STRENGTH);
        pool.ready = 1;
    }
    tc_mutex_unlock(pool.lock);
    tc_entropy_save_seed();
    return TC_OK;
}

void tc_entropy_add(const void *data, size_t len)
{
    if (!pool.ready)
        return;
    tc_mutex_lock(pool.lock);
    mix(data, len);
    tc_mutex_unlock(pool.lock);
}

int tc_entropy_read(void *out, size_t len)
{
    uint8_t block[TC_SHA256_SIZE], *p = out;
    uint32_t noise;
    int err = tc_entropy_init();

    if (err)
        return err;
    tc_mutex_lock(pool.lock);
    noise = tc_entropy_jitter();
    mix(&noise, sizeof noise);
    while (len) {
        tc_sha256 h;
        size_t n = TC_MIN(len, sizeof block);
        tc_sha256_init(&h);
        tc_sha256_update(&h, pool.key, sizeof pool.key);
        tc_sha256_update(&h, "out", 3);
        tc_sha256_update(&h, &pool.counter, sizeof pool.counter);
        tc_sha256_final(&h, block);
        pool.counter++;
        memcpy(p, block, n);
        p += n;
        len -= n;
    }
    /* Replace the key, so this output can't be recomputed later. */
    mix("next", 4);
    tc_mutex_unlock(pool.lock);
    memset(block, 0, sizeof block);
    return TC_OK;
}

int tc_entropy_strength(void)
{
    return pool.strength;
}

const char *tc_entropy_sources(void)
{
    return pool.sources;
}

void tc_entropy_save_seed(void)
{
    const char *path = tc_entropy_seed_path();
    uint8_t seed[SEED_SIZE];
    FILE *f;

    if (!path || !pool.ready || tc_entropy_read(seed, sizeof seed) != TC_OK)
        return;
    f = fopen(path, "wb");
    if (f) {
        fwrite(seed, 1, sizeof seed, f);
        fclose(f);
    }
    memset(seed, 0, sizeof seed);
}
