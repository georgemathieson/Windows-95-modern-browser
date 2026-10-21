#include "view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_surface.h"
#include "tc_utf8.h"
#include "tc_version.h"

enum page_kind { PAGE_HOME, PAGE_BLANK, PAGE_MESSAGE, PAGE_SOURCE };

/* Most of a big document is still worth seeing; all of it costs too much
   memory on a 1990s PC. */
#define SOURCE_MAX_CHARS (2u * 1024 * 1024)
#define TAB_WIDTH 8

typedef struct vline {
    uint32_t start, len; /* in source.text */
} vline;

typedef struct source_text {
    WCHAR *text;
    size_t len, text_cap;
    size_t head_len;     /* characters belonging to the response head */
    vline *lines;        /* lines after wrapping */
    size_t nlines, cap;
    int wrap_cols;
} source_text;

typedef struct view {
    HWND hwnd;
    enum page_kind page;
    char url[2048];
    char heading[256];
    char *text, *detail;
    char warning[512];
    source_text source;
    int top_line;        /* first visible source line */
    int line_height, char_width;

    /* Back buffer: a 32 bpp top-down DIB section selected into `dc`. */
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    uint32_t *bits;
    int width, height;

    HFONT font_logo, font_tagline, font_heading, font_body, font_small, font_mono;
    int paint_count;
} view;

#define BANNER_TOP     TC_RGB(0, 0, 192)
#define BANNER_BOTTOM  TC_RGB(0, 0, 40)
#define PAGE_MARGIN    24
#define SOURCE_MARGIN  6

/* IntelliPoint's wheel message on Windows 95, which has no WM_MOUSEWHEEL. */
static UINT msh_mousewheel;

static view *get_view(HWND hwnd)
{
    return (view *)GetWindowLongA(hwnd, GWL_USERDATA);
}

static HFONT make_font(const char *face, int px, int weight, int italic, int charset, int pitch)
{
    return CreateFontA(-px, 0, 0, 0, weight, (DWORD)italic, FALSE, FALSE, (DWORD)charset,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, (DWORD)pitch, face);
}

static char *copy_string(const char *s)
{
    char *p;
    size_t n;
    if (!s)
        return NULL;
    n = strlen(s) + 1;
    p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

/* ---- Back buffer ---- */

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

/* ---- Source text: decoding, wrapping, scrolling ---- */

static void source_free(source_text *s)
{
    free(s->text);
    free(s->lines);
    memset(s, 0, sizeof *s);
}

/* Add one character, growing the buffer. 0 when full or out of memory. */
static int source_put(source_text *s, WCHAR c)
{
    if (s->len == s->text_cap) {
        size_t cap = s->text_cap ? s->text_cap * 2 : 4096;
        WCHAR *p;
        if (s->text_cap >= SOURCE_MAX_CHARS)
            return 0;
        cap = TC_MIN(cap, SOURCE_MAX_CHARS);
        p = realloc(s->text, cap * sizeof *p);
        if (!p)
            return 0;
        s->text = p;
        s->text_cap = cap;
    }
    s->text[s->len++] = c;
    return 1;
}

/* Decode UTF-8 into display characters: tabs expanded, CR dropped, controls
   and characters outside the BMP (which Windows 95 can't draw) shown as '?'.
   Returns 0 if it ran out of room. */
static int source_append(source_text *s, const char *utf8, size_t len, size_t *column)
{
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        int ok = 1;
        i += tc_utf8_decode(utf8 + i, len - i, &cp);
        if (cp == '\r')
            continue;
        if (cp == '\t') {
            do
                ok = source_put(s, L' ');
            while (ok && ++*column % TAB_WIDTH);
        } else if (cp == '\n') {
            ok = source_put(s, L'\n');
            *column = 0;
        } else {
            ok = source_put(s, (WCHAR)(cp < 0x20 || cp > 0xFFFF || (cp >= 0x7F && cp < 0xA0) ? '?' : cp));
            ++*column;
        }
        if (!ok)
            return 0;
    }
    return 1;
}

static int source_push_line(source_text *s, uint32_t start, uint32_t len)
{
    if (s->nlines == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 1024;
        vline *p = realloc(s->lines, cap * sizeof *p);
        if (!p)
            return 0;
        s->lines = p;
        s->cap = cap;
    }
    s->lines[s->nlines].start = start;
    s->lines[s->nlines].len = len;
    s->nlines++;
    return 1;
}

