/* Tachyon for Windows 95: program entry point.
 *
 *   TACHYON.EXE [address] [--smoke-test]
 *
 * --smoke-test opens the window, waits for the first paint and exits with 0
 * (CI runs this under Wine). */
#include <windows.h>
#include <commctrl.h>
#include <string.h>

#include "resource.h"
#include "shell.h"
#include "view.h"

static void parse_command_line(char *cmdline, const char **url, int *smoke_test)
{
    char *token = strtok(cmdline, " \t");
    *url = NULL;
    *smoke_test = 0;
    while (token) {
        if (strcmp(token, "--smoke-test") == 0)
            *smoke_test = 1;
        else if (!*url)
            *url = token;
        token = strtok(NULL, " \t");
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show)
{
    HACCEL accel;
    HWND frame;
    MSG msg;
    const char *url;
    int smoke_test;

    (void)prev;
    parse_command_line(cmdline, &url, &smoke_test);

    InitCommonControls();
    if (!tw_view_register(instance) || !tw_shell_register(instance)) {
        MessageBoxA(NULL, "Tachyon could not register its window classes.", "Tachyon", MB_OK | MB_ICONSTOP);
        return 1;
    }
    frame = tw_shell_create(instance, url, show, smoke_test);
    if (!frame) {
        MessageBoxA(NULL, "Tachyon could not create its window.", "Tachyon", MB_OK | MB_ICONSTOP);
        return 1;
    }
    accel = LoadAcceleratorsA(instance, MAKEINTRESOURCEA(IDR_ACCEL));

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!TranslateAcceleratorA(frame, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return (int)msg.wParam;
}
