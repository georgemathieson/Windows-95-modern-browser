#include "view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tc_dither.h"
#include "tc_log.h"
#include "tc_outline.h"
#include "tc_surface.h"
#include "tc_utf8.h"
#include "tc_version.h"

/* What the view shows. */
enum display { D_BLANK, D_PAGE, D_TREE, D_SOURCE };

/* Most of a big document is still worth seeing; all of it costs too much
   memory on a 1990s PC. */
#define SOURCE_MAX_CHARS (2u * 1024 * 1024)
#define TAB_WIDTH 8

#define RELAYOUT_TIMER 1
#define RELAYOUT_MS    150   /* lay out again this long after the last resize */
#define HOVER_TIMER    2
#define IMAGE_TIMER    3
#define ANIM_TIMER     4
#define IMAGE_MS       250   /* pictures arriving together are shown together */
#define HOVER_MS       100   /* Windows 95 has no TrackMouseEvent: poll while over a link */
#define LINE_SCROLL    40    /* pixels per arrow key */
#define WHEEL_SCROLL   48    /* pixels per wheel notch */
#define SCROLL_MAX     30000 /* Windows 95's scroll bars count to 32767 */

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
    tc_font_set *fonts;
    int image_relayout;          /* a picture arrived that layout was waiting for */
    int image_timer;             /* IMAGE_TIMER is set */
    int anim_timer;              /* ANIM_TIMER is set */
    tc_rect stale;               /* animations to paint again, in document coordinates */
    tw_view_mode mode;
    char url[2048];
    char warning[512];

    /* An HTML page, or a non-HTML document shown as source. */
    int has_page;
    tc_page page;
    int is_text;         /* a non-HTML document: source only */
    char *head;          /* the response head, for the source */
    size_t head_len;
    char *text_body;     /* a non-HTML document's body */
    size_t text_len;

    /* The rendered page: scroll position, size, links. */
    int sx, sy;
    float doc_w, doc_h;
    int wheel_rest;
    int dirty;           /* the back buffer must be painted again */
    const tc_element *hover, *pressed;
    HCURSOR hand, arrow;

    /* Source and tree. */
    source_text source;
    int top_line;        /* first visible source line or tree row */
    int line_height, char_width;
    tc_outline outline;
    int has_outline;
    tc_outline_text row_text;
    int selected;        /* selected tree row, or -1 */
    char info[512];      /* the line above the tree */

    /* Back buffer: a 32 bpp top-down DIB section selected into `dc`. */
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    uint32_t *bits;
    int width, height;
    /* On a 256-colour display, what's painted is dithered (decision 39) into
       an 8 bpp DIB section in `dc8`, whose colours are `palette`'s. */
    HDC dc8;
    HBITMAP bitmap8, old_bitmap8;
    uint8_t *bits8;
    int stride8;
    int depth;           /* the display's bits per pixel */
    int palette_display;
    HPALETTE palette;

    HFONT font_small, font_mono;
    int paint_count;
} view;

#define TREE_INDENT    2    /* characters per level */
#define TREE_BOX       12   /* pixels for the [+] box and its gap */
#define SOURCE_MARGIN  6

/* IntelliPoint's wheel message on Windows 95, which has no WM_MOUSEWHEEL. */
static UINT msh_mousewheel;
/* --dither: dither on any display, to see what a 256-colour one shows. */
static int force_dither;

static view *get_view(HWND hwnd)
{
    return (view *)GetWindowLongA(hwnd, GWL_USERDATA);
}

static HFONT make_font(const char *face, int px, int weight, int italic, int charset, int pitch)
{
    return CreateFontA(-px, 0, 0, 0, weight, (DWORD)italic, FALSE, FALSE, (DWORD)charset,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, (DWORD)pitch, face);
}

static enum display display_of(const view *v)
{
    if (v->is_text)
        return D_SOURCE;
    if (!v->has_page)
        return D_BLANK;
    return v->mode == TW_MODE_TREE ? D_TREE : v->mode == TW_MODE_SOURCE ? D_SOURCE : D_PAGE;
}

/* ---- Back buffer ---- */

static void free_dither_buffer(view *v)
{
    if (v->dc8) {
        SelectObject(v->dc8, v->old_bitmap8);
        DeleteObject(v->bitmap8);
        DeleteDC(v->dc8);
    }
    v->dc8 = NULL;
    v->bitmap8 = v->old_bitmap8 = NULL;
    v->bits8 = NULL;
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
    free_dither_buffer(v);
}

static HPALETTE make_palette(void)
{
    struct {
        WORD version, count;
        PALETTEENTRY entries[TC_DITHER_COLOURS];
    } lp;
    uint32_t colours[TC_DITHER_COLOURS];
    int i;
    tc_dither_palette(colours);
    lp.version = 0x300;
    lp.count = TC_DITHER_COLOURS;
    for (i = 0; i < TC_DITHER_COLOURS; i++) {
        lp.entries[i].peRed = (BYTE)(colours[i] >> 16);
        lp.entries[i].peGreen = (BYTE)(colours[i] >> 8);
        lp.entries[i].peBlue = (BYTE)colours[i];
        lp.entries[i].peFlags = 0;
    }
    return CreatePalette((LOGPALETTE *)&lp);
}

/* The 8 bpp copy of the back buffer the palette's colours are drawn from.
   Without it, Windows rounds to its nearest colours. */
static void make_dither_buffer(view *v, HDC screen)
{
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD colours[256];
    } bmi;
    uint32_t colours[TC_DITHER_COLOURS];
    void *bits = NULL;
    int i;

    memset(&bmi, 0, sizeof bmi);
    bmi.header.biSize = sizeof bmi.header;
    bmi.header.biWidth = v->width;
    bmi.header.biHeight = -v->height;
    bmi.header.biPlanes = 1;
    bmi.header.biBitCount = 8;
    bmi.header.biCompression = BI_RGB;
    bmi.header.biClrUsed = TC_DITHER_COLOURS;
    tc_dither_palette(colours);
    for (i = 0; i < TC_DITHER_COLOURS; i++) {
        bmi.colours[i].rgbRed = (BYTE)(colours[i] >> 16);
        bmi.colours[i].rgbGreen = (BYTE)(colours[i] >> 8);
        bmi.colours[i].rgbBlue = (BYTE)colours[i];
    }
    v->dc8 = CreateCompatibleDC(screen);
    v->bitmap8 = CreateDIBSection(screen, (BITMAPINFO *)&bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!v->dc8 || !v->bitmap8 || !bits) {
        if (v->bitmap8)
            DeleteObject(v->bitmap8);
        if (v->dc8)
            DeleteDC(v->dc8);
        v->dc8 = NULL;
        v->bitmap8 = NULL;
        return;
    }
    v->old_bitmap8 = SelectObject(v->dc8, v->bitmap8);
    v->bits8 = bits;
    v->stride8 = (v->width + 3) & ~3;
}

