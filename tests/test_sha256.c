#include "tc_test.h"

#include "tc_sha256.h"

void tc_suite_sha256(void);

static void hex(const uint8_t *digest, char out[65])
{
    static const char digits[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; i++) {
        out[i * 2] = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 15];
    }
    out[64] = '\0';
}

static const char *sha256_hex(const char *s, char out[65])
{
    uint8_t digest[32];
    tc_sha256_digest(s, strlen(s), digest);
    hex(digest, out);
    return out;
}

/* Test vectors from FIPS 180-4 / NIST's examples. */
static void test_known_vectors(void)
{
    char out[65];
    TC_CHECK_STR(sha256_hex("", out), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    TC_CHECK_STR(sha256_hex("abc", out), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    TC_CHECK_STR(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", out),
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void test_incremental_matches_one_shot(void)
{
    const char *msg = "The modern web, sent back to 1995. 0123456789 abcdefghijklmnopqrstuvwxyz";
    uint8_t a[32], b[32];
    tc_sha256 h;
    size_t i;

    tc_sha256_digest(msg, strlen(msg), a);
    tc_sha256_init(&h);
    for (i = 0; msg[i]; i++)
        tc_sha256_update(&h, msg + i, 1);
    tc_sha256_final(&h, b);
    TC_CHECK(memcmp(a, b, 32) == 0);
}

static void test_million_a(void)
{
    char block[1000], out[65];
    uint8_t digest[32];
    tc_sha256 h;
    int i;

    memset(block, 'a', sizeof block);
    tc_sha256_init(&h);
    for (i = 0; i < 1000; i++)
        tc_sha256_update(&h, block, sizeof block);
    tc_sha256_final(&h, digest);
    hex(digest, out);
    TC_CHECK_STR(out, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

void tc_suite_sha256(void)
{
    TC_RUN(test_known_vectors);
    TC_RUN(test_incremental_matches_one_shot);
    TC_RUN(test_million_a);
}
