#include "view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_surface.h"
#include "tc_version.h"

enum page_kind { PAGE_HOME, PAGE_BLANK, PAGE_NOT_YET };

typedef struct view {
    HWND hwnd;
    enum page_kind page;
    char url[2048];
    char title[256];

    /* Back buffer: a 32 bpp top-down DIB section selected into `dc`. */
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    uint32_t *bits;
    int width, height;

    HFONT font_logo, font_tagline, font_heading, font_body, font_small;
    int paint_count;
} view;

#define BANNER_TOP     TC_RGB(0, 0, 192)
#define BANNER_BOTTOM  TC_RGB(0, 0, 40)
#define PAGE_MARGIN    24

static view *get_view(HWND hwnd)
{
    return (view *)GetWindowLongA(hwnd, GWL_USERDATA);
}

static HFONT make_font(const char *face, int px, int weight, int italic)
{
    return CreateFontA(-px, 0, 0, 0, weight, (DWORD)italic, FALSE, FALSE, ANSI_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

static void free_backbuffer(view *v)
{
    if (v->dc) {
        SelectObject(v->dc, v->old_bitmap);
        DeleteObject(v->bitmap);
        DeleteDC(v->dc);
    }
    v->dc = NULL;
    v->bitmap = v->old_bitmap = NULL;
    v->bits = NULL;
    v->width = v->height = 0;
}

static int ensure_backbuffer(view *v, int width, int height)
{
    BITMAPINFO bmi;
    HDC screen;
    void *bits = NULL;

    if (v->dc && v->width == width && v->height == height)
        return 1;
    free_backbuffer(v);
    if (width <= 0 || height <= 0)
        return 0;

    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; /* top-down rows, like tc_surface */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(v->hwnd);
    v->dc = CreateCompatibleDC(screen);
    v->bitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(v->hwnd, screen);
    if (!v->dc || !v->bitmap || !bits) {
        if (v->bitmap)
            DeleteObject(v->bitmap);
        if (v->dc)
            DeleteDC(v->dc);
        v->dc = NULL;
        v->bitmap = NULL;
        return 0;
    }
    v->old_bitmap = SelectObject(v->dc, v->bitmap);
    v->bits = bits;
    v->width = width;
    v->height = height;
    return 1;
}

/* Draw wrapped text in `r` and move r->top below it. */
static void text_block(view *v, HFONT font, COLORREF color, RECT *r, const char *text)
{
    RECT measure = *r;
    SelectObject(v->dc, font);
    SetTextColor(v->dc, color);
    DrawTextA(v->dc, text, -1, &measure, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    DrawTextA(v->dc, text, -1, r, DT_WORDBREAK | DT_NOPREFIX);
    r->top += measure.bottom - measure.top;
}

static void text_at(view *v, HFONT font, COLORREF color, int x, int y, const char *text)
{
    SelectObject(v->dc, font);
    SetTextColor(v->dc, color);
    TextOutA(v->dc, x, y, text, (int)strlen(text));
}

static void text_shadowed(view *v, HFONT font, int x, int y, const char *text)
{
    text_at(v, font, RGB(0, 0, 0), x + 3, y + 3, text);
    text_at(v, font, RGB(255, 255, 255), x, y, text);
}

static void paint_home(view *v, tc_surface *s)
{
    char footer[256];
    RECT r;
    int banner = 120;

    tc_surface_gradient_v(s, tc_rect_make(0, 0, s->width, banner), BANNER_TOP, BANNER_BOTTOM);
    text_shadowed(v, v->font_logo, PAGE_MARGIN, 10, TC_NAME);
    text_at(v, v->font_tagline, RGB(255, 255, 160), PAGE_MARGIN + 4, 82, "The modern web, sent back to 1995.");

    SetRect(&r, PAGE_MARGIN, banner + 20, v->width - PAGE_MARGIN, v->height);
    text_block(v, v->font_heading, RGB(0, 0, 128), &r, "Sprint 0: Foundations");
    r.top += 6;
    text_block(v, v->font_body, RGB(0, 0, 0), &r,
               "This is Tachyon's first build. The window, menus and this page work, "
               "but nothing is loaded from the network yet.");
    r.top += 14;
    text_block(v, v->font_heading, RGB(0, 0, 128), &r, "Coming next");
    r.top += 6;
    text_block(v, v->font_body, RGB(0, 0, 0), &r,
               "\x95  Sprint 1: HTTP and TLS 1.3, so Tachyon can fetch https:// pages\r\n"
               "\x95  Sprint 2: an HTML5 parser and DOM\r\n"
               "\x95  Sprint 3: First Light, with CSS, layout and text: Wikipedia on Windows 95");
    r.top += 18;
    snprintf(footer, sizeof footer, "%s %s  \x95  GNU GPL v3  \x95  %s", TC_NAME, TC_VERSION, TC_HOMEPAGE);
    text_block(v, v->font_small, RGB(128, 128, 128), &r, footer);
}

static void paint_not_yet(view *v, tc_surface *s)
{
    RECT r;
    int banner = 56;

    tc_surface_gradient_v(s, tc_rect_make(0, 0, s->width, banner), BANNER_TOP, BANNER_BOTTOM);
    text_shadowed(v, v->font_heading, PAGE_MARGIN, 18, "Tachyon can't show this page yet");

    SetRect(&r, PAGE_MARGIN, banner + 20, v->width - PAGE_MARGIN, v->height);
    text_block(v, v->font_heading, RGB(0, 0, 128), &r, v->url);
    r.top += 8;
    text_block(v, v->font_body, RGB(0, 0, 0), &r,
               "Tachyon can't connect to the internet yet. HTTP and TLS arrive in Sprint 1.\r\n\r\n"
               "Use Go > Home to return to the start page.");
}

static void render(view *v)
{
    tc_surface s;

    if (!v->dc)
        return;
    GdiFlush(); /* finish GDI drawing before touching the bits directly */
    tc_surface_wrap(&s, v->bits, v->width, v->height, v->width);
    tc_surface_fill(&s, tc_rect_make(0, 0, v->width, v->height), TC_RGB(255, 255, 255));
    SetBkMode(v->dc, TRANSPARENT);
    switch (v->page) {
    case PAGE_HOME:
        paint_home(v, &s);
        break;
    case PAGE_NOT_YET:
        paint_not_yet(v, &s);
        break;
    case PAGE_BLANK:
        break;
    }
}

static void on_paint(view *v)
{
    PAINTSTRUCT ps;
    RECT client;
    HDC dc = BeginPaint(v->hwnd, &ps);

    GetClientRect(v->hwnd, &client);
    if (ensure_backbuffer(v, client.right, client.bottom)) {
        render(v);
        BitBlt(dc, 0, 0, v->width, v->height, v->dc, 0, 0, SRCCOPY);
    } else {
        FillRect(dc, &client, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }
    EndPaint(v->hwnd, &ps);
    v->paint_count++;
}

static LRESULT CALLBACK view_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    view *v = get_view(hwnd);

    switch (msg) {
    case WM_CREATE:
        v = calloc(1, sizeof *v);
        if (!v)
            return -1;
        v->hwnd = hwnd;
        v->page = PAGE_BLANK;
        v->font_logo = make_font("Times New Roman", 64, FW_BOLD, 1);
        v->font_tagline = make_font("Arial", 20, FW_NORMAL, 1);
        v->font_heading = make_font("Arial", 18, FW_BOLD, 0);
        v->font_body = make_font("Arial", 15, FW_NORMAL, 0);
        v->font_small = make_font("Arial", 12, FW_NORMAL, 0);
        SetWindowLongA(hwnd, GWL_USERDATA, (LONG)v);
        return 0;
    case WM_ERASEBKGND:
        return 1; /* on_paint covers everything */
    case WM_PAINT:
        on_paint(v);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;
    case WM_DESTROY:
        if (v) {
            free_backbuffer(v);
            DeleteObject(v->font_logo);
            DeleteObject(v->font_tagline);
            DeleteObject(v->font_heading);
            DeleteObject(v->font_body);
            DeleteObject(v->font_small);
            free(v);
            SetWindowLongA(hwnd, GWL_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int tw_view_register(HINSTANCE instance)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = view_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = TW_VIEW_CLASS;
    return RegisterClassA(&wc) != 0;
}

HWND tw_view_create(HWND parent, HINSTANCE instance, int id)
{
    return CreateWindowExA(WS_EX_CLIENTEDGE, TW_VIEW_CLASS, "",
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                           0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, instance, NULL);
}

const char *tw_view_show(HWND hwnd, const char *url)
{
    view *v = get_view(hwnd);
    if (!v)
        return "";
    snprintf(v->url, sizeof v->url, "%s", url);
    if (strcmp(url, "about:tachyon") == 0 || strcmp(url, "about:home") == 0) {
        v->page = PAGE_HOME;
        snprintf(v->title, sizeof v->title, "Welcome to Tachyon");
    } else if (strcmp(url, "about:blank") == 0) {
        v->page = PAGE_BLANK;
        snprintf(v->title, sizeof v->title, "about:blank");
    } else {
        v->page = PAGE_NOT_YET;
        snprintf(v->title, sizeof v->title, "%s", url);
    }
    InvalidateRect(hwnd, NULL, FALSE);
    return v->title;
}

int tw_view_paint_count(HWND hwnd)
{
    view *v = get_view(hwnd);
    return v ? v->paint_count : 0;
}
