#include "tc_html_parser.h"

#include <stdlib.h>
#include <string.h>

#include "tc_html_tokenizer.h"

enum mode {
    M_INITIAL,
    M_BEFORE_HTML,
    M_BEFORE_HEAD,
    M_IN_HEAD,
    M_IN_HEAD_NOSCRIPT,
    M_AFTER_HEAD,
    M_IN_BODY,
    M_TEXT,
    M_IN_TABLE,
    M_IN_TABLE_TEXT,
    M_IN_CAPTION,
    M_IN_COLUMN_GROUP,
    M_IN_TABLE_BODY,
    M_IN_ROW,
    M_IN_CELL,
    M_IN_TEMPLATE,
    M_AFTER_BODY,
    M_IN_FRAMESET,
    M_AFTER_FRAMESET,
    M_AFTER_AFTER_BODY,
    M_AFTER_AFTER_FRAMESET,
    M_COUNT
};

enum scope { SCOPE_DEFAULT, SCOPE_LIST_ITEM, SCOPE_BUTTON, SCOPE_TABLE };

typedef struct parser {
    tc_tokenizer tok;
    tc_document *doc;
    const tc_html_options *opt;
    int mode, original_mode;

    tc_element **stack;          /* the stack of open elements */
    size_t depth, stack_cap;
    tc_element **afe;            /* active formatting elements; NULL is a marker */
    size_t afe_len, afe_cap;
    int *tmodes;                 /* the stack of template insertion modes */
    size_t tmodes_len, tmodes_cap;

    tc_element *head, *form;
    const tc_element *context;   /* the fragment case */
    tc_node *fragment;           /* the fragment case: receives the root's children */
    int context_is_html_ip;

    int frameset_ok, foster, scripting;
    int skip_lf;                 /* drop a newline at the start of the next token */
    int reprocess, ack, stopped, nomem;
    int tag;                     /* the current tag token's tc_tag */
    tc_buf table_chars;          /* "pending table character tokens" */

    tc_confidence confidence;
    tc_encoding encoding;
    tc_encoding change_to;       /* a <meta> wants this encoding: parse again */
} parser;

typedef void (*mode_fn)(parser *p, tc_token *t);
static const mode_fn modes[M_COUNT];

/* --- Small helpers ----------------------------------------------------- */

static void perr(parser *p)
{
    if (p->opt && p->opt->on_error)
        p->opt->on_error(p->opt->ctx, TC_HTML_ERR_TREE_CONSTRUCTION);
}

static int is_ws_byte(char c)
{
    return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ';
}

static size_t ws_prefix(const tc_token *t)
{
    size_t n = 0;
    while (n < t->data_len && is_ws_byte(t->data[n]))
        n++;
    return n;
}

static void consume(tc_token *t, size_t n)
{
    t->data += n;
    t->data_len -= n;
}

static int all_ws(const char *s, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
        if (!is_ws_byte(s[i]))
            return 0;
    return 1;
}

static int ascii_ieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z')
            x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z')
            y = (char)(y + 32);
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

static int ascii_istarts(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        char x = *s, y = *prefix;
        if (x >= 'A' && x <= 'Z')
            x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z')
            y = (char)(y + 32);
        if (x != y)
            return 0;
    }
    return 1;
}

static const char *token_attr(const tc_token *t, const char *name)
{
    size_t i;
    for (i = 0; i < t->attr_count; i++)
        if (strcmp(t->attrs[i].name, name) == 0)
            return t->attrs[i].value;
    return NULL;
}

static void set_tokenizer_state(parser *p, int state)
{
    tc_tokenizer_set_state(&p->tok, state);
}

/* --- The stack of open elements ---------------------------------------- */

static tc_element *current(const parser *p)
{
    return p->depth ? p->stack[p->depth - 1] : NULL;
}

static const tc_element *adjusted_current(const parser *p)
{
    if (p->context && p->depth == 1)
        return p->context;
    return current(p);
}

static int is_html(const tc_element *el, int tag)
{
    return el && el->node.ns == TC_NS_HTML && el->node.tag == tag;
}

static void push(parser *p, tc_element *el)
{
    if (p->depth == p->stack_cap) {
        size_t cap = p->stack_cap ? p->stack_cap * 2 : 64;
        tc_element **grown = realloc(p->stack, cap * sizeof *grown);
        if (!grown) {
            p->nomem = 1;
            return;
        }
        p->stack = grown;
        p->stack_cap = cap;
    }
    p->stack[p->depth++] = el;
}

static void pop(parser *p)
{
    if (p->depth)
        p->depth--;
}

static long stack_index(const parser *p, const tc_element *el)
{
    size_t i = p->depth;
    while (i-- > 0)
        if (p->stack[i] == el)
            return (long)i;
    return -1;
}

static void stack_remove_at(parser *p, size_t i)
{
    memmove(p->stack + i, p->stack + i + 1, (p->depth - i - 1) * sizeof *p->stack);
    p->depth--;
}

static void stack_remove(parser *p, const tc_element *el)
{
    long i = stack_index(p, el);
    if (i >= 0)
        stack_remove_at(p, (size_t)i);
}

static void stack_insert_at(parser *p, size_t i, tc_element *el)
{
    push(p, el);
    if (p->nomem)
        return;
    memmove(p->stack + i + 1, p->stack + i, (p->depth - 1 - i) * sizeof *p->stack);
    p->stack[i] = el;
}

static void pop_until_html(parser *p, int tag)
{
    while (p->depth) {
        tc_element *el = current(p);
        pop(p);
        if (is_html(el, tag))
            break;
    }
}

static void pop_until_el(parser *p, const tc_element *target)
{
    while (p->depth) {
        tc_element *el = current(p);
        pop(p);
        if (el == target)
            break;
    }
}

static int has_template(const parser *p)
{
    size_t i;
    for (i = 0; i < p->depth; i++)
        if (is_html(p->stack[i], TC_TAG_TEMPLATE))
            return 1;
    return 0;
}

/* "Parsing template contents": a template is open, or is the context. */
static int in_template_contents(const parser *p)
{
    return has_template(p) || is_html(p->context, TC_TAG_TEMPLATE);
}

static int is_special(const tc_element *el)
{
    switch (el->node.ns) {
    case TC_NS_HTML:
        switch (el->node.tag) {
        case TC_TAG_ADDRESS: case TC_TAG_APPLET: case TC_TAG_AREA: case TC_TAG_ARTICLE: case TC_TAG_ASIDE:
        case TC_TAG_BASE: case TC_TAG_BASEFONT: case TC_TAG_BGSOUND: case TC_TAG_BLOCKQUOTE: case TC_TAG_BODY:
        case TC_TAG_BR: case TC_TAG_BUTTON: case TC_TAG_CAPTION: case TC_TAG_CENTER: case TC_TAG_COL:
        case TC_TAG_COLGROUP: case TC_TAG_DD: case TC_TAG_DETAILS: case TC_TAG_DIR: case TC_TAG_DIV:
        case TC_TAG_DL: case TC_TAG_DT: case TC_TAG_EMBED: case TC_TAG_FIELDSET: case TC_TAG_FIGCAPTION:
        case TC_TAG_FIGURE: case TC_TAG_FOOTER: case TC_TAG_FORM: case TC_TAG_FRAME: case TC_TAG_FRAMESET:
        case TC_TAG_H1: case TC_TAG_H2: case TC_TAG_H3: case TC_TAG_H4: case TC_TAG_H5: case TC_TAG_H6:
        case TC_TAG_HEAD: case TC_TAG_HEADER: case TC_TAG_HGROUP: case TC_TAG_HR: case TC_TAG_HTML:
        case TC_TAG_IFRAME: case TC_TAG_IMG: case TC_TAG_INPUT: case TC_TAG_KEYGEN: case TC_TAG_LI:
        case TC_TAG_LINK: case TC_TAG_LISTING: case TC_TAG_MAIN: case TC_TAG_MARQUEE: case TC_TAG_MENU:
        case TC_TAG_META: case TC_TAG_NAV: case TC_TAG_NOEMBED: case TC_TAG_NOFRAMES: case TC_TAG_NOSCRIPT:
        case TC_TAG_OBJECT: case TC_TAG_OL: case TC_TAG_P: case TC_TAG_PARAM: case TC_TAG_PLAINTEXT:
        case TC_TAG_PRE: case TC_TAG_SCRIPT: case TC_TAG_SEARCH: case TC_TAG_SECTION: case TC_TAG_SELECT:
        case TC_TAG_SOURCE: case TC_TAG_STYLE: case TC_TAG_SUMMARY: case TC_TAG_TABLE: case TC_TAG_TBODY:
        case TC_TAG_TD: case TC_TAG_TEMPLATE: case TC_TAG_TEXTAREA: case TC_TAG_TFOOT: case TC_TAG_TH:
        case TC_TAG_THEAD: case TC_TAG_TITLE: case TC_TAG_TR: case TC_TAG_TRACK: case TC_TAG_UL:
        case TC_TAG_WBR: case TC_TAG_XMP:
            return 1;
        default:
            return 0;
        }
    case TC_NS_MATHML:
        return el->node.tag == TC_TAG_MI || el->node.tag == TC_TAG_MO || el->node.tag == TC_TAG_MN ||
               el->node.tag == TC_TAG_MS || el->node.tag == TC_TAG_MTEXT || el->node.tag == TC_TAG_ANNOTATION_XML;
    case TC_NS_SVG:
        return el->node.tag == TC_TAG_FOREIGNOBJECT || el->node.tag == TC_TAG_DESC || el->node.tag == TC_TAG_TITLE;
    default:
        return 0;
    }
}

static int is_scope_boundary(const tc_element *el, int scope)
{
    int tag = el->node.tag;

    if (scope == SCOPE_TABLE)
        return el->node.ns == TC_NS_HTML && (tag == TC_TAG_HTML || tag == TC_TAG_TABLE || tag == TC_TAG_TEMPLATE);
    switch (el->node.ns) {
    case TC_NS_HTML:
        if (tag == TC_TAG_APPLET || tag == TC_TAG_CAPTION || tag == TC_TAG_HTML || tag == TC_TAG_TABLE ||
            tag == TC_TAG_TD || tag == TC_TAG_TH || tag == TC_TAG_MARQUEE || tag == TC_TAG_OBJECT ||
            tag == TC_TAG_SELECT || tag == TC_TAG_TEMPLATE)
            return 1;
        if (scope == SCOPE_LIST_ITEM)
            return tag == TC_TAG_OL || tag == TC_TAG_UL;
        if (scope == SCOPE_BUTTON)
            return tag == TC_TAG_BUTTON;
        return 0;
    case TC_NS_MATHML:
        return tag == TC_TAG_MI || tag == TC_TAG_MO || tag == TC_TAG_MN || tag == TC_TAG_MS ||
               tag == TC_TAG_MTEXT || tag == TC_TAG_ANNOTATION_XML;
    case TC_NS_SVG:
        return tag == TC_TAG_FOREIGNOBJECT || tag == TC_TAG_DESC || tag == TC_TAG_TITLE;
    default:
        return 0;
    }
}

/* Has an HTML element with this tag in the given scope. */
static int in_scope(const parser *p, int tag, int scope)
{
    size_t i = p->depth;
    while (i-- > 0) {
        const tc_element *el = p->stack[i];
        if (is_html(el, tag))
            return 1;
        if (is_scope_boundary(el, scope))
            return 0;
    }
    return 0;
}

static int el_in_scope(const parser *p, const tc_element *target, int scope)
{
    size_t i = p->depth;
    while (i-- > 0) {
        const tc_element *el = p->stack[i];
        if (el == target)
            return 1;
        if (is_scope_boundary(el, scope))
            return 0;
    }
    return 0;
}

static int is_heading(const tc_element *el)
{
    int tag = el->node.tag;
    return el->node.ns == TC_NS_HTML && tag >= TC_TAG_H1 && tag <= TC_TAG_H6;
}

static int heading_in_scope(const parser *p)
{
    size_t i = p->depth;
    while (i-- > 0) {
        const tc_element *el = p->stack[i];
        if (is_heading(el))
            return 1;
        if (is_scope_boundary(el, SCOPE_DEFAULT))
            return 0;
    }
    return 0;
}

static int implied_end_tag(const tc_element *el, int thorough)
{
    if (el->node.ns != TC_NS_HTML)
        return 0;
    switch (el->node.tag) {
    case TC_TAG_DD: case TC_TAG_DT: case TC_TAG_LI: case TC_TAG_OPTGROUP: case TC_TAG_OPTION: case TC_TAG_P:
    case TC_TAG_RB: case TC_TAG_RP: case TC_TAG_RT: case TC_TAG_RTC:
        return 1;
    case TC_TAG_CAPTION: case TC_TAG_COLGROUP: case TC_TAG_TBODY: case TC_TAG_TD: case TC_TAG_TFOOT:
    case TC_TAG_TH: case TC_TAG_THEAD: case TC_TAG_TR:
        return thorough;
    default:
        return 0;
    }
}

/* "Generate implied end tags", except for HTML elements with tag `except`. */
static void generate_implied_end_tags(parser *p, int except)
{
    while (p->depth && implied_end_tag(current(p), 0) && !is_html(current(p), except))
        pop(p);
}

static void generate_all_implied_end_tags(parser *p)
{
    while (p->depth && implied_end_tag(current(p), 1))
        pop(p);
}

/* Whether an HTML element has the current token's tag name (which may not be
   a known tag). */
static int has_token_name(const parser *p, const tc_element *el, const tc_token *t)
{
    if (el->node.ns != TC_NS_HTML)
        return 0;
    if (p->tag != TC_TAG_UNKNOWN)
        return el->node.tag == p->tag;
    return el->node.tag == TC_TAG_UNKNOWN && strlen(el->name) == t->name_len &&
           memcmp(el->name, t->name, t->name_len) == 0;
}

/* --- Creating and inserting nodes -------------------------------------- */

typedef struct { const char *from, *to; } rename;

