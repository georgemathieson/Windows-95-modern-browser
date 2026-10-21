# Sprint 0 spike: JavaScript and TLS on Windows 95

**Question:** can a modern JavaScript engine and a TLS 1.3 library run on
Windows 95?

**Answer:** yes. QuickJS-ng 0.17.0 and mbedTLS 3.6.7 both cross-compile for
the Windows 95 target, pass `win95check`, link against crtdll.dll and run
correctly under Wine. Each needed small, contained changes.

The files in [`sprint-0/`](sprint-0/) reproduce the results:

| File | What it is |
|---|---|
| `quickjs-ng-0.17.0-win9x-threading.patch` | Patch to QuickJS-ng's `cutils.h` (apply with `-DJS_WIN9X`) |
| `qjs_spike.c` | Evaluates a script using ES2023 features and prints the result |
| `mbedtls-3.6.7-win95-config.h` | `MBEDTLS_USER_CONFIG_FILE` for Windows 95 |
| `tls_spike.c` | mbedTLS self-tests, then TLS 1.3 and TLS 1.2 handshakes to a local server |
| `build_tls_spike.sh` | How the TLS spike was built |

Upstream sources: QuickJS-ng tag `v0.17.0` (commit `6d46d07`), mbedTLS tag
`v3.6.7` (commit `068ff08`).

## QuickJS-ng

**Problem:** on Windows, QuickJS-ng's threading wrappers (used by
`Atomics.wait`/`notify`) call `InitOnceExecuteOnce` and the
`CONDITION_VARIABLE` functions, which only exist from Windows Vista onwards.
Windows 95 would refuse to load the program.

**Fix:** with `JS_WIN9X` defined, the patch builds them from what Windows 95
has: a compare-and-swap guard for `js_once` (GCC's `__sync` builtins compile
to an inline `lock cmpxchg`, available on the 486 and later) and the classic
semaphore-based condition variable (the algorithm SDL uses) for `js_cond_*`.

**Result:** under Wine, the spike prints

```
{"a":[1,4,9],"b":1,"c":"tok"} 6765 18446744073709551616 1995 123 5 0.3000000000 1970-01-01T00:00:00.000Z
```

which exercises arrow functions, classes with private fields, template
literals, BigInt, regex named groups, `Array.prototype.toSorted` (ES2023),
`Math.hypot`, number formatting and `Date`. The binary is about 1.5 MB.

**For Sprint 5:** the condition variable needs a stress test with real threads
before we rely on it.

## mbedTLS

Changes in `mbedtls-3.6.7-win95-config.h`, each found by `win95check`:

| Problem | Fix |
|---|---|
| The platform entropy source is `BCryptGenRandom` (Vista+) | `MBEDTLS_NO_PLATFORM_ENTROPY` plus `MBEDTLS_ENTROPY_HARDWARE_ALT`: we provide `mbedtls_hardware_poll()` |
| `net_sockets.c` needs Winsock 2 and `getaddrinfo` (XP) | Drop `MBEDTLS_NET_C`; Tachyon supplies its own send/receive callbacks over Winsock 1.1 |
| X.509 IP address matching calls `inet_pton` (Vista) | Build with `_WIN32_WINNT=0x0400`, so mbedTLS uses its own parser |
| MinGW builds call `vsnprintf_s` (not in old CRTs) | `MBEDTLS_PLATFORM_(V)SNPRINTF_MACRO` to MinGW's own C99 `(v)snprintf` |
| The CA directory loader uses `FindFirstFileW` (a stub on Windows 95) | Drop `MBEDTLS_FS_IO`; Tachyon reads the CA bundle itself and parses it from memory |
| AES-NI code uses SSE registers. Windows 95 never enables SSE, so it would fault even on a CPU that reports AES-NI, for example in a VM on a modern PC | Drop `MBEDTLS_AESNI_C` and `MBEDTLS_PADLOCK_C` |
| crtdll's import library has no `_time32`/`_gmtime32`, and a 32-bit `time_t` ends in 2038 | `MBEDTLS_PLATFORM_TIME_MACRO` to our own clock (`GetSystemTimeAsFileTime`) with a 64-bit time type, and our own `gmtime_r` |
| No persistent key storage needed | Drop `MBEDTLS_PSA_CRYPTO_STORAGE_C`, `MBEDTLS_PSA_ITS_FILE_C`, `MBEDTLS_TIMING_C` |

**Result:** under Wine, against `openssl s_server` on localhost with a test CA:

```
self-tests: passed
clock: 2026-10-20 21:32 UTC
handshake ok: TLSv1.3 TLS1-3-CHACHA20-POLY1305-SHA256 (9 ms)
handshake ok: TLSv1.2 TLS-ECDHE-ECDSA-WITH-CHACHA20-POLY1305-SHA256 (55 ms)
```

Both handshakes verify the server certificate (`MBEDTLS_SSL_VERIFY_REQUIRED`).
The times come from a modern CPU; Sprint 1 must measure on emulated period
hardware.

**For Sprint 1:**

- The spike's entropy source is timing jitter only. It is **not** good enough
  to ship: build a real entropy pool.
- mbedTLS 3.6 is the long-term-support branch; 4.x is current. Decide which to
  vendor before starting.

## Toolchain findings

- **C runtime.** Linking against crtdll.dll instead of msvcrt.dll works with
  MinGW-w64's `libcrtdll.a`: name the libraries explicitly with
  `-nodefaultlibs`. That removes the last DLL that a fresh Windows 95 install
  lacks. It also drops the `GetModuleHandleW` stub import that msvcrt builds
  have. Now the default (`TACHYON_CRT=crtdll`).
- **CPU.** Our own code compiled with `-march=i586` contains no Pentium Pro
  instructions, but the prebuilt libgcc, libmingwex and startup code do
  (`cmov`, including in code that runs at startup). Until we build our own
  toolchain, the minimum CPU is a Pentium Pro.
- **Imports from dead code stay.** GNU ld on Windows keeps an import even when
  `--gc-sections` removes the code that used it, so unwanted APIs must be
  configured out, not just left unused.