static int ensure_backbuffer(view *v, int width, int height)
{
    BITMAPINFO bmi;
    HDC screen;
    void *bits = NULL;
    int depth;

    if (v->dc && v->width == width && v->height == height)
        return 1;
    free_backbuffer(v);
    v->dirty = 1;
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
    if (!v->dc || !v->bitmap || !bits) {
        if (v->bitmap)
            DeleteObject(v->bitmap);
        if (v->dc)
            DeleteDC(v->dc);
        v->dc = NULL;
        v->bitmap = NULL;
        ReleaseDC(v->hwnd, screen);
        return 0;
    }
    v->old_bitmap = SelectObject(v->dc, v->bitmap);
    v->bits = bits;
    v->width = width;
    v->height = height;
    depth = GetDeviceCaps(screen, BITSPIXEL) * GetDeviceCaps(screen, PLANES);
    v->palette_display = (GetDeviceCaps(screen, RASTERCAPS) & RC_PALETTE) && depth == 8;
    if (depth != v->depth) {
        tc_log("display: %d bits per pixel%s%s", depth, v->palette_display ? ", a palette" : "",
               v->palette_display || force_dither ? "; dithering" : "");
        v->depth = depth;
    }
    if (v->palette_display && !v->palette)
        v->palette = make_palette();
    if (v->palette_display || force_dither)
        make_dither_buffer(v, screen);
    ReleaseDC(v->hwnd, screen);
    return 1;
}

static void redraw(view *v)
{
    v->dirty = 1;
    InvalidateRect(v->hwnd, NULL, FALSE);
}

/* ---- Source text: decoding, wrapping ---- */

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

/* ---- Lines (source and tree) ---- */

/* The height of the information bar above the tree. */
static int info_height(const view *v)
{
    return display_of(v) == D_TREE ? v->line_height + 8 : 0;
}

static int visible_lines(view *v)
{
    RECT client;
    GetClientRect(v->hwnd, &client);
    return v->line_height
               ? TC_MAX(1, (client.bottom - info_height(v) - 2 * SOURCE_MARGIN) / v->line_height)
               : 1;
}

/* Lines of source or rows of tree. */
static int total_lines(const view *v)
{
    switch (display_of(v)) {
    case D_SOURCE: return (int)v->source.nlines;
    case D_TREE: return (int)v->outline.count;
    default: return 0;
    }
}

/* Scroll bar positions for the page, in units small enough for Windows 95's
   16-bit scroll bars. */
static int scroll_unit(float size)
{
    return size > SCROLL_MAX ? (int)(size / SCROLL_MAX) + 1 : 1;
}

static void set_bar(HWND hwnd, int bar, int max, int page, int pos)
{
    SCROLLINFO si;
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = max;
    si.nPage = (UINT)TC_MAX(page, 0);
    si.nPos = pos;
    SetScrollInfo(hwnd, bar, &si, TRUE);
}

static void update_scrollbars(view *v)
{
    RECT client;
    GetClientRect(v->hwnd, &client);
    if (display_of(v) == D_PAGE) {
        int uy = scroll_unit(v->doc_h), ux = scroll_unit(v->doc_w);
        set_bar(v->hwnd, SB_VERT, (int)(v->doc_h / uy), client.bottom / uy, v->sy / uy);
        if (v->doc_w > client.right + 1) {
            ShowScrollBar(v->hwnd, SB_HORZ, TRUE);
            set_bar(v->hwnd, SB_HORZ, (int)(v->doc_w / ux), client.right / ux, v->sx / ux);
        } else {
            ShowScrollBar(v->hwnd, SB_HORZ, FALSE);
        }
        return;
    }
    ShowScrollBar(v->hwnd, SB_HORZ, FALSE);
    set_bar(v->hwnd, SB_VERT, total_lines(v) ? total_lines(v) - 1 : 0, visible_lines(v), v->top_line);
}

static void scroll_lines_to(view *v, int line)
{
    int max = total_lines(v) - visible_lines(v);
    line = TC_CLAMP(line, 0, TC_MAX(max, 0));
    if (line != v->top_line) {
        tc_trace("scroll %d -> %d", v->top_line, line);
        v->top_line = line;
        update_scrollbars(v);
        redraw(v);
    }
}

/* Re-wrap the source for the current width; keep the top line in view. */
static void layout_source(view *v)
{
    RECT client;
    uint32_t top_char;
    int cols;
    size_t i;

    if (display_of(v) != D_SOURCE || !v->char_width)
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
}

/* ---- The page ---- */

static void page_bounds(view *v, int *max_x, int *max_y)
{
    RECT client;
    GetClientRect(v->hwnd, &client);
    *max_x = TC_MAX(0, (int)(v->doc_w + 0.5f) - client.right);
    *max_y = TC_MAX(0, (int)(v->doc_h + 0.5f) - client.bottom);
}

static void paint_page_area(view *v, tc_rect clip)
{
    tc_surface s;
    tc_surface_wrap(&s, v->bits, v->width, v->height, v->width);
    tc_paint(v->page.layout, &s, clip, (float)v->sx, (float)v->sy);
}

/* Scroll the page: what's drawn moves, and only the strip that comes into
   view is painted. */
static void scroll_page_to(view *v, int x, int y)
{
    int max_x, max_y, dy;
    page_bounds(v, &max_x, &max_y);
    x = TC_CLAMP(x, 0, max_x);
    y = TC_CLAMP(y, 0, max_y);
    if (x == v->sx && y == v->sy)
        return;
    dy = y - v->sy;
    tc_trace("scroll page %d,%d -> %d,%d", v->sx, v->sy, x, y);
    if (x == v->sx && !v->dirty && v->dc && dy > -v->height && dy < v->height) {
        int rows = v->height - (dy > 0 ? dy : -dy);
        GdiFlush();
        if (dy > 0)
            memmove(v->bits, v->bits + (size_t)dy * (size_t)v->width, (size_t)rows * (size_t)v->width * 4);
        else
            memmove(v->bits - (size_t)dy * (size_t)v->width, v->bits, (size_t)rows * (size_t)v->width * 4);
        v->sy = y;
        paint_page_area(v, dy > 0 ? tc_rect_make(0, rows, v->width, dy) : tc_rect_make(0, 0, v->width, -dy));
        ScrollWindowEx(v->hwnd, 0, -dy, NULL, NULL, NULL, NULL, SW_INVALIDATE);
    } else {
        v->sx = x;
        v->sy = y;
        redraw(v);
    }
    update_scrollbars(v);
}

static void scroll_by(view *v, int dx, int dy)
{
    if (display_of(v) == D_PAGE)
        scroll_page_to(v, v->sx + dx, v->sy + dy);
    else if (v->line_height)
        scroll_lines_to(v, v->top_line + dy / v->line_height + (dy > 0 && dy < v->line_height) -
                               (dy < 0 && -dy < v->line_height));
}

