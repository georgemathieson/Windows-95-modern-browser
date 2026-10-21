#include <math.h>
#include <stdlib.h>

#include "tc_html_parser.h"
#include "tc_style_internal.h"
#include "tc_test.h"

void tc_suite_style(void);

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

#define CHECK_COLOR(actual, expected) TC_CHECK_INT((unsigned long)(actual), (unsigned long)(expected))

typedef struct page {
    tc_document *doc;
    tc_style_source *sources;
    size_t count;
    tc_styled *styled;
} page;

static void page_style(page *p, float width)
{
    tc_media_env env;
    env.width = env.device_width = width;
    env.height = env.device_height = 600;
    env.color_bits = 8;
    tc_styled_free(p->styled);
    p->styled = NULL;
    TC_CHECK_INT(tc_style_document(p->doc, p->sources, p->count, &env, "target", &p->styled), TC_OK);
}

static void page_load(page *p, const char *html)
{
    memset(p, 0, sizeof *p);
    TC_CHECK_INT(tc_html_parse(html, strlen(html), NULL, &p->doc), TC_OK);
    TC_CHECK_INT(tc_style_sources(p->doc, &p->sources, &p->count), TC_OK);
    page_style(p, 800);
}

static void page_free(page *p)
{
    tc_styled_free(p->styled);
    tc_style_sources_free(p->sources, p->count);
    tc_document_free(p->doc);
}

static const tc_element *by_id(const page *p, const char *id)
{
    const tc_node *n;
    for (n = tc_dom_next(&p->doc->node, &p->doc->node); n; n = tc_dom_next(&p->doc->node, n)) {
        const char *v;
        if (n->type == TC_NODE_ELEMENT && (v = tc_dom_get_attr((const tc_element *)n, "id")) && !strcmp(v, id))
            return (const tc_element *)n;
    }
    return NULL;
}

static const tc_element_styles *styles(const page *p, const char *id)
{
    return tc_styled_element(p->styled, by_id(p, id));
}

/* The element's style, or the initial style if it has none (so a failed
   test doesn't crash). */
static const tc_style *style(const page *p, const char *id)
{
    static tc_style none;
    const tc_element_styles *s = styles(p, id);
    if (!s || !s->style) {
        char msg[80];
        snprintf(msg, sizeof msg, "no style for #%s", id);
        tc_test_fail(__FILE__, __LINE__, msg);
        return &none;
    }
    return s->style;
}

static const char *family(const tc_style *s, uint32_t i)
{
    return s->font_family && i < s->font_family->count ? s->font_family->names[i] : "";
}

static void test_names(void)
{
    TC_CHECK(tc_prop_names_sorted());
    TC_CHECK_STR(tc_prop_name(P_MARGIN_TOP), "margin-top");
    TC_CHECK(tc_prop_inherited(P_COLOR));
    TC_CHECK(!tc_prop_inherited(P_DISPLAY));
}

