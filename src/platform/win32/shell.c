#include "shell.h"

#include <commctrl.h>
#include <stdlib.h>
#include <string.h>

#include "resource.h"
#include "tc_fixup.h"
#include "tc_version.h"
#include "view.h"

#define HISTORY_MAX  64
#define SMOKE_TIMER  1
#define SMOKE_MS     1500

#define BUTTON_W     64
#define BUTTON_H     24
#define GAP          4
#define ADDRESS_H    22
#define LABEL_W      52
#define GO_W         40

typedef struct shell {
    HWND hwnd;
    HINSTANCE instance;
    HWND buttons[5];
    HWND address_label, address, go, view, status;
    HFONT gui_font;
    HWND last_focus;
    int exit_code;

    char *history[HISTORY_MAX];
    int history_len;
    int history_pos; /* index of the current page, -1 when empty */
} shell;

static const struct {
    const char *label;
    int command;
} toolbar[] = {
    { "Back", IDM_GO_BACK },
    { "Forward", IDM_GO_FORWARD },
    { "Stop", IDM_VIEW_STOP },
    { "Reload", IDM_VIEW_RELOAD },
    { "Home", IDM_GO_HOME },
};

/* The Edit class's own window procedure, which the address bar subclasses. */
static WNDPROC edit_proc;

static shell *get_shell(HWND hwnd)
{
    return (shell *)GetWindowLongA(hwnd, GWL_USERDATA);
}

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static void set_status(shell *sh, const char *text)
{
    SendMessageA(sh->status, SB_SETTEXTA, 0, (LPARAM)text);
}