static const rename svg_attrs[] = {
    { "attributename", "attributeName" }, { "attributetype", "attributeType" },
    { "basefrequency", "baseFrequency" }, { "baseprofile", "baseProfile" }, { "calcmode", "calcMode" },
    { "clippathunits", "clipPathUnits" }, { "diffuseconstant", "diffuseConstant" }, { "edgemode", "edgeMode" },
    { "filterunits", "filterUnits" }, { "glyphref", "glyphRef" }, { "gradienttransform", "gradientTransform" },
    { "gradientunits", "gradientUnits" }, { "kernelmatrix", "kernelMatrix" },
    { "kernelunitlength", "kernelUnitLength" }, { "keypoints", "keyPoints" }, { "keysplines", "keySplines" },
    { "keytimes", "keyTimes" }, { "lengthadjust", "lengthAdjust" }, { "limitingconeangle", "limitingConeAngle" },
    { "markerheight", "markerHeight" }, { "markerunits", "markerUnits" }, { "markerwidth", "markerWidth" },
    { "maskcontentunits", "maskContentUnits" }, { "maskunits", "maskUnits" }, { "numoctaves", "numOctaves" },
    { "pathlength", "pathLength" }, { "patterncontentunits", "patternContentUnits" },
    { "patterntransform", "patternTransform" }, { "patternunits", "patternUnits" }, { "pointsatx", "pointsAtX" },
    { "pointsaty", "pointsAtY" }, { "pointsatz", "pointsAtZ" }, { "preservealpha", "preserveAlpha" },
    { "preserveaspectratio", "preserveAspectRatio" }, { "primitiveunits", "primitiveUnits" }, { "refx", "refX" },
    { "refy", "refY" }, { "repeatcount", "repeatCount" }, { "repeatdur", "repeatDur" },
    { "requiredextensions", "requiredExtensions" }, { "requiredfeatures", "requiredFeatures" },
    { "specularconstant", "specularConstant" }, { "specularexponent", "specularExponent" },
    { "spreadmethod", "spreadMethod" }, { "startoffset", "startOffset" }, { "stddeviation", "stdDeviation" },
    { "stitchtiles", "stitchTiles" }, { "surfacescale", "surfaceScale" }, { "systemlanguage", "systemLanguage" },
    { "tablevalues", "tableValues" }, { "targetx", "targetX" }, { "targety", "targetY" },
    { "textlength", "textLength" }, { "viewbox", "viewBox" }, { "viewtarget", "viewTarget" },
    { "xchannelselector", "xChannelSelector" }, { "ychannelselector", "yChannelSelector" },
    { "zoomandpan", "zoomAndPan" }
};

static const rename svg_tags[] = {
    { "altglyph", "altGlyph" }, { "altglyphdef", "altGlyphDef" }, { "altglyphitem", "altGlyphItem" },
    { "animatecolor", "animateColor" }, { "animatemotion", "animateMotion" },
    { "animatetransform", "animateTransform" }, { "clippath", "clipPath" }, { "feblend", "feBlend" },
    { "fecolormatrix", "feColorMatrix" }, { "fecomponenttransfer", "feComponentTransfer" },
    { "fecomposite", "feComposite" }, { "feconvolvematrix", "feConvolveMatrix" },
    { "fediffuselighting", "feDiffuseLighting" }, { "fedisplacementmap", "feDisplacementMap" },
    { "fedistantlight", "feDistantLight" }, { "fedropshadow", "feDropShadow" }, { "feflood", "feFlood" },
    { "fefunca", "feFuncA" }, { "fefuncb", "feFuncB" }, { "fefuncg", "feFuncG" }, { "fefuncr", "feFuncR" },
    { "fegaussianblur", "feGaussianBlur" }, { "feimage", "feImage" }, { "femerge", "feMerge" },
    { "femergenode", "feMergeNode" }, { "femorphology", "feMorphology" }, { "feoffset", "feOffset" },
    { "fepointlight", "fePointLight" }, { "fespecularlighting", "feSpecularLighting" },
    { "fespotlight", "feSpotLight" }, { "fetile", "feTile" }, { "feturbulence", "feTurbulence" },
    { "foreignobject", "foreignObject" }, { "glyphref", "glyphRef" }, { "lineargradient", "linearGradient" },
    { "radialgradient", "radialGradient" }, { "textpath", "textPath" }
};

static const struct {
    const char *name, *prefix, *local;
    tc_ns ns;
} foreign_attrs[] = {
    { "xlink:actuate", "xlink", "actuate", TC_NS_XLINK }, { "xlink:arcrole", "xlink", "arcrole", TC_NS_XLINK },
    { "xlink:href", "xlink", "href", TC_NS_XLINK }, { "xlink:role", "xlink", "role", TC_NS_XLINK },
    { "xlink:show", "xlink", "show", TC_NS_XLINK }, { "xlink:title", "xlink", "title", TC_NS_XLINK },
    { "xlink:type", "xlink", "type", TC_NS_XLINK }, { "xml:lang", "xml", "lang", TC_NS_XML },
    { "xml:space", "xml", "space", TC_NS_XML }, { "xmlns", NULL, "xmlns", TC_NS_XMLNS },
    { "xmlns:xlink", "xmlns", "xlink", TC_NS_XMLNS }
};

static const char *find_rename(const rename *table, size_t n, const char *name)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (strcmp(table[i].from, name) == 0)
            return table[i].to;
    return NULL;
}

static void check_nomem(parser *p)
{
    if (p->doc->nomem)
        p->nomem = 1;
}

/* "Create an element for the token" in namespace ns, adjusting SVG and
   MathML names and foreign attributes as the callers of "insert a foreign
   element" do. */
static tc_element *create_for_token(parser *p, const tc_token *t, tc_ns ns)
{
    const char *name = t->name;
    size_t name_len = t->name_len, i;
    tc_element *el;

    if (ns == TC_NS_SVG) {
        const char *renamed = find_rename(svg_tags, TC_ARRAY_LEN(svg_tags), t->name);
        if (renamed) {
            name = renamed;
            name_len = strlen(renamed);
        }
    }
    el = tc_dom_create_element(p->doc, ns, name, name_len);
    if (!el) {
        p->nomem = 1;
        return NULL;
    }
    for (i = 0; i < t->attr_count; i++) {
        const tc_token_attr *a = &t->attrs[i];
        const char *aname = a->name, *prefix = NULL;
        tc_ns ans = TC_NS_NONE;
        if (ns == TC_NS_MATHML && strcmp(aname, "definitionurl") == 0)
            aname = "definitionURL";
        if (ns == TC_NS_SVG) {
            const char *renamed = find_rename(svg_attrs, TC_ARRAY_LEN(svg_attrs), aname);
            if (renamed)
                aname = renamed;
        }
        if (ns != TC_NS_HTML) {
            size_t k;
            for (k = 0; k < TC_ARRAY_LEN(foreign_attrs); k++)
                if (strcmp(foreign_attrs[k].name, a->name) == 0) {
                    aname = foreign_attrs[k].local;
                    prefix = foreign_attrs[k].prefix;
                    ans = foreign_attrs[k].ns;
                    break;
                }
        }
        if (tc_dom_add_attr(p->doc, el, ans, prefix, aname, strlen(aname), a->value, a->value_len)) {
            p->nomem = 1;
            return NULL;
        }
    }
    if (ns == TC_NS_MATHML && el->node.tag == TC_TAG_ANNOTATION_XML) {
        const char *enc = token_attr(t, "encoding");
        if (enc && (ascii_ieq(enc, "text/html") || ascii_ieq(enc, "application/xhtml+xml")))
            el->flags |= TC_EL_HTML_INTEGRATION_POINT;
    }
    return el;
}

static tc_element *create_named(parser *p, const char *name)
{
    tc_element *el = tc_dom_create_element(p->doc, TC_NS_HTML, name, strlen(name));
    if (!el)
        p->nomem = 1;
    return el;
}

/* A new element for the token an element was made for: the same name and
   attributes. (Until scripts can change attributes, the element's are the
   token's.) */
static tc_element *clone_element(parser *p, const tc_element *src)
{
    tc_element *el = tc_dom_create_element(p->doc, (tc_ns)src->node.ns, src->name, strlen(src->name));
    uint32_t i;

    if (!el) {
        p->nomem = 1;
        return NULL;
    }
    for (i = 0; i < src->attr_count; i++) {
        const tc_attr *a = &src->attrs[i];
        if (tc_dom_add_attr(p->doc, el, (tc_ns)a->ns, a->prefix, a->name, strlen(a->name), a->value, a->value_len)) {
            p->nomem = 1;
            return NULL;
        }
    }
    el->flags = src->flags;
    return el;
}

/* "The appropriate place for inserting a node", then adjusted (the fragment
   case sends the root's children to the fragment). */
static void insertion_location(parser *p, tc_node *override, tc_node **parent, tc_node **ref)
{
    tc_element *target_el = override ? NULL : current(p);
    tc_node *target = override ? override : target_el ? &target_el->node : &p->doc->node;

    *ref = NULL;
    if (p->foster && target->type == TC_NODE_ELEMENT && target->ns == TC_NS_HTML &&
        (target->tag == TC_TAG_TABLE || target->tag == TC_TAG_TBODY || target->tag == TC_TAG_TFOOT ||
         target->tag == TC_TAG_THEAD || target->tag == TC_TAG_TR)) {
        long last_template = -1, last_table = -1, i;
        for (i = (long)p->depth - 1; i >= 0; i--) {
            if (last_template < 0 && is_html(p->stack[i], TC_TAG_TEMPLATE))
                last_template = i;
            if (last_table < 0 && is_html(p->stack[i], TC_TAG_TABLE))
                last_table = i;
        }
        if (last_template >= 0 && last_template > last_table) {
            target = &p->stack[last_template]->node;
        } else if (last_table < 0) {
            target = &p->stack[0]->node;
            goto adjust;
        } else if (p->stack[last_table]->node.parent) {
            target = p->stack[last_table]->node.parent;
            *ref = &p->stack[last_table]->node;
        } else {
            target = &p->stack[last_table - 1]->node;
        }
    }
    if (target->type == TC_NODE_ELEMENT && ((tc_element *)target)->content &&
        target->ns == TC_NS_HTML && target->tag == TC_TAG_TEMPLATE) {
        target = ((tc_element *)target)->content;
        *ref = NULL;
    }
adjust:
    /* Very deep: attach to the element at the maximum depth instead. */
    if (p->depth > TC_HTML_MAX_DEPTH && target->type == TC_NODE_ELEMENT) {
        long i = stack_index(p, (tc_element *)target);
        if (i >= TC_HTML_MAX_DEPTH) {
            target = &p->stack[TC_HTML_MAX_DEPTH - 1]->node;
            *ref = NULL;
        }
    }
    if (p->fragment && p->depth && target == &p->stack[0]->node)
        target = p->fragment;
    *parent = target;
}

/* "Insert an element at the adjusted insertion location". */
static void insert_at(tc_element *el, tc_node *parent, tc_node *ref)
{
    if (el->node.parent)
        return;
    if (parent->type == TC_NODE_DOCUMENT) {
        const tc_node *c;
        for (c = parent->first_child; c; c = c->next)
            if (c->type == TC_NODE_ELEMENT)
                return;
    }
    tc_dom_insert_before(parent, &el->node, ref);
}

static void insert_element(parser *p, tc_element *el)
{
    tc_node *parent, *ref;

    insertion_location(p, NULL, &parent, &ref);
    insert_at(el, parent, ref);
}

/* "Insert a foreign element" (and "insert an HTML element"). */
static tc_element *insert_for_token(parser *p, const tc_token *t, tc_ns ns)
{
    tc_element *el = create_for_token(p, t, ns);

    if (!el)
        return NULL;
    insert_element(p, el);
    push(p, el);
    return el;
}

static tc_element *insert_html(parser *p, const tc_token *t)
{
    return insert_for_token(p, t, TC_NS_HTML);
}

/* Insert an HTML element for a start tag with no attributes. */
static tc_element *insert_named(parser *p, const char *name)
{
    tc_element *el = create_named(p, name);

    if (!el)
        return NULL;
    insert_element(p, el);
    push(p, el);
    return el;
}

static void insert_chars(parser *p, const char *s, size_t len)
{
    tc_node *parent, *ref, *prev;

    if (!len || !p->depth)
        return;
    insertion_location(p, NULL, &parent, &ref);
    if (parent->type == TC_NODE_DOCUMENT)
        return;
    prev = ref ? ref->prev : parent->last_child;
    if (prev && prev->type == TC_NODE_TEXT) {
        if (tc_dom_append_data(p->doc, (tc_chardata *)prev, s, len))
            p->nomem = 1;
    } else {
        tc_chardata *text = tc_dom_create_text(p->doc, s, len);
        if (!text) {
            p->nomem = 1;
            return;
        }
        tc_dom_insert_before(parent, &text->node, ref);
    }
}

/* Insert a comment or processing instruction: at parent if given, else at
   the adjusted insertion location. */
static void insert_comment_or_pi(parser *p, const tc_token *t, tc_node *parent)
{
    tc_node *ref = NULL, *node;

    if (!parent)
        insertion_location(p, NULL, &parent, &ref);
    else if (p->fragment && p->depth && parent == &p->stack[0]->node)
        parent = p->fragment;
    if (t->type == TC_TOKEN_PI) {
        tc_pi *pi = tc_dom_create_pi(p->doc, t->name, t->name_len, t->data, t->data_len);
        node = pi ? &pi->node : NULL;
    } else {
        tc_chardata *c = tc_dom_create_comment(p->doc, t->data, t->data_len);
        node = c ? &c->node : NULL;
    }
    if (!node) {
        p->nomem = 1;
        return;
    }
    tc_dom_insert_before(parent, node, ref);
}

/* "The generic raw text / RCDATA element parsing algorithm". */
static void parse_text_element(parser *p, tc_token *t, int state)
{
    insert_html(p, t);
    set_tokenizer_state(p, state);
    p->original_mode = p->mode;
    p->mode = M_TEXT;
}

/* --- Active formatting elements --------------------------------------- */