static void test_user_agent(void)
{
    page p;
    const tc_style *s;
    page_load(&p, "<!DOCTYPE html><html id=html><head id=head><title>t</title></head><body id=body>"
                  "<h1 id=h1>T</h1><p id=p>a <a id=a href=x>b</a> <code id=code>c</code> <b id=b>d</b></p>"
                  "<pre id=pre>x</pre><pre id=pre2 style='font-family: monospace, monospace'>y</pre>"
                  "<ul><li id=li>1<ul><li id=li2>2</ul></ul><input id=hidden type=hidden style='display:block!important'>"
                  "<blockquote id=bq>q</blockquote><hr id=hr><table id=table><tr><td id=td>c</td></tr></table>"
                  "<div hidden id=hiddendiv><span id=inside>x</span></div><noscript id=noscript>n</noscript>"
                  "</body></html>");
    TC_CHECK_INT(style(&p, "html")->display, TC_DISPLAY_BLOCK);
    TC_CHECK_INT(style(&p, "head")->display, TC_DISPLAY_NONE);
    s = style(&p, "body");
    TC_CHECK_INT(s->display, TC_DISPLAY_BLOCK);
    CHECK_PX(s->margin[0].length.px, 8);
    CHECK_PX(s->margin[3].length.px, 8);
    s = style(&p, "h1");
    CHECK_PX(s->font_size, 32);
    TC_CHECK_INT(s->font_weight, 700);
    CHECK_PX(s->margin[0].length.px, 0.67 * 32);
    s = style(&p, "p");
    CHECK_PX(s->margin[0].length.px, 16);
    CHECK_PX(s->margin[1].length.px, 0);
    TC_CHECK_STR(family(s, 0), "serif");
    s = style(&p, "a");
    CHECK_COLOR(s->color, 0xFF0000EE);
    TC_CHECK_INT(s->decoration_line, TC_DECORATION_UNDERLINE);
    TC_CHECK_INT(s->decorations, TC_DECORATION_UNDERLINE);
    TC_CHECK_INT(s->cursor, TC_CURSOR_POINTER);
    TC_CHECK_INT(s->display, TC_DISPLAY_INLINE);
    /* Monospace text is 13px, unless the family is more than "monospace". */
    CHECK_PX(style(&p, "code")->font_size, 13);
    TC_CHECK_STR(family(style(&p, "code"), 0), "monospace");
    s = style(&p, "pre");
    CHECK_PX(s->font_size, 13);
    TC_CHECK_INT(s->ws_collapse, TC_COLLAPSE_PRESERVE);
    TC_CHECK_INT(s->text_wrap, TC_WRAP_MODE_NOWRAP);
    CHECK_PX(style(&p, "pre2")->font_size, 16);
    TC_CHECK_INT(style(&p, "b")->font_weight, 700);
    s = style(&p, "li");
    TC_CHECK_INT(s->display, TC_DISPLAY_LIST_ITEM);
    TC_CHECK_INT(s->list_style_type, TC_LIST_DISC);
    TC_CHECK(styles(&p, "li")->marker != NULL);
    TC_CHECK_INT(style(&p, "li2")->list_style_type, TC_LIST_CIRCLE);
    /* The user agent's !important beats the author's. */
    TC_CHECK_INT(style(&p, "hidden")->display, TC_DISPLAY_NONE);
    s = style(&p, "bq");
    CHECK_PX(s->margin[1].length.px, 40);
    CHECK_PX(s->margin[3].length.px, 40);
    s = style(&p, "hr");
    TC_CHECK_INT(s->border_style[0], TC_BORDER_INSET);
    CHECK_PX(s->border_width[0], 1);
    CHECK_COLOR(s->color, 0xFF808080);
    CHECK_COLOR(s->border_color[0], 0xFF808080);
    TC_CHECK_INT(s->margin[1].kind, TC_SIZE_AUTO);
    TC_CHECK_INT(style(&p, "table")->display, TC_DISPLAY_TABLE);
    TC_CHECK_INT(style(&p, "td")->display, TC_DISPLAY_TABLE_CELL);
    CHECK_PX(style(&p, "td")->padding[0].px, 1);
    /* Nothing inside display: none is styled. */
    TC_CHECK_INT(style(&p, "hiddendiv")->display, TC_DISPLAY_NONE);
    TC_CHECK(styles(&p, "inside") == NULL);
    /* Scripting is off, so noscript shows. */
    TC_CHECK_INT(style(&p, "noscript")->display, TC_DISPLAY_INLINE);
    page_free(&p);
}

static void test_cascade(void)
{
    page p;
    page_load(&p, "<!DOCTYPE html><style>"
                  "p { color: red } .c { color: green } #i { color: blue } p { color: yellow }"
                  "div p { color: black } .imp { color: red !important } #i2 { color: blue }"
                  "#s { color: red !important }"
                  "</style>"
                  "<p id=tag>a</p><p id=cls class=c>b</p><p id=i class=c>c</p>"
                  "<p id=i2 class=imp>d</p><p id=attr class=c style='color: lime'>e</p>"
                  "<p id=s style='color: lime'>f</p><p id=s2 style='color: lime !important' class=imp>g</p>"
                  "<div><p id=desc>h</p></div>");
    CHECK_COLOR(style(&p, "tag")->color, 0xFFFFFF00);    /* the later of two equal rules */
    CHECK_COLOR(style(&p, "cls")->color, 0xFF008000);    /* class beats type */
    CHECK_COLOR(style(&p, "i")->color, 0xFF0000FF);      /* id beats class */
    CHECK_COLOR(style(&p, "i2")->color, 0xFFFF0000);     /* !important beats id */
    CHECK_COLOR(style(&p, "attr")->color, 0xFF00FF00);   /* the style attribute beats the sheet */
    CHECK_COLOR(style(&p, "s")->color, 0xFFFF0000);      /* !important beats the style attribute */
    CHECK_COLOR(style(&p, "s2")->color, 0xFF00FF00);     /* ...unless that's !important too */
    CHECK_COLOR(style(&p, "desc")->color, 0xFF000000);   /* two types beat one */
    page_free(&p);
}

