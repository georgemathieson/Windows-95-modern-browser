/* Element names the engine knows, as small integers.
 *
 * The tree builder and later stages compare element names constantly, so
 * each known name gets a number; any other name is TC_TAG_UNKNOWN and is
 * kept as a string. The numbers say nothing about the namespace: check the
 * element's namespace too (an SVG <title> isn't an HTML <title>).
 *
 * The list must stay sorted by byte value (strcmp), for binary search. */
#ifndef TC_TAGS_H
#define TC_TAGS_H

#include "tc_base.h"

#define TC_TAG_LIST(X)                \
    X(A, "a")                         \
    X(ABBR, "abbr")                   \
    X(ADDRESS, "address")             \
    X(ANNOTATION_XML, "annotation-xml") \
    X(APPLET, "applet")               \
    X(AREA, "area")                   \
    X(ARTICLE, "article")             \
    X(ASIDE, "aside")                 \
    X(AUDIO, "audio")                 \
    X(B, "b")                         \
    X(BASE, "base")                   \
    X(BASEFONT, "basefont")           \
    X(BDI, "bdi")                     \
    X(BDO, "bdo")                     \
    X(BGSOUND, "bgsound")             \
    X(BIG, "big")                     \
    X(BLOCKQUOTE, "blockquote")       \
    X(BODY, "body")                   \
    X(BR, "br")                       \
    X(BUTTON, "button")               \
    X(CANVAS, "canvas")               \
    X(CAPTION, "caption")             \
    X(CENTER, "center")               \
    X(CITE, "cite")                   \
    X(CODE, "code")                   \
    X(COL, "col")                     \
    X(COLGROUP, "colgroup")           \
    X(DATA, "data")                   \
    X(DATALIST, "datalist")           \
    X(DD, "dd")                       \
    X(DEL, "del")                     \
    X(DESC, "desc")                   \
    X(DETAILS, "details")             \
    X(DFN, "dfn")                     \
    X(DIALOG, "dialog")               \
    X(DIR, "dir")                     \
    X(DIV, "div")                     \
    X(DL, "dl")                       \
    X(DT, "dt")                       \
    X(EM, "em")                       \
    X(EMBED, "embed")                 \
    X(FIELDSET, "fieldset")           \
    X(FIGCAPTION, "figcaption")       \
    X(FIGURE, "figure")               \
    X(FONT, "font")                   \
    X(FOOTER, "footer")               \
    X(FOREIGNOBJECT, "foreignObject") \
    X(FORM, "form")                   \
    X(FRAME, "frame")                 \
    X(FRAMESET, "frameset")           \
    X(H1, "h1")                       \
    X(H2, "h2")                       \
    X(H3, "h3")                       \
    X(H4, "h4")                       \
    X(H5, "h5")                       \
    X(H6, "h6")                       \
    X(HEAD, "head")                   \
    X(HEADER, "header")               \
    X(HGROUP, "hgroup")               \
    X(HR, "hr")                       \
    X(HTML, "html")                   \
    X(I, "i")                         \
    X(IFRAME, "iframe")               \
    X(IMAGE, "image")                 \
    X(IMG, "img")                     \
    X(INPUT, "input")                 \
    X(INS, "ins")                     \
    X(KBD, "kbd")                     \
    X(KEYGEN, "keygen")               \
    X(LABEL, "label")                 \
    X(LEGEND, "legend")               \
    X(LI, "li")                       \
    X(LINK, "link")                   \
    X(LISTING, "listing")             \
    X(MAIN, "main")                   \
    X(MALIGNMARK, "malignmark")       \
    X(MAP, "map")                     \
    X(MARK, "mark")                   \
    X(MARQUEE, "marquee")             \
    X(MATH, "math")                   \
    X(MENU, "menu")                   \
    X(META, "meta")                   \
    X(METER, "meter")                 \
    X(MGLYPH, "mglyph")               \
    X(MI, "mi")                       \
    X(MN, "mn")                       \
    X(MO, "mo")                       \
    X(MS, "ms")                       \
    X(MTEXT, "mtext")                 \
    X(NAV, "nav")                     \
    X(NOBR, "nobr")                   \
    X(NOEMBED, "noembed")             \
    X(NOFRAMES, "noframes")           \
    X(NOSCRIPT, "noscript")           \
    X(OBJECT, "object")               \
    X(OL, "ol")                       \
    X(OPTGROUP, "optgroup")           \
    X(OPTION, "option")               \
    X(OUTPUT, "output")               \
    X(P, "p")                         \
    X(PARAM, "param")                 \
    X(PICTURE, "picture")             \
    X(PLAINTEXT, "plaintext")         \
    X(PRE, "pre")                     \
    X(PROGRESS, "progress")           \
    X(Q, "q")                         \
    X(RB, "rb")                       \
    X(RP, "rp")                       \
    X(RT, "rt")                       \
    X(RTC, "rtc")                     \
    X(RUBY, "ruby")                   \
    X(S, "s")                         \
    X(SAMP, "samp")                   \
    X(SCRIPT, "script")               \
    X(SEARCH, "search")               \
    X(SECTION, "section")             \
    X(SELECT, "select")               \
    X(SELECTEDCONTENT, "selectedcontent") \
    X(SLOT, "slot")                   \
    X(SMALL, "small")                 \
    X(SOURCE, "source")               \
    X(SPAN, "span")                   \
    X(STRIKE, "strike")               \
    X(STRONG, "strong")               \
    X(STYLE, "style")                 \
    X(SUB, "sub")                     \
    X(SUMMARY, "summary")             \
    X(SUP, "sup")                     \
    X(SVG, "svg")                     \
    X(TABLE, "table")                 \
    X(TBODY, "tbody")                 \
    X(TD, "td")                       \
    X(TEMPLATE, "template")           \
    X(TEXTAREA, "textarea")           \
    X(TFOOT, "tfoot")                 \
    X(TH, "th")                       \
    X(THEAD, "thead")                 \
    X(TIME, "time")                   \
    X(TITLE, "title")                 \
    X(TR, "tr")                       \
    X(TRACK, "track")                 \
    X(TT, "tt")                       \
    X(U, "u")                         \
    X(UL, "ul")                       \
    X(VAR, "var")                     \
    X(VIDEO, "video")                 \
    X(WBR, "wbr")                     \
    X(XMP, "xmp")

typedef enum tc_tag {
    TC_TAG_UNKNOWN,
#define TC_TAG_ENUM(id, name) TC_TAG_##id,
    TC_TAG_LIST(TC_TAG_ENUM)
#undef TC_TAG_ENUM
    TC_TAG_COUNT
} tc_tag;

/* The tag for a name (case-sensitive), or TC_TAG_UNKNOWN. */
tc_tag tc_tag_lookup(const char *name, size_t len);

/* The name of a known tag ("" for TC_TAG_UNKNOWN). */
const char *tc_tag_name(tc_tag tag);

#endif
