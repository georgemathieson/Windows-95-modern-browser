/* tc_platform.h for Windows 95: time, threads and entropy. */
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "tc_platform.h"

/* ---- Time ---- */

/* 100 ns intervals between 1601-01-01 (FILETIME) and 1970-01-01. */
#define FILETIME_UNIX_EPOCH 116444736000000000ULL

int64_t tc_time_unix(void)
{
    FILETIME ft;
    uint64_t t;
    GetSystemTimeAsFileTime(&ft);
    t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (int64_t)((t - FILETIME_UNIX_EPOCH) / 10000000ULL);
}

uint32_t tc_time_ms(void)
{
    return GetTickCount();
}

/* ---- Threads ---- */

struct tc_mutex {
    CRITICAL_SECTION cs;
};

struct tc_sem {
    HANDLE handle;
};

tc_mutex *tc_mutex_create(void)
{
    tc_mutex *m = malloc(sizeof *m);
    if (m)
        InitializeCriticalSection(&m->cs);
    return m;
}

void tc_mutex_destroy(tc_mutex *m)
{
    if (m) {
        DeleteCriticalSection(&m->cs);
        free(m);
    }
}

void tc_mutex_lock(tc_mutex *m)
{
    EnterCriticalSection(&m->cs);
}

void tc_mutex_unlock(tc_mutex *m)
{
    LeaveCriticalSection(&m->cs);
}

tc_sem *tc_sem_create(unsigned initial)
{
    tc_sem *s = malloc(sizeof *s);
    if (!s)
        return NULL;
    s->handle = CreateSemaphoreA(NULL, (LONG)initial, 0x7FFFFFFF, NULL);
    if (!s->handle) {
        free(s);
        return NULL;
    }
    return s;
}

void tc_sem_destroy(tc_sem *s)
{
    if (s) {
        CloseHandle(s->handle);
        free(s);
    }
}

void tc_sem_post(tc_sem *s)
{
    ReleaseSemaphore(s->handle, 1, NULL);
}

int tc_sem_wait(tc_sem *s, uint32_t timeout_ms)
{
    DWORD r = WaitForSingleObject(s->handle, timeout_ms == TC_WAIT_FOREVER ? INFINITE : timeout_ms);
    return r == WAIT_OBJECT_0 ? TC_OK : TC_ERR_TIMEOUT;
}

typedef struct thread_start {
    void (*fn)(void *);
    void *arg;
} thread_start;

static DWORD WINAPI thread_main(LPVOID param)
{
    thread_start start = *(thread_start *)param;
    free(param);
    start.fn(start.arg);
    return 0;
}

int tc_thread_start(void (*fn)(void *arg), void *arg)
{
    thread_start *start = malloc(sizeof *start);
    DWORD id; /* Windows 95's CreateThread fails if this pointer is NULL */
    HANDLE h;

    if (!start)
        return TC_ERR_NOMEM;
    start->fn = fn;
    start->arg = arg;
    h = CreateThread(NULL, 0, thread_main, start, 0, &id);
    if (!h) {
        free(start);
        return TC_ERR_NOMEM;
    }
    CloseHandle(h);
    return TC_OK;
}

/* ---- Entropy ---- */

/* CryptoAPI arrived with Windows 95 OSR2 and Internet Explorer 3.02, so it is
   looked up at runtime and Tachyon manages without it. */
typedef BOOL(WINAPI *acquire_fn)(ULONG_PTR *, LPCSTR, LPCSTR, DWORD, DWORD);
typedef BOOL(WINAPI *gen_random_fn)(ULONG_PTR, DWORD, BYTE *);

#define TW_PROV_RSA_FULL        1
#define TW_CRYPT_VERIFYCONTEXT  0xF0000000

size_t tc_entropy_os(void *buf, size_t len)
{
    static int tried;
    static ULONG_PTR provider;
    static gen_random_fn gen_random;

    if (!tried) {
        HMODULE advapi = LoadLibraryA("ADVAPI32.DLL");
        acquire_fn acquire = advapi ? (acquire_fn)GetProcAddress(advapi, "CryptAcquireContextA") : NULL;
        tried = 1;
        gen_random = advapi ? (gen_random_fn)GetProcAddress(advapi, "CryptGenRandom") : NULL;
        if (!acquire || !gen_random ||
            !acquire(&provider, NULL, NULL, TW_PROV_RSA_FULL, TW_CRYPT_VERIFYCONTEXT))
            gen_random = NULL;
    }
    if (!gen_random || !gen_random(provider, (DWORD)len, buf))
        return 0;
    return len;
}

/* Every Pentium has a time-stamp counter; check anyway, since CPUID itself
   is missing on most 486s. */
static int have_tsc(void)
{
    static int known, result;
    if (!known) {
        unsigned long before, after, edx = 0;
        __asm__ volatile("pushfl\n\t"
                         "pushfl\n\t"
                         "popl %0\n\t"
                         "movl %0, %1\n\t"
                         "xorl $0x200000, %0\n\t"
                         "pushl %0\n\t"
                         "popfl\n\t"
                         "pushfl\n\t"
                         "popl %0\n\t"
                         "popfl"
                         : "=&r"(after), "=&r"(before));
        if ((before ^ after) & 0x200000) {
            unsigned long eax = 1, ebx, ecx;
            __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
        }
        result = (int)((edx >> 4) & 1);
        known = 1;
    }
    return result;
}

static uint32_t timestamp(void)
{
    if (have_tsc()) {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return lo ^ hi;
    } else {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return (uint32_t)t.LowPart;
    }
}

uint32_t tc_entropy_jitter(void)
{
    /* Reads scattered over 64 KB, more than a Pentium's 8 KB data cache, so
       the time taken varies with cache misses, DRAM refresh and interrupts. */
    static volatile uint8_t scratch[65536];
    uint32_t start = timestamp(), x = start;
    int i;
    for (i = 0; i < 64; i++) {
        x = x * 1103515245u + 12345u;
        scratch[x >> 16] += (uint8_t)x;
    }
    return timestamp() - start;
}

void tc_entropy_snapshot(void (*add)(const void *data, size_t len))
{
    DWORD ids[3];
    FILETIME now;
    LARGE_INTEGER counter;
    MEMORYSTATUS memory;
    POINT cursor;

    ids[0] = GetCurrentProcessId();
    ids[1] = GetCurrentThreadId();
    ids[2] = GetTickCount();
    add(ids, sizeof ids);
    GetSystemTimeAsFileTime(&now);
    add(&now, sizeof now);
    QueryPerformanceCounter(&counter);
    add(&counter, sizeof counter);
    memory.dwLength = sizeof memory;
    GlobalMemoryStatus(&memory);
    add(&memory, sizeof memory);
    if (GetCursorPos(&cursor))
        add(&cursor, sizeof cursor);
}

const char *tc_entropy_seed_path(void)
{
    static char path[MAX_PATH];
    if (!path[0]) {
        char *slash;
        if (!GetModuleFileNameA(NULL, path, MAX_PATH - 16))
            return NULL;
        slash = strrchr(path, '\\');
        strcpy(slash ? slash + 1 : path, "TACHYON.RND");
    }
    return path;
}