/* Lay the page out again for the view's size (after a resize). */
/* Lay the page out again for the view's size (if that changed, or force). */
static void relayout(view *v, int force)
{
    RECT client;
    float fraction;
    if (!v->has_page)
        return;
    GetClientRect(v->hwnd, &client);
    if (client.right <= 0 || client.bottom <= 0)
        return;
    if (!force && v->page.layout && v->page.width == (float)client.right)
        return;
    fraction = v->doc_h > 0 ? v->sy / v->doc_h : 0;
    SetCursor(LoadCursorA(NULL, (LPCSTR)IDC_WAIT));
    tc_log("layout again at %ldx%ld", (long)client.right, (long)client.bottom);
    if (tc_page_layout(&v->page, v->fonts, (float)client.right, (float)client.bottom) != TC_OK)
        tc_log("out of memory laying out");
    if (v->page.layout)
        tc_layout_size(v->page.layout, &v->doc_w, &v->doc_h);
    v->sy = (int)(fraction * v->doc_h);
    v->pressed = NULL; /* the hover timer checks what is under the mouse now */
    scroll_page_to(v, v->sx, v->sy);
    update_scrollbars(v);
    redraw(v);
}

/* ---- Drawing ---- */

/* A source line for the log: ASCII, with \uXXXX for anything else. */
static const char *wide_ascii(char *out, size_t cap, const WCHAR *s, size_t len)
{
    size_t i, n = 0;
    for (i = 0; i < len && n + 7 < cap; i++) {
        if (s[i] >= 0x20 && s[i] < 0x7F)
            out[n++] = (char)s[i];
        else
            n += (size_t)snprintf(out + n, cap - n, "\\u%04X", (unsigned)s[i]);
    }
    out[n] = '\0';
    return out;
}

static void paint_source(view *v)
{
    int i, y = SOURCE_MARGIN, rows = visible_lines(v) + 1;
    char ascii[100];

    tc_trace("paint source: lines %d+%d of %u, %dx%d", v->top_line, rows, (unsigned)v->source.nlines, v->width,
             v->height);
    SelectObject(v->dc, v->font_mono);
    for (i = 0; i < rows && (size_t)(v->top_line + i) < v->source.nlines; i++) {
        const vline *line = &v->source.lines[v->top_line + i];
        tc_trace("line %d: %s", v->top_line + i, wide_ascii(ascii, sizeof ascii, v->source.text + line->start, line->len));
        SetTextColor(v->dc, line->start < v->source.head_len ? RGB(0, 0, 128) : RGB(0, 0, 0));
        TextOutW(v->dc, SOURCE_MARGIN, y, v->source.text + line->start, (int)line->len);
        y += v->line_height;
    }
}

static COLORREF span_color(int kind)
{
    switch (kind) {
    case TC_SPAN_TAG: return RGB(136, 18, 128);
    case TC_SPAN_ATTR_NAME: return RGB(153, 69, 0);
    case TC_SPAN_ATTR_VALUE: return RGB(26, 26, 166);
    case TC_SPAN_TEXT: return RGB(0, 0, 0);
    case TC_SPAN_COMMENT: return RGB(35, 110, 37);
    case TC_SPAN_DOCTYPE: return RGB(110, 110, 110);
    case TC_SPAN_NOTE: return RGB(128, 128, 128);
    default: return RGB(136, 18, 128);
    }
}

/* Where a tree row's [+] box starts, in pixels. Deep rows stop indenting at
   half the width, so their text stays on screen. */
static int row_x(const view *v, const tc_outline_row *row)
{
    int indent = TREE_INDENT * v->char_width;
    int max_levels = indent ? TC_MAX(1, v->width / 2 / indent) : 1;
    return SOURCE_MARGIN + TC_MIN((int)row->depth, max_levels) * indent;
}

static void draw_box(tc_surface *s, int x, int y, int expanded)
{
    tc_color frame = TC_RGB(128, 128, 128), mark = TC_RGB(0, 0, 0);

    tc_surface_fill(s, tc_rect_make(x, y, 9, 9), TC_RGB(255, 255, 255));
    tc_surface_fill(s, tc_rect_make(x, y, 9, 1), frame);
    tc_surface_fill(s, tc_rect_make(x, y + 8, 9, 1), frame);
    tc_surface_fill(s, tc_rect_make(x, y, 1, 9), frame);
    tc_surface_fill(s, tc_rect_make(x + 8, y, 1, 9), frame);
    tc_surface_fill(s, tc_rect_make(x + 2, y + 4, 5, 1), mark);
    if (!expanded)
        tc_surface_fill(s, tc_rect_make(x + 4, y + 2, 1, 5), mark);
}

/* UTF-8 to UTF-16 for TextOutW; characters beyond the BMP become '?'. */
static int to_wide(const char *utf8, size_t len, WCHAR *out, int max)
{
    size_t i = 0;
    int n = 0;
    while (i < len && n < max) {
        uint32_t cp;
        i += tc_utf8_decode(utf8 + i, len - i, &cp);
        out[n++] = (WCHAR)(cp > 0xFFFF || cp < 0x20 ? '?' : cp);
    }
    return n;
}

static void paint_tree(view *v, tc_surface *s)
{
    int i, rows = visible_lines(v) + 1, top = info_height(v) + SOURCE_MARGIN;
    WCHAR wide[TC_OUTLINE_MAX_TEXT + 8];
    char ascii[100];

    tc_trace("paint tree: rows %d+%d of %u, %dx%d", v->top_line, rows, (unsigned)v->outline.count, v->width,
             v->height);
    /* Pixels first, then GDI's text on top. */
    tc_surface_fill(s, tc_rect_make(0, 0, s->width, info_height(v) - 1), TC_RGB(232, 232, 232));
    tc_surface_fill(s, tc_rect_make(0, info_height(v) - 1, s->width, 1), TC_RGB(160, 160, 160));
    for (i = 0; i < rows && (size_t)(v->top_line + i) < v->outline.count; i++) {
        const tc_outline_row *row = &v->outline.rows[v->top_line + i];
        int y = top + i * v->line_height;
        if (v->top_line + i == v->selected)
            tc_surface_fill(s, tc_rect_make(0, y, s->width, v->line_height), TC_RGB(204, 224, 255));
        if (row->expandable)
            draw_box(s, row_x(v, row), y + (v->line_height - 9) / 2, row->expanded);
    }
    GdiFlush();

    SelectObject(v->dc, v->font_small);
    SetTextColor(v->dc, RGB(64, 64, 64));
    TextOutA(v->dc, SOURCE_MARGIN, (info_height(v) - v->line_height) / 2 + 1, v->info, (int)strlen(v->info));
    SelectObject(v->dc, v->font_mono);
    for (i = 0; i < rows && (size_t)(v->top_line + i) < v->outline.count; i++) {
        const tc_outline_row *row = &v->outline.rows[v->top_line + i];
        const tc_outline_text *t = &v->row_text;
        int x = row_x(v, row) + TREE_BOX, y = top + i * v->line_height;
        size_t k;
        if (tc_outline_row_text(&v->outline, (size_t)(v->top_line + i), &v->row_text) != TC_OK)
            continue;
        tc_trace("row %d: %s", v->top_line + i, tc_log_ascii(ascii, sizeof ascii, t->text.data, t->text.len));
        for (k = 0; k < t->nspans && x < v->width; k++) {
            size_t start = t->span_start[k];
            size_t end = k + 1 < t->nspans ? t->span_start[k + 1] : t->text.len;
            int n = to_wide(t->text.data + start, end - start, wide, (int)TC_ARRAY_LEN(wide));
            SetTextColor(v->dc, span_color(t->span_kind[k]));
            TextOutW(v->dc, x, y, wide, n);
            x += n * v->char_width;
        }
    }
}

