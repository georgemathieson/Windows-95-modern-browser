/* A libFuzzer target for everything after parsing: CSS parsing, the
 * cascade, the box tree, layout, painting, hit testing and fragments, on
 * arbitrary input. Built with AddressSanitizer and UBSan.
 *
 * The first input byte picks the mode and the viewport width. Even modes:
 * the rest is a page. Odd modes: the rest is a style sheet, applied to a
 * fixed page with a bit of everything. Text is set in the Liberation fonts
 * if they're installed (so glyph code runs too), else the box font. The
 * painted pixels are animated and dithered as the browser would. */
#include <stdlib.h>
#include <string.h>

#include "tc_dither.h"
#include "tc_page.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static const char fixed_page[] =
    "<!DOCTYPE html><html lang=en><head><title>t</title><style id=fuzz></style></head><body class=b>"
    "<img class=anim src='data:image/gif;base64,"
    "R0lGODlhBAAEAPAAAP8AAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQECgAAACH/C0ltYWdlTWFnaWNrDmdhbW1hPTAuNDU0"
    "NTQ1ACwAAAAABAAEAAACBISPCQUAIfkEBBQAAAAh/wtJbWFnZU1hZ2ljaw5nYW1tYT0wLjQ1NDU0NQAsAAAAAAIAAgCAAP8A"
    "AAAAAgKEUQAh+QQEHgAAACH/C0ltYWdlTWFnaWNrDmdhbW1hPTAuNDU0NTQ1ACwAAAAABAAEAIAAAP8AAAACBISPCQUAOw=='>"
    "<img src=later.png>"
    "<h1 id=top>Heading <small>small</small></h1>"
    "<p>Some <a href='#x'>link</a> text with <b>bold</b>, <i>italic</i>, <code>code</code>"
    " and <span style='display:inline-block;padding:2px'>an inline block</span>.<br>After a break.</p>"
    "<div class='a b'><div id=x>nested <em>block</em><div>deeper</div> after</div></div>"
    "<ul><li>one<li>two<ol start=3 reversed><li>three<li value=-2>four</ol></ul>"
    "<table border=1><caption>c</caption><tr><th>h<td>d</table>"
    "<pre>pre  formatted\n\ttext</pre><blockquote><p>quoted</blockquote>"
    "<form><input value=v><input type=checkbox checked><select><option>o</select>"
    "<textarea>t</textarea><button>b</button></form>"
    "<img alt=alt width=20 height=10><iframe></iframe><hr><center>centred</center>"
    "<img id=pic src='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAQAAAACCAYAAAB/qH1jAAAAIGNIUk0AAHomAACAhAAA+gAAAIDoAAB1MAAA6mAAADqYAAAXcJy6UTwAAAAGYktHRAD/AP8A/6C9p5MAAAAHdElNRQfqChYJAC0Ox+VcAAAAHUlEQVQI1y3EsQEAMAzDIPx5P1eWMrBoYSmg3ysdukELeS2aDPQAAAAASUVORK5CYII='>"
    "<div class=pics style='background: url(data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAQAAAACCAYAAAB/qH1jAAAAIGNIUk0AAHomAACAhAAA+gAAAIDoAAB1MAAA6mAAADqYAAAXcJy6UTwAAAAGYktHRAD/AP8A/6C9p5MAAAAHdElNRQfqChYJAC0Ox+VcAAAAHUlEQVQI1y3EsQEAMAzDIPx5P1eWMrBoYSmg3ysdukELeS2aDPQAAAAASUVORK5CYII=) 3px 50% / 7px repeat-x, "
    "linear-gradient(45deg, red, blue 40%, lime)'>tiled</div>"
    "<div hidden>hidden</div><span dir=rtl>\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D</span> caf\xC3\xA9 \xE2\x80\x94 "
    "\xF0\x9F\x98\x80<details><summary>s</summary>d</details><svg><circle/></svg>"
    "</body></html>";

static tc_font_set *fonts(void)
{
    static tc_font_set *set;
    if (!set) {
        tc_style_init();
        set = tc_font_set_new();
        if (!set)
            abort();
        tc_font_set_add_dir(set, "/usr/share/fonts/truetype/liberation");
    }
    return set;
}