static void test_inheritance(void)
{
    page p;
    const tc_style *s;
    page_load(&p, "<!DOCTYPE html><style>"
                  "#outer { color: red; border: 2px solid; font-size: 20px; line-height: 1.5; padding: 3px }"
                  "#bc { border-color: inherit; border-style: solid } #init { color: initial }"
                  "#unset { color: unset; padding: unset } #inh { padding: inherit }"
                  "#lh { line-height: 2em } #lhc { font-size: 10px }"
                  "</style><div id=outer><span id=in>a</span><span id=bc>b</span>"
                  "<span id=init>c</span><span id=unset>d</span><span id=inh>e</span>"
                  "<div id=lh><span id=lhc>x</span></div></div>");
    s = style(&p, "in");
    CHECK_COLOR(s->color, 0xFFFF0000);
    CHECK_PX(s->font_size, 20);
    CHECK_PX(s->border_width[0], 0);  /* border isn't inherited */
    TC_CHECK_INT(s->line_height_kind, TC_LINE_HEIGHT_NUMBER);
    CHECK_PX(s->line_height, 1.5);
    CHECK_COLOR(style(&p, "outer")->border_color[0], 0xFFFF0000); /* currentcolor */
    CHECK_COLOR(style(&p, "bc")->border_color[2], 0xFFFF0000);
    CHECK_COLOR(style(&p, "init")->color, 0xFF000000);
    CHECK_COLOR(style(&p, "unset")->color, 0xFFFF0000);
    CHECK_PX(style(&p, "unset")->padding[0].px, 0);
    CHECK_PX(style(&p, "inh")->padding[0].px, 3);
    /* line-height in em is computed where it's declared, then inherited. */
    TC_CHECK_INT(style(&p, "lhc")->line_height_kind, TC_LINE_HEIGHT_PX);
    CHECK_PX(style(&p, "lhc")->line_height, 40);
    page_free(&p);
}

