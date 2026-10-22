/* Tachyon for Windows 95: program entry point.
 *
 *   TACHYON.EXE [address] [--proxy HOST:PORT] [--cacert FILE] [--fonts DIR] [--dither]
 *               [--smoke-test] [--crash-test]
 *
 * --proxy sends every request through an HTTP proxy. --cacert trusts the
 * certificate authorities in a PEM file too (a company proxy's, say).
 * --fonts adds the TrueType fonts in a directory (for Wine, which lacks
 * Windows 95's). --dither shows pages as a 256-colour display would.
 * --smoke-test opens the window, waits for the first paint and exits with 0
 * (CI runs this under Wine). --crash-test crashes on purpose once the window
 * is open, to test the crash report in TACHYON.LOG. */
#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>

#include "resource.h"
#include "shell.h"
#include "tc_entropy.h"
#include "tc_fetch.h"
#include "tc_font.h"
#include "tc_log.h"
#include "tc_style.h"
#include "tc_tls.h"
#include "tw_log.h"
#include "view.h"

static char proxy_host[256];
static const char *cacert_file;
static const char *fonts_dir;
static int crash_test;

static void parse_command_line(char *cmdline, tw_shell_options *opts)
{
    char *token = strtok(cmdline, " \t");
    while (token) {
        if (strcmp(token, "--smoke-test") == 0) {
            opts->smoke_test = 1;
        } else if (strcmp(token, "--crash-test") == 0) {
            crash_test = 1;
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
        } else if (strcmp(token, "--fonts") == 0) {
            fonts_dir = strtok(NULL, " \t");
        } else if (strcmp(token, "--dither") == 0) {
            tw_view_force_dither(1);
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

/* Tachyon's optional font pack (decision 36), in FONTS beside the program.
   Added after Windows' own fonts, so it's the last resort. */
static size_t add_font_pack(tc_font_set *fonts)
{
    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH - 8);
    char *slash = n && n < MAX_PATH - 8 ? strrchr(dir, '\\') : NULL;
    if (!slash)
        return 0;
    strcpy(slash + 1, "FONTS");
    return tc_font_set_add_dir(fonts, dir);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show)
{
    tw_shell_options opts;
    HACCEL accel;
    HWND frame;
    MSG msg;
    size_t pack;

    (void)prev;
    memset(&opts, 0, sizeof opts);
    opts.show = show;
    opts.last_run = tw_log_start(cmdline, strstr(cmdline, "--crash-test") != NULL);
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
    tc_log("secure connections ready: %d trusted roots; randomness %d of 256 bits (%s)", tc_tls_trusted_count(),
           tc_entropy_strength(), tc_entropy_sources());
    /* Styles and fonts are ready before any page loads on another thread. */
    tc_style_init();
    opts.fonts = tc_font_set_new();
    opts.pool = tc_conn_pool_new();
    opts.cache = tc_cache_new(2u << 20);
    if (!opts.fonts || !opts.pool || !opts.cache) {
        MessageBoxA(NULL, "Tachyon doesn't have enough memory to start.", "Tachyon", MB_OK | MB_ICONSTOP);
        return 1;
    }
    if (fonts_dir)
        tc_font_set_add_dir(opts.fonts, fonts_dir);
    tc_font_set_add_system(opts.fonts);
    pack = add_font_pack(opts.fonts);
    tc_log("%u font faces, %u of them from the font pack", (unsigned)tc_font_set_count(opts.fonts), (unsigned)pack);
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
    if (crash_test)
        tw_log_crash_test();

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        stir_entropy(&msg);
        if (!TranslateAcceleratorA(frame, accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    tc_entropy_save_seed();
    tw_log_stop();
    return (int)msg.wParam;
}