static void update_nav_state(shell *sh)
{
    HMENU menu = GetMenu(sh->hwnd);
    int can_back = sh->history_pos > 0;
    int can_forward = sh->history_pos >= 0 && sh->history_pos < sh->history_len - 1;

    EnableWindow(sh->buttons[0], can_back);
    EnableWindow(sh->buttons[1], can_forward);
    EnableWindow(sh->buttons[2], FALSE); /* nothing loads in the background yet */
    EnableMenuItem(menu, IDM_GO_BACK, MF_BYCOMMAND | (can_back ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, IDM_GO_FORWARD, MF_BYCOMMAND | (can_forward ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, IDM_VIEW_STOP, MF_BYCOMMAND | MF_GRAYED);
}

/* Show the current history entry. */
static void show_current(shell *sh)
{
    const char *url = sh->history[sh->history_pos];
    const char *title = tw_view_show(sh->view, url);
    char caption[300];

    SetWindowTextA(sh->address, url);
    wsprintfA(caption, "%.200s - %s", title, TC_NAME);
    SetWindowTextA(sh->hwnd, caption);
    set_status(sh, "Done");
    update_nav_state(sh);
}

static void navigate(shell *sh, const char *url)
{
    char *copy = copy_string(url);
    int i;

    if (!copy)
        return;
    /* Going somewhere new drops the forward history. */
    for (i = sh->history_pos + 1; i < sh->history_len; i++)
        free(sh->history[i]);
    sh->history_len = sh->history_pos + 1;
    if (sh->history_len == HISTORY_MAX) {
        free(sh->history[0]);
        memmove(sh->history, sh->history + 1, (HISTORY_MAX - 1) * sizeof sh->history[0]);
        sh->history_len--;
    }
    sh->history[sh->history_len++] = copy;
    sh->history_pos = sh->history_len - 1;
    show_current(sh);
}

static void go_to_address(shell *sh)
{
    char typed[2048];
    tc_buf url;

    GetWindowTextA(sh->address, typed, sizeof typed);
    tc_buf_init(&url);
    if (tc_fixup_address(typed, &url) == TC_OK) {
        navigate(sh, tc_buf_cstr(&url));
        SetFocus(sh->view);
    } else {
        MessageBeep(MB_ICONEXCLAMATION);
        set_status(sh, "Type a web address, such as example.com. Searching comes in a later sprint.");
    }
    tc_buf_free(&url);
}

static void show_about(shell *sh)
{
    MSGBOXPARAMSA mb;
    char text[512];

    wsprintfA(text,
              "%s %s\r\nSprint %d: %s\r\n\r\n"
              "A modern web browser for Windows 95.\r\n\r\n"
              "%s\r\nReleased under the GNU General Public License v3.",
              TC_NAME, TC_VERSION, TC_SPRINT, TC_SPRINT_NAME, TC_HOMEPAGE);
    memset(&mb, 0, sizeof mb);
    mb.cbSize = sizeof mb;
    mb.hwndOwner = sh->hwnd;
    mb.hInstance = sh->instance;
    mb.lpszText = text;
    mb.lpszCaption = "About Tachyon";
    mb.dwStyle = MB_OK | MB_USERICON;
    mb.lpszIcon = MAKEINTRESOURCEA(IDI_TACHYON);
    MessageBoxIndirectA(&mb);
}

/* Edit commands go to whichever edit control has the focus. */
static void edit_command(UINT message, WPARAM wp, LPARAM lp)
{
    HWND focus = GetFocus();
    char cls[16];
    if (focus && GetClassNameA(focus, cls, sizeof cls) && lstrcmpiA(cls, "Edit") == 0)
        SendMessageA(focus, message, wp, lp);
}

static void on_command(shell *sh, int id)
{
    switch (id) {
    case IDM_FILE_OPEN:
        SetFocus(sh->address);
        SendMessageA(sh->address, EM_SETSEL, 0, -1);
        break;
    case IDM_FILE_EXIT:
        DestroyWindow(sh->hwnd);
        break;
    case IDM_EDIT_CUT:
        edit_command(WM_CUT, 0, 0);
        break;
    case IDM_EDIT_COPY:
        edit_command(WM_COPY, 0, 0);
        break;
    case IDM_EDIT_PASTE:
        edit_command(WM_PASTE, 0, 0);
        break;
    case IDM_EDIT_SELECTALL:
        edit_command(EM_SETSEL, 0, -1);
        break;
    case IDM_VIEW_STOP:
        break;
    case IDM_VIEW_RELOAD:
        if (sh->history_pos >= 0)
            show_current(sh);
        break;
    case IDM_GO_BACK:
        if (sh->history_pos > 0) {
            sh->history_pos--;
            show_current(sh);
        }
        break;
    case IDM_GO_FORWARD:
        if (sh->history_pos < sh->history_len - 1) {
            sh->history_pos++;
            show_current(sh);
        }
        break;
    case IDM_GO_HOME:
        navigate(sh, TW_HOME_URL);
        break;
    case IDM_GO_ADDRESS:
        go_to_address(sh);
        break;
    case IDM_HELP_ABOUT:
        show_about(sh);
        break;
    }
}

/* The address bar loads the page on Enter. */
static LRESULT CALLBACK address_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageA(GetParent(hwnd), WM_COMMAND, IDM_GO_ADDRESS, 0);
        return 0;
    }
    if (msg == WM_CHAR && (wp == '\r' || wp == '\n'))
        return 0; /* no beep */
    return CallWindowProcA(edit_proc, hwnd, msg, wp, lp);
}

static HWND child(shell *sh, DWORD ex_style, const char *cls, const char *text, DWORD style, int id)
{
    HWND hwnd = CreateWindowExA(ex_style, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                                sh->hwnd, (HMENU)(INT_PTR)id, sh->instance, NULL);
    if (hwnd)
        SendMessageA(hwnd, WM_SETFONT, (WPARAM)sh->gui_font, FALSE);
    return hwnd;
}

