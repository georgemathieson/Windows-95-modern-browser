/* Tachyon for Windows 95: program entry point.
 *
 *   TACHYON.EXE [address] [--proxy HOST:PORT] [--cacert FILE] [--smoke-test]
 *
 * --proxy sends every request through an HTTP proxy. --cacert trusts the
 * certificate authorities in a PEM file too (a company proxy's, say).
 * --smoke-test opens the window, waits for the first paint and exits with 0
 * (CI runs this under Wine). */
#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>

#include "resource.h"
#include "shell.h"
#include "tc_entropy.h"
#include "tc_tls.h"
#include "view.h"

static char proxy_host[256];
static const char *cacert_file;

static void parse_command_line(char *cmdline, tw_shell_options *opts)
{
    char *token = strtok(cmdline, " \t");
    while (token) {
        if (strcmp(token, "--smoke-test") == 0) {
            opts->smoke_test = 1;
        } else if (strcmp(token, "--proxy") == 0) {
            char *value = strtok(NULL, " \t"), *colon = value ? strrchr(value, ':') : NULL;
            if (colon && (size_t)(colon - value) < sizeof proxy_host) {
                memcpy(proxy_host, value, (size_t)(colon - value));
                proxy_host[colon - value] = '\0';
                opts->proxy_host = proxy_host;
                opts->proxy_port = (uint16_t)atoi(colon + 1);
            }
        } else if (strcmp(token, "--cacert") == 0) {
            cacert_file = strtok(NULL, " \t");
        } else if (!opts->start_url) {
            opts->start_url = token;
        }
        token = strtok(NULL, " \t");
    }
}

/* Mouse and keyboard timings are unpredictable: feed them to the entropy
   pool (they are mixed in, not counted). */
static void stir_entropy(const MSG *msg)
{
    if ((msg->message >= WM_KEYFIRST && msg->message <= WM_KEYLAST) ||
        (msg->message >= WM_MOUSEFIRST && msg->message <= WM_MOUSELAST))
        tc_entropy_add(msg, sizeof *msg);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show)
{
    tw_shell_options opts;
    HACCEL accel;
    HWND frame;
    MSG msg;

    (void)prev;
    memset(&opts, 0, sizeof opts);
    opts.show = show;
    parse_command_line(cmdline, &opts);

    InitCommonControls();
    if (tc_net_init() != TC_OK) {
        MessageBoxA(NULL, "Tachyon couldn't start Windows networking (Winsock).", "Tachyon", MB_OK | MB_ICONSTOP);
        return 1;
    }
    if (tc_tls_init() != TC_OK) {
        MessageBoxA(NULL, "Tachyon couldn't set up secure connections.", "Tachyon", MB_OK | MB_ICONSTOP);
        return 1;
    }
    if (cacert_file && tc_tls_add_trusted_file(cacert_file) != TC_OK)
        MessageBoxA(NULL, "Tachyon found no certificates in the --cacert file.", "Tachyon", MB_OK | MB_ICONWARNING);
    if (!tw_view_register(instance) || !tw_shell_register(instance)) {
        MessageBoxA(NULL, "Tachyon could not register its window classes.", "Tachyon", MB_OK | MB_ICONSTOP);
        return 1;
    }
    frame = tw_shell_create(instance, &opts);
    if (!frame) {
        MessageBoxA(NULL, "Tachyon could not create its window.", "Tachyon", MB_OK | MB_ICONSTOP);
        return 1;
    }
    accel = LoadAcceleratorsA(instance, MAKEINTRESOURCEA(IDR_ACCEL));

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        stir_entropy(&msg);
        if (!TranslateAcceleratorA(frame, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    tc_entropy_save_seed();
    return (int)msg.wParam;
}