static void afe_append(parser *p, tc_element *el)
{
    if (p->afe_len == p->afe_cap) {
        size_t cap = p->afe_cap ? p->afe_cap * 2 : 16;
        tc_element **grown = realloc(p->afe, cap * sizeof *grown);
        if (!grown) {
            p->nomem = 1;
            return;
        }
        p->afe = grown;
        p->afe_cap = cap;
    }
    p->afe[p->afe_len++] = el;
}

static void afe_insert_at(parser *p, size_t i, tc_element *el)
{
    afe_append(p, el);
    if (p->nomem)
        return;
    memmove(p->afe + i + 1, p->afe + i, (p->afe_len - 1 - i) * sizeof *p->afe);
    p->afe[i] = el;
}

static void afe_remove_at(parser *p, size_t i)
{
    memmove(p->afe + i, p->afe + i + 1, (p->afe_len - i - 1) * sizeof *p->afe);
    p->afe_len--;
}

static long afe_index(const parser *p, const tc_element *el)
{
    size_t i = p->afe_len;
    while (i-- > 0)
        if (p->afe[i] == el)
            return (long)i;
    return -1;
}

static void afe_marker(parser *p)
{
    afe_append(p, NULL);
}

static int same_attrs(const tc_element *a, const tc_element *b)
{
    uint32_t i, j;

    if (a->attr_count != b->attr_count)
        return 0;
    for (i = 0; i < a->attr_count; i++) {
        for (j = 0; j < b->attr_count; j++)
            if (a->attrs[i].ns == b->attrs[j].ns && strcmp(a->attrs[i].name, b->attrs[j].name) == 0 &&
                a->attrs[i].value_len == b->attrs[j].value_len &&
                memcmp(a->attrs[i].value, b->attrs[j].value, a->attrs[i].value_len) == 0)
                break;
        if (j == b->attr_count)
            return 0;
    }
    return 1;
}

/* "Push onto the list of active formatting elements", with the Noah's Ark
   clause: at most three identical elements after the last marker. */
static void afe_push(parser *p, tc_element *el)
{
    size_t i = p->afe_len, count = 0, earliest = 0;

    while (i-- > 0) {
        const tc_element *e = p->afe[i];
        if (!e)
            break;
        if (e->node.ns == el->node.ns && strcmp(e->name, el->name) == 0 && same_attrs(e, el)) {
            count++;
            earliest = i;
        }
    }
    if (count >= 3)
        afe_remove_at(p, earliest);
    afe_append(p, el);
}

static void reconstruct_afe(parser *p)
{
    size_t i;

    if (!p->afe_len)
        return;
    if (!p->afe[p->afe_len - 1] || stack_index(p, p->afe[p->afe_len - 1]) >= 0)
        return;
    i = p->afe_len - 1;
    while (i > 0 && p->afe[i - 1] && stack_index(p, p->afe[i - 1]) < 0)
        i--;
    for (; i < p->afe_len && !p->nomem; i++) {
        tc_element *el = clone_element(p, p->afe[i]);
        if (!el)
            return;
        insert_element(p, el);
        push(p, el);
        p->afe[i] = el;
    }
}

static void clear_afe_to_marker(parser *p)
{
    while (p->afe_len) {
        tc_element *e = p->afe[--p->afe_len];
        if (!e)
            break;
    }
}

/* --- Algorithms used by several modes ---------------------------------- */

static void close_p(parser *p)
{
    generate_implied_end_tags(p, TC_TAG_P);
    if (!is_html(current(p), TC_TAG_P))
        perr(p);
    pop_until_html(p, TC_TAG_P);
}

static void close_p_if_in_button_scope(parser *p)
{
    if (in_scope(p, TC_TAG_P, SCOPE_BUTTON))
        close_p(p);
}

static void reset_insertion_mode(parser *p)
{
    size_t i = p->depth;

    while (i-- > 0) {
        const tc_element *node = p->stack[i];
        int last = i == 0;
        if (last && p->context)
            node = p->context;
        if (node->node.ns == TC_NS_HTML) {
            switch (node->node.tag) {
            case TC_TAG_TD:
            case TC_TAG_TH:
                if (!last) {
                    p->mode = M_IN_CELL;
                    return;
                }
                break;
            case TC_TAG_TR:
                p->mode = M_IN_ROW;
                return;
            case TC_TAG_TBODY:
            case TC_TAG_THEAD:
            case TC_TAG_TFOOT:
                p->mode = M_IN_TABLE_BODY;
                return;
            case TC_TAG_CAPTION:
                p->mode = M_IN_CAPTION;
                return;
            case TC_TAG_COLGROUP:
                p->mode = M_IN_COLUMN_GROUP;
                return;
            case TC_TAG_TABLE:
                p->mode = M_IN_TABLE;
                return;
            case TC_TAG_TEMPLATE:
                p->mode = p->tmodes_len ? p->tmodes[p->tmodes_len - 1] : M_IN_BODY;
                return;
            case TC_TAG_HEAD:
                if (!last) {
                    p->mode = M_IN_HEAD;
                    return;
                }
                break;
            case TC_TAG_BODY:
                p->mode = M_IN_BODY;
                return;
            case TC_TAG_FRAMESET:
                p->mode = M_IN_FRAMESET;
                return;
            case TC_TAG_HTML:
                p->mode = p->head ? M_AFTER_HEAD : M_BEFORE_HEAD;
                return;
            default:
                break;
            }
        }
        if (last) {
            p->mode = M_IN_BODY;
            return;
        }
    }
    p->mode = M_IN_BODY;
}

static void push_tmode(parser *p, int mode)
{
    if (p->tmodes_len == p->tmodes_cap) {
        size_t cap = p->tmodes_cap ? p->tmodes_cap * 2 : 8;
        int *grown = realloc(p->tmodes, cap * sizeof *grown);
        if (!grown) {
            p->nomem = 1;
            return;
        }
        p->tmodes = grown;
        p->tmodes_cap = cap;
    }
    p->tmodes[p->tmodes_len++] = mode;
}

static void pop_tmode(parser *p)
{
    if (p->tmodes_len)
        p->tmodes_len--;
}

static void switch_template_mode(parser *p, int mode)
{
    pop_tmode(p);
    push_tmode(p, mode);
    p->mode = mode;
    p->reprocess = 1;
}

/* A <meta> declared an encoding ("change the encoding"). */
static void meta_encoding(parser *p, tc_encoding enc)
{
    if (enc == TC_ENC_NONE || p->confidence != TC_CONFIDENCE_TENTATIVE)
        return;
    if (p->encoding == TC_ENC_UTF_16BE || p->encoding == TC_ENC_UTF_16LE) {
        p->confidence = TC_CONFIDENCE_CERTAIN;
        return;
    }
    if (enc == TC_ENC_UTF_16BE || enc == TC_ENC_UTF_16LE)
        enc = TC_ENC_UTF_8;
    else if (enc == TC_ENC_X_USER_DEFINED)
        enc = TC_ENC_WINDOWS_1252;
    if (enc == p->encoding) {
        p->confidence = TC_CONFIDENCE_CERTAIN;
        return;
    }
    /* Start again from the bytes in the new encoding. */
    p->change_to = enc;
    p->stopped = 1;
}

/* The adoption agency algorithm. Returns 1 if the caller should act as for
   "any other end tag". */
static int adoption_agency(parser *p)
{
    int subject = p->tag;
    tc_element *cur = current(p);
    int outer;

    if (is_html(cur, subject) && afe_index(p, cur) < 0) {
        pop(p);
        return 0;
    }
    for (outer = 0; outer < 8 && !p->nomem; outer++) {
        tc_element *fe = NULL, *furthest = NULL, *common, *node, *last_node, *new_el;
        long fe_afe = -1, fe_stack, fb_stack = -1, node_stack, i;
        size_t bookmark;
        int inner = 0, bookmark_moved = 0;
        tc_node *parent, *ref, *child;

        for (i = (long)p->afe_len - 1; i >= 0; i--) {
            if (!p->afe[i])
                break;
            if (is_html(p->afe[i], subject)) {
                fe = p->afe[i];
                fe_afe = i;
                break;
            }
        }
        if (!fe)
            return 1;
        fe_stack = stack_index(p, fe);
        if (fe_stack < 0) {
            perr(p);
            afe_remove_at(p, (size_t)fe_afe);
            return 0;
        }
        if (!el_in_scope(p, fe, SCOPE_DEFAULT)) {
            perr(p);
            return 0;
        }
        if (fe != current(p))
            perr(p);
        for (i = fe_stack + 1; i < (long)p->depth; i++)
            if (is_special(p->stack[i])) {
                furthest = p->stack[i];
                fb_stack = i;
                break;
            }
        if (!furthest) {
            pop_until_el(p, fe);
            afe_remove_at(p, (size_t)afe_index(p, fe));
            return 0;
        }
        common = p->stack[fe_stack - 1];
        bookmark = (size_t)fe_afe;
        node = last_node = furthest;
        node_stack = fb_stack;
        for (;;) {
            long node_afe;
            inner++;
            node_stack--;
            node = p->stack[node_stack];
            if (node == fe)
                break;
            node_afe = afe_index(p, node);
            if (inner > 3 && node_afe >= 0) {
                afe_remove_at(p, (size_t)node_afe);
                if ((size_t)node_afe < bookmark)
                    bookmark--;
                node_afe = -1;
            }
            if (node_afe < 0) {
                stack_remove_at(p, (size_t)node_stack);
                continue;
            }
            new_el = clone_element(p, node);
            if (!new_el)
                return 0;
            p->afe[node_afe] = new_el;
            p->stack[node_stack] = new_el;
            node = new_el;
            if (!bookmark_moved) {
                bookmark = (size_t)node_afe + 1;
                bookmark_moved = 1;
            }
            tc_dom_append(&node->node, &last_node->node);
            last_node = node;
        }
        insertion_location(p, &common->node, &parent, &ref);
        tc_dom_remove(&last_node->node);
        if (parent->type != TC_NODE_DOCUMENT)
            tc_dom_insert_before(parent, &last_node->node, ref);

        new_el = clone_element(p, fe);
        if (!new_el)
            return 0;
        while ((child = furthest->node.first_child) != NULL)
            tc_dom_append(&new_el->node, child);
        tc_dom_append(&furthest->node, &new_el->node);

        i = afe_index(p, fe);
        if (i >= 0) {
            afe_remove_at(p, (size_t)i);
            if ((size_t)i < bookmark)
                bookmark--;
        }
        if (bookmark > p->afe_len)
            bookmark = p->afe_len;
        afe_insert_at(p, bookmark, new_el);

        stack_remove(p, fe);
        i = stack_index(p, furthest);
        stack_insert_at(p, (size_t)i + 1, new_el);
    }
    return 0;
}

/* --- The insertion modes ---------------------------------------------- */

static void in_body(parser *p, tc_token *t);
static void in_head(parser *p, tc_token *t);
static void in_table(parser *p, tc_token *t);

/* Process (a part of) t with another mode's rules. */
static void using_rules(parser *p, int mode, tc_token *t)
{
    modes[mode](p, t);
}

/* Process the leading whitespace of a character token with another mode's
   rules. */
static void ws_using_rules(parser *p, int mode, tc_token *t, size_t n)
{
    tc_token ws = *t;
    ws.data_len = n;
    using_rules(p, mode, &ws);
    consume(t, n);
}

static const char *const quirky_public_prefixes[] = {
    "+//Silmaril//dtd html Pro v0r11 19970101//",
    "-//AS//DTD HTML 3.0 asWedit + extensions//",
    "-//AdvaSoft Ltd//DTD HTML 3.0 asWedit + extensions//",
    "-//IETF//DTD HTML 2.0 Level 1//",
    "-//IETF//DTD HTML 2.0 Level 2//",
    "-//IETF//DTD HTML 2.0 Strict Level 1//",
    "-//IETF//DTD HTML 2.0 Strict Level 2//",
    "-//IETF//DTD HTML 2.0 Strict//",
    "-//IETF//DTD HTML 2.0//",
    "-//IETF//DTD HTML 2.1E//",
    "-//IETF//DTD HTML 3.0//",
    "-//IETF//DTD HTML 3.2 Final//",
    "-//IETF//DTD HTML 3.2//",
    "-//IETF//DTD HTML 3//",
    "-//IETF//DTD HTML Level 0//",
    "-//IETF//DTD HTML Level 1//",
    "-//IETF//DTD HTML Level 2//",
    "-//IETF//DTD HTML Level 3//",
    "-//IETF//DTD HTML Strict Level 0//",
    "-//IETF//DTD HTML Strict Level 1//",
    "-//IETF//DTD HTML Strict Level 2//",
    "-//IETF//DTD HTML Strict Level 3//",
    "-//IETF//DTD HTML Strict//",
    "-//IETF//DTD HTML//",
    "-//Metrius//DTD Metrius Presentational//",
    "-//Microsoft//DTD Internet Explorer 2.0 HTML Strict//",
    "-//Microsoft//DTD Internet Explorer 2.0 HTML//",
    "-//Microsoft//DTD Internet Explorer 2.0 Tables//",
    "-//Microsoft//DTD Internet Explorer 3.0 HTML Strict//",
    "-//Microsoft//DTD Internet Explorer 3.0 HTML//",
    "-//Microsoft//DTD Internet Explorer 3.0 Tables//",
    "-//Netscape Comm. Corp.//DTD HTML//",
    "-//Netscape Comm. Corp.//DTD Strict HTML//",
    "-//O'Reilly and Associates//DTD HTML 2.0//",
    "-//O'Reilly and Associates//DTD HTML Extended 1.0//",
    "-//O'Reilly and Associates//DTD HTML Extended Relaxed 1.0//",
    "-//SQ//DTD HTML 2.0 HoTMetaL + extensions//",
    "-//SoftQuad Software//DTD HoTMetaL PRO 6.0::19990601::extensions to HTML 4.0//",
    "-//SoftQuad//DTD HoTMetaL PRO 4.0::19971010::extensions to HTML 4.0//",
    "-//Spyglass//DTD HTML 2.0 Extended//",
    "-//Sun Microsystems Corp.//DTD HotJava HTML//",
    "-//Sun Microsystems Corp.//DTD HotJava Strict HTML//",
    "-//W3C//DTD HTML 3 1995-03-24//",
    "-//W3C//DTD HTML 3.2 Draft//",
    "-//W3C//DTD HTML 3.2 Final//",
    "-//W3C//DTD HTML 3.2//",
    "-//W3C//DTD HTML 3.2S Draft//",
    "-//W3C//DTD HTML 4.0 Frameset//",
    "-//W3C//DTD HTML 4.0 Transitional//",
    "-//W3C//DTD HTML Experimental 19960712//",
    "-//W3C//DTD HTML Experimental 970421//",
    "-//W3C//DTD W3 HTML//",
    "-//W3O//DTD W3 HTML 3.0//",
    "-//WebTechs//DTD Mozilla HTML 2.0//",
    "-//WebTechs//DTD Mozilla HTML//"
};