static int create_children(shell *sh)
{
    size_t i;

    sh->gui_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for (i = 0; i < TC_ARRAY_LEN(toolbar); i++) {
        sh->buttons[i] = child(sh, 0, "BUTTON", toolbar[i].label, BS_PUSHBUTTON, toolbar[i].command);
        if (!sh->buttons[i])
            return 0;
    }
    sh->address_label = child(sh, 0, "STATIC", "Address:", SS_LEFT, IDC_ADDRESS_LABEL);
    sh->address = child(sh, WS_EX_CLIENTEDGE, "EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, IDC_ADDRESS);
    sh->go = child(sh, 0, "BUTTON", "Go", BS_PUSHBUTTON, IDM_GO_ADDRESS);
    sh->view = tw_view_create(sh->hwnd, sh->instance, IDC_VIEW);
    sh->status = CreateStatusWindowA(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, "Ready", sh->hwnd, IDC_STATUS);
    if (!sh->address_label || !sh->address || !sh->go || !sh->view || !sh->status)
        return 0;
    edit_proc = (WNDPROC)SetWindowLongA(sh->address, GWL_WNDPROC, (LONG)address_proc);
    return 1;
}

static void layout(shell *sh, int width, int height)
{
    RECT status;
    int i, y = GAP, view_top, status_h;

    for (i = 0; i < (int)TC_ARRAY_LEN(toolbar); i++)
        MoveWindow(sh->buttons[i], GAP + i * (BUTTON_W + GAP), y, BUTTON_W, BUTTON_H, TRUE);
    y += BUTTON_H + GAP;
    MoveWindow(sh->address_label, GAP + 2, y + 4, LABEL_W, ADDRESS_H - 4, TRUE);
    MoveWindow(sh->address, GAP + LABEL_W, y, width - LABEL_W - GO_W - 3 * GAP, ADDRESS_H, TRUE);
    MoveWindow(sh->go, width - GO_W - GAP, y, GO_W, ADDRESS_H, TRUE);
    view_top = y + ADDRESS_H + GAP;

    SendMessageA(sh->status, WM_SIZE, 0, 0);
    GetWindowRect(sh->status, &status);
    status_h = status.bottom - status.top;
    MoveWindow(sh->view, 0, view_top, width, height - view_top - status_h, TRUE);
}

static LRESULT CALLBACK shell_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    shell *sh = get_shell(hwnd);

    if (msg == WM_CREATE) {
        const CREATESTRUCTA *cs = (const CREATESTRUCTA *)lp;
        sh = (shell *)cs->lpCreateParams;
        sh->hwnd = hwnd;
        SetWindowLongA(hwnd, GWL_USERDATA, (LONG)sh);
        if (!create_children(sh)) {
            SetWindowLongA(hwnd, GWL_USERDATA, 0); /* tw_shell_create frees sh */
            return -1;
        }
        return 0;
    }
    if (!sh)
        return DefWindowProcA(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_SIZE:
        layout(sh, LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = 400;
        mm->ptMinTrackSize.y = 240;
        return 0;
    }
    case WM_ACTIVATE:
        /* Give the focus back to the child that had it. */
        if (LOWORD(wp) == WA_INACTIVE)
            sh->last_focus = GetFocus();
        else
            SetFocus(sh->last_focus && IsChild(hwnd, sh->last_focus) ? sh->last_focus : sh->address);
        return 0;
    case WM_COMMAND:
        on_command(sh, LOWORD(wp));
        return 0;
    case WM_TIMER:
        if (wp == SMOKE_TIMER) {
            KillTimer(hwnd, SMOKE_TIMER);
            sh->exit_code = tw_view_paint_count(sh->view) > 0 ? 0 : 2;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(sh->exit_code);
        return 0;
    case WM_NCDESTROY: {
        /* Sent after the children are gone, so nothing can still use sh. */
        int i;
        for (i = 0; i < sh->history_len; i++)
            free(sh->history[i]);
        free(sh);
        SetWindowLongA(hwnd, GWL_USERDATA, 0);
        break;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int tw_shell_register(HINSTANCE instance)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = shell_proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(IDI_TACHYON));
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEA(IDR_MAINMENU);
    wc.lpszClassName = TW_SHELL_CLASS;
    return RegisterClassA(&wc) != 0;
}

HWND tw_shell_create(HINSTANCE instance, const char *start_url, int show, int smoke_test)
{
    shell *sh = calloc(1, sizeof *sh);
    HWND hwnd;
    tc_buf url;

    if (!sh)
        return NULL;
    sh->instance = instance;
    sh->history_pos = -1;
    hwnd = CreateWindowExA(0, TW_SHELL_CLASS, TC_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, instance, sh);
    if (!hwnd) {
        free(sh);
        return NULL;
    }

    tc_buf_init(&url);
    if (!start_url || tc_fixup_address(start_url, &url) != TC_OK)
        tc_buf_append_str(&url, TW_HOME_URL);
    navigate(sh, tc_buf_cstr(&url));
    tc_buf_free(&url);

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetFocus(sh->address);
    SendMessageA(sh->address, EM_SETSEL, 0, -1);
    if (smoke_test)
        SetTimer(hwnd, SMOKE_TIMER, SMOKE_MS, NULL);
    return hwnd;
}