static void render(view *v)
{
    tc_surface s;

    if (!v->dc)
        return;
    GdiFlush(); /* finish GDI drawing before touching the bits directly */
    tc_surface_wrap(&s, v->bits, v->width, v->height, v->width);
    switch (display_of(v)) {
    case D_PAGE:
        if (v->page.layout) {
            tc_trace("paint page at %d,%d, %dx%d", v->sx, v->sy, v->width, v->height);
            paint_page_area(v, tc_rect_make(0, 0, v->width, v->height));
            break;
        }
        /* fall through */
    case D_BLANK:
        tc_surface_fill(&s, tc_rect_make(0, 0, v->width, v->height), TC_RGB(255, 255, 255));
        break;
    case D_SOURCE:
        tc_surface_fill(&s, tc_rect_make(0, 0, v->width, v->height), TC_RGB(255, 255, 255));
        SetBkMode(v->dc, TRANSPARENT);
        paint_source(v);
        break;
    case D_TREE:
        tc_surface_fill(&s, tc_rect_make(0, 0, v->width, v->height), TC_RGB(255, 255, 255));
        SetBkMode(v->dc, TRANSPARENT);
        paint_tree(v, &s);
        break;
    }
    GdiFlush();
    v->dirty = 0;
}

/* Move animated pictures on to their next frames, mark where they need
   painting, and wait for the next one due. */
static void animate(view *v)
{
    tc_rect dirty;
    uint32_t wait;
    if (v->anim_timer)
        KillTimer(v->hwnd, ANIM_TIMER);
    v->anim_timer = 0;
    if (!v->has_page || display_of(v) != D_PAGE || !v->page.layout)
        return;
    wait = tc_page_animate(&v->page, GetTickCount(), &dirty);
    if (!tc_rect_is_empty(dirty)) {
        RECT r;
        v->stale = tc_rect_union(v->stale, dirty);
        SetRect(&r, dirty.x - v->sx, dirty.y - v->sy, dirty.x - v->sx + dirty.w, dirty.y - v->sy + dirty.h);
        InvalidateRect(v->hwnd, &r, FALSE);
    }
    if (wait)
        v->anim_timer = SetTimer(v->hwnd, ANIM_TIMER, wait, NULL) != 0;
}

static void on_paint(view *v)
{
    PAINTSTRUCT ps;
    RECT client;
    HPALETTE old_palette = NULL;
    HDC dc = BeginPaint(v->hwnd, &ps);

    GetClientRect(v->hwnd, &client);
    if (ensure_backbuffer(v, client.right, client.bottom)) {
        if (v->dirty) {
            render(v);
        } else if (!tc_rect_is_empty(v->stale) && display_of(v) == D_PAGE && v->page.layout) {
            GdiFlush();
            paint_page_area(v, tc_rect_make(v->stale.x - v->sx, v->stale.y - v->sy, v->stale.w, v->stale.h));
        }
        v->stale = tc_rect_make(0, 0, 0, 0);
        if (v->bits8) {
            /* The pattern stays put on the page as it scrolls. */
            int page = display_of(v) == D_PAGE;
            tc_surface s;
            GdiFlush();
            tc_surface_wrap(&s, v->bits, v->width, v->height, v->width);
            tc_dither(&s, tc_rect_make(ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                                       ps.rcPaint.bottom - ps.rcPaint.top),
                      v->bits8, v->stride8, page ? v->sx : 0, page ? v->sy : 0);
        }
        if (v->palette_display && v->palette) {
            old_palette = SelectPalette(dc, v->palette, FALSE);
            RealizePalette(dc);
        }
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top, v->bits8 ? v->dc8 : v->dc, ps.rcPaint.left, ps.rcPaint.top,
               SRCCOPY);
        if (old_palette)
            SelectPalette(dc, old_palette, TRUE);
    } else {
        FillRect(dc, &client, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }
    EndPaint(v->hwnd, &ps);
    v->paint_count++;
    if (!v->anim_timer) /* animations just painted start playing */
        animate(v);
}

/* ---- Input ---- */

static int scroll_pos(view *v, int bar, int code, int pos, int line, int page, int max)
{
    SCROLLINFO si;
    switch (code) {
    case SB_LINEUP: return pos - line;
    case SB_LINEDOWN: return pos + line;
    case SB_PAGEUP: return pos - page;
    case SB_PAGEDOWN: return pos + page;
    case SB_TOP: return 0;
    case SB_BOTTOM: return max;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        memset(&si, 0, sizeof si);
        si.cbSize = sizeof si;
        si.fMask = SIF_TRACKPOS;
        GetScrollInfo(v->hwnd, bar, &si);
        return si.nTrackPos;
    default: return pos;
    }
}

static void on_scroll(view *v, int bar, int code)
{
    RECT client;
    GetClientRect(v->hwnd, &client);
    if (display_of(v) == D_PAGE) {
        int max_x, max_y;
        page_bounds(v, &max_x, &max_y);
        if (bar == SB_VERT) {
            int unit = scroll_unit(v->doc_h);
            int y = code == SB_THUMBTRACK || code == SB_THUMBPOSITION
                        ? scroll_pos(v, bar, code, 0, 0, 0, 0) * unit
                        : scroll_pos(v, bar, code, v->sy, LINE_SCROLL, client.bottom - LINE_SCROLL, max_y);
            scroll_page_to(v, v->sx, y);
        } else {
            int unit = scroll_unit(v->doc_w);
            int x = code == SB_THUMBTRACK || code == SB_THUMBPOSITION
                        ? scroll_pos(v, bar, code, 0, 0, 0, 0) * unit
                        : scroll_pos(v, bar, code, v->sx, LINE_SCROLL, client.right - LINE_SCROLL, max_x);
            scroll_page_to(v, x, v->sy);
        }
        return;
    }
    if (bar == SB_VERT)
        scroll_lines_to(v, scroll_pos(v, bar, code, v->top_line, 1, visible_lines(v), total_lines(v)));
}

/* Select a tree row and scroll it into view. */
static void select_row(view *v, int row)
{
    int page = visible_lines(v);

    if (!v->outline.count)
        return;
    row = TC_CLAMP(row, 0, (int)v->outline.count - 1);
    v->selected = row;
    if (row < v->top_line)
        scroll_lines_to(v, row);
    else if (row >= v->top_line + page)
        scroll_lines_to(v, row - page + 1);
    redraw(v);
}

/* Expand or collapse a row, keeping the selection on the same node. */
static void set_expanded(view *v, int row, int expanded)
{
    const tc_node *selected = v->selected >= 0 && (size_t)v->selected < v->outline.count
                                  ? v->outline.rows[v->selected].node
                                  : NULL;

    if (row < 0 || (size_t)row >= v->outline.count || !v->outline.rows[row].expandable ||
        v->outline.rows[row].expanded == expanded)
        return;
    tc_trace("%s row %d", expanded ? "open" : "close", row);
    tc_outline_set_expanded(&v->outline, (size_t)row, expanded);
    v->selected = selected ? (int)tc_outline_find(&v->outline, selected) : -1;
    scroll_lines_to(v, v->top_line); /* clamps to the new length */
    update_scrollbars(v);
    redraw(v);
}

