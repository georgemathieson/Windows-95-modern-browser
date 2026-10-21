#include "tw_log.h"

#include <cpuid.h>
#include <stdio.h>
#include <string.h>

#include "tc_log.h"
#include "tc_sha256.h"
#include "tc_version.h"

/* Room for several sessions, but small enough for Windows 95's Notepad,
   which can't open a file over 64 KB. */
#define LOG_MAX_BYTES (48u * 1024)

#define SESSION_MARK "=== Tachyon "
#define CLEAN_MARK   "=== closed normally"
#define CRASH_MARK   "*** CRASH"

static char log_path[MAX_PATH], old_path[MAX_PATH];
static HANDLE log_file = INVALID_HANDLE_VALUE;
static DWORD window_thread;
static int quiet;
static LONG crashing;
static LPTOP_LEVEL_EXCEPTION_FILTER previous_filter;

static void write_raw(const char *s, size_t len)
{
    DWORD written;
    if (log_file != INVALID_HANDLE_VALUE)
        WriteFile(log_file, s, (DWORD)len, &written, NULL);
}

static void write_line(const char *line, size_t len, void *ctx)
{
    (void)ctx;
    write_raw(line, len);
    write_raw("\r\n", 2);
}

/* path: this program's folder plus name. */
static void beside_exe(char *path, const char *name)
{
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash;
    if (n == 0 || n >= MAX_PATH)
        path[0] = '\0';
    slash = strrchr(path, '\\');
    if (slash && (size_t)(slash + 1 - path) + strlen(name) < MAX_PATH)
        strcpy(slash + 1, name);
    else
        strcpy(path, name);
}

/* A loaded module's code section, from its headers in memory. */
static int code_range(DWORD base, DWORD *start, DWORD *end)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_SECTION_HEADER *sec;
    WORD i;

    if (IsBadReadPtr(dos, sizeof *dos) || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (const IMAGE_NT_HEADERS *)(base + (DWORD)dos->e_lfanew);
    if (IsBadReadPtr(nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    sec = IMAGE_FIRST_SECTION(nt);
    if (IsBadReadPtr(sec, nt->FileHeader.NumberOfSections * sizeof *sec))
        return 0;
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sec[i].Characteristics & IMAGE_SCN_CNT_CODE) {
            *start = base + sec[i].VirtualAddress;
            *end = *start + sec[i].Misc.VirtualSize;
            return 1;
        }
    }
    return 0;
}

/* The first 16 hex digits of the SHA-256 of our code, so a report can be
   matched with the build that wrote it. */
static void code_hash(char out[17])
{
    DWORD start, end;
    uint8_t digest[TC_SHA256_SIZE];
    int i;

    strcpy(out, "unknown");
    if (!code_range((DWORD)GetModuleHandleA(NULL), &start, &end))
        return;
    tc_sha256_digest((const void *)start, end - start, digest);
    for (i = 0; i < 8; i++)
        sprintf(out + 2 * i, "%02x", digest[i]);
}