static tc_quirks_mode doctype_mode(const tc_token *t)
{
    const char *pub = t->public_id, *sys = t->system_id;
    size_t i;

    if (t->force_quirks || !t->name || strcmp(t->name, "html") != 0)
        return TC_QUIRKS;
    if (pub && (ascii_ieq(pub, "-//W3O//DTD W3 HTML Strict 3.0//EN//") ||
                ascii_ieq(pub, "-/W3C/DTD HTML 4.0 Transitional/EN") || ascii_ieq(pub, "HTML")))
        return TC_QUIRKS;
    if (sys && ascii_ieq(sys, "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd"))
        return TC_QUIRKS;
    if (pub) {
        for (i = 0; i < TC_ARRAY_LEN(quirky_public_prefixes); i++)
            if (ascii_istarts(pub, quirky_public_prefixes[i]))
                return TC_QUIRKS;
        if ((!sys || !*sys) && (ascii_istarts(pub, "-//W3C//DTD HTML 4.01 Frameset//") ||
                                ascii_istarts(pub, "-//W3C//DTD HTML 4.01 Transitional//")))
            return TC_QUIRKS;
        if (ascii_istarts(pub, "-//W3C//DTD XHTML 1.0 Frameset//") ||
            ascii_istarts(pub, "-//W3C//DTD XHTML 1.0 Transitional//"))
            return TC_LIMITED_QUIRKS;
        if (sys && *sys && (ascii_istarts(pub, "-//W3C//DTD HTML 4.01 Frameset//") ||
                            ascii_istarts(pub, "-//W3C//DTD HTML 4.01 Transitional//")))
            return TC_LIMITED_QUIRKS;
    }
    return TC_NO_QUIRKS;
}

static void initial(parser *p, tc_token *t)
{
    switch (t->type) {
    case TC_TOKEN_CHARS:
        consume(t, ws_prefix(t));
        if (!t->data_len)
            return;
        break;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, &p->doc->node);
        return;
    case TC_TOKEN_DOCTYPE: {
        tc_doctype *dt;
        if (!t->name || strcmp(t->name, "html") != 0 || t->public_id ||
            (t->system_id && strcmp(t->system_id, "about:legacy-compat") != 0))
            perr(p);
        dt = tc_dom_create_doctype(p->doc, t->name, t->public_id, t->system_id);
        if (!dt) {
            p->nomem = 1;
            return;
        }
        tc_dom_append(&p->doc->node, &dt->node);
        p->doc->mode = doctype_mode(t);
        p->mode = M_BEFORE_HTML;
        return;
    }
    default:
        break;
    }
    perr(p);
    p->doc->mode = TC_QUIRKS;
    p->mode = M_BEFORE_HTML;
    p->reprocess = 1;
}

static void before_html(parser *p, tc_token *t)
{
    tc_element *html;

    switch (t->type) {
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, &p->doc->node);
        return;
    case TC_TOKEN_CHARS:
        consume(t, ws_prefix(t));
        if (!t->data_len)
            return;
        break;
    case TC_TOKEN_START_TAG:
        if (p->tag == TC_TAG_HTML) {
            html = create_for_token(p, t, TC_NS_HTML);
            if (!html)
                return;
            insert_at(html, &p->doc->node, NULL);
            push(p, html);
            p->mode = M_BEFORE_HEAD;
            return;
        }
        break;
    case TC_TOKEN_END_TAG:
        if (p->tag != TC_TAG_HEAD && p->tag != TC_TAG_BODY && p->tag != TC_TAG_HTML && p->tag != TC_TAG_BR) {
            perr(p);
            return;
        }
        break;
    default:
        break;
    }
    html = create_named(p, "html");
    if (!html)
        return;
    insert_at(html, &p->doc->node, NULL);
    push(p, html);
    p->mode = M_BEFORE_HEAD;
    p->reprocess = 1;
}

static void before_head(parser *p, tc_token *t)
{
    switch (t->type) {
    case TC_TOKEN_CHARS:
        consume(t, ws_prefix(t));
        if (!t->data_len)
            return;
        break;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, NULL);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        if (p->tag == TC_TAG_HTML) {
            in_body(p, t);
            return;
        }
        if (p->tag == TC_TAG_HEAD) {
            p->head = insert_html(p, t);
            p->mode = M_IN_HEAD;
            return;
        }
        break;
    case TC_TOKEN_END_TAG:
        if (p->tag != TC_TAG_HEAD && p->tag != TC_TAG_BODY && p->tag != TC_TAG_HTML && p->tag != TC_TAG_BR) {
            perr(p);
            return;
        }
        break;
    default:
        break;
    }
    p->head = insert_named(p, "head");
    p->mode = M_IN_HEAD;
    p->reprocess = 1;
}

static void in_head(parser *p, tc_token *t)
{
    size_t n;

    switch (t->type) {
    case TC_TOKEN_CHARS:
        n = ws_prefix(t);
        insert_chars(p, t->data, n);
        consume(t, n);
        if (!t->data_len)
            return;
        break;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, NULL);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        switch (p->tag) {
        case TC_TAG_HTML:
            in_body(p, t);
            return;
        case TC_TAG_BASE:
        case TC_TAG_BASEFONT:
        case TC_TAG_BGSOUND:
        case TC_TAG_LINK:
            insert_html(p, t);
            pop(p);
            p->ack = 1;
            return;
        case TC_TAG_META: {
            const char *charset, *equiv, *content;
            insert_html(p, t);
            pop(p);
            p->ack = 1;
            charset = token_attr(t, "charset");
            equiv = token_attr(t, "http-equiv");
            content = token_attr(t, "content");
            if (charset && tc_encoding_from_label(charset, strlen(charset)) != TC_ENC_NONE)
                meta_encoding(p, tc_encoding_from_label(charset, strlen(charset)));
            else if (equiv && content && ascii_ieq(equiv, "content-type"))
                meta_encoding(p, tc_html_meta_charset(content, strlen(content)));
            return;
        }
        case TC_TAG_TITLE:
            parse_text_element(p, t, TC_TOK_RCDATA);
            return;
        case TC_TAG_NOSCRIPT:
            if (p->scripting) {
                parse_text_element(p, t, TC_TOK_RAWTEXT);
            } else {
                insert_html(p, t);
                p->mode = M_IN_HEAD_NOSCRIPT;
            }
            return;
        case TC_TAG_NOFRAMES:
        case TC_TAG_STYLE:
            parse_text_element(p, t, TC_TOK_RAWTEXT);
            return;
        case TC_TAG_SCRIPT:
            insert_html(p, t);
            set_tokenizer_state(p, TC_TOK_SCRIPT_DATA);
            p->original_mode = p->mode;
            p->mode = M_TEXT;
            return;
        case TC_TAG_TEMPLATE: {
            tc_element *tpl;
            tc_node *parent, *ref;
            afe_marker(p);
            p->frameset_ok = 0;
            p->mode = M_IN_TEMPLATE;
            push_tmode(p, M_IN_TEMPLATE);
            tpl = create_for_token(p, t, TC_NS_HTML);
            if (!tpl)
                return;
            insertion_location(p, NULL, &parent, &ref);
            push(p, tpl);
            /* Declarative shadow roots and content patching (<template for>)
               aren't supported yet: the template stays a plain template. */
            insert_at(tpl, parent, ref);
            return;
        }
        case TC_TAG_HEAD:
            perr(p);
            return;
        default:
            break;
        }
        break;
    case TC_TOKEN_END_TAG:
        switch (p->tag) {
        case TC_TAG_HEAD:
            pop(p);
            p->mode = M_AFTER_HEAD;
            return;
        case TC_TAG_BODY:
        case TC_TAG_HTML:
        case TC_TAG_BR:
            break;
        case TC_TAG_TEMPLATE:
            if (!has_template(p)) {
                perr(p);
                return;
            }
            generate_all_implied_end_tags(p);
            if (!is_html(current(p), TC_TAG_TEMPLATE))
                perr(p);
            pop_until_html(p, TC_TAG_TEMPLATE);
            clear_afe_to_marker(p);
            pop_tmode(p);
            reset_insertion_mode(p);
            return;
        default:
            perr(p);
            return;
        }
        break;
    default:
        break;
    }
    pop(p);
    p->mode = M_AFTER_HEAD;
    p->reprocess = 1;
}

static void in_head_noscript(parser *p, tc_token *t)
{
    size_t n;

    switch (t->type) {
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        switch (p->tag) {
        case TC_TAG_HTML:
            in_body(p, t);
            return;
        case TC_TAG_BASEFONT:
        case TC_TAG_BGSOUND:
        case TC_TAG_LINK:
        case TC_TAG_META:
        case TC_TAG_NOFRAMES:
        case TC_TAG_STYLE:
            in_head(p, t);
            return;
        case TC_TAG_HEAD:
        case TC_TAG_NOSCRIPT:
            perr(p);
            return;
        default:
            break;
        }
        break;
    case TC_TOKEN_END_TAG:
        if (p->tag == TC_TAG_NOSCRIPT) {
            pop(p);
            p->mode = M_IN_HEAD;
            return;
        }
        if (p->tag != TC_TAG_BR) {
            perr(p);
            return;
        }
        break;
    case TC_TOKEN_CHARS:
        n = ws_prefix(t);
        if (n)
            ws_using_rules(p, M_IN_HEAD, t, n);
        if (!t->data_len)
            return;
        break;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        in_head(p, t);
        return;
    default:
        break;
    }
    perr(p);
    pop(p);
    p->mode = M_IN_HEAD;
    p->reprocess = 1;
}

static void after_head(parser *p, tc_token *t)
{
    size_t n;

    switch (t->type) {
    case TC_TOKEN_CHARS:
        n = ws_prefix(t);
        insert_chars(p, t->data, n);
        consume(t, n);
        if (!t->data_len)
            return;
        break;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, NULL);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        switch (p->tag) {
        case TC_TAG_HTML:
            in_body(p, t);
            return;
        case TC_TAG_BODY:
            insert_html(p, t);
            p->frameset_ok = 0;
            p->mode = M_IN_BODY;
            return;
        case TC_TAG_FRAMESET:
            insert_html(p, t);
            p->mode = M_IN_FRAMESET;
            return;
        case TC_TAG_BASE: case TC_TAG_BASEFONT: case TC_TAG_BGSOUND: case TC_TAG_LINK: case TC_TAG_META:
        case TC_TAG_NOFRAMES: case TC_TAG_SCRIPT: case TC_TAG_STYLE: case TC_TAG_TEMPLATE: case TC_TAG_TITLE:
            perr(p);
            if (!p->head)
                return;
            push(p, p->head);
            in_head(p, t);
            stack_remove(p, p->head);
            return;
        case TC_TAG_HEAD:
            perr(p);
            return;
        default:
            break;
        }
        break;
    case TC_TOKEN_END_TAG:
        if (p->tag == TC_TAG_TEMPLATE) {
            in_head(p, t);
            return;
        }
        if (p->tag != TC_TAG_BODY && p->tag != TC_TAG_HTML && p->tag != TC_TAG_BR) {
            perr(p);
            return;
        }
        break;
    default:
        break;
    }
    insert_named(p, "body");
    p->frameset_ok = 1;
    p->mode = M_IN_BODY;
    p->reprocess = 1;
}

/* "Any other end tag" in the in body mode. */
static void any_other_end_tag(parser *p, tc_token *t)
{
    size_t i = p->depth;

    while (i-- > 0) {
        tc_element *node = p->stack[i];
        if (has_token_name(p, node, t)) {
            generate_implied_end_tags(p, p->tag != TC_TAG_UNKNOWN ? p->tag : -1);
            if (node != current(p))
                perr(p);
            pop_until_el(p, node);
            return;
        }
        if (is_special(node)) {
            perr(p);
            return;
        }
    }
}

static int is_hidden_input(const tc_token *t)
{
    const char *type = token_attr(t, "type");
    return type && ascii_ieq(type, "hidden");
}

static void add_missing_attrs(parser *p, tc_element *el, const tc_token *t)
{
    size_t i;
    for (i = 0; i < t->attr_count; i++)
        if (!tc_dom_get_attr(el, t->attrs[i].name) &&
            tc_dom_add_attr(p->doc, el, TC_NS_NONE, NULL, t->attrs[i].name, t->attrs[i].name_len,
                            t->attrs[i].value, t->attrs[i].value_len))
            p->nomem = 1;
}

static int only_closable_left(const parser *p)
{
    size_t i;
    for (i = 0; i < p->depth; i++) {
        const tc_element *el = p->stack[i];
        if (el->node.ns == TC_NS_HTML) {
            switch (el->node.tag) {
            case TC_TAG_DD: case TC_TAG_DT: case TC_TAG_LI: case TC_TAG_OPTGROUP: case TC_TAG_OPTION:
            case TC_TAG_P: case TC_TAG_RB: case TC_TAG_RP: case TC_TAG_RT: case TC_TAG_RTC: case TC_TAG_TBODY:
            case TC_TAG_TD: case TC_TAG_TFOOT: case TC_TAG_TH: case TC_TAG_THEAD: case TC_TAG_TR:
            case TC_TAG_BODY: case TC_TAG_HTML:
                continue;
            default:
                break;
            }
        }
        return 0;
    }
    return 1;
}