static void source_wrap(source_text *s, int cols)
{
    size_t i = 0;
    if (cols < 20)
        cols = 20;
    s->nlines = 0;
    s->wrap_cols = cols;
    while (i <= s->len) {
        size_t end = i;
        while (end < s->len && s->text[end] != L'\n')
            end++;
        do {
            size_t n = TC_MIN(end - i, (size_t)cols);
            if (!source_push_line(s, (uint32_t)i, (uint32_t)n))
                return;
            i += n;
        } while (i < end);
        i = end + 1;
    }
}

static int visible_lines(view *v)
{
    RECT client;
    GetClientRect(v->hwnd, &client);
    return v->line_height ? TC_MAX(1, (client.bottom - 2 * SOURCE_MARGIN) / v->line_height) : 1;
}

static void update_scrollbar(view *v)
{
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (v->page == PAGE_SOURCE) {
        si.nMax = (int)v->source.nlines - 1;
        si.nPage = (UINT)visible_lines(v);
        si.nPos = v->top_line;
    }
    SetScrollInfo(v->hwnd, SB_VERT, &si, TRUE);
}

static void scroll_to(view *v, int line)
{
    int max = (int)v->source.nlines - visible_lines(v);
    line = TC_CLAMP(line, 0, TC_MAX(max, 0));
    if (line != v->top_line) {
        v->top_line = line;
        update_scrollbar(v);
        InvalidateRect(v->hwnd, NULL, FALSE);
    }
}

/* Re-wrap the source for the current width; keep the top line in view. */
static void layout_source(view *v)
{
    RECT client;
    uint32_t top_char;
    int cols;
    size_t i;

    if (v->page != PAGE_SOURCE || !v->char_width)
        return;
    GetClientRect(v->hwnd, &client);
    cols = (client.right - 2 * SOURCE_MARGIN) / v->char_width;
    if (cols == v->source.wrap_cols)
        return;
    top_char = v->source.nlines ? v->source.lines[TC_MIN((size_t)v->top_line, v->source.nlines - 1)].start : 0;
    source_wrap(&v->source, cols);
    v->top_line = 0;
    for (i = 0; i < v->source.nlines && v->source.lines[i].start <= top_char; i++)
        v->top_line = (int)i;
    update_scrollbar(v);
}

/* ---- Drawing ---- */

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
    if (v->warning[0]) {
        RECT box = r, measure = r;
        SelectObject(v->dc, v->font_body);
        DrawTextA(v->dc, v->warning, -1, &measure, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        box.bottom = box.top + (measure.bottom - measure.top) + 16;
        GdiFlush();
        tc_surface_fill(s, tc_rect_make(box.left - 8, box.top, box.right - box.left + 16, box.bottom - box.top),
                        TC_RGB(255, 255, 192));
        r.top += 8;
        text_block(v, v->font_body, RGB(128, 0, 0), &r, v->warning);
        r.top += 20;
    }
    text_block(v, v->font_heading, RGB(0, 0, 128), &r, "Sprint 1: Hello, HTTPS");
    r.top += 6;
    text_block(v, v->font_body, RGB(0, 0, 0), &r,
               "Tachyon can now fetch pages over HTTP and HTTPS (TLS 1.2 and 1.3) and shows their "
               "source. Type an address above, such as example.com or en.wikipedia.org.");
    r.top += 14;
    text_block(v, v->font_heading, RGB(0, 0, 128), &r, "Coming next");
    r.top += 6;
    text_block(v, v->font_body, RGB(0, 0, 0), &r,
               "\x95  Sprint 2: an HTML5 parser and DOM\r\n"
               "\x95  Sprint 3: First Light, with CSS, layout and text: Wikipedia on Windows 95\r\n"
               "\x95  Sprint 4: images and fonts");
    r.top += 18;
    snprintf(footer, sizeof footer, "%s %s  \x95  GNU GPL v3  \x95  %s", TC_NAME, TC_VERSION, TC_HOMEPAGE);
    text_block(v, v->font_small, RGB(128, 128, 128), &r, footer);
}

