#include "shell.h"

#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loader.h"
#include "resource.h"
#include "tc_entropy.h"
#include "tc_fixup.h"
#include "tc_log.h"
#include "tc_url.h"
#include "tc_utf8.h"
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
    HWND address_label, address, go, view, status_bar;
    HFONT gui_font;
    HWND last_focus;
    int exit_code;

    char *history[HISTORY_MAX];
    int history_len;
    int history_pos; /* index of the current page, -1 when empty */

    tw_load *load;   /* the page being loaded, NULL when idle */
    tw_images *images; /* the shown page's pictures being loaded, or NULL */
    char done_status[400]; /* the page's "Done" line, shown again when its pictures are in */
    tw_view_mode mode; /* the page, its tree or its source */
    const char *proxy_host;
    uint16_t proxy_port;
    tw_last_run last_run;
    tc_font_set *fonts;
    tc_conn_pool *pool;
    tc_cache *cache;
    char status[400]; /* the status text, shown again when the mouse leaves a link */
    int hovering;
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
    snprintf(sh->status, sizeof sh->status, "%s", text);
    if (!sh->hovering)
        SendMessageA(sh->status_bar, SB_SETTEXTA, 0, (LPARAM)text);
}

/* Text from the ANSI world (address bar, command line) as UTF-8. */
static int ansi_to_utf8(const char *ansi, tc_buf *out)
{
    WCHAR wide[2048];
    int i, n = MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide, TC_ARRAY_LEN(wide));
    for (i = 0; i < n - 1; i++) {
        char enc[4];
        if (tc_buf_append(out, enc, tc_utf8_encode(wide[i], enc)))
            return TC_ERR_NOMEM;
    }
    return n > 0 ? TC_OK : TC_ERR_INVALID;
}

/* UTF-8 to the ANSI code page, for window titles. */
static void utf8_to_ansi(const char *utf8, size_t len, char *out, int size)
{
    WCHAR wide[512];
    size_t i = 0;
    int n = 0;
    while (i < len && n < (int)TC_ARRAY_LEN(wide) - 1) {
        uint32_t cp;
        i += tc_utf8_decode(utf8 + i, len - i, &cp);
        wide[n++] = (WCHAR)(cp > 0xFFFF ? '?' : cp);
    }
    n = WideCharToMultiByte(CP_ACP, 0, wide, n, out, size - 1, NULL, NULL);
    out[n > 0 ? n : 0] = '\0';
}

static void set_title(shell *sh, const char *title)
{
    char caption[300];
    snprintf(caption, sizeof caption, "%.200s - %s", title, TC_NAME);
    SetWindowTextA(sh->hwnd, caption);
}