static void body_chars(parser *p, tc_token *t)
{
    while (t->data_len && !p->nomem) {
        size_t n = 0;
        if (t->data[0] == '\0') {
            perr(p);
            consume(t, 1);
            continue;
        }
        while (n < t->data_len && t->data[n] != '\0')
            n++;
        reconstruct_afe(p);
        insert_chars(p, t->data, n);
        if (!all_ws(t->data, n))
            p->frameset_ok = 0;
        consume(t, n);
    }
}

static void body_start_tag(parser *p, tc_token *t)
{
    size_t i;

    switch (p->tag) {
    case TC_TAG_HTML:
        perr(p);
        if (!has_template(p) && p->depth)
            add_missing_attrs(p, p->stack[0], t);
        return;
    case TC_TAG_BASE: case TC_TAG_BASEFONT: case TC_TAG_BGSOUND: case TC_TAG_LINK: case TC_TAG_META:
    case TC_TAG_NOFRAMES: case TC_TAG_SCRIPT: case TC_TAG_STYLE: case TC_TAG_TEMPLATE: case TC_TAG_TITLE:
        in_head(p, t);
        return;
    case TC_TAG_BODY:
        perr(p);
        if (p->depth < 2 || !is_html(p->stack[1], TC_TAG_BODY) || has_template(p))
            return;
        p->frameset_ok = 0;
        add_missing_attrs(p, p->stack[1], t);
        return;
    case TC_TAG_FRAMESET:
        perr(p);
        if (p->depth < 2 || !is_html(p->stack[1], TC_TAG_BODY) || !p->frameset_ok)
            return;
        tc_dom_remove(&p->stack[1]->node);
        p->depth = 1;
        insert_html(p, t);
        p->mode = M_IN_FRAMESET;
        return;
    case TC_TAG_ADDRESS: case TC_TAG_ARTICLE: case TC_TAG_ASIDE: case TC_TAG_BLOCKQUOTE: case TC_TAG_CENTER:
    case TC_TAG_DETAILS: case TC_TAG_DIALOG: case TC_TAG_DIR: case TC_TAG_DIV: case TC_TAG_DL:
    case TC_TAG_FIELDSET: case TC_TAG_FIGCAPTION: case TC_TAG_FIGURE: case TC_TAG_FOOTER: case TC_TAG_HEADER:
    case TC_TAG_HGROUP: case TC_TAG_MAIN: case TC_TAG_MENU: case TC_TAG_NAV: case TC_TAG_OL: case TC_TAG_P:
    case TC_TAG_SEARCH: case TC_TAG_SECTION: case TC_TAG_SUMMARY: case TC_TAG_UL:
        close_p_if_in_button_scope(p);
        insert_html(p, t);
        return;
    case TC_TAG_H1: case TC_TAG_H2: case TC_TAG_H3: case TC_TAG_H4: case TC_TAG_H5: case TC_TAG_H6:
        close_p_if_in_button_scope(p);
        if (current(p) && is_heading(current(p))) {
            perr(p);
            pop(p);
        }
        insert_html(p, t);
        return;
    case TC_TAG_PRE:
    case TC_TAG_LISTING:
        close_p_if_in_button_scope(p);
        insert_html(p, t);
        p->skip_lf = 1;
        p->frameset_ok = 0;
        return;
    case TC_TAG_FORM: {
        tc_element *form;
        int tpl = in_template_contents(p);
        if (p->form && !tpl) {
            perr(p);
            return;
        }
        close_p_if_in_button_scope(p);
        form = insert_html(p, t);
        if (!tpl)
            p->form = form;
        return;
    }
    case TC_TAG_LI:
    case TC_TAG_DD:
    case TC_TAG_DT:
        p->frameset_ok = 0;
        for (i = p->depth; i-- > 0;) {
            tc_element *node = p->stack[i];
            int match = p->tag == TC_TAG_LI ? is_html(node, TC_TAG_LI)
                                            : is_html(node, TC_TAG_DD) || is_html(node, TC_TAG_DT);
            if (match) {
                int tag = node->node.tag;
                generate_implied_end_tags(p, tag);
                if (!is_html(current(p), tag))
                    perr(p);
                pop_until_html(p, tag);
                break;
            }
            if (is_special(node) && !is_html(node, TC_TAG_ADDRESS) && !is_html(node, TC_TAG_DIV) &&
                !is_html(node, TC_TAG_P))
                break;
        }
        close_p_if_in_button_scope(p);
        insert_html(p, t);
        return;
    case TC_TAG_PLAINTEXT:
        close_p_if_in_button_scope(p);
        insert_html(p, t);
        set_tokenizer_state(p, TC_TOK_PLAINTEXT);
        return;
    case TC_TAG_BUTTON:
        if (in_scope(p, TC_TAG_BUTTON, SCOPE_DEFAULT)) {
            perr(p);
            generate_implied_end_tags(p, -1);
            pop_until_html(p, TC_TAG_BUTTON);
        }
        reconstruct_afe(p);
        insert_html(p, t);
        p->frameset_ok = 0;
        return;
    case TC_TAG_A:
        for (i = p->afe_len; i-- > 0;) {
            tc_element *a = p->afe[i];
            if (!a)
                break;
            if (is_html(a, TC_TAG_A)) {
                long k;
                perr(p);
                adoption_agency(p);
                k = afe_index(p, a);
                if (k >= 0)
                    afe_remove_at(p, (size_t)k);
                stack_remove(p, a);
                break;
            }
        }
        reconstruct_afe(p);
        {
            tc_element *el = insert_html(p, t);
            if (el)
                afe_push(p, el);
        }
        return;
    case TC_TAG_B: case TC_TAG_BIG: case TC_TAG_CODE: case TC_TAG_EM: case TC_TAG_FONT: case TC_TAG_I:
    case TC_TAG_S: case TC_TAG_SMALL: case TC_TAG_STRIKE: case TC_TAG_STRONG: case TC_TAG_TT: case TC_TAG_U: {
        tc_element *el;
        reconstruct_afe(p);
        el = insert_html(p, t);
        if (el)
            afe_push(p, el);
        return;
    }
    case TC_TAG_NOBR: {
        tc_element *el;
        reconstruct_afe(p);
        if (in_scope(p, TC_TAG_NOBR, SCOPE_DEFAULT)) {
            perr(p);
            if (adoption_agency(p))
                any_other_end_tag(p, t);
            reconstruct_afe(p);
        }
        el = insert_html(p, t);
        if (el)
            afe_push(p, el);
        return;
    }
    case TC_TAG_APPLET:
    case TC_TAG_MARQUEE:
    case TC_TAG_OBJECT:
        reconstruct_afe(p);
        insert_html(p, t);
        afe_marker(p);
        p->frameset_ok = 0;
        return;
    case TC_TAG_TABLE:
        if (p->doc->mode != TC_QUIRKS)
            close_p_if_in_button_scope(p);
        insert_html(p, t);
        p->frameset_ok = 0;
        p->mode = M_IN_TABLE;
        return;
    case TC_TAG_AREA: case TC_TAG_BR: case TC_TAG_EMBED: case TC_TAG_IMG: case TC_TAG_KEYGEN: case TC_TAG_WBR:
        reconstruct_afe(p);
        insert_html(p, t);
        pop(p);
        p->ack = 1;
        p->frameset_ok = 0;
        return;
    case TC_TAG_INPUT:
        if (is_html(p->context, TC_TAG_SELECT)) {
            perr(p);
            return;
        }
        if (in_scope(p, TC_TAG_SELECT, SCOPE_DEFAULT)) {
            perr(p);
            pop_until_html(p, TC_TAG_SELECT);
        }
        reconstruct_afe(p);
        insert_html(p, t);
        pop(p);
        p->ack = 1;
        if (!is_hidden_input(t))
            p->frameset_ok = 0;
        return;
    case TC_TAG_PARAM:
    case TC_TAG_SOURCE:
    case TC_TAG_TRACK:
        insert_html(p, t);
        pop(p);
        p->ack = 1;
        return;
    case TC_TAG_HR:
        close_p_if_in_button_scope(p);
        if (in_scope(p, TC_TAG_SELECT, SCOPE_DEFAULT)) {
            generate_implied_end_tags(p, -1);
            if (in_scope(p, TC_TAG_OPTION, SCOPE_DEFAULT) || in_scope(p, TC_TAG_OPTGROUP, SCOPE_DEFAULT))
                perr(p);
        }
        insert_html(p, t);
        pop(p);
        p->ack = 1;
        p->frameset_ok = 0;
        return;
    case TC_TAG_IMAGE:
        perr(p);
        t->name = "img";
        t->name_len = 3;
        p->tag = TC_TAG_IMG;
        p->reprocess = 1;
        return;
    case TC_TAG_TEXTAREA:
        insert_html(p, t);
        p->skip_lf = 1;
        set_tokenizer_state(p, TC_TOK_RCDATA);
        p->original_mode = p->mode;
        p->frameset_ok = 0;
        p->mode = M_TEXT;
        return;
    case TC_TAG_XMP:
        close_p_if_in_button_scope(p);
        reconstruct_afe(p);
        p->frameset_ok = 0;
        parse_text_element(p, t, TC_TOK_RAWTEXT);
        return;
    case TC_TAG_IFRAME:
        p->frameset_ok = 0;
        parse_text_element(p, t, TC_TOK_RAWTEXT);
        return;
    case TC_TAG_NOEMBED:
        parse_text_element(p, t, TC_TOK_RAWTEXT);
        return;
    case TC_TAG_NOSCRIPT:
        if (p->scripting) {
            parse_text_element(p, t, TC_TOK_RAWTEXT);
            return;
        }
        break;
    case TC_TAG_SELECT:
        if (is_html(p->context, TC_TAG_SELECT)) {
            perr(p);
            return;
        }
        if (in_scope(p, TC_TAG_SELECT, SCOPE_DEFAULT)) {
            perr(p);
            pop_until_html(p, TC_TAG_SELECT);
            return;
        }
        reconstruct_afe(p);
        insert_html(p, t);
        p->frameset_ok = 0;
        return;
    case TC_TAG_OPTION:
        if (in_scope(p, TC_TAG_SELECT, SCOPE_DEFAULT)) {
            generate_implied_end_tags(p, TC_TAG_OPTGROUP);
            if (in_scope(p, TC_TAG_OPTION, SCOPE_DEFAULT))
                perr(p);
        } else if (is_html(current(p), TC_TAG_OPTION)) {
            pop(p);
        }
        reconstruct_afe(p);
        insert_html(p, t);
        return;
    case TC_TAG_OPTGROUP:
        if (in_scope(p, TC_TAG_SELECT, SCOPE_DEFAULT)) {
            generate_implied_end_tags(p, -1);
            if (in_scope(p, TC_TAG_OPTION, SCOPE_DEFAULT) || in_scope(p, TC_TAG_OPTGROUP, SCOPE_DEFAULT))
                perr(p);
        } else if (is_html(current(p), TC_TAG_OPTION)) {
            pop(p);
        }
        reconstruct_afe(p);
        insert_html(p, t);
        return;
    case TC_TAG_RB:
    case TC_TAG_RTC:
        if (in_scope(p, TC_TAG_RUBY, SCOPE_DEFAULT)) {
            generate_implied_end_tags(p, -1);
            if (!is_html(current(p), TC_TAG_RUBY))
                perr(p);
        }
        insert_html(p, t);
        return;
    case TC_TAG_RP:
    case TC_TAG_RT:
        if (in_scope(p, TC_TAG_RUBY, SCOPE_DEFAULT)) {
            generate_implied_end_tags(p, TC_TAG_RTC);
            if (!is_html(current(p), TC_TAG_RTC) && !is_html(current(p), TC_TAG_RUBY))
                perr(p);
        }
        insert_html(p, t);
        return;
    case TC_TAG_MATH:
    case TC_TAG_SVG:
        reconstruct_afe(p);
        insert_for_token(p, t, p->tag == TC_TAG_MATH ? TC_NS_MATHML : TC_NS_SVG);
        if (t->self_closing) {
            pop(p);
            p->ack = 1;
        }
        return;
    case TC_TAG_CAPTION: case TC_TAG_COL: case TC_TAG_COLGROUP: case TC_TAG_FRAME: case TC_TAG_HEAD:
    case TC_TAG_TBODY: case TC_TAG_TD: case TC_TAG_TFOOT: case TC_TAG_TH: case TC_TAG_THEAD: case TC_TAG_TR:
        perr(p);
        return;
    default:
        break;
    }
    reconstruct_afe(p);
    insert_html(p, t);
}