static void test_values(void)
{
    page p;
    const tc_style *s;
    page_load(&p, "<!DOCTYPE html><html style='font-size: 10px'><style>"
                  "#a { width: calc(100% - 20px); height: 50vh; margin: 1em 2rem 3% auto; font-size: 20px;"
                  "     padding: 1in 12pt 0 0; min-width: 0; max-width: 90% }"
                  "#b { font-size: 150%; text-indent: 2em; letter-spacing: 0.1em; word-spacing: normal }"
                  "#c { font: italic bold 12px/1.5 Arial, 'Helvetica Neue', sans-serif }"
                  "#d { font-size: larger } #e { font-weight: bolder } #f { font-weight: lighter }"
                  "#g { width: fit-content; max-height: none; vertical-align: middle; z-index: 3 }"
                  "#h { width: -12px; padding: -1px; vertical-align: 10%; line-height: 20px }"
                  "#i { border: thick double rgb(0 128 0 / 50%); border-left: none }"
                  "#j { margin-inline-start: 5px; padding-block: 1px 2px; inset-inline-end: 3px; inline-size: 7px }"
                  "#k { background: url(x.png) no-repeat center / cover, #abc; list-style: square inside }"
                  "#l { text-decoration: underline dotted red; white-space: pre-wrap; overflow: hidden scroll }"
                  "#m { display: inline flow-root; float: right; text-transform: uppercase; text-align: end }"
                  "#n { overflow-x: visible; overflow-y: scroll; position: absolute; display: inline-table }"
                  "#o { overflow: clip visible }"
                  "</style><div id=a><span id=b><span id=d><b id=e>x</b><b id=f>y</b></span></span></div>"
                  "<p id=c>c</p><div id=g>g</div><div id=h>h</div><div id=i>i</div><div id=j>j</div>"
                  "<ul><li id=k>k</ul><p id=l>l</p><span id=m>m</span><span id=n>n</span><div id=o>o</div>");
    s = style(&p, "a");
    TC_CHECK_INT(s->width.kind, TC_SIZE_LENGTH);
    CHECK_PX(s->width.length.px, -20);
    CHECK_PX(s->width.length.pct, 100);
    CHECK_PX(s->height.length.px, 300);
    CHECK_PX(s->margin[0].length.px, 20);
    CHECK_PX(s->margin[1].length.px, 20);
    CHECK_PX(s->margin[2].length.pct, 3);
    TC_CHECK_INT(s->margin[3].kind, TC_SIZE_AUTO);
    CHECK_PX(s->padding[0].px, 96);
    CHECK_PX(s->padding[1].px, 16);
    TC_CHECK_INT(s->min_width.kind, TC_SIZE_LENGTH);
    CHECK_PX(s->max_width.length.pct, 90);
    s = style(&p, "b");
    CHECK_PX(s->font_size, 30);
    CHECK_PX(s->text_indent.px, 60);
    CHECK_PX(s->letter_spacing, 3);
    CHECK_PX(style(&p, "d")->font_size, 36);
    TC_CHECK_INT(style(&p, "e")->font_weight, 700);
    TC_CHECK_INT(style(&p, "f")->font_weight, 100);
    s = style(&p, "c");
    TC_CHECK_INT(s->font_style, TC_FONT_ITALIC);
    TC_CHECK_INT(s->font_weight, 700);
    CHECK_PX(s->font_size, 12);
    CHECK_PX(s->line_height, 1.5);
    TC_CHECK_INT(s->font_family->count, 3);
    TC_CHECK_STR(family(s, 0), "Arial");
    TC_CHECK_STR(family(s, 1), "Helvetica Neue");
    TC_CHECK_STR(family(s, 2), "sans-serif");
    s = style(&p, "g");
    TC_CHECK_INT(s->width.kind, TC_SIZE_FIT_CONTENT);
    TC_CHECK_INT(s->max_height.kind, TC_SIZE_NONE);
    TC_CHECK_INT(s->vertical_align, TC_VALIGN_MIDDLE);
    TC_CHECK_INT(s->z_auto, 0);
    TC_CHECK_INT(s->z_index, 3);
    TC_CHECK_INT(style(&p, "a")->z_auto, 1);
    s = style(&p, "h");
    TC_CHECK_INT(s->width.kind, TC_SIZE_AUTO);   /* negative widths are invalid */
    CHECK_PX(s->padding[0].px, 0);
    TC_CHECK_INT(s->vertical_align, TC_VALIGN_LENGTH);
    CHECK_PX(s->vertical_align_length.px, 2);
    s = style(&p, "i");
    CHECK_PX(s->border_width[0], 5);
    TC_CHECK_INT(s->border_style[0], TC_BORDER_DOUBLE);
    CHECK_COLOR(s->border_color[0], 0x80008000);
    CHECK_PX(s->border_width[3], 0);
    TC_CHECK_INT(s->border_style[3], TC_BORDER_NONE);
    s = style(&p, "j");
    CHECK_PX(s->margin[3].length.px, 5);
    CHECK_PX(s->padding[0].px, 1);
    CHECK_PX(s->padding[2].px, 2);
    CHECK_PX(s->inset[1].length.px, 3);
    CHECK_PX(s->width.length.px, 7);
    s = style(&p, "k");
    CHECK_COLOR(s->background_color, 0xFFAABBCC);
    TC_CHECK_INT(s->list_style_type, TC_LIST_SQUARE);
    TC_CHECK_INT(s->list_style_position, TC_LIST_INSIDE);
    s = style(&p, "l");
    TC_CHECK_INT(s->decoration_line, TC_DECORATION_UNDERLINE);
    TC_CHECK_INT(s->decoration_style, TC_DECORATION_DOTTED);
    CHECK_COLOR(s->decoration_color, 0xFFFF0000);
    TC_CHECK_INT(s->ws_collapse, TC_COLLAPSE_PRESERVE);
    TC_CHECK_INT(s->text_wrap, TC_WRAP_MODE_WRAP);
    TC_CHECK_INT(s->overflow_x, TC_OVERFLOW_HIDDEN);
    TC_CHECK_INT(s->overflow_y, TC_OVERFLOW_SCROLL);
    s = style(&p, "m");
    TC_CHECK_INT(s->display, TC_DISPLAY_BLOCK);  /* inline-block, blockified by the float */
    TC_CHECK_INT(s->float_, TC_FLOAT_RIGHT);
    TC_CHECK_INT(s->text_transform, TC_TRANSFORM_UPPERCASE);
    TC_CHECK_INT(s->text_align, TC_ALIGN_RIGHT);
    s = style(&p, "n");
    TC_CHECK_INT(s->display, TC_DISPLAY_TABLE);
    TC_CHECK_INT(s->overflow_x, TC_OVERFLOW_AUTO); /* visible with scroll is auto */
    TC_CHECK_INT(s->overflow_y, TC_OVERFLOW_SCROLL);
    s = style(&p, "o");
    TC_CHECK_INT(s->overflow_x, TC_OVERFLOW_CLIP); /* clip and visible go together */
    TC_CHECK_INT(s->overflow_y, TC_OVERFLOW_VISIBLE);
    page_free(&p);
}

