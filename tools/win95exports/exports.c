/* EXPORTS.EXE: record which functions this copy of Windows exports.
 *
 * Run it on Windows 95. It loads the system DLLs Tachyon and its installer
 * use, walks their export tables in memory and writes EXPORTS.TXT next to
 * itself (or to C:\EXPORTS.TXT if that fails). tools/win95check/dump_exports.py compares
 * the file with win95check's lists. Only names are recorded.
 *
 *   EXPORTS.EXE [/q]      /q: no message box when done */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *const dlls[] = {
    "KERNEL32.DLL", "USER32.DLL", "GDI32.DLL", "ADVAPI32.DLL", "COMCTL32.DLL",
    "COMDLG32.DLL", "SHELL32.DLL", "OLE32.DLL", "WSOCK32.DLL", "CRTDLL.DLL",
    "MSVCRT.DLL", "WS2_32.DLL",
};

static int write_exports(FILE *f, const char *name)
{
    HMODULE module = LoadLibraryA(name);
    const unsigned char *base;
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_EXPORT_DIRECTORY *dir;
    const DWORD *names;
    DWORD rva, i;

    fprintf(f, "[%s]\n", name);
    if (!module) {
        fprintf(f, "# not present\n");
        return 0;
    }
    base = (const unsigned char *)module;
    nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (rva) {
        dir = (const IMAGE_EXPORT_DIRECTORY *)(base + rva);
        names = (const DWORD *)(base + dir->AddressOfNames);
        for (i = 0; i < dir->NumberOfNames; i++)
            fprintf(f, "%s\n", (const char *)(base + names[i]));
    }
    FreeLibrary(module);
    return 1;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show)
{
    char path[MAX_PATH], message[MAX_PATH + 64], *slash;
    OSVERSIONINFOA version;
    FILE *f;
    size_t i;
    int found = 0, quiet = strstr(cmdline, "/q") != NULL;

    (void)prev;
    (void)show;
    GetModuleFileNameA(instance, path, sizeof path);
    slash = strrchr(path, '\\');
    strcpy(slash ? slash + 1 : path, "EXPORTS.TXT");
    f = fopen(path, "w");
    if (!f) {
        strcpy(path, "C:\\EXPORTS.TXT");
        f = fopen(path, "w");
    }
    if (!f) {
        if (!quiet)
            MessageBoxA(NULL, "Could not create EXPORTS.TXT.", "Tachyon exports", MB_OK | MB_ICONSTOP);
        return 1;
    }

    memset(&version, 0, sizeof version);
    version.dwOSVersionInfoSize = sizeof version;
    GetVersionExA(&version);
    fprintf(f, "# Tachyon EXPORTS.EXE\n# Windows %lu.%lu build %lu platform %lu %s\n",
            version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber & 0xFFFF,
            version.dwPlatformId, version.szCSDVersion);
    for (i = 0; i < sizeof dlls / sizeof dlls[0]; i++)
        found += write_exports(f, dlls[i]);
    fclose(f);

    if (!quiet) {
        sprintf(message, "Wrote the exports of %d DLLs to\n%s", found, path);
        MessageBoxA(NULL, message, "Tachyon exports", MB_OK | MB_ICONINFORMATION);
    }
    return 0;
}