static void body_end_tag(parser *p, tc_token *t)
{
    switch (p->tag) {
    case TC_TAG_TEMPLATE:
        in_head(p, t);
        return;
    case TC_TAG_BODY:
    case TC_TAG_HTML:
        if (!in_scope(p, TC_TAG_BODY, SCOPE_DEFAULT)) {
            perr(p);
            return;
        }
        if (!only_closable_left(p))
            perr(p);
        p->mode = M_AFTER_BODY;
        if (p->tag == TC_TAG_HTML)
            p->reprocess = 1;
        return;
    case TC_TAG_ADDRESS: case TC_TAG_ARTICLE: case TC_TAG_ASIDE: case TC_TAG_BLOCKQUOTE: case TC_TAG_BUTTON:
    case TC_TAG_CENTER: case TC_TAG_DETAILS: case TC_TAG_DIALOG: case TC_TAG_DIR: case TC_TAG_DIV:
    case TC_TAG_DL: case TC_TAG_FIELDSET: case TC_TAG_FIGCAPTION: case TC_TAG_FIGURE: case TC_TAG_FOOTER:
    case TC_TAG_HEADER: case TC_TAG_HGROUP: case TC_TAG_LISTING: case TC_TAG_MAIN: case TC_TAG_MENU:
    case TC_TAG_NAV: case TC_TAG_OL: case TC_TAG_PRE: case TC_TAG_SEARCH: case TC_TAG_SECTION:
    case TC_TAG_SELECT: case TC_TAG_SUMMARY: case TC_TAG_UL:
        if (!in_scope(p, p->tag, SCOPE_DEFAULT)) {
            perr(p);
            return;
        }
        generate_implied_end_tags(p, -1);
        if (!is_html(current(p), p->tag))
            perr(p);
        pop_until_html(p, p->tag);
        return;
    case TC_TAG_FORM:
        if (!in_template_contents(p)) {
            tc_element *node = p->form;
            p->form = NULL;
            if (!node || !el_in_scope(p, node, SCOPE_DEFAULT)) {
                perr(p);
                return;
            }
            generate_implied_end_tags(p, -1);
            if (current(p) != node)
                perr(p);
            stack_remove(p, node);
        } else {
            if (!in_scope(p, TC_TAG_FORM, SCOPE_DEFAULT)) {
                perr(p);
                return;
            }
            generate_implied_end_tags(p, -1);
            if (!is_html(current(p), TC_TAG_FORM))
                perr(p);
            pop_until_html(p, TC_TAG_FORM);
        }
        return;
    case TC_TAG_P:
        if (!in_scope(p, TC_TAG_P, SCOPE_BUTTON)) {
            perr(p);
            insert_named(p, "p");
        }
        close_p(p);
        return;
    case TC_TAG_LI:
        if (!in_scope(p, TC_TAG_LI, SCOPE_LIST_ITEM)) {
            perr(p);
            return;
        }
        generate_implied_end_tags(p, TC_TAG_LI);
        if (!is_html(current(p), TC_TAG_LI))
            perr(p);
        pop_until_html(p, TC_TAG_LI);
        return;
    case TC_TAG_DD:
    case TC_TAG_DT:
        if (!in_scope(p, p->tag, SCOPE_DEFAULT)) {
            perr(p);
            return;
        }
        generate_implied_end_tags(p, p->tag);
        if (!is_html(current(p), p->tag))
            perr(p);
        pop_until_html(p, p->tag);
        return;
    case TC_TAG_H1: case TC_TAG_H2: case TC_TAG_H3: case TC_TAG_H4: case TC_TAG_H5: case TC_TAG_H6:
        if (!heading_in_scope(p)) {
            perr(p);
            return;
        }
        generate_implied_end_tags(p, -1);
        if (!is_html(current(p), p->tag))
            perr(p);
        while (p->depth) {
            tc_element *el = current(p);
            pop(p);
            if (is_heading(el))
                break;
        }
        return;
    case TC_TAG_A: case TC_TAG_B: case TC_TAG_BIG: case TC_TAG_CODE: case TC_TAG_EM: case TC_TAG_FONT:
    case TC_TAG_I: case TC_TAG_NOBR: case TC_TAG_S: case TC_TAG_SMALL: case TC_TAG_STRIKE: case TC_TAG_STRONG:
    case TC_TAG_TT: case TC_TAG_U:
        if (adoption_agency(p))
            any_other_end_tag(p, t);
        return;
    case TC_TAG_APPLET:
    case TC_TAG_MARQUEE:
    case TC_TAG_OBJECT:
        if (!in_scope(p, p->tag, SCOPE_DEFAULT)) {
            perr(p);
            return;
        }
        generate_implied_end_tags(p, -1);
        if (!is_html(current(p), p->tag))
            perr(p);
        pop_until_html(p, p->tag);
        clear_afe_to_marker(p);
        return;
    case TC_TAG_BR: {
        tc_token br;
        perr(p);
        memset(&br, 0, sizeof br);
        br.type = TC_TOKEN_START_TAG;
        br.name = "br";
        br.name_len = 2;
        body_start_tag(p, &br);
        return;
    }
    default:
        any_other_end_tag(p, t);
        return;
    }
}

static void in_body(parser *p, tc_token *t)
{
    switch (t->type) {
    case TC_TOKEN_CHARS:
        body_chars(p, t);
        return;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, NULL);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        body_start_tag(p, t);
        return;
    case TC_TOKEN_END_TAG:
        body_end_tag(p, t);
        return;
    case TC_TOKEN_EOF:
        if (p->tmodes_len) {
            using_rules(p, M_IN_TEMPLATE, t);
            return;
        }
        if (!only_closable_left(p))
            perr(p);
        p->stopped = 1;
        return;
    }
}

static void text_mode(parser *p, tc_token *t)
{
    switch (t->type) {
    case TC_TOKEN_CHARS:
        insert_chars(p, t->data, t->data_len);
        consume(t, t->data_len);
        return;
    case TC_TOKEN_EOF:
        perr(p);
        pop(p);
        p->mode = p->original_mode;
        p->reprocess = 1;
        return;
    case TC_TOKEN_END_TAG:
        /* </script> would run the script here (Sprint 5). */
        pop(p);
        p->mode = p->original_mode;
        return;
    default:
        return;
    }
}

static void clear_to_table_context(parser *p)
{
    while (p->depth && !is_html(current(p), TC_TAG_TABLE) && !is_html(current(p), TC_TAG_TEMPLATE) &&
           !is_html(current(p), TC_TAG_HTML))
        pop(p);
}

static void clear_to_table_body_context(parser *p)
{
    while (p->depth && !is_html(current(p), TC_TAG_TBODY) && !is_html(current(p), TC_TAG_TFOOT) &&
           !is_html(current(p), TC_TAG_THEAD) && !is_html(current(p), TC_TAG_TEMPLATE) &&
           !is_html(current(p), TC_TAG_HTML))
        pop(p);
}

static void clear_to_table_row_context(parser *p)
{
    while (p->depth && !is_html(current(p), TC_TAG_TR) && !is_html(current(p), TC_TAG_TEMPLATE) &&
           !is_html(current(p), TC_TAG_HTML))
        pop(p);
}

static void in_table(parser *p, tc_token *t)
{
    tc_element *cur = current(p);

    switch (t->type) {
    case TC_TOKEN_CHARS:
        if (is_html(cur, TC_TAG_TABLE) || is_html(cur, TC_TAG_TBODY) || is_html(cur, TC_TAG_TEMPLATE) ||
            is_html(cur, TC_TAG_TFOOT) || is_html(cur, TC_TAG_THEAD) || is_html(cur, TC_TAG_TR)) {
            tc_buf_clear(&p->table_chars);
            p->original_mode = p->mode;
            p->mode = M_IN_TABLE_TEXT;
            p->reprocess = 1;
            return;
        }
        break;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, NULL);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        switch (p->tag) {
        case TC_TAG_CAPTION:
            clear_to_table_context(p);
            afe_marker(p);
            insert_html(p, t);
            p->mode = M_IN_CAPTION;
            return;
        case TC_TAG_COLGROUP:
            clear_to_table_context(p);
            insert_html(p, t);
            p->mode = M_IN_COLUMN_GROUP;
            return;
        case TC_TAG_COL:
            clear_to_table_context(p);
            insert_named(p, "colgroup");
            p->mode = M_IN_COLUMN_GROUP;
            p->reprocess = 1;
            return;
        case TC_TAG_TBODY:
        case TC_TAG_TFOOT:
        case TC_TAG_THEAD:
            clear_to_table_context(p);
            insert_html(p, t);
            p->mode = M_IN_TABLE_BODY;
            return;
        case TC_TAG_TD:
        case TC_TAG_TH:
        case TC_TAG_TR:
            clear_to_table_context(p);
            insert_named(p, "tbody");
            p->mode = M_IN_TABLE_BODY;
            p->reprocess = 1;
            return;
        case TC_TAG_TABLE:
            perr(p);
            if (!in_scope(p, TC_TAG_TABLE, SCOPE_TABLE))
                return;
            pop_until_html(p, TC_TAG_TABLE);
            reset_insertion_mode(p);
            p->reprocess = 1;
            return;
        case TC_TAG_STYLE:
        case TC_TAG_SCRIPT:
        case TC_TAG_TEMPLATE:
            in_head(p, t);
            return;
        case TC_TAG_INPUT:
            if (!is_hidden_input(t))
                break;
            perr(p);
            insert_html(p, t);
            pop(p);
            p->ack = 1;
            return;
        case TC_TAG_FORM: {
            tc_element *form;
            int tpl = in_template_contents(p);
            perr(p);
            if (p->form && !tpl)
                return;
            form = insert_html(p, t);
            if (!tpl)
                p->form = form;
            pop(p);
            return;
        }
        default:
            break;
        }
        break;
    case TC_TOKEN_END_TAG:
        switch (p->tag) {
        case TC_TAG_TABLE:
            if (!in_scope(p, TC_TAG_TABLE, SCOPE_TABLE)) {
                perr(p);
                return;
            }
            pop_until_html(p, TC_TAG_TABLE);
            reset_insertion_mode(p);
            return;
        case TC_TAG_BODY: case TC_TAG_CAPTION: case TC_TAG_COL: case TC_TAG_COLGROUP: case TC_TAG_HTML:
        case TC_TAG_TBODY: case TC_TAG_TD: case TC_TAG_TFOOT: case TC_TAG_TH: case TC_TAG_THEAD: case TC_TAG_TR:
            perr(p);
            return;
        case TC_TAG_TEMPLATE:
            in_head(p, t);
            return;
        default:
            break;
        }
        break;
    case TC_TOKEN_EOF:
        in_body(p, t);
        return;
    }
    /* Anything else: foster parent. */
    perr(p);
    p->foster = 1;
    in_body(p, t);
    p->foster = 0;
}

static void in_table_text(parser *p, tc_token *t)
{
    if (t->type == TC_TOKEN_CHARS) {
        size_t i, start = 0;
        for (i = 0; i <= t->data_len; i++) {
            if (i == t->data_len || t->data[i] == '\0') {
                if (tc_buf_append(&p->table_chars, t->data + start, i - start))
                    p->nomem = 1;
                if (i < t->data_len)
                    perr(p);
                start = i + 1;
            }
        }
        consume(t, t->data_len);
        return;
    }
    if (p->table_chars.len) {
        tc_token chars;
        memset(&chars, 0, sizeof chars);
        chars.type = TC_TOKEN_CHARS;
        chars.data = p->table_chars.data;
        chars.data_len = p->table_chars.len;
        if (!all_ws(chars.data, chars.data_len)) {
            /* Reprocess with in table's "anything else": foster parenting. */
            perr(p);
            p->foster = 1;
            body_chars(p, &chars);
            p->foster = 0;
        } else {
            insert_chars(p, chars.data, chars.data_len);
        }
        tc_buf_clear(&p->table_chars);
    }
    p->mode = p->original_mode;
    p->reprocess = 1;
}

static void in_caption(parser *p, tc_token *t)
{
    if (t->type == TC_TOKEN_END_TAG && p->tag == TC_TAG_CAPTION) {
        if (!in_scope(p, TC_TAG_CAPTION, SCOPE_TABLE)) {
            perr(p);
            return;
        }
        generate_implied_end_tags(p, -1);
        if (!is_html(current(p), TC_TAG_CAPTION))
            perr(p);
        pop_until_html(p, TC_TAG_CAPTION);
        clear_afe_to_marker(p);
        p->mode = M_IN_TABLE;
        return;
    }
    if ((t->type == TC_TOKEN_START_TAG &&
         (p->tag == TC_TAG_CAPTION || p->tag == TC_TAG_COL || p->tag == TC_TAG_COLGROUP || p->tag == TC_TAG_TBODY ||
          p->tag == TC_TAG_TD || p->tag == TC_TAG_TFOOT || p->tag == TC_TAG_TH || p->tag == TC_TAG_THEAD ||
          p->tag == TC_TAG_TR)) ||
        (t->type == TC_TOKEN_END_TAG && p->tag == TC_TAG_TABLE)) {
        if (!in_scope(p, TC_TAG_CAPTION, SCOPE_TABLE)) {
            perr(p);
            return;
        }
        generate_implied_end_tags(p, -1);
        if (!is_html(current(p), TC_TAG_CAPTION))
            perr(p);
        pop_until_html(p, TC_TAG_CAPTION);
        clear_afe_to_marker(p);
        p->mode = M_IN_TABLE;
        p->reprocess = 1;
        return;
    }
    if (t->type == TC_TOKEN_END_TAG &&
        (p->tag == TC_TAG_BODY || p->tag == TC_TAG_COL || p->tag == TC_TAG_COLGROUP || p->tag == TC_TAG_HTML ||
         p->tag == TC_TAG_TBODY || p->tag == TC_TAG_TD || p->tag == TC_TAG_TFOOT || p->tag == TC_TAG_TH ||
         p->tag == TC_TAG_THEAD || p->tag == TC_TAG_TR)) {
        perr(p);
        return;
    }
    in_body(p, t);
}

static void in_column_group(parser *p, tc_token *t)
{
    size_t n;

    switch (t->type) {
    case TC_TOKEN_CHARS:
        n = ws_prefix(t);
        insert_chars(p, t->data, n);
        consume(t, n);
        if (!t->data_len)
            return;
        break;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, NULL);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        if (p->tag == TC_TAG_HTML) {
            in_body(p, t);
            return;
        }
        if (p->tag == TC_TAG_COL) {
            insert_html(p, t);
            pop(p);
            p->ack = 1;
            return;
        }
        if (p->tag == TC_TAG_TEMPLATE) {
            in_head(p, t);
            return;
        }
        break;
    case TC_TOKEN_END_TAG:
        if (p->tag == TC_TAG_COLGROUP) {
            if (!is_html(current(p), TC_TAG_COLGROUP)) {
                perr(p);
                return;
            }
            pop(p);
            p->mode = M_IN_TABLE;
            return;
        }
        if (p->tag == TC_TAG_COL) {
            perr(p);
            return;
        }
        if (p->tag == TC_TAG_TEMPLATE) {
            in_head(p, t);
            return;
        }
        break;
    case TC_TOKEN_EOF:
        in_body(p, t);
        return;
    }
    if (!is_html(current(p), TC_TAG_COLGROUP)) {
        perr(p);
        if (t->type == TC_TOKEN_CHARS) {
            /* Ignore the characters up to the next whitespace. */
            size_t skip = 0;
            while (skip < t->data_len && !is_ws_byte(t->data[skip]))
                skip++;
            consume(t, skip);
        }
        return;
    }
    pop(p);
    p->mode = M_IN_TABLE;
    p->reprocess = 1;
}

