#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tc_html_parser.h"
#include "tc_layout.h"
#include "tc_test.h"

void tc_suite_layout(void);

/* With no fonts, text is set in the box font, whose sizes are fixed
   fractions of the font size: at 10px each character is 6px wide, a space
   3px, and a line (line-height: normal) 10px tall. So these tests don't
   depend on the fonts installed. */

#define CHECK_PX(actual, expected) \
    do { \
        double tc_a_ = (actual), tc_e_ = (expected); \
        tc_test_checks++; \
        if (fabs(tc_a_ - tc_e_) > 0.01) { \
            char tc_msg_[160]; \
            snprintf(tc_msg_, sizeof tc_msg_, "%s == %g, expected %g", #actual, tc_a_, tc_e_); \
            tc_test_fail(__FILE__, __LINE__, tc_msg_); \
        } \
    } while (0)

typedef struct laid {
    tc_document *doc;
    tc_style_source *sources;
    size_t count;
    tc_styled *styled;
    tc_font_set *fonts;
    tc_layout *layout;
} laid;

static const char prologue[] = "<!DOCTYPE html><style>body { margin: 0; font: 10px x }</style>";

static int lay_out(laid *l, const char *body, float width)
{
    tc_buf html;
    tc_media_env env;
    int err;
    memset(l, 0, sizeof *l);
    tc_buf_init(&html);
    tc_buf_append(&html, prologue, sizeof prologue - 1);
    tc_buf_append(&html, body, strlen(body));
    err = tc_html_parse(tc_buf_cstr(&html), html.len, NULL, &l->doc);
    tc_buf_free(&html);
    if (!err)
        err = tc_style_sources(l->doc, &l->sources, &l->count);
    env.width = env.device_width = width;
    env.height = env.device_height = 300;
    env.color_bits = 8;
    if (!err)
        err = tc_style_document(l->doc, l->sources, l->count, &env, NULL, &l->styled);
    l->fonts = tc_font_set_new();
    if (!err)
        err = l->fonts ? tc_layout_document(l->doc, l->styled, l->fonts, width, 300, &l->layout) : TC_ERR_NOMEM;
    return err;
}

/* Checks without stringifying the (long) HTML. */
static int setup_ok(int err, int line)
{
    tc_test_checks++;
    if (err)
        tc_test_fail(__FILE__, line, "setting up the page failed");
    return err;
}
#define LAY_OUT(...) setup_ok(lay_out(__VA_ARGS__), __LINE__)

static void laid_free(laid *l)
{
    tc_layout_free(l->layout);
    tc_font_set_free(l->fonts);
    tc_styled_free(l->styled);
    tc_style_sources_free(l->sources, l->count);
    tc_document_free(l->doc);
}

static const tc_element *by_id(const laid *l, const char *id)
{
    const tc_node *n;
    for (n = tc_dom_next(&l->doc->node, &l->doc->node); n; n = tc_dom_next(&l->doc->node, n)) {
        const char *v;
        if (n->type == TC_NODE_ELEMENT && (v = tc_dom_get_attr((const tc_element *)n, "id")) != NULL &&
            strcmp(v, id) == 0)
            return (const tc_element *)n;
    }
    return NULL;
}

static const tc_box *find_box(const tc_box *b, const tc_element *el)
{
    const tc_box *c, *found;
    if (b->element == el)
        return b;
    for (c = b->first; c; c = c->next)
        if ((found = find_box(c, el)) != NULL)
            return found;
    return NULL;
}

/* The element's first box; a dummy with everything -1 if it has none, so
   checks fail rather than crash. */
static const tc_box *box_of(const laid *l, const char *id)
{
    static tc_box none;
    const tc_element *el = by_id(l, id);
    const tc_box *b = el && l->layout ? find_box(tc_layout_root(l->layout), el) : NULL;
    if (!b) {
        char msg[80];
        snprintf(msg, sizeof msg, "no box for #%s", id);
        tc_test_fail(__FILE__, __LINE__, msg);
        none.x = none.y = none.w = none.h = -1;
        return &none;
    }
    return b;
}

/* Where an element's box is in the document. */
static void position_of(const laid *l, const char *id, float *x, float *y)
{
    *x = *y = -1;
    TC_CHECK(tc_layout_find(l->layout, by_id(l, id), x, y));
}

static void test_block_box_model(void)
{
    laid l;
    const tc_box *b;
    float x, y;
    LAY_OUT(&l,
                         "<div id=a style='margin: 10px 20px; padding: 5px; border: 2px solid; height: 50px'></div>"
                         "<div id=b style='width: 50%; padding: 0 10px'></div>"
                         "<div id=c style='width: 100px; margin: 0 auto'></div>"
                         "<div id=d style='width: 50%; padding: 10px; border: 5px solid; box-sizing: border-box'></div>"
                         "<div id=e style='width: 100px; margin-left: auto; margin-right: 30px'></div>",
                         400);
    b = box_of(&l, "a");
    CHECK_PX(b->x, 20);
    CHECK_PX(b->y, 0); /* its margin collapses through body's */
    position_of(&l, "a", &x, &y);
    CHECK_PX(y, 10);
    CHECK_PX(b->w, 360);
    CHECK_PX(b->h, 64);
    CHECK_PX(b->padding[0], 5);
    CHECK_PX(b->border[3], 2);
    b = box_of(&l, "b");
    CHECK_PX(b->w, 220); /* 50% of 400, plus padding */
    b = box_of(&l, "c");
    CHECK_PX(b->x, 150);
    CHECK_PX(b->margin[1], 150);
    b = box_of(&l, "d");
    CHECK_PX(b->w, 200);
    b = box_of(&l, "e");
    CHECK_PX(b->x, 270);
    laid_free(&l);
}

static void test_margin_collapsing(void)
{
    laid l;
    float x, y;
    LAY_OUT(&l,
                         "<div id=a style='margin-bottom: 30px; height: 10px'></div>"
                         "<div id=b style='margin-top: 20px; height: 10px'></div>"
                         /* Positive and negative: the largest of each, added. */
                         "<div id=c style='margin-bottom: 30px; height: 10px'></div>"
                         "<div id=d style='margin-top: -10px; height: 10px'></div>"
                         /* A first child's margin collapses through its parent's. */
                         "<div id=p style='margin-top: 10px'><div id=pc style='margin-top: 25px; height: 5px'></div></div>"
                         /* Not through a border. */
                         "<div id=q style='border-top: 1px solid'><div id=qc style='margin-top: 25px; height: 5px'></div></div>"
                         /* An empty block's margins collapse through it. */
                         "<div id=r style='height: 10px'></div><div id=e style='margin: 15px 0'></div>"
                         "<div id=s style='margin-top: 5px; height: 10px'></div>",
                         400);
    position_of(&l, "a", &x, &y);
    CHECK_PX(y, 0);
    position_of(&l, "b", &x, &y);
    CHECK_PX(y, 40); /* 10 + max(30, 20) */
    position_of(&l, "d", &x, &y);
    CHECK_PX(y, 80); /* c at 50, then 30 - 10 */
    position_of(&l, "p", &x, &y);
    CHECK_PX(y, 90 + 25);
    position_of(&l, "pc", &x, &y);
    CHECK_PX(y, 115);
    position_of(&l, "q", &x, &y);
    CHECK_PX(y, 120);
    position_of(&l, "qc", &x, &y);
    CHECK_PX(y, 120 + 1 + 25);
    position_of(&l, "s", &x, &y);
    CHECK_PX(y, 161 + 15); /* r at 151; e's 15px margins and s's 5px collapse */
    laid_free(&l);
}

/* The frags of a block container's lines, by type. */
static uint32_t count_frags(const tc_box *b, int type, float *first_x)
{
    uint32_t i, n = 0;
    *first_x = -1;
    for (i = 0; i < b->nfrags; i++)
        if (b->frags[i].type == type) {
            if (!n)
                *first_x = b->frags[i].x;
            n++;
        }
    return n;
}

static void test_lines(void)
{
    laid l;
    const tc_box *b;
    float x;
    LAY_OUT(&l,
                         /* Three 5-letter words fit in 100px (30 + 3 + 30 + 3 + 30), the fourth doesn't. */
                         "<div id=a style='width: 100px'>aaaaa aaaaa aaaaa aaaaa</div>"
                         "<div id=b style='width: 100px; text-align: center'>aaaaa</div>"
                         "<div id=c style='width: 100px; text-align: right'>aaaaa</div>"
                         "<div id=d style='width: 100px; line-height: 25px'>aaaaa aaaaa aaaaa aaaaa</div>"
                         "<div id=e style='width: 20px'>aaaaaaaaaa</div>"
                         "<div id=f style='width: 50px; white-space: nowrap'>aaaaa aaaaa aaaaa</div>"
                         "<div id=g style='width: 100px'>aa<br>aa<br><br>aa</div>"
                         "<div id=h style='width: 100px; white-space: pre'>a\n\nb</div>"
                         "<div id=i style='width: 100px; padding-left: 7px; text-indent: 10px'>aaaaa</div>",
                         400);
    b = box_of(&l, "a");
    CHECK_PX(b->h, 20);
    TC_CHECK(b->has_lines);
    b = box_of(&l, "b");
    TC_CHECK_INT(count_frags(b, TC_FRAG_TEXT, &x), 1);
    CHECK_PX(x, 35);
    b = box_of(&l, "c");
    TC_CHECK_INT(count_frags(b, TC_FRAG_TEXT, &x), 1);
    CHECK_PX(x, 70);
    CHECK_PX(box_of(&l, "d")->h, 50);
    CHECK_PX(box_of(&l, "e")->h, 10); /* a word too long for its line overflows */
    CHECK_PX(box_of(&l, "f")->h, 10);
    CHECK_PX(box_of(&l, "g")->h, 40);
    CHECK_PX(box_of(&l, "h")->h, 30);
    b = box_of(&l, "i");
    TC_CHECK_INT(count_frags(b, TC_FRAG_TEXT, &x), 1);
    CHECK_PX(x, 17);
    laid_free(&l);
}

static void test_inline_blocks_and_lists(void)
{
    laid l;
    const tc_box *b;
    LAY_OUT(&l,
                         "<div>aa <span id=s style='display: inline-block; padding: 0 2px'>aaaaa aa</span></div>"
                         "<div id=f style='display: inline-block; width: 40px; height: 20px'></div>"
                         "<ol><li>one<li id=two>two<li value=7>seven<li id=eight>eight</ol>"
                         "<div id=n style='display: none'>gone</div>",
                         400);
    b = box_of(&l, "s");
    TC_CHECK_INT(b->type, TC_BOX_INLINE_BLOCK);
    CHECK_PX(b->w, 30 + 3 + 12 + 4); /* shrink-to-fit */
    CHECK_PX(b->x, 15);
    b = box_of(&l, "f");
    CHECK_PX(b->w, 40);
    CHECK_PX(b->h, 20);
    TC_CHECK_INT(box_of(&l, "two")->number, 2);
    TC_CHECK_INT(box_of(&l, "eight")->number, 8);
    TC_CHECK(!tc_layout_find(l.layout, by_id(&l, "n"), NULL, NULL));
    laid_free(&l);
}

static void test_hit_testing_and_size(void)
{
    laid l;
    float w, h;
    LAY_OUT(&l,
                         "<div id=a style='height: 100px'><span id=s>aaaaa</span></div>"
                         "<div id=b style='height: 100px; margin-left: 50px'></div>"
                         "<div style='height: 1000px'></div>",
                         400);
    TC_CHECK(tc_layout_hit(l.layout, 5, 5) == by_id(&l, "s"));
    TC_CHECK(tc_layout_hit(l.layout, 200, 50) == by_id(&l, "a"));
    TC_CHECK(tc_layout_hit(l.layout, 60, 150) == by_id(&l, "b"));
    TC_CHECK(tc_layout_hit(l.layout, 10, 150) != by_id(&l, "b"));
    tc_layout_size(l.layout, &w, &h);
    CHECK_PX(w, 400);
    CHECK_PX(h, 1200);
    laid_free(&l);

    /* A short document is the viewport's size. */
    LAY_OUT(&l, "<p>a", 400);
    tc_layout_size(l.layout, &w, &h);
    CHECK_PX(h, 300);
    laid_free(&l);
}

static void test_painting(void)
{
    laid l;
    tc_surface s;
    LAY_OUT(&l,
                         "<div style='height: 20px; background: #f00'></div>"
                         "<div style='height: 20px; border-top: 4px solid #00f; background: #0f0'></div>",
                         100);
    TC_CHECK_INT(tc_surface_init(&s, 100, 50), TC_OK);
    tc_paint(l.layout, &s, tc_rect_make(0, 0, 100, 50), 0, 0);
    TC_CHECK_INT(tc_surface_get(&s, 50, 10) & 0xFFFFFF, 0xFF0000);
    TC_CHECK_INT(tc_surface_get(&s, 50, 21) & 0xFFFFFF, 0x0000FF);
    TC_CHECK_INT(tc_surface_get(&s, 50, 30) & 0xFFFFFF, 0x00FF00);
    TC_CHECK_INT(tc_surface_get(&s, 50, 49) & 0xFFFFFF, 0xFFFFFF); /* the canvas */
    /* Scrolled down 20px, painting all but the top 10px. */
    tc_surface_fill(&s, tc_rect_make(0, 0, 100, 50), TC_RGB(1, 2, 3));
    tc_paint(l.layout, &s, tc_rect_make(0, 10, 100, 40), 0, 20);
    TC_CHECK_INT(tc_surface_get(&s, 50, 2) & 0xFFFFFF, 0x010203);
    TC_CHECK_INT(tc_surface_get(&s, 50, 12) & 0xFFFFFF, 0x00FF00);
    TC_CHECK_INT(tc_surface_get(&s, 50, 30) & 0xFFFFFF, 0xFFFFFF);
    tc_surface_free(&s);
    laid_free(&l);
}

static void test_deep_and_odd_documents(void)
{
    laid l;
    tc_buf html, dump;
    int i;
    tc_buf_init(&html);
    for (i = 0; i < 2000; i++)
        tc_buf_append(&html, "<div><span>", 11);
    tc_buf_append(&html, "deep", 4);
    LAY_OUT(&l, tc_buf_cstr(&html), 400);
    tc_buf_free(&html);
    laid_free(&l);

    LAY_OUT(&l,
                         "<span>a<div>block in inline</div>b</span>"
                         "<table><tr><td>cell</td></tr></table>"
                         "<div style='width: -5px; height: 1e30px; padding: -3px'></div>"
                         "<div style='font-size: 0'>zero</div>"
                         "<img alt=picture><input value=text><select><option>o</select>",
                         0);
    tc_buf_init(&dump);
    TC_CHECK_INT(tc_layout_dump(l.layout, &dump), TC_OK);
    TC_CHECK(strstr(tc_buf_cstr(&dump), "\"inline\"") != NULL);
    tc_buf_free(&dump);
    laid_free(&l);
}

/* Lengths a page can't mean: everything stays finite and paintable. */
static void test_absurd_lengths(void)
{
    laid l;
    tc_surface s;
    float w, h;
    LAY_OUT(&l,
            "<div id=a style='height: 5e52px; width: 1e30%'></div>"
            "<div id=b style='margin-left: -1e38px; padding: calc(1e38px * 1e38)'>x</div>"
            "<p style='font-size: 1e30px; line-height: 1e30'>big</p>"
            "<p style='letter-spacing: -1e30px; word-spacing: 1e30px'>spaced out text</p>"
            "<span style='display: inline-block; width: 3e38px; vertical-align: 1e38%'>x</span>",
            400);
    CHECK_PX(box_of(&l, "a")->h, TC_LENGTH_MAX);
    TC_CHECK(box_of(&l, "b")->x >= -TC_LENGTH_MAX);
    tc_layout_size(l.layout, &w, &h);
    TC_CHECK(w >= 400 && w <= 1e8f && h >= 300 && h <= 1e8f);
    TC_CHECK_INT(tc_surface_init(&s, 50, 50), TC_OK);
    tc_paint(l.layout, &s, tc_rect_make(0, 0, 50, 50), 0, 0);
    tc_paint(l.layout, &s, tc_rect_make(0, 0, 50, 50), w - 50, h - 50);
    tc_paint(l.layout, &s, tc_rect_make(0, 0, 50, 50), 0, TC_LENGTH_MAX);
    tc_surface_free(&s);
    laid_free(&l);
}

void tc_suite_layout(void)
{
    TC_RUN(test_block_box_model);
    TC_RUN(test_margin_collapsing);
    TC_RUN(test_lines);
    TC_RUN(test_inline_blocks_and_lists);
    TC_RUN(test_hit_testing_and_size);
    TC_RUN(test_painting);
    TC_RUN(test_deep_and_odd_documents);
    TC_RUN(test_absurd_lengths);
}
