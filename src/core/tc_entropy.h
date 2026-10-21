/* The entropy pool behind all of Tachyon's randomness (TLS keys and nonces).
 *
 * Windows 95 has no reliable random number source: CryptoAPI only comes with
 * OSR2 or Internet Explorer 3.02. So the pool combines whatever there is:
 *
 *   - the OS generator when present (CryptGenRandom, /dev/urandom)
 *   - CPU timing jitter, which every Pentium provides
 *   - a seed saved by the previous run (TACHYON.RND next to the program)
 *   - system state and user input timings, mixed in but not counted
 *
 * Everything is hashed with SHA-256 into a 32-byte key; output is generated
 * from the key and a counter, and the key is replaced after every read. */
#ifndef TC_ENTROPY_H
#define TC_ENTROPY_H

#include "tc_base.h"

/* Gather the initial entropy. Called once at startup; tc_entropy_read does it
   if needed. Takes a moment on old CPUs. */
int tc_entropy_init(void);

/* Mix in extra input (e.g. mouse and keyboard timings). Not counted. */
void tc_entropy_add(const void *data, size_t len);

/* Fill out with random bytes. */
int tc_entropy_read(void *out, size_t len);

/* How many bits of entropy the initial gathering is credited with (at most
   256). Below 256 means randomness is weaker than it should be. */
int tc_entropy_strength(void);
/* Which sources contributed, e.g. "CryptGenRandom, CPU jitter, seed file". */
const char *tc_entropy_sources(void);

/* Save a fresh seed for the next run (no-op if the platform has no path). */
void tc_entropy_save_seed(void);

#endif