static void in_table_body(parser *p, tc_token *t)
{
    if (t->type == TC_TOKEN_START_TAG) {
        switch (p->tag) {
        case TC_TAG_TR:
            clear_to_table_body_context(p);
            insert_html(p, t);
            p->mode = M_IN_ROW;
            return;
        case TC_TAG_TH:
        case TC_TAG_TD:
            perr(p);
            clear_to_table_body_context(p);
            insert_named(p, "tr");
            p->mode = M_IN_ROW;
            p->reprocess = 1;
            return;
        case TC_TAG_CAPTION: case TC_TAG_COL: case TC_TAG_COLGROUP: case TC_TAG_TBODY: case TC_TAG_TFOOT:
        case TC_TAG_THEAD:
            goto close_section;
        default:
            break;
        }
    } else if (t->type == TC_TOKEN_END_TAG) {
        switch (p->tag) {
        case TC_TAG_TBODY:
        case TC_TAG_TFOOT:
        case TC_TAG_THEAD:
            if (!in_scope(p, p->tag, SCOPE_TABLE)) {
                perr(p);
                return;
            }
            clear_to_table_body_context(p);
            pop(p);
            p->mode = M_IN_TABLE;
            return;
        case TC_TAG_TABLE:
            goto close_section;
        case TC_TAG_BODY: case TC_TAG_CAPTION: case TC_TAG_COL: case TC_TAG_COLGROUP: case TC_TAG_HTML:
        case TC_TAG_TD: case TC_TAG_TH: case TC_TAG_TR:
            perr(p);
            return;
        default:
            break;
        }
    }
    in_table(p, t);
    return;

close_section:
    if (!in_scope(p, TC_TAG_TBODY, SCOPE_TABLE) && !in_scope(p, TC_TAG_THEAD, SCOPE_TABLE) &&
        !in_scope(p, TC_TAG_TFOOT, SCOPE_TABLE)) {
        perr(p);
        return;
    }
    clear_to_table_body_context(p);
    pop(p);
    p->mode = M_IN_TABLE;
    p->reprocess = 1;
}

static void in_row(parser *p, tc_token *t)
{
    if (t->type == TC_TOKEN_START_TAG) {
        switch (p->tag) {
        case TC_TAG_TH:
        case TC_TAG_TD:
            clear_to_table_row_context(p);
            insert_html(p, t);
            p->mode = M_IN_CELL;
            afe_marker(p);
            return;
        case TC_TAG_CAPTION: case TC_TAG_COL: case TC_TAG_COLGROUP: case TC_TAG_TBODY: case TC_TAG_TFOOT:
        case TC_TAG_THEAD: case TC_TAG_TR:
            goto close_row;
        default:
            break;
        }
    } else if (t->type == TC_TOKEN_END_TAG) {
        switch (p->tag) {
        case TC_TAG_TR:
            if (!in_scope(p, TC_TAG_TR, SCOPE_TABLE)) {
                perr(p);
                return;
            }
            clear_to_table_row_context(p);
            pop(p);
            p->mode = M_IN_TABLE_BODY;
            return;
        case TC_TAG_TABLE:
            goto close_row;
        case TC_TAG_TBODY:
        case TC_TAG_TFOOT:
        case TC_TAG_THEAD:
            if (!in_scope(p, p->tag, SCOPE_TABLE)) {
                perr(p);
                return;
            }
            goto close_row;
        case TC_TAG_BODY: case TC_TAG_CAPTION: case TC_TAG_COL: case TC_TAG_COLGROUP: case TC_TAG_HTML:
        case TC_TAG_TD: case TC_TAG_TH:
            perr(p);
            return;
        default:
            break;
        }
    }
    in_table(p, t);
    return;

close_row:
    if (!in_scope(p, TC_TAG_TR, SCOPE_TABLE)) {
        perr(p);
        return;
    }
    clear_to_table_row_context(p);
    pop(p);
    p->mode = M_IN_TABLE_BODY;
    p->reprocess = 1;
}

static void close_cell(parser *p)
{
    generate_implied_end_tags(p, -1);
    if (!is_html(current(p), TC_TAG_TD) && !is_html(current(p), TC_TAG_TH))
        perr(p);
    while (p->depth) {
        tc_element *el = current(p);
        pop(p);
        if (is_html(el, TC_TAG_TD) || is_html(el, TC_TAG_TH))
            break;
    }
    clear_afe_to_marker(p);
    p->mode = M_IN_ROW;
}

static void in_cell(parser *p, tc_token *t)
{
    if (t->type == TC_TOKEN_END_TAG) {
        switch (p->tag) {
        case TC_TAG_TD:
        case TC_TAG_TH:
            if (!in_scope(p, p->tag, SCOPE_TABLE)) {
                perr(p);
                return;
            }
            generate_implied_end_tags(p, -1);
            if (!is_html(current(p), p->tag))
                perr(p);
            pop_until_html(p, p->tag);
            clear_afe_to_marker(p);
            p->mode = M_IN_ROW;
            return;
        case TC_TAG_BODY: case TC_TAG_CAPTION: case TC_TAG_COL: case TC_TAG_COLGROUP: case TC_TAG_HTML:
            perr(p);
            return;
        case TC_TAG_TABLE: case TC_TAG_TBODY: case TC_TAG_TFOOT: case TC_TAG_THEAD: case TC_TAG_TR:
            if (!in_scope(p, p->tag, SCOPE_TABLE)) {
                perr(p);
                return;
            }
            close_cell(p);
            p->reprocess = 1;
            return;
        default:
            break;
        }
    } else if (t->type == TC_TOKEN_START_TAG) {
        switch (p->tag) {
        case TC_TAG_CAPTION: case TC_TAG_COL: case TC_TAG_COLGROUP: case TC_TAG_TBODY: case TC_TAG_TD:
        case TC_TAG_TFOOT: case TC_TAG_TH: case TC_TAG_THEAD: case TC_TAG_TR:
            if (!in_scope(p, TC_TAG_TD, SCOPE_TABLE) && !in_scope(p, TC_TAG_TH, SCOPE_TABLE)) {
                perr(p); /* can't happen, says the standard; be safe */
                return;
            }
            close_cell(p);
            p->reprocess = 1;
            return;
        default:
            break;
        }
    }
    in_body(p, t);
}

static void in_template(parser *p, tc_token *t)
{
    switch (t->type) {
    case TC_TOKEN_CHARS:
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
    case TC_TOKEN_DOCTYPE:
        in_body(p, t);
        return;
    case TC_TOKEN_START_TAG:
        switch (p->tag) {
        case TC_TAG_BASE: case TC_TAG_BASEFONT: case TC_TAG_BGSOUND: case TC_TAG_LINK: case TC_TAG_META:
        case TC_TAG_NOFRAMES: case TC_TAG_SCRIPT: case TC_TAG_STYLE: case TC_TAG_TEMPLATE: case TC_TAG_TITLE:
            in_head(p, t);
            return;
        case TC_TAG_CAPTION: case TC_TAG_COLGROUP: case TC_TAG_TBODY: case TC_TAG_TFOOT: case TC_TAG_THEAD:
            switch_template_mode(p, M_IN_TABLE);
            return;
        case TC_TAG_COL:
            switch_template_mode(p, M_IN_COLUMN_GROUP);
            return;
        case TC_TAG_TR:
            switch_template_mode(p, M_IN_TABLE_BODY);
            return;
        case TC_TAG_TD:
        case TC_TAG_TH:
            switch_template_mode(p, M_IN_ROW);
            return;
        default:
            switch_template_mode(p, M_IN_BODY);
            return;
        }
    case TC_TOKEN_END_TAG:
        if (p->tag == TC_TAG_TEMPLATE)
            in_head(p, t);
        else
            perr(p);
        return;
    case TC_TOKEN_EOF:
        if (!has_template(p)) {
            p->stopped = 1;
            return;
        }
        perr(p);
        pop_until_html(p, TC_TAG_TEMPLATE);
        clear_afe_to_marker(p);
        pop_tmode(p);
        reset_insertion_mode(p);
        p->reprocess = 1;
        return;
    }
}

static void after_body(parser *p, tc_token *t)
{
    size_t n;

    switch (t->type) {
    case TC_TOKEN_CHARS:
        n = ws_prefix(t);
        if (n)
            ws_using_rules(p, M_IN_BODY, t, n);
        if (!t->data_len)
            return;
        break;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        if (p->depth)
            insert_comment_or_pi(p, t, &p->stack[0]->node);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        if (p->tag == TC_TAG_HTML) {
            in_body(p, t);
            return;
        }
        break;
    case TC_TOKEN_END_TAG:
        if (p->tag == TC_TAG_HTML) {
            if (p->context)
                perr(p);
            else
                p->mode = M_AFTER_AFTER_BODY;
            return;
        }
        break;
    case TC_TOKEN_EOF:
        p->stopped = 1;
        return;
    }
    perr(p);
    p->mode = M_IN_BODY;
    p->reprocess = 1;
}

/* Character tokens in the frameset modes: whitespace is inserted, anything
   else is ignored. */
static void frameset_chars(parser *p, tc_token *t)
{
    while (t->data_len) {
        size_t n = ws_prefix(t);
        insert_chars(p, t->data, n);
        consume(t, n);
        n = 0;
        while (n < t->data_len && !is_ws_byte(t->data[n]))
            n++;
        if (n)
            perr(p);
        consume(t, n);
    }
}

static void in_frameset(parser *p, tc_token *t)
{
    switch (t->type) {
    case TC_TOKEN_CHARS:
        frameset_chars(p, t);
        return;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, NULL);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        switch (p->tag) {
        case TC_TAG_HTML:
            in_body(p, t);
            return;
        case TC_TAG_FRAMESET:
            insert_html(p, t);
            return;
        case TC_TAG_FRAME:
            insert_html(p, t);
            pop(p);
            p->ack = 1;
            return;
        case TC_TAG_NOFRAMES:
            in_head(p, t);
            return;
        default:
            break;
        }
        break;
    case TC_TOKEN_END_TAG:
        if (p->tag == TC_TAG_FRAMESET) {
            if (p->depth == 1) {
                perr(p);
                return;
            }
            pop(p);
            if (!p->context && !is_html(current(p), TC_TAG_FRAMESET))
                p->mode = M_AFTER_FRAMESET;
            return;
        }
        break;
    case TC_TOKEN_EOF:
        if (p->depth != 1)
            perr(p);
        p->stopped = 1;
        return;
    }
    perr(p);
}

static void after_frameset(parser *p, tc_token *t)
{
    switch (t->type) {
    case TC_TOKEN_CHARS:
        frameset_chars(p, t);
        return;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, NULL);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG:
        if (p->tag == TC_TAG_HTML) {
            in_body(p, t);
            return;
        }
        if (p->tag == TC_TAG_NOFRAMES) {
            in_head(p, t);
            return;
        }
        break;
    case TC_TOKEN_END_TAG:
        if (p->tag == TC_TAG_HTML) {
            p->mode = M_AFTER_AFTER_FRAMESET;
            return;
        }
        break;
    case TC_TOKEN_EOF:
        p->stopped = 1;
        return;
    }
    perr(p);
}

static void after_after_body(parser *p, tc_token *t)
{
    size_t n;

    switch (t->type) {
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, &p->doc->node);
        return;
    case TC_TOKEN_DOCTYPE:
        in_body(p, t);
        return;
    case TC_TOKEN_CHARS:
        n = ws_prefix(t);
        if (n)
            ws_using_rules(p, M_IN_BODY, t, n);
        if (!t->data_len)
            return;
        break;
    case TC_TOKEN_START_TAG:
        if (p->tag == TC_TAG_HTML) {
            in_body(p, t);
            return;
        }
        break;
    case TC_TOKEN_EOF:
        p->stopped = 1;
        return;
    default:
        break;
    }
    perr(p);
    p->mode = M_IN_BODY;
    p->reprocess = 1;
}

static void after_after_frameset(parser *p, tc_token *t)
{
    size_t n;

    switch (t->type) {
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, &p->doc->node);
        return;
    case TC_TOKEN_DOCTYPE:
        in_body(p, t);
        return;
    case TC_TOKEN_CHARS:
        while (t->data_len) {
            n = ws_prefix(t);
            if (n)
                ws_using_rules(p, M_IN_BODY, t, n);
            n = 0;
            while (n < t->data_len && !is_ws_byte(t->data[n]))
                n++;
            if (n)
                perr(p);
            consume(t, n);
        }
        return;
    case TC_TOKEN_START_TAG:
        if (p->tag == TC_TAG_HTML) {
            in_body(p, t);
            return;
        }
        if (p->tag == TC_TAG_NOFRAMES) {
            in_head(p, t);
            return;
        }
        break;
    case TC_TOKEN_EOF:
        p->stopped = 1;
        return;
    default:
        break;
    }
    perr(p);
}

static const mode_fn modes[M_COUNT] = {
    initial, before_html, before_head, in_head, in_head_noscript, after_head, in_body, text_mode,
    in_table, in_table_text, in_caption, in_column_group, in_table_body, in_row, in_cell, in_template,
    after_body, in_frameset, after_frameset, after_after_body, after_after_frameset
};

/* --- Foreign content ---------------------------------------------------- */

static int is_mathml_text_ip(const tc_element *el)
{
    int tag = el->node.tag;
    return el->node.ns == TC_NS_MATHML &&
           (tag == TC_TAG_MI || tag == TC_TAG_MO || tag == TC_TAG_MN || tag == TC_TAG_MS || tag == TC_TAG_MTEXT);
}

static int is_html_ip(const parser *p, const tc_element *el)
{
    int tag = el->node.tag;
    if (el == p->context)
        return p->context_is_html_ip;
    if (el->node.ns == TC_NS_MATHML)
        return tag == TC_TAG_ANNOTATION_XML && (el->flags & TC_EL_HTML_INTEGRATION_POINT);
    return el->node.ns == TC_NS_SVG && (tag == TC_TAG_FOREIGNOBJECT || tag == TC_TAG_DESC || tag == TC_TAG_TITLE);
}