/* The fixed page's style element takes the sheet as its text. */
static int fuzz_sheet(void *ctx, const char *url, tc_buf *body, char *charset, size_t charset_size)
{
    (void)ctx;
    (void)url;
    (void)body;
    (void)charset_size;
    charset[0] = '\0';
    return TC_ERR_INVALID; /* no sheets from the network */
}

static void exercise(tc_page *p, float w)
{
    tc_surface s;
    tc_url url;
    tc_rect dirty;
    size_t order[16];
    float x, y, dw, dh;
    int i;

    tc_page_load_styles(p, fuzz_sheet, NULL, NULL);
    if (tc_page_layout(p, fonts(), w, 200) != TC_OK)
        return;
    tc_layout_size(p->layout, &dw, &dh);
    if (tc_surface_init(&s, 64, 48) == TC_OK) {
        /* The top, somewhere in the middle and the bottom. */
        tc_paint(p->layout, &s, tc_rect_make(0, 0, 64, 48), 0, 0);
        tc_paint(p->layout, &s, tc_rect_make(5, 5, 50, 30), dw / 3, dh / 2);
        tc_paint(p->layout, &s, tc_rect_make(0, 0, 64, 48), 0, dh - 48);
        /* Animations move on and are painted again; a palette display's
           copy is dithered. */
        tc_page_animate(p, 1000, &dirty);
        tc_page_animate(p, 1100, &dirty);
        tc_paint(p->layout, &s, tc_rect_make(dirty.x, dirty.y - (int)(dh - 48), dirty.w, dirty.h), 0, dh - 48);
        {
            static uint8_t index[64 * 48];
            tc_dither(&s, tc_rect_make(0, 0, 64, 48), index, 64, (int)(dw / 3), (int)(dh / 2));
        }
        tc_surface_free(&s);
    }
    tc_page_image_order(p, dh / 3, dh / 2, order, TC_ARRAY_LEN(order));
    for (i = 0; i < 16; i++) {
        x = (float)(i * 37 % 300);
        y = dh * (float)i / 16;
        if (tc_page_link_at(p, x, y, &url))
            tc_url_free(&url);
        tc_page_cursor_at(p, x, y);
    }
    tc_page_fragment(p, "x", &x, &y);
    tc_page_fragment(p, "top", &x, &y);
    {
        tc_buf dump;
        tc_buf_init(&dump);
        tc_layout_dump(p->layout, &dump);
        tc_buf_free(&dump);
    }
    /* Lay out again at another width, as resizing does. */
    tc_page_layout(p, fonts(), w / 2 + 1, 100);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const float widths[] = { 0, 1, 50, 320, 800, 3000, 1e6f, 123.45f };
    tc_html_page html;
    tc_page page;
    unsigned mode;
    float w;

    if (size < 1)
        return 0;
    mode = data[0];
    data++;
    size--;
    w = widths[(mode >> 1) % 8];
    fonts();
    if (mode & 1) {
        /* The sheet, put into the fixed page's <style>. */
        tc_buf text;
        const char *at;
        int err;
        tc_buf_init(&text);
        at = strstr(fixed_page, "</style>");
        err = tc_buf_append(&text, fixed_page, (size_t)(at - fixed_page));
        if (!err)
            err = tc_buf_append(&text, data, size);
        if (!err)
            err = tc_buf_append(&text, at, strlen(at));
        if (!err && tc_html_parse_page(text.data, text.len, "utf-8", NULL, &html) == TC_OK) {
            if (tc_page_init(&page, "http://example.com/dir/page.html#x", &html) == TC_OK)
                exercise(&page, w);
            tc_page_free(&page);
        }
        tc_html_page_free(&html);
        tc_buf_free(&text);
    } else {
        if (tc_html_parse_page(data, size, NULL, NULL, &html) == TC_OK) {
            if (tc_page_init(&page, "http://example.com/", &html) == TC_OK)
                exercise(&page, w);
            tc_page_free(&page);
        }
        tc_html_page_free(&html);
    }
    return 0;
}