static void test_custom_properties(void)
{
    page p;
    const tc_style *s;
    page_load(&p, "<!DOCTYPE html><style>"
                  ":root { --c: red; --size: 10px; --m: 1px 2px; --nested: var(--c); --empty:; }"
                  "#a { color: var(--c); margin-top: var(--size) }"
                  "#b { color: var(--missing, blue); padding: calc(var(--size) * 2) }"
                  "#c { --x: var(--y); --y: var(--x); color: var(--x, green) }"
                  "#d { margin: var(--m) } #e { color: var(--missing) }"
                  "#f { --c: lime } #g { color: var(--nested) } #h { --size: initial; margin-top: var(--size, 7px) }"
                  "#i { padding-left: max(12px, env(safe-area-inset-left)) } #j { color: var(--c) var(--empty) }"
                  "#k { --size: 4px } #k > span { margin-top: var(--size) }"
                  "</style><div style='color: purple'><p id=a>a</p><p id=b>b</p><p id=c>c</p><p id=d>d</p>"
                  "<p id=e>e</p></div><div id=f><p id=g>g</p></div><p id=h>h</p><p id=i>i</p><p id=j>j</p>"
                  "<div id=k><span id=k1>k</span></div>");
    s = style(&p, "a");
    CHECK_COLOR(s->color, 0xFFFF0000);
    CHECK_PX(s->margin[0].length.px, 10);
    s = style(&p, "b");
    CHECK_COLOR(s->color, 0xFF0000FF);
    CHECK_PX(s->padding[0].px, 20);
    CHECK_COLOR(style(&p, "c")->color, 0xFF008000);  /* a cycle: invalid, so the fallback */
    s = style(&p, "d");
    CHECK_PX(s->margin[0].length.px, 1);
    CHECK_PX(s->margin[1].length.px, 2);
    CHECK_PX(s->margin[2].length.px, 1);
    CHECK_COLOR(style(&p, "e")->color, 0xFF800080);  /* invalid at computed-value time: inherits */
    /* Custom properties are inherited, and computed where declared: --nested
       is "red" everywhere, even where --c changes. */
    CHECK_COLOR(style(&p, "g")->color, 0xFFFF0000);
    CHECK_PX(style(&p, "h")->margin[0].length.px, 7);
    CHECK_PX(style(&p, "i")->padding[3].px, 12);
    CHECK_COLOR(style(&p, "j")->color, 0xFFFF0000);
    CHECK_PX(style(&p, "k1")->margin[0].length.px, 4);
    /* Elements that don't change custom properties share their parent's. */
    TC_CHECK(style(&p, "a")->custom == style(&p, "b")->custom);
    TC_CHECK(style(&p, "a")->custom == style(&p, "c")->custom); /* its own are invalid */
    TC_CHECK(style(&p, "a")->custom != style(&p, "g")->custom);
    page_free(&p);
}

static void test_media_and_supports(void)
{
    page p;
    page_load(&p, "<!DOCTYPE html><style>"
                  "@media (min-width: 600px) { #a { color: red } }"
                  "@media (max-width: 599px) { #a { color: blue } }"
                  "@media print { #b { color: red } }"
                  "@supports (display: grid) { #c { color: red } }"
                  "@supports (frobnicate: yes) { #c { color: blue } }"
                  "@supports not (frobnicate: yes) { #d { color: red } }"
                  "@supports (color: red) and (selector(:is(a))) { #e { color: red } }"
                  "@supports (--x: y) or (frobnicate: yes) { #f { color: red } }"
                  "@media screen { @supports (color: red) { @media (min-width: 100px) { #g { color: red } } } }"
                  "</style><style media='(max-width: 500px)'>#h { color: red }</style>"
                  "<p id=a>a</p><p id=b>b</p><p id=c>c</p><p id=d>d</p><p id=e>e</p><p id=f>f</p>"
                  "<p id=g>g</p><p id=h>h</p>");
    CHECK_COLOR(style(&p, "a")->color, 0xFFFF0000);
    CHECK_COLOR(style(&p, "b")->color, 0xFF000000);
    CHECK_COLOR(style(&p, "c")->color, 0xFFFF0000);
    CHECK_COLOR(style(&p, "d")->color, 0xFFFF0000);
    CHECK_COLOR(style(&p, "e")->color, 0xFFFF0000);
    CHECK_COLOR(style(&p, "f")->color, 0xFFFF0000);
    CHECK_COLOR(style(&p, "g")->color, 0xFFFF0000);
    CHECK_COLOR(style(&p, "h")->color, 0xFF000000);
    /* A narrower window: styled again. */
    page_style(&p, 400);
    CHECK_COLOR(style(&p, "a")->color, 0xFF0000FF);
    CHECK_COLOR(style(&p, "h")->color, 0xFFFF0000);
    page_free(&p);
}

static void test_layers(void)
{
    page p;
    page_load(&p, "<!DOCTYPE html><style>"
                  "@layer base, theme;"
                  "@layer theme { #a { color: red } }"
                  "@layer base { #a { color: blue } #b { color: blue } }"
                  "#b { color: green }"
                  "@layer theme { #b { color: red } }"
                  "@layer base { #c { color: red !important } }"
                  "@layer theme { #c { color: blue !important } }"
                  "#c { color: lime !important }"
                  "@layer { #d { color: red } } @layer { #d { color: blue } }"
                  "@layer outer { @layer inner { #e { color: red } } #e { color: blue } }"
                  "@layer x.y { #f { color: red } } @layer x { #f { color: blue } }"
                  "#g { color: green } @layer z { #g#g#g { color: red } }"
                  "</style><p id=a>a</p><p id=b>b</p><p id=c>c</p><p id=d>d</p><p id=e>e</p><p id=f>f</p>"
                  "<p id=g>g</p>");
    CHECK_COLOR(style(&p, "a")->color, 0xFFFF0000);  /* theme comes after base */
    CHECK_COLOR(style(&p, "b")->color, 0xFF008000);  /* unlayered beats layered */
    CHECK_COLOR(style(&p, "c")->color, 0xFFFF0000);  /* !important: the first layer wins */
    CHECK_COLOR(style(&p, "d")->color, 0xFF0000FF);  /* anonymous layers in order */
    CHECK_COLOR(style(&p, "e")->color, 0xFF0000FF);  /* a layer's own rules beat its sublayers' */
    CHECK_COLOR(style(&p, "f")->color, 0xFF0000FF);
    CHECK_COLOR(style(&p, "g")->color, 0xFF008000);  /* layers before specificity */
    page_free(&p);
}