/* Start tags that break out of foreign content. */
static int breaks_out(const parser *p, const tc_token *t)
{
    switch (p->tag) {
    case TC_TAG_B: case TC_TAG_BIG: case TC_TAG_BLOCKQUOTE: case TC_TAG_BODY: case TC_TAG_BR: case TC_TAG_CENTER:
    case TC_TAG_CODE: case TC_TAG_DD: case TC_TAG_DIV: case TC_TAG_DL: case TC_TAG_DT: case TC_TAG_EM:
    case TC_TAG_EMBED: case TC_TAG_H1: case TC_TAG_H2: case TC_TAG_H3: case TC_TAG_H4: case TC_TAG_H5:
    case TC_TAG_H6: case TC_TAG_HEAD: case TC_TAG_HR: case TC_TAG_I: case TC_TAG_IMG: case TC_TAG_LI:
    case TC_TAG_LISTING: case TC_TAG_MENU: case TC_TAG_META: case TC_TAG_NOBR: case TC_TAG_OL: case TC_TAG_P:
    case TC_TAG_PRE: case TC_TAG_RUBY: case TC_TAG_S: case TC_TAG_SMALL: case TC_TAG_SPAN: case TC_TAG_STRONG:
    case TC_TAG_STRIKE: case TC_TAG_SUB: case TC_TAG_SUP: case TC_TAG_TABLE: case TC_TAG_TT: case TC_TAG_U:
    case TC_TAG_UL: case TC_TAG_VAR:
        return 1;
    case TC_TAG_FONT:
        return token_attr(t, "color") || token_attr(t, "face") || token_attr(t, "size");
    default:
        return 0;
    }
}

static int lower_name_is(const char *name, const tc_token *t)
{
    size_t i;
    for (i = 0; i < t->name_len; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != t->name[i] || !name[i])
            return 0;
    }
    return name[i] == '\0';
}

static void foreign_content(parser *p, tc_token *t)
{
    switch (t->type) {
    case TC_TOKEN_CHARS:
        while (t->data_len) {
            size_t n = 0;
            if (t->data[0] == '\0') {
                perr(p);
                insert_chars(p, "\xEF\xBF\xBD", 3);
                consume(t, 1);
                continue;
            }
            while (n < t->data_len && t->data[n] != '\0')
                n++;
            insert_chars(p, t->data, n);
            if (!all_ws(t->data, n))
                p->frameset_ok = 0;
            consume(t, n);
        }
        return;
    case TC_TOKEN_COMMENT:
    case TC_TOKEN_PI:
        insert_comment_or_pi(p, t, NULL);
        return;
    case TC_TOKEN_DOCTYPE:
        perr(p);
        return;
    case TC_TOKEN_START_TAG: {
        const tc_element *acn;
        tc_element *el;
        if (breaks_out(p, t))
            goto break_out;
        acn = adjusted_current(p);
        el = insert_for_token(p, t, (tc_ns)acn->node.ns);
        if (el && t->self_closing) {
            /* <svg:script/> would also run the script here. */
            pop(p);
            p->ack = 1;
        }
        return;
    }
    case TC_TOKEN_END_TAG: {
        size_t i;
        tc_element *cur = current(p);
        if ((p->tag == TC_TAG_BR || p->tag == TC_TAG_P))
            goto break_out;
        if (p->tag == TC_TAG_SCRIPT && cur && cur->node.ns == TC_NS_SVG && cur->node.tag == TC_TAG_SCRIPT) {
            pop(p);
            return;
        }
        if (cur && !lower_name_is(cur->name, t))
            perr(p);
        for (i = p->depth; i-- > 0;) {
            tc_element *node = p->stack[i];
            if (i == 0)
                return;
            if (lower_name_is(node->name, t)) {
                pop_until_el(p, node);
                return;
            }
            if (p->stack[i - 1]->node.ns == TC_NS_HTML) {
                modes[p->mode](p, t);
                return;
            }
        }
        return;
    }
    default:
        return;
    }

break_out:
    perr(p);
    while (p->depth) {
        tc_element *cur = current(p);
        if (is_mathml_text_ip(cur) || is_html_ip(p, cur) || cur->node.ns == TC_NS_HTML)
            break;
        pop(p);
    }
    modes[p->mode](p, t);
}

/* --- Driving the parser ------------------------------------------------- */

/* The tree construction dispatcher: HTML rules or foreign content? */
static int use_html_rules(const parser *p, const tc_token *t)
{
    const tc_element *acn = adjusted_current(p);

    if (!acn || acn->node.ns == TC_NS_HTML || t->type == TC_TOKEN_EOF)
        return 1;
    if (is_mathml_text_ip(acn)) {
        if (t->type == TC_TOKEN_CHARS)
            return 1;
        if (t->type == TC_TOKEN_START_TAG && p->tag != TC_TAG_MGLYPH && p->tag != TC_TAG_MALIGNMARK)
            return 1;
    }
    if (acn->node.ns == TC_NS_MATHML && acn->node.tag == TC_TAG_ANNOTATION_XML && t->type == TC_TOKEN_START_TAG &&
        p->tag == TC_TAG_SVG)
        return 1;
    if (is_html_ip(p, acn) && (t->type == TC_TOKEN_START_TAG || t->type == TC_TOKEN_CHARS))
        return 1;
    return 0;
}

static void process(parser *p, tc_token *t)
{
    int guard = 0;

    for (;;) {
        const char *data = t->data;
        int mode = p->mode;
        size_t depth = p->depth;

        if (t->type == TC_TOKEN_CHARS && !t->data_len)
            return;
        p->reprocess = 0;
        if (use_html_rules(p, t))
            modes[p->mode](p, t);
        else
            foreign_content(p, t);
        if (p->stopped || p->nomem)
            return;
        if (!p->reprocess && !(t->type == TC_TOKEN_CHARS && t->data_len))
            return;
        /* Each round must make progress; if a bug ever stops that, drop the
           token rather than hang. */
        if (t->data == data && p->mode == mode && p->depth == depth && !p->reprocess) {
            if (++guard > 4)
                return;
        } else if (++guard > 100000) {
            return;
        }
    }
}

static int in_foreign_content(void *ctx)
{
    const parser *p = ctx;
    const tc_element *acn = adjusted_current(p);
    return acn && acn->node.ns != TC_NS_HTML;
}

static void parser_init(parser *p, tc_document *doc, const char *text, size_t len, const tc_html_options *opt)
{
    memset(p, 0, sizeof *p);
    p->doc = doc;
    p->opt = opt;
    p->scripting = opt ? opt->scripting : 0;
    p->frameset_ok = 1;
    p->mode = M_INITIAL;
    p->confidence = TC_CONFIDENCE_IRRELEVANT;
    p->encoding = TC_ENC_UTF_8;
    p->change_to = TC_ENC_NONE;
    tc_buf_init(&p->table_chars);
    tc_tokenizer_init(&p->tok, text, len);
    p->tok.in_foreign_content = in_foreign_content;
    p->tok.ctx = p;
}

static void parser_free(parser *p)
{
    tc_tokenizer_free(&p->tok);
    tc_buf_free(&p->table_chars);
    free(p->stack);
    free(p->afe);
    free(p->tmodes);
}

/* Errors from the tokenizer go to the same place. */
static void tokenizer_error(void *ctx, int code)
{
    const parser *p = ctx;
    p->opt->on_error(p->opt->ctx, code);
}

static int run(parser *p)
{
    tc_token t;

    if (p->opt && p->opt->on_error)
        p->tok.on_error = tokenizer_error;
    while (!p->stopped && !p->nomem) {
        if (tc_tokenizer_next(&p->tok, &t) != TC_OK) {
            p->nomem = 1;
            break;
        }
        p->tag = t.type == TC_TOKEN_START_TAG || t.type == TC_TOKEN_END_TAG ? (int)tc_tag_lookup(t.name, t.name_len)
                                                                           : TC_TAG_UNKNOWN;
        if (p->skip_lf) {
            p->skip_lf = 0;
            if (t.type == TC_TOKEN_CHARS && t.data_len && t.data[0] == '\n')
                consume(&t, 1);
        }
        p->ack = 0;
        process(p, &t);
        check_nomem(p);
        if (t.type == TC_TOKEN_START_TAG && t.self_closing && !p->ack && p->opt && p->opt->on_error)
            p->opt->on_error(p->opt->ctx, TC_HTML_ERR_NON_VOID_HTML_ELEMENT_START_TAG_WITH_TRAILING_SOLIDUS);
        if (t.type == TC_TOKEN_EOF)
            break;
    }
    p->depth = 0; /* "the end": pop everything */
    return p->nomem ? TC_ERR_NOMEM : TC_OK;
}

/* Parse text (newlines normalized) into doc. */
static int parse_into(tc_document *doc, char *text, size_t len, const tc_html_options *opt, tc_confidence confidence,
                      tc_encoding encoding, tc_encoding *change_to)
{
    parser p;
    int err;

    parser_init(&p, doc, text, len, opt);
    p.confidence = confidence;
    p.encoding = encoding;
    err = run(&p);
    if (change_to)
        *change_to = p.change_to;
    parser_free(&p);
    return err;
}

int tc_html_parse(const char *text, size_t len, const tc_html_options *opt, tc_document **out)
{
    tc_document *doc;
    char *copy;
    int err;

    *out = NULL;
    doc = tc_document_new();
    copy = malloc(len + 1);
    if (!doc || !copy) {
        tc_document_free(doc);
        free(copy);
        return TC_ERR_NOMEM;
    }
    memcpy(copy, text, len);
    len = tc_html_normalize_newlines(copy, len);
    err = parse_into(doc, copy, len, opt, TC_CONFIDENCE_IRRELEVANT, TC_ENC_UTF_8, NULL);
    free(copy);
    *out = doc;
    return err;
}

int tc_html_parse_page(const void *bytes, size_t len, const char *http_charset, const tc_html_options *opt,
                       tc_html_page *page)
{
    tc_confidence confidence;
    tc_encoding_source source;
    tc_encoding enc = tc_html_sniff(bytes, len, http_charset, &confidence, &source);
    int attempt, err = TC_OK;

    memset(page, 0, sizeof *page);
    tc_buf_init(&page->text);
    for (attempt = 0; attempt < 2; attempt++) {
        tc_encoding change_to = TC_ENC_NONE;
        tc_buf_clear(&page->text);
        err = tc_buf_reserve(&page->text, 1);
        if (!err)
            err = tc_decode(enc, bytes, len, &page->text);
        if (err)
            break;
        page->text.len = tc_html_normalize_newlines(page->text.data, page->text.len);
        page->text.data[page->text.len] = '\0';
        tc_document_free(page->doc);
        page->doc = tc_document_new();
        if (!page->doc) {
            err = TC_ERR_NOMEM;
            break;
        }
        err = parse_into(page->doc, page->text.data, page->text.len, opt, confidence, enc, &change_to);
        page->encoding = enc;
        page->encoding_source = source;
        if (err || change_to == TC_ENC_NONE)
            break;
        /* A <meta> named another encoding: start again with that, certain. */
        enc = change_to;
        confidence = TC_CONFIDENCE_CERTAIN;
        source = TC_ENCSRC_META;
    }
    return err;
}

void tc_html_page_free(tc_html_page *page)
{
    tc_document_free(page->doc);
    tc_buf_free(&page->text);
    page->doc = NULL;
}

int tc_html_parse_fragment(tc_document *doc, const tc_element *context, const char *text, size_t len,
                           const tc_html_options *opt, tc_node **out)
{
    parser p;
    tc_element *root;
    const tc_node *n;
    char *copy;
    int err;

    *out = NULL;
    copy = malloc(len + 1);
    if (!copy)
        return TC_ERR_NOMEM;
    memcpy(copy, text, len);
    len = tc_html_normalize_newlines(copy, len);
    parser_init(&p, doc, copy, len, opt);
    p.context = context;
    p.confidence = TC_CONFIDENCE_IRRELEVANT;
    if (context->node.ns == TC_NS_HTML) {
        switch (context->node.tag) {
        case TC_TAG_TITLE:
        case TC_TAG_TEXTAREA:
            set_tokenizer_state(&p, TC_TOK_RCDATA);
            break;
        case TC_TAG_STYLE: case TC_TAG_XMP: case TC_TAG_IFRAME: case TC_TAG_NOEMBED: case TC_TAG_NOFRAMES:
            set_tokenizer_state(&p, TC_TOK_RAWTEXT);
            break;
        case TC_TAG_SCRIPT:
            set_tokenizer_state(&p, TC_TOK_SCRIPT_DATA);
            break;
        case TC_TAG_NOSCRIPT:
            if (p.scripting)
                set_tokenizer_state(&p, TC_TOK_RAWTEXT);
            break;
        case TC_TAG_PLAINTEXT:
            set_tokenizer_state(&p, TC_TOK_PLAINTEXT);
            break;
        default:
            break;
        }
    }
    if (context->node.ns == TC_NS_MATHML && context->node.tag == TC_TAG_ANNOTATION_XML) {
        const char *enc = tc_dom_get_attr(context, "encoding");
        p.context_is_html_ip = enc && (ascii_ieq(enc, "text/html") || ascii_ieq(enc, "application/xhtml+xml"));
    } else {
        p.context_is_html_ip = context->node.ns == TC_NS_SVG &&
                               (context->node.tag == TC_TAG_FOREIGNOBJECT || context->node.tag == TC_TAG_DESC ||
                                context->node.tag == TC_TAG_TITLE);
    }
    root = create_named(&p, "html");
    p.fragment = tc_dom_create_fragment(doc);
    if (!root || !p.fragment) {
        parser_free(&p);
        free(copy);
        return TC_ERR_NOMEM;
    }
    push(&p, root);
    if (is_html(context, TC_TAG_TEMPLATE))
        push_tmode(&p, M_IN_TEMPLATE);
    reset_insertion_mode(&p);
    for (n = &context->node; n; n = n->parent)
        if (tc_dom_is_html(n, TC_TAG_FORM)) {
            p.form = (tc_element *)n;
            break;
        }
    err = run(&p);
    *out = p.fragment;
    parser_free(&p);
    free(copy);
    return err;
}