static void paint_message(view *v, tc_surface *s)
{
    RECT r;
    int banner = 56;

    tc_surface_gradient_v(s, tc_rect_make(0, 0, s->width, banner), BANNER_TOP, BANNER_BOTTOM);
    text_shadowed(v, v->font_heading, PAGE_MARGIN, 18, v->heading);

    SetRect(&r, PAGE_MARGIN, banner + 20, v->width - PAGE_MARGIN, v->height);
    if (v->url[0]) {
        text_block(v, v->font_heading, RGB(0, 0, 128), &r, v->url);
        r.top += 8;
    }
    if (v->text) {
        text_block(v, v->font_body, RGB(0, 0, 0), &r, v->text);
        r.top += 12;
    }
    if (v->detail)
        text_block(v, v->font_small, RGB(96, 96, 96), &r, v->detail);
}

static void paint_source(view *v)
{
    int i, y = SOURCE_MARGIN, rows = visible_lines(v) + 1;
    SelectObject(v->dc, v->font_mono);
    for (i = 0; i < rows && (size_t)(v->top_line + i) < v->source.nlines; i++) {
        const vline *line = &v->source.lines[v->top_line + i];
        SetTextColor(v->dc, line->start < v->source.head_len ? RGB(0, 0, 128) : RGB(0, 0, 0));
        TextOutW(v->dc, SOURCE_MARGIN, y, v->source.text + line->start, (int)line->len);
        y += v->line_height;
    }
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
    case PAGE_MESSAGE:
        paint_message(v, &s);
        break;
    case PAGE_SOURCE:
        paint_source(v);
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

/* ---- Window procedure ---- */

static void on_vscroll(view *v, int code)
{
    SCROLLINFO si;
    int page = visible_lines(v);
    switch (code) {
    case SB_LINEUP: scroll_to(v, v->top_line - 1); break;
    case SB_LINEDOWN: scroll_to(v, v->top_line + 1); break;
    case SB_PAGEUP: scroll_to(v, v->top_line - page); break;
    case SB_PAGEDOWN: scroll_to(v, v->top_line + page); break;
    case SB_TOP: scroll_to(v, 0); break;
    case SB_BOTTOM: scroll_to(v, (int)v->source.nlines); break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        memset(&si, 0, sizeof si);
        si.cbSize = sizeof si;
        si.fMask = SIF_TRACKPOS;
        GetScrollInfo(v->hwnd, SB_VERT, &si);
        scroll_to(v, si.nTrackPos);
        break;
    }
}

static int on_key(view *v, WPARAM key)
{
    int page = visible_lines(v);
    switch (key) {
    case VK_UP: scroll_to(v, v->top_line - 1); return 1;
    case VK_DOWN: scroll_to(v, v->top_line + 1); return 1;
    case VK_PRIOR: scroll_to(v, v->top_line - page); return 1;
    case VK_NEXT:
    case VK_SPACE: scroll_to(v, v->top_line + page); return 1;
    case VK_HOME: scroll_to(v, 0); return 1;
    case VK_END: scroll_to(v, (int)v->source.nlines); return 1;
    }
    return 0;
}

static void measure_mono_font(view *v)
{
    HDC dc = GetDC(v->hwnd);
    TEXTMETRICA tm;
    HGDIOBJ old = SelectObject(dc, v->font_mono);
    GetTextMetricsA(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(v->hwnd, dc);
    v->line_height = tm.tmHeight;
    v->char_width = tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : 8;
}

static LRESULT CALLBACK view_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    view *v = get_view(hwnd);

    if (msg == WM_CREATE) {
        v = calloc(1, sizeof *v);
        if (!v)
            return -1;
        v->hwnd = hwnd;
        v->page = PAGE_BLANK;
        v->font_logo = make_font("Times New Roman", 64, FW_BOLD, 1, ANSI_CHARSET, VARIABLE_PITCH | FF_ROMAN);
        v->font_tagline = make_font("Arial", 20, FW_NORMAL, 1, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS);
        v->font_heading = make_font("Arial", 18, FW_BOLD, 0, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS);
        v->font_body = make_font("Arial", 15, FW_NORMAL, 0, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS);
        v->font_small = make_font("Arial", 12, FW_NORMAL, 0, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS);
        v->font_mono = make_font("Courier New", 13, FW_NORMAL, 0, DEFAULT_CHARSET, FIXED_PITCH | FF_MODERN);
        SetWindowLongA(hwnd, GWL_USERDATA, (LONG)v);
        measure_mono_font(v);
        return 0;
    }
    if (!v)
        return DefWindowProcA(hwnd, msg, wp, lp);

    if (msg == WM_MOUSEWHEEL || (msh_mousewheel && msg == msh_mousewheel)) {
        int delta = msg == WM_MOUSEWHEEL ? (short)HIWORD(wp) : (int)wp;
        scroll_to(v, v->top_line - delta / 40); /* three lines per notch */
        return 0;
    }
    switch (msg) {
    case WM_ERASEBKGND:
        return 1; /* on_paint covers everything */
    case WM_PAINT:
        on_paint(v);
        return 0;
    case WM_SIZE:
        layout_source(v);
        update_scrollbar(v);
        return 0;
    case WM_VSCROLL:
        on_vscroll(v, LOWORD(wp));
        return 0;
    case WM_KEYDOWN:
        if (on_key(v, wp))
            return 0;
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;
    case WM_DESTROY:
        free_backbuffer(v);
        DeleteObject(v->font_logo);
        DeleteObject(v->font_tagline);
        DeleteObject(v->font_heading);
        DeleteObject(v->font_body);
        DeleteObject(v->font_small);
        DeleteObject(v->font_mono);
        source_free(&v->source);
        free(v->text);
        free(v->detail);
        free(v);
        SetWindowLongA(hwnd, GWL_USERDATA, 0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int tw_view_register(HINSTANCE instance)
{
    WNDCLASSA wc;
    msh_mousewheel = RegisterWindowMessageA("MSWHEEL_ROLLMSG");
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
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL,
                           0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, instance, NULL);
}

/* ---- Showing pages ---- */

static view *begin_page(HWND hwnd, enum page_kind page, const char *url)
{
    view *v = get_view(hwnd);
    if (!v)
        return NULL;
    v->page = page;
    snprintf(v->url, sizeof v->url, "%s", url ? url : "");
    free(v->text);
    free(v->detail);
    v->text = v->detail = NULL;
    source_free(&v->source);
    v->top_line = 0;
    return v;
}

static void end_page(view *v)
{
    update_scrollbar(v);
    InvalidateRect(v->hwnd, NULL, FALSE);
}

void tw_view_show_home(HWND hwnd)
{
    view *v = begin_page(hwnd, PAGE_HOME, "about:tachyon");
    if (v)
        end_page(v);
}

void tw_view_show_blank(HWND hwnd)
{
    view *v = begin_page(hwnd, PAGE_BLANK, "about:blank");
    if (v)
        end_page(v);
}

void tw_view_show_message(HWND hwnd, const char *url, const char *heading, const char *text, const char *detail)
{
    view *v = begin_page(hwnd, PAGE_MESSAGE, url);
    if (!v)
        return;
    snprintf(v->heading, sizeof v->heading, "%s", heading);
    v->text = copy_string(text);
    v->detail = copy_string(detail);
    end_page(v);
}

int tw_view_show_source(HWND hwnd, const char *url, const char *head, size_t head_len, const char *body,
                        size_t body_len)
{
    static const char note[] = "\n\n[Tachyon shows only the first 2 MB of large documents.]";
    view *v = begin_page(hwnd, PAGE_SOURCE, url);
    size_t column = 0;

    if (!v)
        return TC_ERR_INVALID;
    source_append(&v->source, head, head_len, &column);
    v->source.head_len = v->source.len;
    if (!source_append(&v->source, body, body_len, &column)) {
        /* Make room for the note by dropping the end of the text. */
        size_t i, n = sizeof note - 1;
        v->source.len = v->source.len > n ? v->source.len - n : 0;
        for (i = 0; i < n && v->source.len < v->source.text_cap; i++)
            v->source.text[v->source.len++] = (WCHAR)note[i];
    }
    if (!v->source.text) {
        tw_view_show_message(hwnd, url, "Out of memory", "This page is too big to show.", NULL);
        return TC_ERR_NOMEM;
    }
    v->source.wrap_cols = 0;
    layout_source(v);
    end_page(v);
    return TC_OK;
}

void tw_view_set_warning(HWND hwnd, const char *warning)
{
    view *v = get_view(hwnd);
    if (v) {
        snprintf(v->warning, sizeof v->warning, "%s", warning ? warning : "");
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

int tw_view_paint_count(HWND hwnd)
{
    view *v = get_view(hwnd);
    return v ? v->paint_count : 0;
}
