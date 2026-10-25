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

/* GetTickCount only moves every 55 ms on Windows 95 (the PC's 18.2 Hz timer),
   too coarse to time a page. The performance counter runs off the timer
   chip at 1.19 MHz there; machines without one fall back. */
uint32_t tc_time_ms(void)
{
    static LARGE_INTEGER freq; /* zero until first use; racing threads store the same value */
    LARGE_INTEGER now;

    if (freq.QuadPart == 0 && !QueryPerformanceFrequency(&freq))
        freq.QuadPart = -1;
    if (freq.QuadPart <= 0 || !QueryPerformanceCounter(&now))
        return GetTickCount();
    return (uint32_t)(now.QuadPart / freq.QuadPart * 1000 + now.QuadPart % freq.QuadPart * 1000 / freq.QuadPart);
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

/* ---- Files and fonts ---- */

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && lstrcmpiA(s + a - b, suffix) == 0;
}

void tc_dir_list(const char *dir, const char *suffix, void (*fn)(void *ctx, const char *path), void *ctx)
{
    char path[MAX_PATH];
    size_t len = strlen(dir);
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if (len + 3 >= MAX_PATH)
        return;
    memcpy(path, dir, len);
    if (len && path[len - 1] != '\\' && path[len - 1] != '/')
        path[len++] = '\\';
    strcpy(path + len, "*");
    h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && ends_with(fd.cFileName, suffix) &&
            len + strlen(fd.cFileName) < MAX_PATH) {
            strcpy(path + len, fd.cFileName);
            fn(ctx, path);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* Windows lists fonts by their full names, like "Arial Bold Italic
   (TrueType)": the family and style are in the name. */
static void font_from_registry(const char *name, const char *file, const char *fonts_dir,
                               void (*fn)(void *ctx, const char *path, const char *family, int weight, int italic),
                               void *ctx)
{
    static const struct {
        const char *suffix;
        int weight, italic;
    } styles[] = {
        { " Bold Italic", 700, 1 }, { " Bold Oblique", 700, 1 }, { " Bold", 700, 0 }, { " Italic", 400, 1 },
        { " Oblique", 400, 1 }, { " Regular", 400, 0 },
    };
    char family[128], path[MAX_PATH];
    size_t len = strlen(name), i;
    int weight = 400, italic = 0, collection = ends_with(file, ".ttc") || strchr(name, '&');

    if (!ends_with(name, " (TrueType)") || (!ends_with(file, ".ttf") && !collection))
        return; /* bitmap fonts */
    if (strchr(file, ':') || file[0] == '\\') {
        if (strlen(file) >= MAX_PATH)
            return;
        strcpy(path, file);
    } else {
        if (strlen(fonts_dir) + strlen(file) + 2 > MAX_PATH)
            return;
        strcpy(path, fonts_dir);
        strcat(path, "\\");
        strcat(path, file);
    }
    if (collection) { /* "MS Gothic & MS PGothic": the faces' names are in the file */
        fn(ctx, path, NULL, 0, 0);
        return;
    }
    len -= 11;
    if (len >= sizeof family)
        return;
    memcpy(family, name, len);
    family[len] = '\0';
    for (i = 0; i < sizeof styles / sizeof styles[0]; i++)
        if (ends_with(family, styles[i].suffix)) {
            family[strlen(family) - strlen(styles[i].suffix)] = '\0';
            weight = styles[i].weight;
            italic = styles[i].italic;
            break;
        }
    fn(ctx, path, family, weight, italic);
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

void tc_system_fonts(void (*fn)(void *ctx, const char *path, const char *family, int weight, int italic), void *ctx)
{
    static const char *const keys[] = {
        "Software\\Microsoft\\Windows\\CurrentVersion\\Fonts",    /* Windows 95 */
        "Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", /* Windows NT */
    };
    char fonts_dir[MAX_PATH];
    UINT len = GetWindowsDirectoryA(fonts_dir, MAX_PATH - 8);
    size_t k;
    int found = 0;

    if (!len || len >= MAX_PATH - 8)
        return;
    strcpy(fonts_dir + len, "\\FONTS");
    for (k = 0; k < sizeof keys / sizeof keys[0] && !found; k++) {
        HKEY key;
        DWORD i;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keys[k], 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        for (i = 0;; i++) {
            char name[256], file[MAX_PATH];
            DWORD name_len = sizeof name, file_len = sizeof file - 1, type;
            LONG r = RegEnumValueA(key, i, name, &name_len, NULL, &type, (BYTE *)file, &file_len);
            if (r == ERROR_NO_MORE_ITEMS)
                break;
            if (r != ERROR_SUCCESS || type != REG_SZ)
                continue;
            file[file_len] = '\0';
            font_from_registry(name, file, fonts_dir, fn, ctx);
            found = 1;
        }
        RegCloseKey(key);
    }
    if (!found) {
        struct dir_fonts d;
        d.fn = fn;
        d.ctx = ctx;
        tc_dir_list(fonts_dir, ".ttf", font_file, &d);
        tc_dir_list(fonts_dir, ".ttc", font_file, &d);
    }
}