static void test_nesting(void)
{
    page p;
    page_load(&p, "<!DOCTYPE html><style>"
                  ".card { color: red; & p { color: blue } .x & { color: green } > span { color: purple }"
                  "  &:hover { color: black } @media (min-width: 100px) { & em { color: lime } }"
                  "  .deep { b { color: navy } } }"
                  ".after { p { color: red } color: blue }"
                  "</style><div class=card id=card><p id=p>p</p><span id=span>s</span><em id=em>e</em>"
                  "<div class=deep><b id=b>b</b></div></div><div class=x><div class=card id=card2>c</div></div>"
                  "<div class=after id=after>a</div>");
    CHECK_COLOR(style(&p, "card")->color, 0xFFFF0000);
    CHECK_COLOR(style(&p, "p")->color, 0xFF0000FF);
    CHECK_COLOR(style(&p, "span")->color, 0xFF800080);
    CHECK_COLOR(style(&p, "em")->color, 0xFF00FF00);
    CHECK_COLOR(style(&p, "b")->color, 0xFF000080);
    CHECK_COLOR(style(&p, "card2")->color, 0xFF008000);
    CHECK_COLOR(style(&p, "after")->color, 0xFF0000FF);
    page_free(&p);
}

static void test_revert_and_all(void)
{
    page p;
    const tc_style *s;
    page_load(&p, "<!DOCTYPE html><style>"
                  "p { margin: 0 } #a { margin: revert } div { display: inline } #b { display: revert }"
                  "#c { all: unset } #d { all: initial } span { display: block } #e { display: revert }"
                  "</style><div style='color: red'><p id=a>a</p><div id=b>b</div><p id=c>c</p>"
                  "<p id=d>d</p><span id=e>e</span></div>");
    CHECK_PX(style(&p, "a")->margin[0].length.px, 16);
    TC_CHECK_INT(style(&p, "b")->display, TC_DISPLAY_BLOCK);
    s = style(&p, "c");
    TC_CHECK_INT(s->display, TC_DISPLAY_INLINE);
    CHECK_PX(s->margin[0].length.px, 0);
    CHECK_COLOR(s->color, 0xFFFF0000);
    CHECK_COLOR(style(&p, "d")->color, 0xFF000000);
    TC_CHECK_INT(style(&p, "e")->display, TC_DISPLAY_INLINE);
    page_free(&p);
}

static void test_blockify(void)
{
    page p;
    page_load(&p, "<!DOCTYPE html><html style='display: inline' id=root><style>"
                  "#f { display: flex } </style><span id=a style='float: left'>a</span>"
                  "<span id=b style='position: absolute; float: left'>b</span>"
                  "<div id=f><span id=item>i</span></div><span id=c style='display: inline-flex'><span id=d>d</span></span>");
    TC_CHECK_INT(style(&p, "root")->display, TC_DISPLAY_BLOCK);
    TC_CHECK_INT(style(&p, "a")->display, TC_DISPLAY_BLOCK);
    TC_CHECK_INT(style(&p, "b")->display, TC_DISPLAY_BLOCK);
    TC_CHECK_INT(style(&p, "b")->float_, TC_FLOAT_NONE);
    TC_CHECK_INT(style(&p, "item")->display, TC_DISPLAY_BLOCK);
    TC_CHECK_INT(style(&p, "c")->display, TC_DISPLAY_INLINE_FLEX);
    TC_CHECK_INT(style(&p, "d")->display, TC_DISPLAY_BLOCK);
    page_free(&p);
}