static int on_tree_key(view *v, WPARAM key)
{
    int page = visible_lines(v), sel = v->selected;
    const tc_outline_row *row = sel >= 0 && (size_t)sel < v->outline.count ? &v->outline.rows[sel] : NULL;

    switch (key) {
    case VK_UP: select_row(v, sel < 0 ? v->top_line : sel - 1); return 1;
    case VK_DOWN: select_row(v, sel < 0 ? v->top_line : sel + 1); return 1;
    case VK_PRIOR: select_row(v, (sel < 0 ? v->top_line : sel) - page); return 1;
    case VK_NEXT: select_row(v, (sel < 0 ? v->top_line : sel) + page); return 1;
    case VK_HOME: select_row(v, 0); return 1;
    case VK_END: select_row(v, (int)v->outline.count - 1); return 1;
    case VK_LEFT:
        if (row && row->expanded)
            set_expanded(v, sel, 0);
        else if (row)
            select_row(v, (int)tc_outline_parent(&v->outline, (size_t)sel));
        return 1;
    case VK_RIGHT:
        if (row && row->expandable && !row->expanded)
            set_expanded(v, sel, 1);
        else if (row && row->expanded)
            select_row(v, sel + 1);
        return 1;
    case VK_RETURN:
        if (row)
            set_expanded(v, sel, !row->expanded);
        return 1;
    case VK_SPACE: scroll_lines_to(v, v->top_line + page); return 1;
    }
    return 0;
}

/* A click in the tree: the [+] box toggles, elsewhere selects (and a
   double click toggles). */
static void on_tree_click(view *v, int x, int y, int double_click)
{
    int row = v->top_line + (y - info_height(v) - SOURCE_MARGIN) / TC_MAX(v->line_height, 1);
    int box;

    if (y < info_height(v) + SOURCE_MARGIN || row >= (int)v->outline.count)
        return;
    box = row_x(v, &v->outline.rows[row]);
    select_row(v, row);
    if (double_click || (x >= box - 2 && x < box + TREE_BOX))
        set_expanded(v, row, !v->outline.rows[row].expanded);
}

static int on_key(view *v, WPARAM key)
{
    RECT client;
    int page, shift = GetKeyState(VK_SHIFT) < 0;
    enum display d = display_of(v);

    if (d == D_TREE)
        return on_tree_key(v, key);
    GetClientRect(v->hwnd, &client);
    page = d == D_PAGE ? TC_MAX(client.bottom - LINE_SCROLL, LINE_SCROLL) : visible_lines(v) * v->line_height;
    switch (key) {
    case VK_UP: scroll_by(v, 0, -LINE_SCROLL); return 1;
    case VK_DOWN: scroll_by(v, 0, LINE_SCROLL); return 1;
    case VK_LEFT: scroll_by(v, -LINE_SCROLL, 0); return 1;
    case VK_RIGHT: scroll_by(v, LINE_SCROLL, 0); return 1;
    case VK_PRIOR: scroll_by(v, 0, -page); return 1;
    case VK_NEXT: scroll_by(v, 0, page); return 1;
    case VK_SPACE: scroll_by(v, 0, shift ? -page : page); return 1;
    case VK_HOME:
        if (d == D_PAGE)
            scroll_page_to(v, 0, 0);
        else
            scroll_lines_to(v, 0);
        return 1;
    case VK_END:
        if (d == D_PAGE)
            scroll_page_to(v, v->sx, (int)v->doc_h);
        else
            scroll_lines_to(v, total_lines(v));
        return 1;
    }
    return 0;
}

/* The link under the mouse, and telling the frame about it. */
static const tc_element *link_at(view *v, int x, int y, tc_url *url)
{
    if (display_of(v) != D_PAGE || !v->page.layout)
        return NULL;
    return tc_page_link_at(&v->page, (float)(x + v->sx), (float)(y + v->sy), url);
}

static void set_hover(view *v, int x, int y)
{
    tc_url url;
    const tc_element *link = x >= 0 && y >= 0 ? link_at(v, x, y, &url) : NULL;
    if (link == v->hover) {
        if (link)
            tc_url_free(&url);
        return;
    }
    v->hover = link;
    if (link)
        SetTimer(v->hwnd, HOVER_TIMER, HOVER_MS, NULL);
    else
        KillTimer(v->hwnd, HOVER_TIMER);
    SendMessageA(GetParent(v->hwnd), WM_TW_HOVER, 0, (LPARAM)(link ? url.href : NULL));
    if (link)
        tc_url_free(&url);
}

/* While over a link: has the mouse left the view, or has the page moved
   under it? */
static void check_hover(view *v)
{
    POINT pt;
    if (!v->hover) { /* a new page came in under it */
        KillTimer(v->hwnd, HOVER_TIMER);
        return;
    }
    GetCursorPos(&pt);
    if (WindowFromPoint(pt) != v->hwnd) {
        set_hover(v, -1, -1);
        return;
    }
    ScreenToClient(v->hwnd, &pt);
    set_hover(v, pt.x, pt.y);
}