static void cpu_name(char *out, size_t size)
{
    unsigned a, b, c, d;
    char vendor[13];

    if (!__get_cpuid(0, &a, &b, &c, &d)) {
        snprintf(out, size, "without CPUID");
        return;
    }
    memcpy(vendor, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = '\0';
    __get_cpuid(1, &a, &b, &c, &d);
    snprintf(out, size, "%s family %u model %u stepping %u", vendor, (a >> 8) & 15, (a >> 4) & 15, a & 15);
}

/* How the last run ended: look at the end of the log, from its last
   session's first line. */
static tw_last_run read_last_run(DWORD *size)
{
    static char tail[16 * 1024];
    HANDLE f = CreateFileA(log_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD len, got = 0;
    char *session, *p;

    *size = 0;
    if (f == INVALID_HANDLE_VALUE)
        return TW_LAST_RUN_NONE;
    *size = GetFileSize(f, NULL);
    if (*size == 0xFFFFFFFF)
        *size = 0;
    len = *size < sizeof tail - 1 ? *size : (DWORD)sizeof tail - 1;
    SetFilePointer(f, -(LONG)len, NULL, FILE_END);
    if (!ReadFile(f, tail, len, &got, NULL))
        got = 0;
    CloseHandle(f);
    tail[got] = '\0';
    if (!got)
        return TW_LAST_RUN_NONE;
    /* A session starts with a line of its own, not with the copy of it in
       a crash report's recent lines (which are indented). */
    session = tail;
    for (p = strstr(tail, SESSION_MARK); p; p = strstr(p + 1, SESSION_MARK)) {
        const char *line = p;
        while (line > tail && line[-1] != '\n')
            line--;
        if (*line == '[')
            session = p;
    }
    if (strstr(session, CRASH_MARK))
        return TW_LAST_RUN_CRASHED;
    if (strstr(session, CLEAN_MARK))
        return TW_LAST_RUN_CLEAN;
    return TW_LAST_RUN_UNCLEAN;
}

/* ---- The crash report ----
 *
 * A crash may leave little stack and the C library in a bad state, so this
 * formats with static buffers and its own number printing, and writes the
 * most useful lines first in case looking up modules goes wrong. */

static char report[256];
static char *out_end;

static void out_start(void)
{
    out_end = report;
    report[0] = '\0';
}

static void out_str(const char *s)
{
    while (*s && out_end < report + sizeof report - 3)
        *out_end++ = *s++;
    *out_end = '\0';
}

static void out_hex(DWORD v)
{
    char digits[9];
    int i;
    for (i = 7; i >= 0; i--, v >>= 4)
        digits[i] = "0123456789ABCDEF"[v & 15];
    digits[8] = '\0';
    out_str(digits);
}

static void out_flush(void)
{
    write_raw(report, (size_t)(out_end - report));
    write_raw("\r\n", 2);
    out_start();
}

static void out_reg(const char *name, DWORD v)
{
    out_str(name);
    out_str("=");
    out_hex(v);
    out_str(" ");
}

typedef struct module {
    DWORD base, code_start, code_end; /* no code: start == end */
    char name[16];
} module;

static module modules[32];
static int nmodules;
static int lookups; /* VirtualQuery calls, which are slow: limited */

/* The module whose code contains addr, or NULL. */
static const module *code_module(DWORD addr)
{
    static char path[MAX_PATH];
    MEMORY_BASIC_INFORMATION mbi;
    module *m;
    const char *slash;
    DWORD base;
    int i;

    for (i = 0; i < nmodules; i++)
        if (addr >= modules[i].code_start && addr < modules[i].code_end)
            return &modules[i];
    if (addr < 0x10000 || nmodules == (int)TC_ARRAY_LEN(modules) || ++lookups > 4000)
        return NULL;
    if (!VirtualQuery((void *)addr, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || !mbi.AllocationBase)
        return NULL;
    base = (DWORD)mbi.AllocationBase;
    for (i = 0; i < nmodules; i++)
        if (modules[i].base == base)
            return NULL; /* a module we know, outside its code */
    m = &modules[nmodules++];
    memset(m, 0, sizeof *m);
    m->base = base;
    if (!code_range(base, &m->code_start, &m->code_end))
        return NULL;
    if (GetModuleFileNameA((HMODULE)base, path, sizeof path)) {
        slash = strrchr(path, '\\');
        lstrcpynA(m->name, slash ? slash + 1 : path, sizeof m->name);
    } else {
        lstrcpynA(m->name, "?", sizeof m->name);
    }
    return addr >= m->code_start && addr < m->code_end ? m : NULL;
}

/* "TACHYON.EXE 0040A1B2" (look it up in the symbols file) or "KERNEL32.DLL+0002A485". */
static void out_code(const module *m, DWORD addr)
{
    out_str(m->name);
    if (m->base == (DWORD)GetModuleHandleA(NULL)) {
        out_str(" ");
        out_hex(addr);
    } else {
        out_str("+");
        out_hex(addr - m->base);
    }
}

static int page_readable(DWORD addr)
{
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery((void *)addr, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
           !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

/* Report code addresses stored in [from, to): return addresses, mostly.
   Nearest (most recent) first. */
static void scan_stack(DWORD from, DWORD to, int max_hits)
{
    DWORD p, page = 0;
    int ok = 0, hits = 0;

    for (p = from & ~3u; p < to && hits < max_hits; p += 4) {
        const module *m;
        DWORD v;
        if ((p & ~0xFFFu) != page) {
            page = p & ~0xFFFu;
            ok = page_readable(page);
        }
        if (!ok) {
            p = page + 0x1000 - 4;
            continue;
        }
        v = *(const DWORD *)p;
        m = code_module(v);
        if (m) {
            out_str("  ");
            out_hex(p);
            out_str(": ");
            out_code(m, v);
            out_flush();
            hits++;
        }
    }
    if (!hits) {
        out_str("  (none)");
        out_flush();
    }
}

static void report_recent(const char *line, void *ctx)
{
    (void)ctx;
    write_raw("  ", 2);
    write_raw(line, strlen(line));
    write_raw("\r\n", 2);
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    const CONTEXT *c = ep->ContextRecord;
    const module *m;
    DWORD stack_top, stack_low;

    /* (Windows 95's InterlockedIncrement returns only the result's sign.) */
    if (InterlockedExchange(&crashing, 1))
        return EXCEPTION_CONTINUE_SEARCH; /* crashed while reporting: let Windows handle it */
    __asm__ volatile("movl %%fs:4, %0\n\tmovl %%fs:8, %1" : "=r"(stack_top), "=r"(stack_low));

    out_start();
    write_raw("\r\n", 2);
    out_str(CRASH_MARK ": exception ");
    out_hex(er->ExceptionCode);
    out_str(" at ");
    out_hex((DWORD)er->ExceptionAddress);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
        out_str(er->ExceptionInformation[0] ? " writing " : " reading ");
        out_hex((DWORD)er->ExceptionInformation[1]);
    }
    out_str(GetCurrentThreadId() == window_thread ? ", window thread" : ", page loader thread");
    out_flush();
    out_reg("EAX", c->Eax);
    out_reg("EBX", c->Ebx);
    out_reg("ECX", c->Ecx);
    out_reg("EDX", c->Edx);
    out_reg("ESI", c->Esi);
    out_reg("EDI", c->Edi);
    out_flush();
    out_reg("EBP", c->Ebp);
    out_reg("ESP", c->Esp);
    out_reg("EIP", c->Eip);
    out_reg("EFL", c->EFlags);
    out_reg("CS", c->SegCs);
    out_reg("SS", c->SegSs);
    out_reg("FS", c->SegFs);
    out_flush();
    out_str("Thread stack ");
    out_hex(stack_low);
    out_str("-");
    out_hex(stack_top);
    out_flush();
    write_raw("Recent, oldest first:\r\n", 23);
    tc_log_recent(report_recent, NULL);
    FlushFileBuffers(log_file);

    m = code_module((DWORD)er->ExceptionAddress);
    if (m) {
        out_str("Crashed in ");
        out_code(m, (DWORD)er->ExceptionAddress);
        out_flush();
    }
    out_str("Code addresses near ESP:");
    out_flush();
    scan_stack(c->Esp, c->Esp + 2048, 16);
    if (c->Esp < stack_low || c->Esp >= stack_top) {
        /* Running on another stack (Windows 95 does, in parts of its 16-bit
           code): the thread's own stack shows how it got there. */
        out_str("Code addresses on the thread's own stack:");
        out_flush();
        scan_stack(stack_low, stack_top, 48);
    } else {
        out_str("Further up the stack:");
        out_flush();
        scan_stack(c->Esp + 2048, stack_top, 48);
    }
    out_str("*** end of crash report");
    out_flush();
    FlushFileBuffers(log_file);

    if (quiet)
        return EXCEPTION_EXECUTE_HANDLER;
    return previous_filter ? previous_filter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

/* ---- Starting and stopping ---- */

static void write_header(const char *cmdline, tw_last_run last)
{
    static const char *const last_text[] = { "first run", "closed normally", "crashed (its report is above)",
                                              "ended without closing (killed, or a crash with no report)" };
    OSVERSIONINFOA os;
    MEMORYSTATUS mem;
    SYSTEMTIME now;
    char hash[17], cpu[64];
    HDC screen;

    GetLocalTime(&now);
    write_raw("\r\n", 2);
    tc_log(SESSION_MARK "%s (Sprint %d: %s), started %04u-%02u-%02u %02u:%02u:%02u", TC_VERSION, TC_SPRINT,
           TC_SPRINT_NAME, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    code_hash(hash);
    tc_log("build %s, configured %lld, code %s", TC_BUILD_COMMIT, TC_BUILD_UNIX, hash);

    memset(&os, 0, sizeof os);
    os.dwOSVersionInfoSize = sizeof os;
    GetVersionExA(&os);
    cpu_name(cpu, sizeof cpu);
    mem.dwLength = sizeof mem;
    GlobalMemoryStatus(&mem);
    tc_log("Windows %lu.%lu.%lu%s%s (platform %lu); %s; %lu MB RAM, %lu MB free", os.dwMajorVersion,
           os.dwMinorVersion, os.dwBuildNumber & 0xFFFF, os.szCSDVersion[0] ? " " : "", os.szCSDVersion,
           os.dwPlatformId, cpu, (unsigned long)(mem.dwTotalPhys >> 20), (unsigned long)(mem.dwAvailPhys >> 20));
    screen = GetDC(NULL);
    tc_log("screen %dx%d, %d bits per pixel; command line: %.80s", GetSystemMetrics(SM_CXSCREEN),
           GetSystemMetrics(SM_CYSCREEN), GetDeviceCaps(screen, BITSPIXEL) * GetDeviceCaps(screen, PLANES),
           cmdline ? cmdline : "");
    ReleaseDC(NULL, screen);
    tc_log("last run: %s", last_text[last]);
}

tw_last_run tw_log_start(const char *cmdline, int quiet_crash)
{
    tw_last_run last;
    DWORD size;

    window_thread = GetCurrentThreadId();
    quiet = quiet_crash;
    beside_exe(log_path, "TACHYON.LOG");
    beside_exe(old_path, "TACHYON.OLD");
    last = read_last_run(&size);
    /* Start afresh when the log is big, but keep a crash report in it. */
    if (size > LOG_MAX_BYTES && last != TW_LAST_RUN_CRASHED) {
        DeleteFileA(old_path);
        MoveFileA(log_path, old_path);
    }
    log_file = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file != INVALID_HANDLE_VALUE)
        SetFilePointer(log_file, 0, NULL, FILE_END);
    tc_log_init(write_line, NULL);
    write_header(cmdline, last);
    previous_filter = SetUnhandledExceptionFilter(on_crash);
    return last;
}

void tw_log_stop(void)
{
    if (log_file == INVALID_HANDLE_VALUE)
        return;
    tc_log(CLEAN_MARK);
    tc_log_shutdown();
    CloseHandle(log_file);
    log_file = INVALID_HANDLE_VALUE;
}

void tw_log_view(HWND owner)
{
    char shortp[MAX_PATH], command[MAX_PATH + 16], text[MAX_PATH + 64];
    const char *path = GetShortPathNameA(log_path, shortp, sizeof shortp) ? shortp : log_path;

    snprintf(command, sizeof command, "notepad.exe %s", path);
    if (WinExec(command, SW_SHOWNORMAL) < 32) {
        snprintf(text, sizeof text, "Tachyon couldn't start Notepad. The log is %s.", log_path);
        MessageBoxA(owner, text, "Tachyon", MB_OK | MB_ICONINFORMATION);
    }
}

/* Read at run time, so the compiler can't see the write is bad. */
static int *volatile crash_target = (int *)16;

/* Not inlined, so the report has a few of our frames to find. */
__attribute__((noinline)) static void crash_inner(int *p)
{
    tc_trace("crash test: writing to %p", (void *)p);
    *p = 1;
}

__attribute__((noinline)) void tw_log_crash_test(void)
{
    tc_log("crash test");
    crash_inner(crash_target);
}