static void test_hints(void)
{
    page p;
    const tc_style *s;
    page_load(&p, "<!DOCTYPE html><body id=body bgcolor=black text=white link=yellow marginwidth=0>"
                  "<a id=a href=x>a</a><font id=font color=red size=5 face='Arial, sans-serif'>f</font>"
                  "<font id=font2 size=-1>g</font><div id=div align=center>c</div><center id=center>c</center>"
                  "<table id=t border=1 cellpadding=5 bgcolor=chucknorris width=50%><tr><td id=td width=20>c"
                  "</td></tr></table><table border=0><tr><td id=td0>c</td></tr></table>"
                  "<img id=img width=100 height=50 border=2 hspace=3><hr id=hr size=1>"
                  "<p id=p style='background-color: blue' bgcolor=red>p</p></body>");
    s = style(&p, "body");
    CHECK_COLOR(s->background_color, 0xFF000000);
    CHECK_COLOR(s->color, 0xFFFFFFFF);
    CHECK_PX(s->margin[3].length.px, 0);
    CHECK_PX(s->margin[0].length.px, 8);
    CHECK_COLOR(style(&p, "a")->color, 0xFFFFFF00);
    s = style(&p, "font");
    CHECK_COLOR(s->color, 0xFFFF0000);
    CHECK_PX(s->font_size, 24);
    TC_CHECK_STR(family(s, 0), "Arial");
    CHECK_PX(style(&p, "font2")->font_size, 16 * 8.0 / 9);
    TC_CHECK_INT(style(&p, "div")->text_align, TC_ALIGN_CENTER);
    TC_CHECK_INT(style(&p, "center")->text_align, TC_ALIGN_CENTER);
    s = style(&p, "t");
    CHECK_COLOR(s->background_color, 0xFFC00000);
    CHECK_PX(s->width.length.pct, 50);
    CHECK_PX(s->border_width[0], 1);
    TC_CHECK_INT(s->border_style[0], TC_BORDER_OUTSET);
    s = style(&p, "td");
    CHECK_PX(s->padding[0].px, 5);
    CHECK_PX(s->border_width[1], 1);
    TC_CHECK_INT(s->border_style[1], TC_BORDER_INSET);
    CHECK_PX(s->width.length.px, 20);
    CHECK_PX(style(&p, "td0")->border_width[0], 0);
    s = style(&p, "img");
    CHECK_PX(s->width.length.px, 100);
    CHECK_PX(s->height.length.px, 50);
    CHECK_PX(s->border_width[0], 2);
    CHECK_PX(s->margin[1].length.px, 3);
    CHECK_PX(style(&p, "hr")->border_width[2], 0);
    CHECK_COLOR(style(&p, "p")->background_color, 0xFF0000FF); /* p has no bgcolor hint anyway */
    page_free(&p);
}

static void test_pseudo_elements(void)
{
    page p;
    const tc_element_styles *e;
    page_load(&p, "<!DOCTYPE html><style>"
                  "#a::before { content: 'x' attr(data-y) 'z'; color: red } #a::after { content: none }"
                  "#b::after { color: red } #c::marker { color: green } #d::before { content: ''; display: none }"
                  "p::first-line { color: blue }"
                  "</style><p id=a data-y=Y>a</p><p id=b>b</p><ul><li id=c>c</ul><q id=q>q</q><p id=d>d</p>");
    e = styles(&p, "a");
    TC_CHECK(e && e->before && !e->after);
    if (e && e->before) {
        TC_CHECK_INT(e->before->content_kind, TC_GENERATED_ITEMS);
        TC_CHECK_INT(e->before->content->count, 3);
        TC_CHECK_STR(e->before->content->items[1].text, "Y");
        CHECK_COLOR(e->before->color, 0xFFFF0000);
        TC_CHECK_INT(e->before->display, TC_DISPLAY_INLINE);
    }
    CHECK_COLOR(style(&p, "a")->color, 0xFF000000); /* ::first-line is ignored */
    e = styles(&p, "b");
    TC_CHECK(e && !e->after); /* no content */
    e = styles(&p, "c");
    TC_CHECK(e && e->marker);
    if (e && e->marker)
        CHECK_COLOR(e->marker->color, 0xFF008000);
    e = styles(&p, "q");
    TC_CHECK(e && e->before && e->after);
    if (e && e->before)
        TC_CHECK_INT(e->before->content->items[0].type, TC_CONTENT_OPEN_QUOTE);
    e = styles(&p, "d");
    TC_CHECK(e && !e->before);
    page_free(&p);
}

static void test_quirks(void)
{
    page p;
    page_load(&p, "<style>p { margin-top: 10 } .Foo { color: red } td { font-size: 30px }</style>"
                  "<p id=a class=foo>a</p><div style='font-size: 20px'><table><tr><td id=td>x</td></tr></table>"
                  "<table><tr><th id=th>y</th></tr></table></div><form id=form></form>");
    TC_CHECK_INT(p.doc->mode, TC_QUIRKS);
    CHECK_PX(style(&p, "a")->margin[0].length.px, 10);
    CHECK_COLOR(style(&p, "a")->color, 0xFFFF0000);
    CHECK_PX(style(&p, "td")->font_size, 30);
    CHECK_PX(style(&p, "th")->font_size, 16); /* tables don't inherit font-size in quirks mode */
    CHECK_PX(style(&p, "form")->margin[2].length.px, 16);
    page_free(&p);
}