static void on_wheel(view *v, int delta)
{
    /* Trackpads send small steps; keep the remainder for the next. */
    int total = delta * WHEEL_SCROLL + v->wheel_rest, px = total / 120;
    v->wheel_rest = total - px * 120;
    tc_trace("wheel %d: %d px", delta, -px);
    if (display_of(v) == D_PAGE)
        scroll_page_to(v, v->sx, v->sy - px);
    else if (v->line_height && px)
        scroll_lines_to(v, v->top_line - (px > 0 ? TC_MAX(1, px / v->line_height) : TC_MIN(-1, px / v->line_height)));
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

static void clear_document(view *v)
{
    if (v->has_outline)
        tc_outline_free(&v->outline);
    v->has_outline = 0;
    if (v->has_page)
        tc_page_free(&v->page);
    v->has_page = 0;
    v->is_text = 0;
    free(v->head);
    free(v->text_body);
    v->head = v->text_body = NULL;
    v->head_len = v->text_len = 0;
    source_free(&v->source);
    v->top_line = 0;
    v->selected = -1;
    v->sx = v->sy = 0;
    v->doc_w = v->doc_h = 0;
    if (v->anim_timer)
        KillTimer(v->hwnd, ANIM_TIMER);
    v->anim_timer = 0;
    v->stale = tc_rect_make(0, 0, 0, 0);
    if (v->hover)
        set_hover(v, -1, -1);
    v->pressed = NULL;
    v->info[0] = '\0';
}

static LRESULT CALLBACK view_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    view *v = get_view(hwnd);

    if (msg == WM_CREATE) {
        const CREATESTRUCTA *cs = (const CREATESTRUCTA *)lp;
        v = calloc(1, sizeof *v);
        if (!v)
            return -1;
        v->hwnd = hwnd;
        v->fonts = cs->lpCreateParams;
        v->selected = -1;
        v->dirty = 1;
        tc_outline_text_init(&v->row_text);
        v->font_small = make_font("Arial", 12, FW_NORMAL, 0, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS);
        v->font_mono = make_font("Courier New", 13, FW_NORMAL, 0, DEFAULT_CHARSET, FIXED_PITCH | FF_MODERN);
        v->hand = LoadCursorA(cs->hInstance, "HAND");
        v->arrow = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
        SetWindowLongA(hwnd, GWL_USERDATA, (LONG)v);
        measure_mono_font(v);
        return 0;
    }
    if (!v)
        return DefWindowProcA(hwnd, msg, wp, lp);

    if (msg == WM_MOUSEWHEEL || (msh_mousewheel && msg == msh_mousewheel)) {
        on_wheel(v, msg == WM_MOUSEWHEEL ? (short)HIWORD(wp) : (int)wp);
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
        if (v->has_page && display_of(v) == D_PAGE && v->page.width != (float)LOWORD(lp))
            SetTimer(hwnd, RELAYOUT_TIMER, RELAYOUT_MS, NULL);
        if (display_of(v) == D_PAGE)
            scroll_page_to(v, v->sx, v->sy); /* clamp to the new size */
        update_scrollbars(v);
        v->dirty = 1;
        return 0;
    case WM_TIMER:
        if (wp == RELAYOUT_TIMER) {
            KillTimer(hwnd, RELAYOUT_TIMER);
            relayout(v, 0);
        } else if (wp == HOVER_TIMER) {
            check_hover(v);
        } else if (wp == IMAGE_TIMER) {
            KillTimer(hwnd, IMAGE_TIMER);
            v->image_timer = 0;
            if (v->image_relayout)
                relayout(v, 1);
            else if (display_of(v) == D_PAGE)
                redraw(v);
            v->image_relayout = 0;
        } else if (wp == ANIM_TIMER) {
            animate(v);
        }
        return 0;
    case WM_VSCROLL:
        on_scroll(v, SB_VERT, LOWORD(wp));
        return 0;
    case WM_HSCROLL:
        on_scroll(v, SB_HORZ, LOWORD(wp));
        return 0;
    case WM_KEYDOWN:
        if (on_key(v, wp))
            return 0;
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_MOUSEMOVE:
        set_hover(v, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(v->hover && v->hand ? v->hand : v->arrow);
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetFocus(hwnd);
        if (display_of(v) == D_TREE)
            on_tree_click(v, (short)LOWORD(lp), (short)HIWORD(lp), msg == WM_LBUTTONDBLCLK);
        else
            v->pressed = link_at(v, (short)LOWORD(lp), (short)HIWORD(lp), NULL);
        return 0;
    case WM_LBUTTONUP: {
        tc_url url;
        const tc_element *link = v->pressed ? link_at(v, (short)LOWORD(lp), (short)HIWORD(lp), &url) : NULL;
        if (link && link == v->pressed) {
            tc_log("follow %.120s", url.href);
            v->pressed = NULL;
            SendMessageA(GetParent(hwnd), WM_TW_FOLLOW, 0, (LPARAM)url.href);
            tc_url_free(&url);
            return 0;
        }
        if (link)
            tc_url_free(&url);
        v->pressed = NULL;
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, RELAYOUT_TIMER);
        KillTimer(hwnd, HOVER_TIMER);
        KillTimer(hwnd, IMAGE_TIMER);
        KillTimer(hwnd, ANIM_TIMER);
        free_backbuffer(v);
        if (v->palette)
            DeleteObject(v->palette);
        DeleteObject(v->font_small);
        DeleteObject(v->font_mono);
        clear_document(v);
        tc_outline_text_free(&v->row_text);
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
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = view_proc;
    wc.hInstance = instance;
    wc.hCursor = NULL; /* WM_SETCURSOR picks */
    wc.lpszClassName = TW_VIEW_CLASS;
    return RegisterClassA(&wc) != 0;
}

HWND tw_view_create(HWND parent, HINSTANCE instance, int id, tc_font_set *fonts)
{
    return CreateWindowExA(WS_EX_CLIENTEDGE, TW_VIEW_CLASS, "",
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL,
                           0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, instance, fonts);
}

void tw_view_page_size(HWND hwnd, int *w, int *h)
{
    RECT client;
    GetClientRect(hwnd, &client);
    *w = TC_MAX(client.right, 100);
    *h = TC_MAX(client.bottom, 100);
}

void tw_view_visible(HWND hwnd, int *top, int *bottom)
{
    view *v = get_view(hwnd);
    RECT client;
    GetClientRect(hwnd, &client);
    *top = v ? v->sy : 0;
    *bottom = *top + client.bottom;
}

/* ---- Showing pages ---- */

static void finish(view *v)
{
    layout_source(v);
    update_scrollbars(v);
    redraw(v);
}

/* Fill v->source with a response head and body. */
static int fill_source(view *v, const char *head, size_t head_len, const char *body, size_t body_len)
{
    static const char note[] = "\n\n[Tachyon shows only the first 2 MB of large documents.]";
    size_t column = 0;

    source_free(&v->source);
    source_append(&v->source, head ? head : "", head_len, &column);
    v->source.head_len = v->source.len;
    if (!source_append(&v->source, body, body_len, &column)) {
        /* Make room for the note by dropping the end of the text. */
        size_t i, n = sizeof note - 1;
        v->source.len = v->source.len > n ? v->source.len - n : 0;
        for (i = 0; i < n && v->source.len < v->source.text_cap; i++)
            v->source.text[v->source.len++] = (WCHAR)note[i];
    }
    if (!v->source.text)
        return TC_ERR_NOMEM;
    v->source.wrap_cols = 0;
    v->top_line = 0;
    return TC_OK;
}

static const char *encoding_source_text(tc_encoding_source source)
{
    switch (source) {
    case TC_ENCSRC_BOM: return "from a byte order mark";
    case TC_ENCSRC_HTTP: return "from the HTTP header";
    case TC_ENCSRC_PRESCAN: return "from the page";
    case TC_ENCSRC_META: return "from a <meta> tag, read again";
    default: return "a guess";
    }
}

/* "1,234" */
static void with_commas(size_t n, char *out, size_t size)
{
    char digits[32];
    size_t len, i, o = 0;

    snprintf(digits, sizeof digits, "%lu", (unsigned long)n);
    len = strlen(digits);
    for (i = 0; i < len && o + 2 < size; i++) {
        if (i && (len - i) % 3 == 0)
            out[o++] = ',';
        out[o++] = digits[i];
    }
    out[o] = '\0';
}

static void build_info(view *v)
{
    const tc_html_page *doc = &v->page.html;
    tc_dom_stats stats;
    char elements[32], texts[32];
    const char *mode = doc->doc->mode == TC_QUIRKS          ? "quirks mode"
                       : doc->doc->mode == TC_LIMITED_QUIRKS ? "limited-quirks mode"
                                                             : "standards mode";
    size_t n;

    tc_dom_count(&doc->doc->node, &stats);
    with_commas(stats.elements, elements, sizeof elements);
    with_commas(stats.texts, texts, sizeof texts);
    snprintf(v->info, sizeof v->info, "Document tree: %s elements, %s text nodes; %s (%s); %s", elements, texts,
             tc_encoding_name(doc->encoding), encoding_source_text(doc->encoding_source), mode);
    tc_log("%s; depth %u", v->info, (unsigned)stats.max_depth);
    if (!tc_encoding_supported(doc->encoding)) {
        n = strlen(v->info);
        snprintf(v->info + n, sizeof v->info - n, ". Tachyon can't decode %s yet, so some text will look wrong.",
                 tc_encoding_name(doc->encoding));
    }
}

/* Get the tree or source ready for the mode. */
static int prepare_mode(view *v)
{
    v->top_line = 0;
    v->selected = -1;
    switch (display_of(v)) {
    case D_SOURCE:
        if (!v->is_text)
            return fill_source(v, v->head, v->head_len, v->page.html.text.data ? v->page.html.text.data : "",
                               v->page.html.text.len);
        return fill_source(v, v->head, v->head_len, v->text_body ? v->text_body : "", v->text_len);
    case D_TREE:
        if (!v->has_outline) {
            if (tc_outline_init(&v->outline, &v->page.html.doc->node) != TC_OK)
                return TC_ERR_NOMEM;
            v->has_outline = 1;
            build_info(v);
        }
        return TC_OK;
    default:
        return TC_OK;
    }
}

static char *copy_mem(const char *s, size_t len)
{
    char *p = malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}

int tw_view_show_page(HWND hwnd, const char *head, size_t head_len, tc_page *page)
{
    view *v = get_view(hwnd);
    int w, h;

    if (!v)
        return TC_ERR_INVALID;
    clear_document(v);
    v->page = *page;
    memset(page, 0, sizeof *page);
    v->has_page = 1;
    v->image_relayout = 0;
    {
        /* Decoded pictures may use an eighth of the memory, within reason. */
        MEMORYSTATUS mem;
        size_t budget;
        mem.dwLength = sizeof mem;
        GlobalMemoryStatus(&mem);
        budget = mem.dwTotalPhys / 8;
        tc_page_set_image_memory(&v->page, TC_CLAMP(budget, (size_t)8 << 20, (size_t)64 << 20));
    }
    snprintf(v->url, sizeof v->url, "%s", v->page.url.href ? v->page.url.href : "");
    v->head = copy_mem(head ? head : "", head ? head_len : 0);
    v->head_len = head ? head_len : 0;
    tw_view_page_size(hwnd, &w, &h);
    if (!v->page.layout || v->page.width != (float)w) {
        if (tc_page_layout(&v->page, v->fonts, (float)w, (float)h) != TC_OK) {
            tw_view_show_message(hwnd, v->url, "Out of memory", "This page is too big to show.", NULL);
            return TC_ERR_NOMEM;
        }
    }
    tc_layout_size(v->page.layout, &v->doc_w, &v->doc_h);
    if (prepare_mode(v) != TC_OK) {
        tw_view_show_message(hwnd, v->url, "Out of memory", "This page is too big to show.", NULL);
        return TC_ERR_NOMEM;
    }
    finish(v);
    return TC_OK;
}

int tw_view_show_html(HWND hwnd, const char *url, const char *html)
{
    tc_html_page parsed;
    tc_page page;
    int err = tc_html_parse_page(html, strlen(html), "utf-8", NULL, &parsed);
    if (!err)
        err = tc_page_init(&page, url, &parsed);
    else
        tc_html_page_free(&parsed);
    if (!err)
        err = tw_view_show_page(hwnd, NULL, 0, &page);
    if (err)
        tc_log("couldn't show %s: %s", url, tc_err_text(err));
    return err;
}

int tw_view_show_source(HWND hwnd, const char *url, const char *head, size_t head_len, const char *body,
                        size_t body_len)
{
    view *v = get_view(hwnd);

    if (!v)
        return TC_ERR_INVALID;
    clear_document(v);
    snprintf(v->url, sizeof v->url, "%s", url ? url : "");
    v->is_text = 1;
    v->head = copy_mem(head, head_len);
    v->head_len = head_len;
    v->text_body = copy_mem(body, body_len);
    v->text_len = body_len;
    if (!v->head || !v->text_body || prepare_mode(v) != TC_OK) {
        tw_view_show_message(hwnd, url, "Out of memory", "This page is too big to show.", NULL);
        return TC_ERR_NOMEM;
    }
    finish(v);
    return TC_OK;
}

int tw_view_set_mode(HWND hwnd, tw_view_mode mode)
{
    view *v = get_view(hwnd);
    if (!v)
        return 0;
    v->mode = mode;
    if (v->has_page && !v->is_text) {
        if (prepare_mode(v) != TC_OK)
            v->mode = TW_MODE_PAGE;
        if (display_of(v) == D_PAGE && v->page.width != (float)v->width && v->width > 0)
            relayout(v, 0);
        finish(v);
        return 1;
    }
    return 0;
}

int tw_view_scroll_to_fragment(HWND hwnd, const char *fragment)
{
    view *v = get_view(hwnd);
    float x, y;
    if (!v || display_of(v) != D_PAGE)
        return 0;
    if (!fragment || !*fragment || strcmp(fragment, "top") == 0) {
        scroll_page_to(v, 0, 0);
        return 1;
    }
    if (!tc_page_fragment(&v->page, fragment, &x, &y))
        return 0;
    scroll_page_to(v, 0, (int)y);
    return 1;
}

const char *tw_view_url(HWND hwnd)
{
    view *v = get_view(hwnd);
    return v && v->has_page ? v->url : "";
}

void tw_view_set_warning(HWND hwnd, const char *warning)
{
    view *v = get_view(hwnd);
    if (v)
        snprintf(v->warning, sizeof v->warning, "%s", warning ? warning : "");
}

int tw_view_paint_count(HWND hwnd)
{
    view *v = get_view(hwnd);
    return v ? v->paint_count : 0;
}

/* ---- Built-in pages ---- */

/* Append text with &, <, > and " escaped. */
static void escaped(tc_buf *out, const char *s)
{
    for (; s && *s; s++)
        switch (*s) {
        case '&': tc_buf_append_str(out, "&amp;"); break;
        case '<': tc_buf_append_str(out, "&lt;"); break;
        case '>': tc_buf_append_str(out, "&gt;"); break;
        case '"': tc_buf_append_str(out, "&quot;"); break;
        default: tc_buf_append_char(out, *s); break;
        }
}

/* The look of Tachyon's own pages. */
static const char page_style[] =
    "<style>"
    "body { margin: 0; font: 15px Arial, sans-serif; background: white; color: black; }"
    ".banner { background: #000080; color: white; padding: 10px 24px 14px; border-bottom: 4px solid #0000c0; }"
    ".logo { font: italic bold 64px 'Times New Roman', serif; margin: 0; }"
    ".tag { font: italic 20px Arial, sans-serif; color: #ffffa0; margin: 0 0 0 4px; }"
    ".banner h1 { font: bold 22px Arial, sans-serif; margin: 6px 0 0; }"
    ".main { padding: 6px 24px 16px; }"
    "h2 { color: #000080; font-size: 18px; margin: 18px 0 6px; }"
    ".url { color: #000080; font-weight: bold; font-size: 16px; margin: 16px 0 8px; }"
    ".detail { color: #606060; font-size: 12px; }"
    ".warning { background: #ffffc0; color: #800000; padding: 8px 10px; margin: 16px 0 0; border: 1px solid #d0d080; }"
    ".footer { color: gray; font-size: 12px; margin-top: 20px; }"
    "a { color: #0000ee; }"
    "</style>";

void tw_view_show_home(HWND hwnd)
{
    view *v = get_view(hwnd);
    tc_buf html;
    if (!v)
        return;
    tc_buf_init(&html);
    tc_buf_printf(&html,
                  "<!DOCTYPE html><html><head><meta charset=utf-8><title>Welcome to Tachyon</title>%s</head><body>"
                  "<div class=banner><p class=logo>Tachyon</p><p class=tag>The modern web, sent back to 1995.</p>"
                  "</div><div class=main>",
                  page_style);
    if (v->warning[0]) {
        tc_buf_append_str(&html, "<p class=warning>");
        escaped(&html, v->warning);
        tc_buf_append_str(&html, "</p>");
    }
    tc_buf_printf(&html,
                  "<h2>Sprint 3: First Light</h2>"
                  "<p>Tachyon draws pages itself now: style sheets, fonts, layout and painting, all its own. Try "
                  "<a href=\"https://en.wikipedia.org/wiki/Windows_95\">Windows 95 on Wikipedia</a> or "
                  "<a href=\"https://example.com/\">example.com</a>. View &gt; Document Tree and View &gt; Page "
                  "Source (Ctrl+U) show what's underneath.</p>"
                  "<p>Tables, floats and positioned boxes are laid out one after another for now, and pictures "
                  "show as frames with their descriptions.</p>"
                  "<h2>Coming next</h2><ul>"
                  "<li>Sprint 4: images and fonts</li><li>Sprint 5: JavaScript</li>"
                  "<li>Sprint 6: forms, cookies and navigation</li><li>Sprint 7: modern layout</li></ul>"
                  "<p class=footer>%s %s &bull; GNU GPL v3 &bull; <a href=\"%s\">%s</a> &bull; "
                  "<a href=\"about:fonts\">Fonts</a></p></div></body></html>",
                  TC_NAME, TC_VERSION, TC_HOMEPAGE, TC_HOMEPAGE);
    tw_view_show_html(hwnd, "about:tachyon", html.data ? tc_buf_cstr(&html) : "");
    tc_buf_free(&html);
}

void tw_view_show_blank(HWND hwnd)
{
    tw_view_show_html(hwnd, "about:blank", "<!DOCTYPE html><title>about:blank</title>");
}

void tw_view_show_message(HWND hwnd, const char *url, const char *heading, const char *text, const char *detail)
{
    tc_buf html;
    tc_buf_init(&html);
    tc_buf_printf(&html, "<!DOCTYPE html><html><head><meta charset=utf-8><title>");
    escaped(&html, heading);
    tc_buf_printf(&html, "</title>%s</head><body><div class=banner><h1>", page_style);
    escaped(&html, heading);
    tc_buf_append_str(&html, "</h1></div><div class=main>");
    if (url && *url) {
        tc_buf_append_str(&html, "<p class=url>");
        escaped(&html, url);
        tc_buf_append_str(&html, "</p>");
    }
    if (text) {
        tc_buf_append_str(&html, "<p>");
        escaped(&html, text);
        tc_buf_append_str(&html, "</p>");
    }
    if (detail) {
        tc_buf_append_str(&html, "<p class=detail>");
        escaped(&html, detail);
        tc_buf_append_str(&html, "</p>");
    }
    tc_buf_append_str(&html, "</div></body></html>");
    tw_view_show_html(hwnd, url && *url ? url : "about:blank", html.data ? tc_buf_cstr(&html) : "");
    tc_buf_free(&html);
}

void tw_view_show_fonts(HWND hwnd)
{
    view *v = get_view(hwnd);
    tc_buf html;
    size_t i, n;
    if (!v)
        return;
    n = v->fonts ? tc_font_set_count(v->fonts) : 0;
    tc_buf_init(&html);
    tc_buf_printf(&html,
                  "<!DOCTYPE html><html><head><meta charset=utf-8><title>Fonts</title>%s"
                  "<style>td { padding: 2px 8px 2px 0; } .sample { font-size: 18px; }</style></head><body>"
                  "<div class=banner><h1>Fonts</h1></div><div class=main><p>Tachyon found %u TrueType font faces "
                  "(decision 27). Pages asking for serif get Times New Roman, sans-serif Arial and monospace "
                  "Courier New.</p><table>",
                  page_style, (unsigned)n);
    for (i = 0; i < n; i++) {
        int weight, italic;
        const char *family = tc_font_set_face(v->fonts, i, &weight, &italic);
        tc_buf_append_str(&html, "<tr><td>");
        escaped(&html, family);
        tc_buf_printf(&html, "</td><td>%d%s</td><td class=sample style=\"font-family: '", weight,
                      italic ? " italic" : "");
        escaped(&html, family);
        tc_buf_printf(&html, "'; font-weight: %d; font-style: %s\">The quick brown fox jumps over the lazy dog"
                             "</td></tr>",
                      weight, italic ? "italic" : "normal");
    }
    tc_buf_append_str(&html, "</table></div></body></html>");
    tw_view_show_html(hwnd, "about:fonts", html.data ? tc_buf_cstr(&html) : "");
    tc_buf_free(&html);
}

void tw_view_force_dither(int on)
{
    force_dither = on;
}

int tw_view_realize_palette(HWND hwnd)
{
    view *v = get_view(hwnd);
    HPALETTE old;
    UINT changed;
    HDC dc;
    if (!v || !v->palette_display || !v->palette)
        return 0;
    dc = GetDC(hwnd);
    old = SelectPalette(dc, v->palette, FALSE);
    changed = RealizePalette(dc);
    SelectPalette(dc, old, TRUE);
    ReleaseDC(hwnd, dc);
    if (changed)
        InvalidateRect(hwnd, NULL, FALSE);
    return changed != 0;
}

void tw_view_display_changed(HWND hwnd)
{
    view *v = get_view(hwnd);
    if (!v)
        return;
    free_backbuffer(v); /* made again for the new colour depth */
    InvalidateRect(hwnd, NULL, FALSE);
}

const tc_page *tw_view_page(HWND hwnd)
{
    view *v = get_view(hwnd);
    return v && v->has_page ? &v->page : NULL;
}

void tw_view_image_arrived(HWND hwnd, size_t index, tc_image *img)
{
    view *v = get_view(hwnd);
    if (!v || !v->has_page) {
        tc_image_free(img);
        return;
    }
    v->image_relayout |= tc_page_image_arrived(&v->page, index, img);
    if (!v->image_timer) /* setting it again would put it off */
        v->image_timer = SetTimer(hwnd, IMAGE_TIMER, IMAGE_MS, NULL) != 0;
}

void tw_view_font_arrived(HWND hwnd, size_t index, const void *data, size_t len)
{
    view *v = get_view(hwnd);
    if (!v || !v->has_page)
        return;
    v->image_relayout |= tc_page_font_arrived(&v->page, index, data, len);
    if (!v->image_timer)
        v->image_timer = SetTimer(hwnd, IMAGE_TIMER, IMAGE_MS, NULL) != 0;
}