static void update_nav_state(shell *sh)
{
    HMENU menu = GetMenu(sh->hwnd);
    int can_back = sh->history_pos > 0;
    int can_forward = sh->history_pos >= 0 && sh->history_pos < sh->history_len - 1;
    int loading = sh->load != NULL || sh->images != NULL;

    EnableWindow(sh->buttons[0], can_back);
    EnableWindow(sh->buttons[1], can_forward);
    EnableWindow(sh->buttons[2], loading);
    EnableMenuItem(menu, IDM_GO_BACK, MF_BYCOMMAND | (can_back ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, IDM_GO_FORWARD, MF_BYCOMMAND | (can_forward ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, IDM_VIEW_STOP, MF_BYCOMMAND | (loading ? MF_ENABLED : MF_GRAYED));
}

/* Abandon the current load, and the shown page's pictures. Their last
   messages still arrive and free them. */
static void cancel_load(shell *sh)
{
    if (sh->load) {
        tw_load_cancel(sh->load);
        sh->load = NULL;
    }
    if (sh->images) {
        tw_images_cancel(sh->images);
        sh->images = NULL;
    }
}

static void load_options(shell *sh, tw_load_options *opts)
{
    memset(opts, 0, sizeof *opts);
    opts->proxy_host = sh->proxy_host;
    opts->proxy_port = sh->proxy_port;
    opts->fonts = sh->fonts;
    opts->pool = sh->pool;
    opts->cache = sh->cache;
    tw_view_page_size(sh->view, &opts->width, &opts->height);
}

/* The shown page's pictures, now that the window has it. */
static void start_images(shell *sh)
{
    const tc_page *page = tw_view_page(sh->view);
    tw_load_options opts;
    int top, bottom;
    if (!page)
        return;
    load_options(sh, &opts);
    tw_view_visible(sh->view, &top, &bottom);
    sh->images = tw_images_start(sh->hwnd, page, top, bottom, &opts);
}

static void on_image(shell *sh, WPARAM slot, tw_images *job)
{
    unsigned done, total;
    if (job != sh->images) {
        tc_image_free(tw_images_take(job, slot));
        return;
    }
    tw_view_image_arrived(sh->view, tw_images_index(job, slot), tw_images_take(job, slot));
    tw_images_progress(job, &done, &total);
    if (done < total) {
        char text[100];
        snprintf(text, sizeof text, "Loading pictures: %u of %u", done, total);
        set_status(sh, text);
    }
}

static void on_images_done(shell *sh, tw_images *job)
{
    if (job == sh->images) {
        sh->images = NULL;
        set_status(sh, sh->done_status);
        update_nav_state(sh);
    }
    tw_images_free(job);
}

/* ---- Showing the current history entry ---- */

static void show_about_page(shell *sh, const char *url)
{
    if (strcmp(url, "about:tachyon") == 0 || strcmp(url, "about:home") == 0) {
        tw_view_show_home(sh->view);
        set_title(sh, "Welcome to Tachyon");
    } else if (strcmp(url, "about:blank") == 0) {
        tw_view_show_blank(sh->view);
        set_title(sh, url);
    } else if (strcmp(url, "about:fonts") == 0) {
        tw_view_show_fonts(sh->view);
        set_title(sh, "Fonts");
    } else {
        tw_view_show_message(sh->view, url, "Page not found", "Tachyon doesn't have a page with that name.",
                             "Try about:tachyon.");
        set_title(sh, url);
    }
    set_status(sh, "Done");
}

static void show_current(shell *sh, int reload)
{
    const char *url = sh->history[sh->history_pos];
    tw_load_options opts;

    cancel_load(sh);
    tc_log("open %.130s", url);
    SetWindowTextA(sh->address, url);
    if (strncmp(url, "about:", 6) == 0) {
        show_about_page(sh, url);
    } else {
        load_options(sh, &opts);
        opts.reload = reload;
        sh->load = tw_load_start(sh->hwnd, url, &opts);
        if (sh->load) {
            tw_view_show_message(sh->view, url, "Loading...", NULL, NULL);
            set_status(sh, "Starting...");
        } else {
            tw_view_show_message(sh->view, url, "Out of memory", "Tachyon couldn't start loading the page.", NULL);
        }
        set_title(sh, url);
    }
    update_nav_state(sh);
}

static void push_history(shell *sh, const char *url)
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
}

static void navigate(shell *sh, const char *url)
{
    push_history(sh, url);
    if (sh->history_pos >= 0 && strcmp(sh->history[sh->history_pos], url) == 0)
        show_current(sh, 0);
}

/* A followed link: another place in the same page just scrolls there. */
static void follow(shell *sh, const char *url_text)
{
    tc_url url, current;
    int same = 0;

    if (tc_url_parse(url_text, NULL, &url) != TC_OK)
        return;
    if (strcmp(url.scheme, "http") != 0 && strcmp(url.scheme, "https") != 0 && strcmp(url.scheme, "about") != 0) {
        char text[300];
        snprintf(text, sizeof text, "Tachyon can't open %s: links yet.", url.scheme);
        MessageBeep(MB_ICONEXCLAMATION);
        set_status(sh, text);
        tc_url_free(&url);
        return;
    }
    if (url.fragment && tc_url_parse(tw_view_url(sh->view), NULL, &current) == TC_OK) {
        size_t a = strcspn(url.href, "#"), b = strcspn(current.href, "#");
        same = a == b && strncmp(url.href, current.href, a) == 0;
        tc_url_free(&current);
    }
    if (same && !sh->load) {
        push_history(sh, url.href);
        SetWindowTextA(sh->address, url.href);
        if (!tw_view_scroll_to_fragment(sh->view, url.fragment))
            set_status(sh, "That part of the page isn't there.");
        update_nav_state(sh);
    } else {
        navigate(sh, url.href);
    }
    tc_url_free(&url);
}

/* Turn typed (ANSI) text into a URL and go there. 0 if it isn't one. */
static int navigate_typed(shell *sh, const char *typed)
{
    tc_buf utf8, fixed;
    tc_url url;
    int ok = 0;

    tc_buf_init(&utf8);
    tc_buf_init(&fixed);
    if (ansi_to_utf8(typed, &utf8) == TC_OK && tc_fixup_address(tc_buf_cstr(&utf8), &fixed) == TC_OK &&
        tc_url_parse(tc_buf_cstr(&fixed), NULL, &url) == TC_OK) {
        navigate(sh, url.href);
        tc_url_free(&url);
        ok = 1;
    }
    tc_buf_free(&utf8);
    tc_buf_free(&fixed);
    return ok;
}

static void go_to_address(shell *sh)
{
    char typed[2048];

    GetWindowTextA(sh->address, typed, sizeof typed);
    if (navigate_typed(sh, typed)) {
        SetFocus(sh->view);
    } else {
        MessageBeep(MB_ICONEXCLAMATION);
        set_status(sh, "That isn't a web address. Try one like example.com. (Searching comes in a later sprint.)");
    }
}

/* ---- Load progress and results ---- */

static void on_progress(shell *sh, tw_load *load)
{
    char text[400];

    if (load != sh->load)
        return; /* an abandoned load */
    switch (load->stage) {
    case TW_STAGE_READING:
        set_status(sh, "Reading the page...");
        return;
    case TW_STAGE_STYLES:
        snprintf(text, sizeof text, "Loading style sheet %u: %s", load->sheet, load->sheet_url);
        set_status(sh, text);
        return;
    case TW_STAGE_LAYOUT:
        set_status(sh, "Laying out the page...");
        return;
    default:
        break;
    }
    switch (load->phase) {
    case TC_FETCH_RESOLVING:
        snprintf(text, sizeof text, "Looking up %s...", load->host);
        break;
    case TC_FETCH_CONNECTING:
        snprintf(text, sizeof text, "Connecting to %s...", load->host);
        break;
    case TC_FETCH_SECURING:
        snprintf(text, sizeof text, "Making a secure connection to %s...", load->host);
        break;
    case TC_FETCH_WAITING:
        snprintf(text, sizeof text, "Waiting for %s...", load->host);
        break;
    case TC_FETCH_REDIRECTING:
        snprintf(text, sizeof text, "Redirecting to %s...", load->host);
        break;
    case TC_FETCH_RECEIVING:
        if (load->total > 0)
            snprintf(text, sizeof text, "Receiving from %s: %u KB of %u KB", load->host,
                     (unsigned)(load->received / 1024), (unsigned)(load->total / 1024));
        else
            snprintf(text, sizeof text, "Receiving from %s: %u KB", load->host, (unsigned)(load->received / 1024));
        break;
    default:
        return;
    }
    set_status(sh, text);
}

/* The document's title for the window caption, or NULL if it has none. */
static const char *document_title(const tc_document *doc, char *out, int size)
{
    tc_buf title;
    const char *result = NULL;

    tc_buf_init(&title);
    if (tc_document_title(doc, &title) == TC_OK && title.len) {
        utf8_to_ansi(title.data, title.len, out, size);
        result = out[0] ? out : NULL;
    }
    tc_buf_free(&title);
    return result;
}

static void show_error(shell *sh, tw_load *load)
{
    const tc_response *r = &load->response;
    const char *heading, *text;
    char detail[512];

    snprintf(detail, sizeof detail, "%s", r->error_detail[0] ? r->error_detail : tc_err_text(load->result));
    switch (load->result) {
    case TC_ERR_DNS:
        heading = "Server not found";
        text = "Check the address, and that this computer is connected to the internet. Windows 95 needs "
               "TCP/IP and a network card or Dial-Up Networking set up.";
        break;
    case TC_ERR_CONNECT:
        heading = "Couldn't connect";
        text = "The server didn't accept the connection. It may be down, or a firewall may be in the way.";
        break;
    case TC_ERR_TIMEOUT:
        heading = "The connection timed out";
        text = "The server took too long to respond. Try again later.";
        break;
    case TC_ERR_CERT: {
        SYSTEMTIME now;
        heading = "This connection isn't secure";
        text = "Tachyon couldn't confirm that this is really the site you asked for, so it won't show the page.";
        if (r->tls.cert_flags & 0x0201) { /* expired or not yet valid: the clock may be wrong */
            static const char *months[] = { "January", "February", "March", "April", "May", "June", "July",
                                            "August", "September", "October", "November", "December" };
            size_t n = strlen(detail);
            GetLocalTime(&now);
            snprintf(detail + n, sizeof detail - n, " Your computer's clock says %d %s %d.", now.wDay,
                     months[(now.wMonth - 1) % 12], now.wYear);
        }
        break;
    }
    case TC_ERR_TLS:
        heading = "Secure connection failed";
        text = "Tachyon and the server couldn't agree on a secure connection. The server may need an "
               "encryption method Tachyon doesn't have yet.";
        break;
    case TC_ERR_CANCELLED:
        heading = "Stopped";
        text = "Loading was stopped. Press Reload to try again.";
        detail[0] = '\0';
        break;
    case TC_ERR_UNSUPPORTED:
        heading = "Can't open this address";
        text = NULL;
        break;
    case TC_ERR_PROXY:
        heading = "Proxy error";
        text = "The proxy server Tachyon was told to use didn't connect it to the site.";
        break;
    default:
        heading = "The page couldn't be loaded";
        text = NULL;
        break;
    }
    tc_log("failed: %s (%s); %.90s", heading, tc_err_text(load->result), detail);
    tw_view_show_message(sh->view, r->url ? r->url : load->url, heading, text, detail[0] ? detail : NULL);
    set_title(sh, heading);
    set_status(sh, heading);
}

static void show_response(shell *sh, tw_load *load)
{
    const tc_response *r = &load->response;
    char title[300], status[300];
    MEMORYSTATUS mem;
    tc_buf head;
    size_t i;
    int n;

    /* A redirect lands somewhere else: that is the address now. */
    if (r->url && strcmp(r->url, sh->history[sh->history_pos]) != 0) {
        char *final_url = copy_string(r->url);
        if (final_url) {
            free(sh->history[sh->history_pos]);
            sh->history[sh->history_pos] = final_url;
        }
    }
    SetWindowTextA(sh->address, sh->history[sh->history_pos]);

    n = snprintf(status, sizeof status, "Done: %u KB in %u.%u s%s", (unsigned)(load->body_len / 1024),
                 (unsigned)(r->elapsed_ms / 1000), (unsigned)(r->elapsed_ms % 1000 / 100),
                 r->cached ? " (from memory)" : "");
    if (load->has_page) {
        uint32_t laid = load->parse_ms + load->styles_ms + load->page.style_ms + load->page.layout_ms;
        n += snprintf(status + n, sizeof status - (size_t)n, "; %u style sheet%s; shown in %u.%u s",
                      load->page.sheets_loaded, load->page.sheets_loaded == 1 ? "" : "s", (unsigned)(laid / 1000),
                      (unsigned)(laid % 1000 / 100));
    }
    tc_buf_init(&head);
    tc_buf_printf(&head, "HTTP/1.%d %d %s\n", r->minor_version, r->status, r->status_text ? r->status_text : "");
    for (i = 0; i < r->headers.len; i++)
        tc_buf_printf(&head, "%s: %s\n", r->headers.items[i].name, r->headers.items[i].value);
    tc_buf_append_char(&head, '\n');
    if (load->has_page) {
        const char *doc_title = document_title(load->page.html.doc, title, sizeof title);
        set_title(sh, doc_title ? doc_title : sh->history[sh->history_pos]);
        tw_view_set_mode(sh->view, sh->mode);
        tw_view_show_page(sh->view, tc_buf_cstr(&head), head.len, &load->page);
        if (r->url && strchr(r->url, '#'))
            tw_view_scroll_to_fragment(sh->view, strchr(r->url, '#') + 1);
    } else {
        set_title(sh, sh->history[sh->history_pos]);
        tw_view_show_source(sh->view, sh->history[sh->history_pos], tc_buf_cstr(&head), head.len,
                            tc_buf_cstr(&r->body), r->body.len);
    }
    tc_buf_free(&head);

    if (r->tls.version[0])
        snprintf(status + n, sizeof status - (size_t)n, ". Secure: %s, %s", r->tls.version, r->tls.cipher);
    else
        snprintf(status + n, sizeof status - (size_t)n, ". Not secure (plain HTTP)");
    set_status(sh, status);
    snprintf(sh->done_status, sizeof sh->done_status, "%s", status);
    if (load->has_page)
        start_images(sh);
    mem.dwLength = sizeof mem;
    GlobalMemoryStatus(&mem);
    tc_log("HTTP %d. %s; %lu MB RAM free", r->status, status, (unsigned long)(mem.dwAvailPhys >> 20));
}

static void on_done(shell *sh, tw_load *load)
{
    if (load == sh->load) {
        sh->load = NULL;
        if (load->result == TC_OK)
            show_response(sh, load);
        else
            show_error(sh, load);
        update_nav_state(sh);
    }
    tw_load_free(load);
}

/* ---- Commands ---- */

static void show_about(shell *sh)
{
    MSGBOXPARAMSA mb;
    char text[1024];

    snprintf(text, sizeof text,
             "%s %s\r\nSprint %d: %s\r\n\r\n"
             "A modern web browser for Windows 95.\r\n\r\n"
             "Secure connections: Mbed TLS 4.1, %d trusted root certificates.\r\n"
             "Randomness: %d of 256 bits (%s).\r\n\r\n"
             "%s\r\nReleased under the GNU General Public License v3.",
             TC_NAME, TC_VERSION, TC_SPRINT, TC_SPRINT_NAME, tc_tls_trusted_count(), tc_entropy_strength(),
             tc_entropy_sources(), TC_HOMEPAGE);
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

/* Pages show themselves, their document tree or their source. */
static void set_mode(shell *sh, tw_view_mode mode)
{
    static const char *const names[] = { "page", "document tree", "page source" };
    static const int items[] = { IDM_VIEW_PAGE, IDM_VIEW_TREE, IDM_VIEW_SOURCE };
    char text[100];
    sh->mode = mode;
    tc_log("view: %s", names[mode]);
    CheckMenuRadioItem(GetMenu(sh->hwnd), IDM_VIEW_PAGE, IDM_VIEW_SOURCE, items[mode], MF_BYCOMMAND);
    if (!tw_view_set_mode(sh->view, mode)) {
        snprintf(text, sizeof text, "Pages will show their %s.", mode == TW_MODE_PAGE ? "contents" : names[mode]);
        set_status(sh, text);
    }
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
        if (sh->load) {
            /* The load reports back as cancelled and shows the Stopped page. */
            tw_load_cancel(sh->load);
            set_status(sh, "Stopping...");
        } else if (sh->images) {
            tw_images_cancel(sh->images); /* its last message restores the status */
        }
        break;
    case IDM_VIEW_RELOAD:
        if (sh->history_pos >= 0)
            show_current(sh, 1);
        break;
    case IDM_VIEW_PAGE:
        set_mode(sh, TW_MODE_PAGE);
        break;
    case IDM_VIEW_TREE:
        set_mode(sh, TW_MODE_TREE);
        break;
    case IDM_VIEW_SOURCE:
        /* The menu item picks the source; Ctrl+U goes back and forth. */
        set_mode(sh, GetKeyState(VK_CONTROL) < 0 && sh->mode == TW_MODE_SOURCE ? TW_MODE_PAGE : TW_MODE_SOURCE);
        break;
    case IDM_GO_BACK:
        if (sh->history_pos > 0) {
            sh->history_pos--;
            show_current(sh, 0);
        }
        break;
    case IDM_GO_FORWARD:
        if (sh->history_pos < sh->history_len - 1) {
            sh->history_pos++;
            show_current(sh, 0);
        }
        break;
    case IDM_GO_HOME:
        navigate(sh, TW_HOME_URL);
        break;
    case IDM_GO_ADDRESS:
        go_to_address(sh);
        break;
    case IDM_HELP_LOG:
        tw_log_view(sh->hwnd);
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
    sh->view = tw_view_create(sh->hwnd, sh->instance, IDC_VIEW, sh->fonts);
    sh->status_bar = CreateStatusWindowA(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, "Ready", sh->hwnd, IDC_STATUS);
    if (!sh->address_label || !sh->address || !sh->go || !sh->view || !sh->status_bar)
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

    SendMessageA(sh->status_bar, WM_SIZE, 0, 0);
    GetWindowRect(sh->status_bar, &status);
    status_h = status.bottom - status.top;
    MoveWindow(sh->view, 0, view_top, width, height - view_top - status_h, TRUE);
}

/* The home page's warnings: a clock set before this build (certificates
   can't be checked) and a crash last time. */
static void show_warnings(shell *sh)
{
    char warning[500];
    size_t n = 0;

    warning[0] = '\0';
    if (tc_time_unix() < TC_BUILD_UNIX - 86400) {
        SYSTEMTIME now;
        static const char *months[] = { "January", "February", "March", "April", "May", "June", "July",
                                        "August", "September", "October", "November", "December" };
        GetLocalTime(&now);
        n += (size_t)snprintf(warning, sizeof warning,
                              "Your computer's clock says %d %s %d. Secure (https) websites won't open until the "
                              "date is right: double-click the clock on the taskbar to set it. ",
                              now.wDay, months[(now.wMonth - 1) % 12], now.wYear);
        tc_log("the clock is before this build");
    }
    if (n < sizeof warning && (sh->last_run == TW_LAST_RUN_CRASHED || sh->last_run == TW_LAST_RUN_UNCLEAN))
        snprintf(warning + n, sizeof warning - n, "%s Help > View Log shows what it was doing: please send us "
                 "TACHYON.LOG, or a photo of the end of it.",
                 sh->last_run == TW_LAST_RUN_CRASHED ? "Tachyon crashed the last time it ran."
                                                     : "Tachyon didn't close properly the last time it ran.");
    if (warning[0])
        tw_view_set_warning(sh->view, warning);
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
    if (!sh) {
        /* A load that outlived the window still has to be freed. */
        if (msg == WM_TW_DONE)
            tw_load_free((tw_load *)lp);
        else if (msg == WM_TW_IMAGE)
            tc_image_free(tw_images_take((tw_images *)lp, wp));
        else if (msg == WM_TW_IMAGES_DONE)
            tw_images_free((tw_images *)lp);
        return DefWindowProcA(hwnd, msg, wp, lp);
    }

    switch (msg) {
    case WM_TW_PROGRESS:
        on_progress(sh, (tw_load *)lp);
        return 0;
    case WM_TW_DONE:
        on_done(sh, (tw_load *)lp);
        return 0;
    case WM_TW_IMAGE:
        on_image(sh, wp, (tw_images *)lp);
        return 0;
    case WM_TW_IMAGES_DONE:
        on_images_done(sh, (tw_images *)lp);
        return 0;
    case WM_TW_HOVER:
        /* A link's address in the status bar while the mouse is on it. */
        if (lp) {
            char ansi[600];
            utf8_to_ansi((const char *)lp, strlen((const char *)lp), ansi, sizeof ansi);
            sh->hovering = 1;
            SendMessageA(sh->status_bar, SB_SETTEXTA, 0, (LPARAM)ansi);
        } else {
            sh->hovering = 0;
            SendMessageA(sh->status_bar, SB_SETTEXTA, 0, (LPARAM)sh->status);
        }
        return 0;
    case WM_TW_FOLLOW:
        sh->hovering = 0;
        follow(sh, (const char *)lp);
        return 0;
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
    case WM_QUERYNEWPALETTE:
        return tw_view_realize_palette(sh->view);
    case WM_PALETTECHANGED:
        /* Another program's colours: the view maps its own again. */
        if ((HWND)wp != hwnd && !IsChild(hwnd, (HWND)wp))
            tw_view_realize_palette(sh->view);
        return 0;
    case WM_DISPLAYCHANGE:
        tw_view_display_changed(sh->view);
        return 0;
    case WM_TIMER:
        if (wp == SMOKE_TIMER) {
            KillTimer(hwnd, SMOKE_TIMER);
            sh->exit_code = tw_view_paint_count(sh->view) > 0 ? 0 : 2;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_ENDSESSION:
        /* Windows is shutting down and won't return to the message loop. */
        if (wp)
            tw_log_stop();
        return 0;
    case WM_DESTROY:
        cancel_load(sh);
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

HWND tw_shell_create(HINSTANCE instance, const tw_shell_options *opts)
{
    shell *sh = calloc(1, sizeof *sh);
    HWND hwnd;

    if (!sh)
        return NULL;
    sh->instance = instance;
    sh->history_pos = -1;
    sh->proxy_host = opts->proxy_host;
    sh->proxy_port = opts->proxy_port;
    sh->last_run = opts->last_run;
    sh->fonts = opts->fonts;
    sh->pool = opts->pool;
    sh->cache = opts->cache;
    hwnd = CreateWindowExA(0, TW_SHELL_CLASS, TC_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, instance, sh);
    if (!hwnd) {
        free(sh);
        return NULL;
    }

    show_warnings(sh);
    CheckMenuRadioItem(GetMenu(hwnd), IDM_VIEW_PAGE, IDM_VIEW_SOURCE, IDM_VIEW_PAGE, MF_BYCOMMAND);
    if (!opts->start_url || !navigate_typed(sh, opts->start_url))
        navigate(sh, TW_HOME_URL);

    ShowWindow(hwnd, opts->show);
    UpdateWindow(hwnd);
    SetFocus(sh->address);
    SendMessageA(sh->address, EM_SETSEL, 0, -1);
    if (opts->smoke_test)
        SetTimer(hwnd, SMOKE_TIMER, SMOKE_MS, NULL);
    return hwnd;
}