static void test_sharing(void)
{
    page p;
    tc_style_stats stats;
    page_load(&p, "<!DOCTYPE html><p><span id=a>a</span> <span id=b>b</span> <span id=c style='color:red'>c</span></p>");
    TC_CHECK(style(&p, "a") == style(&p, "b"));
    TC_CHECK(style(&p, "a") != style(&p, "c"));
    tc_styled_stats(p.styled, &stats);
    TC_CHECK(stats.elements >= 6);
    TC_CHECK(stats.styles < stats.elements);
    TC_CHECK(stats.selectors > 100);
    page_free(&p);
}

static void test_sheets(void)
{
    static const char *const garbage[] = {
        "", "}}}{{{", "a{", "@media", "@import", "@layer a.;", "p { color: }", "@supports ((((((", "a { b { c {",
        "@import url(x) layer(", ":root{--x:var(--x)}", "a{margin:1px 2px 3px 4px 5px}", "\\", "@layer{@layer{",
        "a{font:}", "a{font:12px}", "a{background:red,blue}", "a{display:inline inline}",
    };
    tc_stylesheet *s;
    size_t i;
    const char *css = "@import 'a.css'; @import url(b.css) screen; @import url(\"c.css\") layer(x);"
                      "p { color: red } a {} @import 'late.css';";
    s = tc_stylesheet_parse(css, strlen(css), TC_ORIGIN_AUTHOR, 0, "http://example.com/");
    TC_CHECK(s != NULL);
    if (s) {
        TC_CHECK_INT(tc_stylesheet_import_count(s), 3);
        TC_CHECK_STR(tc_stylesheet_import_url(s, 0), "a.css");
        TC_CHECK_STR(tc_stylesheet_import_url(s, 1), "b.css");
        TC_CHECK_STR(tc_stylesheet_import_url(s, 2), "c.css");
        TC_CHECK_INT(tc_stylesheet_rule_count(s), 1);
        TC_CHECK_STR(tc_stylesheet_url(s), "http://example.com/");
        tc_stylesheet_free(s);
    }
    for (i = 0; i < TC_ARRAY_LEN(garbage); i++) {
        s = tc_stylesheet_parse(garbage[i], strlen(garbage[i]), TC_ORIGIN_AUTHOR, 0, NULL);
        TC_CHECK(s != NULL);
        tc_stylesheet_free(s);
    }
}

static void test_imports(void)
{
    page p;
    tc_stylesheet *imported;
    page_load(&p, "<!DOCTYPE html><style>@import 'x.css' layer(lib); #a { color: blue } #b { color: blue }</style>"
                  "<p id=a>a</p><p id=b>b</p>");
    TC_CHECK_INT(p.count, 1);
    if (p.count == 1 && tc_stylesheet_import_count(p.sources[0].sheet) == 1) {
        const char *css = "#a { color: red } #b { color: red !important }";
        imported = tc_stylesheet_parse(css, strlen(css), TC_ORIGIN_AUTHOR, 0, NULL);
        tc_stylesheet_set_import(p.sources[0].sheet, 0, imported);
        page_style(&p, 800);
        CHECK_COLOR(style(&p, "a")->color, 0xFF0000FF); /* layered, so the sheet's own rule wins */
        CHECK_COLOR(style(&p, "b")->color, 0xFFFF0000);
    } else {
        TC_CHECK(0);
    }
    page_free(&p);
}

static void test_sources(void)
{
    page p;
    page_load(&p, "<!DOCTYPE html><link rel=stylesheet href=a.css><link rel='alternate stylesheet' href=b.css>"
                  "<link rel=STYLESHEET href=c.css media=print><link rel=icon href=d.ico>"
                  "<style type=text/plain>p{}</style><style>p { color: red }</style><link rel=stylesheet>");
    TC_CHECK_INT(p.count, 3);
    if (p.count == 3) {
        TC_CHECK_STR(p.sources[0].href, "a.css");
        TC_CHECK(p.sources[0].sheet == NULL);
        TC_CHECK_STR(p.sources[1].href, "c.css");
        TC_CHECK_STR(p.sources[1].media, "print");
        TC_CHECK(p.sources[2].href == NULL && p.sources[2].sheet != NULL);
    }
    page_free(&p);
}

void tc_suite_style(void)
{
    tc_style_init();
    TC_RUN(test_names);
    TC_RUN(test_user_agent);
    TC_RUN(test_cascade);
    TC_RUN(test_inheritance);
    TC_RUN(test_values);
    TC_RUN(test_custom_properties);
    TC_RUN(test_media_and_supports);
    TC_RUN(test_layers);
    TC_RUN(test_nesting);
    TC_RUN(test_revert_and_all);
    TC_RUN(test_blockify);
    TC_RUN(test_hints);
    TC_RUN(test_pseudo_elements);
    TC_RUN(test_quirks);
    TC_RUN(test_sharing);
    TC_RUN(test_sheets);
    TC_RUN(test_imports);
    TC_RUN(test_sources);
}
