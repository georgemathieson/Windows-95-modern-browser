/* Block and inline layout (CSS 2.1 sections 8 to 10, CSS Text 3): widths
 * and auto margins, margin collapsing, white-space processing, line
 * breaking, text-align and vertical-align, inline-blocks sized to fit, and
 * placeholders for replaced elements. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tc_layout_internal.h"
#include "tc_utf8.h"

enum { TOP, RIGHT, BOTTOM, LEFT };

/* ---- Lengths and the box model ---- */

static float lp(const tc_length *len, float basis)
{
    return tc_length_resolve(len, basis < 0 ? 0 : basis);
}

static int has_percent(const tc_length *len)
{
    return len->pct != 0 || len->expr != NULL;
}

/* Padding, borders and margins (auto margins as 0) against the containing
   block's width. */
static void box_model(tc_box *b, float cbw)
{
    const tc_style *s = b->style;
    int i;
    for (i = 0; i < 4; i++) {
        b->padding[i] = lp(&s->padding[i], cbw);
        if (b->padding[i] < 0)
            b->padding[i] = 0;
        b->border[i] = s->border_width[i];
        b->margin[i] = s->margin[i].kind == TC_SIZE_LENGTH ? lp(&s->margin[i].length, cbw) : 0;
    }
}

static float mbp_h(const tc_box *b)
{
    return b->border[LEFT] + b->border[RIGHT] + b->padding[LEFT] + b->padding[RIGHT];
}

static float mbp_v(const tc_box *b)
{
    return b->border[TOP] + b->border[BOTTOM] + b->padding[TOP] + b->padding[BOTTOM];
}

/* A content-box size from a computed size (box-sizing applied), or -1 for
   auto and anything Tachyon can't resolve. */
static float content_size(const tc_box *b, const tc_size *size, float basis, int horizontal)
{
    float v;
    if (size->kind != TC_SIZE_LENGTH || (basis < 0 && has_percent(&size->length)))
        return -1;
    v = lp(&size->length, basis);
    if (b->style->box_sizing == TC_BOX_BORDER)
        v -= horizontal ? mbp_h(b) : mbp_v(b);
    return v < 0 ? 0 : v;
}

static float clamp_size(const tc_box *b, float v, const tc_size *min, const tc_size *max, float basis, int horizontal)
{
    float hi = content_size(b, max, basis, horizontal), lo = content_size(b, min, basis, horizontal);
    if (max->kind == TC_SIZE_LENGTH && hi >= 0 && v > hi)
        v = hi;
    if (min->kind == TC_SIZE_LENGTH && lo >= 0 && v < lo)
        v = lo;
    return v;
}

/* ---- Fonts ---- */

const tc_font *tc_layout_font(tc_layout *l, const tc_style *s)
{
    static const char *const fallback[] = { "serif" };
    const tc_font_families *f = s->font_family;
    return tc_font_match(l->fonts, f ? f->names : fallback, f ? f->count : 1, s->font_weight,
                         s->font_style != TC_FONT_NORMAL, s->font_size);
}

/* A style's extents above and below the baseline: its font's ascent and
   descent, plus half the leading from line-height. */
static void strut(tc_layout *l, const tc_style *s, float *above, float *below)
{
    const tc_font_metrics *m = tc_font_get_metrics(tc_layout_font(l, s));
    float a = m->ascent, d = m->descent, lh, half;
    switch (s->line_height_kind) {
    case TC_LINE_HEIGHT_NUMBER: lh = s->line_height * s->font_size; break;
    case TC_LINE_HEIGHT_PX: lh = s->line_height; break;
    default: lh = a + d + m->line_gap; break;
    }
    half = (lh - (a + d)) / 2;
    *above = a + half;
    *below = d + half;
}

/* ---- Inline content: items ---- */

enum { IT_TEXT, IT_OPEN, IT_CLOSE, IT_ATOMIC, IT_BREAK };

typedef struct item {
    uint8_t kind;
    uint8_t wrap;              /* text: soft wraps allowed */
    uint8_t preserve;          /* text: spaces are preserved (they hang, and don't collapse) */
    uint8_t break_word;        /* text: words may break anywhere to fit */
    tc_box *box;
    const char *text;          /* text after white-space processing and text-transform */
    uint32_t len;
    const tc_font *font;
    float letter_spacing, word_spacing, space;
} item;

typedef struct items {
    item *v;
    uint32_t len, cap;
} items;

static item *push_item(tc_layout *l, items *it, int kind, tc_box *box)
{
    item *x;
    if (it->len == it->cap) {
        uint32_t cap = it->cap ? it->cap * 2 : 64;
        item *grown = realloc(it->v, cap * sizeof *grown);
        if (!grown) {
            l->nomem = 1;
            return NULL;
        }
        it->v = grown;
        it->cap = cap;
    }
    x = &it->v[it->len++];
    memset(x, 0, sizeof *x);
    x->kind = (uint8_t)kind;
    x->box = box;
    return x;
}

static uint32_t to_upper(uint32_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
        return c - 32;
    if (c == 0xFF)
        return 0x178;
    if (c >= 0x100 && c <= 0x17F && (c & 1) && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F)
        return c - 1;
    return c;
}

static uint32_t to_lower(uint32_t c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
        return c + 32;
    if (c >= 0x100 && c <= 0x17F && !(c & 1) && c != 0x130 && c != 0x138)
        return c + 1;
    return c;
}

static int is_word_char(uint32_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'' || c >= 0xC0;
}

typedef struct ws_state {
    int space;      /* the last character was a collapsible space (or the line just started) */
    int column;     /* for tabs in preserved text */
    int word;       /* capitalize: inside a word */
} ws_state;

/* White-space processing (CSS Text 3, 4.1) and text-transform, into the
   layout's arena; a preserved newline ends the text there (*rest is the
   remainder, after the newline). */
static const char *process_text(tc_layout *l, const tc_style *s, const char *text, size_t len, ws_state *ws,
                                uint32_t *out_len, size_t *used, int *forced)
{
    int collapse = s->ws_collapse == TC_COLLAPSE_COLLAPSE || s->ws_collapse == TC_COLLAPSE_PRESERVE_BREAKS;
    int keep_newlines = s->ws_collapse != TC_COLLAPSE_COLLAPSE;
    char *out = tc_arena_alloc(&l->arena, len * 8 + 8), *p = out;
    size_t i = 0;

    *forced = 0;
    if (!out) {
        l->nomem = 1;
        *out_len = 0;
        *used = len;
        return "";
    }
    while (i < len) {
        uint32_t c;
        size_t n = tc_utf8_decode(text + i, len - i, &c);
        if (c == '\n' && keep_newlines) {
            i += n;
            *forced = 1;
            ws->space = 1;
            ws->column = 0;
            break;
        }
        if (c == '\r' || c == '\f')
            c = ' ';
        if (c == '\n' || (c == '\t' && collapse))
            c = ' ';
        if (collapse && c == ' ') {
            /* Spaces before a preserved newline go too (pre-line). */
            if (!ws->space) {
                size_t k = i + n;
                while (keep_newlines && k < len && (text[k] == ' ' || text[k] == '\t'))
                    k++;
                if (!(keep_newlines && k < len && text[k] == '\n'))
                    *p++ = ' ';
            }
            ws->space = 1;
            ws->word = 0;
            i += n;
            continue;
        }
        if (c == '\t') {
            int spaces = 8 - ws->column % 8;
            while (spaces--)
                *p++ = ' ', ws->column++;
            ws->space = 0;
            i += n;
            continue;
        }
        if (c == 0x200B || c == 0xAD) { /* zero width space, soft hyphen: break opportunities only */
            *p++ = '\x01';
            i += n;
            continue;
        }
        switch (s->text_transform) {
        case TC_TRANSFORM_UPPERCASE: c = to_upper(c); break;
        case TC_TRANSFORM_LOWERCASE: c = to_lower(c); break;
        case TC_TRANSFORM_CAPITALIZE:
            if (!ws->word && is_word_char(c))
                c = to_upper(c);
            break;
        default: break;
        }
        ws->word = is_word_char(c);
        ws->space = 0;
        ws->column++;
        p += tc_utf8_encode(c < 0x20 ? 0xFFFD : c, p);
        i += n;
    }
    *out_len = (uint32_t)(p - out);
    *used = i;
    return out;
}

static void text_items(tc_layout *l, items *it, tc_box *b, ws_state *ws)
{
    const tc_style *s = b->style;
    const tc_font *font = tc_layout_font(l, s);
    size_t pos = 0;
    while (pos < b->len) {
        uint32_t len;
        size_t used;
        int forced;
        const char *text = process_text(l, s, b->text + pos, b->len - pos, ws, &len, &used, &forced);
        item *x;
        if (len) {
            x = push_item(l, it, IT_TEXT, b);
            if (!x)
                return;
            x->text = text;
            x->len = len;
            x->font = font;
            x->wrap = s->text_wrap == TC_WRAP_MODE_WRAP;
            x->preserve = s->ws_collapse == TC_COLLAPSE_PRESERVE || s->ws_collapse == TC_COLLAPSE_BREAK_SPACES;
            x->break_word = s->overflow_wrap != TC_WRAP_NORMAL || s->word_break == TC_BREAK_ALL ||
                            s->word_break == TC_BREAK_WORD;
            x->letter_spacing = s->letter_spacing;
            x->word_spacing = s->word_spacing;
            x->space = tc_font_get_metrics(font)->space_width;
        }
        if (forced && !push_item(l, it, IT_BREAK, b))
            return;
        pos += used ? used : 1;
    }
}

static void collect_items(tc_layout *l, items *it, tc_box *b, ws_state *ws, int depth)
{
    tc_box *c;
    for (c = b->first; c && !l->nomem; c = c->next) {
        switch (c->type) {
        case TC_BOX_TEXT:
            text_items(l, it, c, ws);
            break;
        case TC_BOX_INLINE:
            push_item(l, it, IT_OPEN, c);
            if (depth < TC_LAYOUT_MAX_DEPTH + 8)
                collect_items(l, it, c, ws, depth + 1);
            push_item(l, it, IT_CLOSE, c);
            break;
        case TC_BOX_BREAK:
            push_item(l, it, IT_BREAK, c);
            ws->space = 1;
            break;
        default:
            push_item(l, it, IT_ATOMIC, c);
            ws->space = 0;
            break;
        }
    }
}

/* ---- Intrinsic widths ---- */

static void intrinsic(tc_layout *l, tc_box *b, float *min_w, float *max_w, int depth);
static void layout_atomic(tc_layout *l, tc_box *b, float cbw, int depth);

static float word_width(const item *x, uint32_t start, uint32_t end)
{
    uint32_t i, n = 0;
    float w;
    /* Break-opportunity markers take no room. */
    for (i = start; i < end; i++)
        n += x->text[i] == '\x01';
    if (!n)
        return tc_font_measure(x->font, x->text + start, end - start, x->letter_spacing);
    w = 0;
    while (start < end) {
        uint32_t k = start;
        while (k < end && x->text[k] != '\x01')
            k++;
        w += tc_font_measure(x->font, x->text + start, k - start, x->letter_spacing);
        start = k + 1;
    }
    return w;
}

static float space_width(const item *x, uint32_t n)
{
    return n * (x->space + x->letter_spacing + x->word_spacing);
}

static float side(const tc_box *b, int left)
{
    int i = left ? LEFT : RIGHT;
    return b->margin[i] + b->border[i] + b->padding[i];
}

/* An atomic or block-level box's outer widths: margins, borders and
   padding (percentages as 0) around its min- and max-content widths. */
static void outer_widths(tc_layout *l, tc_box *b, float *min_w, float *max_w, int depth)
{
    float w, mn = 0, mx = 0;
    box_model(b, 0);
    w = content_size(b, &b->style->width, 0, 1);
    if (b->type == TC_BOX_REPLACED) {
        layout_atomic(l, b, 0, depth);
        mn = mx = b->w - mbp_h(b);
    } else if (w >= 0 && !has_percent(&b->style->width.length)) {
        mn = mx = w;
    } else {
        intrinsic(l, b, &mn, &mx, depth + 1);
    }
    mn = clamp_size(b, mn, &b->style->min_width, &b->style->max_width, 0, 1);
    mx = clamp_size(b, mx, &b->style->min_width, &b->style->max_width, 0, 1);
    *min_w = mn + mbp_h(b) + b->margin[LEFT] + b->margin[RIGHT];
    *max_w = mx + mbp_h(b) + b->margin[LEFT] + b->margin[RIGHT];
}

static void inline_intrinsic(tc_layout *l, tc_box *b, float *min_w, float *max_w, int depth)
{
    items it = { NULL, 0, 0 };
    ws_state ws = { 1, 0, 0 };
    float line = 0, unit = 0, mn = 0, mx = 0;
    uint32_t i;

    collect_items(l, &it, b, &ws, 0);
    for (i = 0; i < it.len; i++) {
        item *x = &it.v[i];
        float a, c;
        switch (x->kind) {
        case IT_TEXT: {
            uint32_t p = 0;
            while (p < x->len) {
                uint32_t q = p;
                if (x->text[p] == ' ') {
                    while (q < x->len && x->text[q] == ' ')
                        q++;
                    line += space_width(x, q - p);
                    if (x->wrap)
                        unit = 0;
                    else
                        unit += space_width(x, q - p);
                } else {
                    while (q < x->len && x->text[q] != ' ' && (x->text[q] != '\x01' || q == p))
                        q++;
                    a = word_width(x, p, q);
                    line += a;
                    unit = x->break_word ? a : unit + a;
                    if (q < x->len && x->text[q] == '\x01' && x->wrap)
                        unit = 0;
                }
                mn = TC_MAX(mn, unit);
                p = q;
            }
            break;
        }
        case IT_OPEN:
        case IT_CLOSE:
            box_model(x->box, 0);
            a = (x->box->edges & (x->kind == IT_OPEN ? 1 : 2)) ? side(x->box, x->kind == IT_OPEN) : 0;
            line += a;
            unit += a;
            break;
        case IT_ATOMIC:
            outer_widths(l, x->box, &a, &c, depth);
            line += c;
            unit = x->box->parent->style->text_wrap == TC_WRAP_MODE_WRAP ? a : unit + a;
            mn = TC_MAX(mn, unit);
            if (x->box->parent->style->text_wrap == TC_WRAP_MODE_WRAP)
                unit = 0;
            break;
        case IT_BREAK:
            mx = TC_MAX(mx, line);
            line = unit = 0;
            break;
        }
        mn = TC_MAX(mn, unit);
    }
    mx = TC_MAX(mx, line);
    free(it.v);
    *min_w = mn + TC_MAX(0, lp(&b->style->text_indent, 0));
    *max_w = TC_MAX(mx + lp(&b->style->text_indent, 0), *min_w);
}

/* A block container's min- and max-content widths (content box). */
static void intrinsic(tc_layout *l, tc_box *b, float *min_w, float *max_w, int depth)
{
    tc_box *c;
    *min_w = *max_w = 0;
    if (depth > TC_LAYOUT_MAX_DEPTH + 16)
        return;
    if (b->has_lines) {
        inline_intrinsic(l, b, min_w, max_w, depth);
        return;
    }
    for (c = b->first; c; c = c->next) {
        float mn, mx;
        outer_widths(l, c, &mn, &mx, depth);
        *min_w = TC_MAX(*min_w, mn);
        *max_w = TC_MAX(*max_w, mx);
    }
}

/* ---- Lines ---- */

enum { PC_TEXT, PC_SPACE, PC_OPEN, PC_CLOSE, PC_ATOMIC, PC_BREAK };

typedef struct piece {
    uint8_t kind;
    item *it;
    uint32_t start, end;       /* text */
    float w;
    uint32_t spaces;           /* PC_SPACE: how many */
} piece;

typedef struct line_state {
    tc_layout *l;
    tc_box *block;
    float avail;               /* the content width */
    float y;                   /* the next line's top, from the content box top */
    int first;                 /* the first line (text-indent) */
    piece *pc;
    uint32_t npc, pc_cap;
    float x;                   /* the pieces' width so far */
    int break_at;              /* pieces before this index can end a line; -1 if none */
    float break_x;
    tc_box *open[64];          /* inline boxes open at the line's start */
    uint32_t nopen;
    struct {
        tc_frag *v;
        uint32_t len, cap;
    } frags;
    float last_baseline;
} line_state;

static piece *add_piece(line_state *ls, int kind, item *it, float w)
{
    piece *p;
    if (ls->npc == ls->pc_cap) {
        uint32_t cap = ls->pc_cap ? ls->pc_cap * 2 : 64;
        piece *grown = realloc(ls->pc, cap * sizeof *grown);
        if (!grown) {
            ls->l->nomem = 1;
            return NULL;
        }
        ls->pc = grown;
        ls->pc_cap = cap;
    }
    p = &ls->pc[ls->npc++];
    memset(p, 0, sizeof *p);
    p->kind = (uint8_t)kind;
    p->it = it;
    p->w = w;
    ls->x += w;
    return p;
}

static tc_frag *add_frag(line_state *ls, int type, const tc_box *box)
{
    tc_frag *f;
    if (ls->frags.len == ls->frags.cap) {
        uint32_t cap = ls->frags.cap ? ls->frags.cap * 2 : 32;
        tc_frag *grown = realloc(ls->frags.v, cap * sizeof *grown);
        if (!grown) {
            ls->l->nomem = 1;
            return NULL;
        }
        ls->frags.v = grown;
        ls->frags.cap = cap;
    }
    f = &ls->frags.v[ls->frags.len++];
    memset(f, 0, sizeof *f);
    f->type = (uint8_t)type;
    f->box = box;
    return f;
}

/* How far a box's baseline sits below its parent's (vertical-align). top
   and bottom are placed afterwards. */
static float baseline_shift(tc_layout *l, const tc_style *s, const tc_style *parent, float above, float below)
{
    const tc_font_metrics *pm = tc_font_get_metrics(tc_layout_font(l, parent));
    switch (s->vertical_align) {
    case TC_VALIGN_SUB: return parent->font_size * 0.2f;
    case TC_VALIGN_SUPER: return -parent->font_size * 0.34f;
    case TC_VALIGN_TEXT_TOP: return -pm->ascent + above;
    case TC_VALIGN_TEXT_BOTTOM: return pm->descent - below;
    case TC_VALIGN_MIDDLE: return -pm->x_height / 2 - (below - above) / 2;
    case TC_VALIGN_LENGTH: return -s->vertical_align_length.px;
    default: return 0;
    }
}

typedef struct vbox {          /* an inline box or atomic on the line */
    const tc_box *box;
    float shift;               /* its baseline below the line's */
    float above, below;        /* its extents */
    int edge;                  /* top or bottom aligned: 1 top, 2 bottom */
} vbox;

#define MAX_VBOX 256

static vbox *find_vbox(vbox *vb, uint32_t n, const tc_box *box)
{
    uint32_t i;
    for (i = n; i-- > 0;)
        if (vb[i].box == box)
            return &vb[i];
    return NULL;
}

/* Lay out the pieces [0, count) as a line, and keep the rest for the next. */
static void finish_line(line_state *ls, uint32_t count, int forced)
{
    tc_layout *l = ls->l;
    tc_box *blk = ls->block;
    const tc_style *bs = blk->style;
    vbox vb[MAX_VBOX];
    uint32_t nvb = 0, i, k, end = count, spaces = 0;
    float width = 0, x, offset = 0, extra = 0, top, bottom, baseline, indent;
    int content = 0;
    tc_box *stack[64];
    uint32_t depth;
    float ox = blk->border[LEFT] + blk->padding[LEFT], oy = blk->border[TOP] + blk->padding[TOP];

    /* Collapsible spaces at the end go (CLOSE pieces may follow them). */
    for (k = count; k-- > 0;) {
        piece *p = &ls->pc[k];
        if (p->kind == PC_CLOSE)
            continue;
        if (p->kind == PC_SPACE && !p->it->preserve) {
            p->w = 0;
            continue;
        }
        break;
    }
    for (i = 0; i < count; i++) {
        piece *p = &ls->pc[i];
        width += p->w;
        if (p->kind == PC_TEXT || p->kind == PC_ATOMIC || p->kind == PC_BREAK || (p->kind == PC_SPACE && p->w > 0))
            content = 1;
        if ((p->kind == PC_OPEN || p->kind == PC_CLOSE) && p->w > 0)
            content = 1;
    }
    for (i = 0; i < count; i++)
        if (ls->pc[i].kind == PC_SPACE && ls->pc[i].w > 0)
            spaces += ls->pc[i].spaces;
    indent = ls->first ? lp(&bs->text_indent, ls->avail) : 0;
    if (content) {
        float room = ls->avail - indent - width;
        switch (bs->text_align) {
        case TC_ALIGN_RIGHT: offset = room; break;
        case TC_ALIGN_CENTER: offset = room / 2; break;
        case TC_ALIGN_JUSTIFY:
            if (!forced && end < ls->npc && spaces && room > 0)
                extra = room / (float)spaces;
            break;
        default: break;
        }
        if (offset < 0)
            offset = 0;

        /* Vertical: the block's strut, then each box on the line. */
        strut(l, bs, &vb[0].above, &vb[0].below);
        vb[0].box = blk;
        vb[0].shift = 0;
        vb[0].edge = 0;
        nvb = 1;
        depth = 0;
        for (i = 0; i < ls->nopen && depth < 64; i++)
            stack[depth++] = ls->open[i];
        for (i = 0; i < depth && nvb < MAX_VBOX; i++) {
            vbox *v = &vb[nvb++];
            const vbox *pv = i ? &vb[nvb - 2] : &vb[0];
            v->box = stack[i];
            strut(l, stack[i]->style, &v->above, &v->below);
            v->edge = stack[i]->style->vertical_align == TC_VALIGN_TOP      ? 1
                      : stack[i]->style->vertical_align == TC_VALIGN_BOTTOM ? 2
                                                                            : 0;
            v->shift = pv->shift + baseline_shift(l, stack[i]->style, i ? stack[i - 1]->style : bs, v->above, v->below);
        }
        for (i = 0; i < count && nvb < MAX_VBOX; i++) {
            piece *p = &ls->pc[i];
            const vbox *parent = NULL;
            const tc_box *pb;
            vbox *v;
            if (p->kind != PC_OPEN && p->kind != PC_ATOMIC) {
                if (p->kind == PC_CLOSE && depth)
                    depth--;
                continue;
            }
            pb = p->it->box->parent;
            parent = find_vbox(vb, nvb, pb);
            if (!parent)
                parent = &vb[0];
            v = &vb[nvb++];
            v->box = p->it->box;
            v->edge = p->it->box->style->vertical_align == TC_VALIGN_TOP      ? 1
                      : p->it->box->style->vertical_align == TC_VALIGN_BOTTOM ? 2
                                                                              : 0;
            if (p->kind == PC_OPEN) {
                strut(l, p->it->box->style, &v->above, &v->below);
                if (depth < 64)
                    stack[depth++] = p->it->box;
            } else {
                /* An atomic box sits on its baseline, or its bottom margin edge. */
                const tc_box *a = p->it->box;
                float mb = a->baseline >= 0 ? a->baseline : a->h + a->margin[BOTTOM];
                v->above = a->margin[TOP] + mb;
                v->below = a->h + a->margin[BOTTOM] - mb;
            }
            v->shift = parent->shift +
                       baseline_shift(l, p->it->box->style, parent->box->style, v->above, v->below);
        }
        top = -vb[0].above;
        bottom = vb[0].below;
        for (i = 1; i < nvb; i++)
            if (!vb[i].edge) {
                top = TC_MIN(top, vb[i].shift - vb[i].above);
                bottom = TC_MAX(bottom, vb[i].shift + vb[i].below);
            }
        for (i = 1; i < nvb; i++) {
            float h = vb[i].above + vb[i].below;
            if (vb[i].edge == 1) {
                vb[i].shift = top + vb[i].above;
                bottom = TC_MAX(bottom, top + h);
            }
        }
        for (i = 1; i < nvb; i++) {
            float h = vb[i].above + vb[i].below;
            if (vb[i].edge == 2) {
                vb[i].shift = bottom - vb[i].below;
                top = TC_MIN(top, bottom - h);
            }
        }
        baseline = ls->y - top;

        /* Inline boxes' backgrounds first, outermost first, then the rest. */
        x = indent + offset;
        {
            float pos = x;
            uint32_t sd = 0, first_box = ls->frags.len, order = 0, nord = 0;
            uint32_t ord[128];
            struct {
                const tc_box *box;
                float x0;
                int left;
                uint32_t order;
            } open[64];
            for (i = 0; i < ls->nopen && sd < 64; i++) {
                open[sd].box = ls->open[i];
                open[sd].x0 = pos;
                open[sd].left = 0;
                open[sd].order = order++;
                sd++;
            }
            for (i = 0; i <= count; i++) {
                piece *p = i < count ? &ls->pc[i] : NULL;
                if (p && p->kind == PC_OPEN && sd < 64) {
                    open[sd].box = p->it->box;
                    open[sd].x0 = pos;
                    open[sd].left = (p->it->box->edges & 1) != 0;
                    open[sd].order = order++;
                    sd++;
                }
                if (!p || p->kind == PC_CLOSE) {
                    /* Close one box (or all, at the end). */
                    uint32_t n = p ? 1 : sd;
                    while (n-- && sd) {
                        const tc_box *bx = open[--sd].box;
                        const vbox *v = find_vbox(vb, nvb, bx);
                        const tc_font_metrics *m = tc_font_get_metrics(tc_layout_font(l, bx->style));
                        tc_frag *f = add_frag(ls, TC_FRAG_BOX, bx);
                        if (!f)
                            return;
                        if (nord < 128)
                            ord[nord++] = open[sd].order;
                        f->edges = (uint8_t)(open[sd].left | (p && (bx->edges & 2) ? 2 : 0));
                        f->x = ox + open[sd].x0 + (open[sd].left ? bx->margin[LEFT] : 0);
                        f->w = pos + (p ? p->w : 0) - (p && (bx->edges & 2) ? bx->margin[RIGHT] : 0) -
                               (open[sd].x0 + (open[sd].left ? bx->margin[LEFT] : 0));
                        f->y = oy + baseline + (v ? v->shift : 0) - m->ascent - bx->padding[TOP] - bx->border[TOP];
                        f->h = m->ascent + m->descent + mbp_v(bx);
                    }
                }
                if (p)
                    pos += p->w + (p->kind == PC_SPACE && p->w > 0 ? extra * p->spaces : 0);
            }
            /* They were added as they closed, innermost first: paint the
               outer ones first. */
            if (nord == ls->frags.len - first_box) {
                uint32_t a, b2;
                for (a = 1; a < nord; a++)
                    for (b2 = a; b2 > 0 && ord[b2 - 1] > ord[b2]; b2--) {
                        tc_frag t = ls->frags.v[first_box + b2];
                        uint32_t o = ord[b2];
                        ls->frags.v[first_box + b2] = ls->frags.v[first_box + b2 - 1];
                        ls->frags.v[first_box + b2 - 1] = t;
                        ord[b2] = ord[b2 - 1];
                        ord[b2 - 1] = o;
                    }
            }
        }
        {
            tc_box *cur[64];
            uint32_t cd = 0;
            for (i = 0; i < ls->nopen && cd < 64; i++)
                cur[cd++] = ls->open[i];
            for (i = 0; i < count; i++) {
                piece *p = &ls->pc[i];
                const tc_box *owner = cd ? cur[cd - 1] : blk;
                const vbox *v = find_vbox(vb, nvb, owner);
                float shift = v ? v->shift : 0;
                switch (p->kind) {
                case PC_OPEN:
                    if (cd < 64)
                        cur[cd++] = p->it->box;
                    break;
                case PC_CLOSE:
                    if (cd)
                        cd--;
                    break;
                case PC_TEXT:
                case PC_SPACE:
                    if (p->w > 0 && (p->kind == PC_TEXT || p->it->preserve || extra > 0 || p->spaces)) {
                        tc_frag *f = add_frag(ls, TC_FRAG_TEXT, p->it->box);
                        if (!f)
                            return;
                        f->x = ox + x;
                        f->y = oy + baseline + shift;
                        f->w = p->w + (p->kind == PC_SPACE ? extra * p->spaces : 0);
                        f->h = 0;
                        f->text = p->it->text + p->start;
                        f->len = p->end - p->start;
                        f->font = p->it->font;
                        f->word_spacing = p->it->word_spacing + (p->kind == PC_SPACE ? extra : 0);
                        if (p->kind == PC_SPACE)
                            f->len = 0; /* nothing to draw, but decorations reach across */
                    }
                    break;
                case PC_ATOMIC: {
                    tc_box *a = p->it->box;
                    const vbox *av = find_vbox(vb, nvb, a);
                    tc_frag *f = add_frag(ls, TC_FRAG_ATOMIC, a);
                    float mb = a->baseline >= 0 ? a->baseline : a->h + a->margin[BOTTOM];
                    if (!f)
                        return;
                    a->x = ox + x + a->margin[LEFT];
                    a->y = oy + baseline + (av ? av->shift : 0) - mb;
                    f->x = a->x;
                    f->y = a->y;
                    f->w = a->w;
                    f->h = a->h;
                    break;
                }
                default:
                    break;
                }
                x += p->w + (p->kind == PC_SPACE && p->w > 0 ? extra * p->spaces : 0);
            }
        }
        ls->last_baseline = oy + baseline;
        ls->y += bottom - top;
        l->stats.lines++;
    }
    /* The inline boxes still open carry on to the next line. */
    for (i = 0; i < count; i++) {
        piece *p = &ls->pc[i];
        if (p->kind == PC_OPEN && ls->nopen < 64)
            ls->open[ls->nopen++] = p->it->box;
        else if (p->kind == PC_CLOSE && ls->nopen)
            ls->nopen--;
    }
    if (content)
        ls->first = 0;
    /* Keep the rest. */
    memmove(ls->pc, ls->pc + count, (ls->npc - count) * sizeof *ls->pc);
    ls->npc -= count;
    ls->x = 0;
    for (i = 0; i < ls->npc; i++)
        ls->x += ls->pc[i].w;
    ls->break_at = -1;
}

static float line_room(const line_state *ls)
{
    return ls->avail - (ls->first ? lp(&ls->block->style->text_indent, ls->avail) : 0);
}

/* Whether the line holds anything that stops a word moving to the next. */
static int line_has_content(const line_state *ls)
{
    uint32_t i;
    for (i = 0; i < ls->npc; i++)
        if (ls->pc[i].kind == PC_TEXT || ls->pc[i].kind == PC_ATOMIC)
            return 1;
    return 0;
}

/* Make room for w more: end the line at the last break opportunity. */
static void wrap_for(line_state *ls, float w)
{
    if (ls->x + w <= line_room(ls) + 0.01f || !line_has_content(ls))
        return;
    if (ls->break_at > 0)
        finish_line(ls, (uint32_t)ls->break_at, 0);
}

static void mark_break(line_state *ls)
{
    ls->break_at = (int)ls->npc;
    ls->break_x = ls->x;
}

static void place_text(line_state *ls, item *x)
{
    uint32_t p = 0;
    while (p < x->len && !ls->l->nomem) {
        uint32_t q = p;
        if (x->text[p] == ' ') {
            piece *pc;
            while (q < x->len && x->text[q] == ' ')
                q++;
            /* Collapsible spaces at the start of a line go. */
            if (!x->preserve && !line_has_content(ls)) {
                p = q;
                if (x->wrap)
                    mark_break(ls);
                continue;
            }
            pc = add_piece(ls, PC_SPACE, x, space_width(x, q - p));
            if (!pc)
                return;
            pc->start = p;
            pc->end = q;
            pc->spaces = q - p;
            if (x->wrap)
                mark_break(ls);
        } else {
            float w;
            piece *pc;
            while (q < x->len && x->text[q] != ' ' && x->text[q] != '\x01')
                q++;
            /* A hyphen ends a word too (after a letter, before one). */
            {
                uint32_t h;
                for (h = p + 1; h + 1 < q; h++)
                    if (x->text[h] == '-' && is_word_char((unsigned char)x->text[h - 1]) &&
                        is_word_char((unsigned char)x->text[h + 1])) {
                        q = h + 1;
                        break;
                    }
            }
            w = word_width(x, p, q);
            if (x->wrap)
                wrap_for(ls, w);
            if (x->wrap && x->break_word && w > line_room(ls) - ls->x) {
                /* Break the word where it overflows. */
                uint32_t e = p;
                while (e < q) {
                    uint32_t cp, n = (uint32_t)tc_utf8_decode(x->text + e, q - e, &cp);
                    float cw = word_width(x, p, e + n);
                    if (ls->x + cw > line_room(ls) && e > p)
                        break;
                    e += n;
                }
                pc = add_piece(ls, PC_TEXT, x, word_width(x, p, e));
                if (!pc)
                    return;
                pc->start = p;
                pc->end = e;
                if (e < q) {
                    finish_line(ls, ls->npc, 0);
                    p = e;
                    continue;
                }
                p = q;
                continue;
            }
            pc = add_piece(ls, PC_TEXT, x, w);
            if (!pc)
                return;
            pc->start = p;
            pc->end = q;
            if (q < x->len && x->text[q] == '\x01') {
                q++;
                if (x->wrap)
                    mark_break(ls);
            } else if (q < x->len && x->text[q - 1] == '-' && x->wrap) {
                mark_break(ls);
            }
        }
        p = q;
    }
}

/* Lay out a block container's inline content; returns the lines' height. */
static float layout_lines(tc_layout *l, tc_box *b, float avail, int depth)
{
    items it = { NULL, 0, 0 };
    ws_state ws = { 1, 0, 0 };
    line_state ls;
    uint32_t i;

    memset(&ls, 0, sizeof ls);
    ls.l = l;
    ls.block = b;
    ls.avail = avail;
    ls.first = 1;
    ls.break_at = -1;
    ls.last_baseline = -1;
    collect_items(l, &it, b, &ws, 0);
    for (i = 0; i < it.len && !l->nomem; i++) {
        item *x = &it.v[i];
        switch (x->kind) {
        case IT_TEXT:
            place_text(&ls, x);
            break;
        case IT_OPEN:
            box_model(x->box, avail);
            add_piece(&ls, PC_OPEN, x, (x->box->edges & 1) ? side(x->box, 1) : 0);
            break;
        case IT_CLOSE:
            add_piece(&ls, PC_CLOSE, x, (x->box->edges & 2) ? side(x->box, 0) : 0);
            if (x->box->parent && x->box->parent->style->text_wrap == TC_WRAP_MODE_WRAP && ls.break_at == (int)ls.npc - 1)
                mark_break(&ls); /* a break just before a closing tag still works after it */
            break;
        case IT_ATOMIC: {
            tc_box *a = x->box;
            float w;
            int wrap = a->parent->style->text_wrap == TC_WRAP_MODE_WRAP;
            layout_atomic(l, a, avail, depth + 1);
            w = a->w + a->margin[LEFT] + a->margin[RIGHT];
            if (wrap) {
                mark_break(&ls);
                wrap_for(&ls, w);
            }
            add_piece(&ls, PC_ATOMIC, x, w);
            if (wrap)
                mark_break(&ls);
            break;
        }
        case IT_BREAK:
            add_piece(&ls, PC_BREAK, x, 0);
            finish_line(&ls, ls.npc, 1);
            break;
        }
    }
    if (ls.npc)
        finish_line(&ls, ls.npc, 1);
    free(it.v);
    free(ls.pc);
    if (ls.frags.len) {
        b->frags = tc_arena_alloc(&l->arena, ls.frags.len * sizeof *b->frags);
        if (b->frags) {
            memcpy(b->frags, ls.frags.v, ls.frags.len * sizeof *b->frags);
            b->nfrags = ls.frags.len;
            l->stats.frags += ls.frags.len;
        } else {
            l->nomem = 1;
        }
    }
    free(ls.frags.v);
    b->baseline = ls.last_baseline;
    return ls.y;
}

/* ---- Blocks ---- */

typedef struct margins {
    float pos, neg;
} margins;

static void madd(margins *m, float v)
{
    if (v > m->pos)
        m->pos = v;
    if (v < m->neg)
        m->neg = v;
}

static margins mmerge(margins a, margins b)
{
    madd(&a, b.pos);
    madd(&a, b.neg);
    return a;
}

static float msum(margins m)
{
    return m.pos + m.neg;
}

typedef struct block_result {
    margins top, bottom;
    int self_collapse;
} block_result;

/* Boxes whose margins don't collapse with their children's. */
static int new_context(const tc_box *b)
{
    const tc_style *s = b->style;
    return !b->parent || !b->parent->parent || b->type == TC_BOX_INLINE_BLOCK || s->overflow_x != TC_OVERFLOW_VISIBLE ||
           s->float_ != TC_FLOAT_NONE || s->position == TC_POSITION_ABSOLUTE || s->position == TC_POSITION_FIXED ||
           s->display == TC_DISPLAY_FLOW_ROOT || s->display == TC_DISPLAY_TABLE || s->display == TC_DISPLAY_TABLE_CELL ||
           s->display == TC_DISPLAY_TABLE_CAPTION || s->display == TC_DISPLAY_FLEX || s->display == TC_DISPLAY_GRID;
}

static void layout_block(tc_layout *l, tc_box *b, float cbw, float cbh, float forced_w, block_result *res, int depth);

/* A replaced box's size: from its style, or the defaults. */
static void layout_replaced(tc_layout *l, tc_box *b, float cbw, float cbh)
{
    const tc_style *s = b->style;
    float w, h, dw = 300, dh = 150;
    box_model(b, cbw);
    switch (b->replaced) {
    case TC_REPLACED_IMAGE:
        dw = dh = 0;
        if (b->len) {
            /* No picture until Sprint 4: its alternative text in its place. */
            const tc_font *f = tc_layout_font(l, s);
            const tc_font_metrics *m = tc_font_get_metrics(f);
            dw = tc_font_measure(f, b->text, b->len, 0) + 4;
            dh = m->ascent + m->descent + 4;
        }
        break;
    case TC_REPLACED_TEXT_FIELD: {
        const tc_font_metrics *m = tc_font_get_metrics(tc_layout_font(l, s));
        const char *size = b->element ? tc_dom_get_attr(b->element, "size") : NULL;
        int chars = size ? atoi(size) : 0;
        dw = (chars > 0 ? chars : 20) * s->font_size * 0.55f;
        dh = m->ascent + m->descent + m->line_gap;
        break;
    }
    case TC_REPLACED_CHECKBOX:
    case TC_REPLACED_RADIO:
        dw = dh = 13;
        break;
    default:
        if (b->element && b->element->node.ns == TC_NS_SVG) {
            /* An SVG root's width and height attributes. */
            const char *aw = tc_dom_get_attr(b->element, "width"), *ah = tc_dom_get_attr(b->element, "height");
            if (aw && atof(aw) > 0)
                dw = (float)atof(aw);
            if (ah && atof(ah) > 0)
                dh = (float)atof(ah);
        }
        break;
    }
    w = content_size(b, &s->width, cbw, 1);
    h = content_size(b, &s->height, cbh, 0);
    if (w < 0 && h < 0) {
        w = dw;
        h = dh;
    } else if (w < 0) {
        w = dh > 0 && b->replaced == TC_REPLACED_IMAGE && !b->len ? h : dw;
    } else if (h < 0) {
        h = dw > 0 && b->replaced == TC_REPLACED_IMAGE && !b->len ? w : dh;
    }
    w = clamp_size(b, w, &s->min_width, &s->max_width, cbw, 1);
    h = clamp_size(b, h, &s->min_height, &s->max_height, cbh, 0);
    b->w = w + mbp_h(b);
    b->h = h + mbp_v(b);
    b->baseline = b->replaced == TC_REPLACED_TEXT_FIELD
                      ? b->border[TOP] + b->padding[TOP] + tc_font_get_metrics(tc_layout_font(l, s))->ascent
                      : -1;
    b->ink[0] = b->ink[1] = 0;
    b->ink[2] = b->w;
    b->ink[3] = b->h;
}

/* Inline-blocks (sized to fit) and inline replaced boxes. */
static void layout_atomic(tc_layout *l, tc_box *b, float cbw, int depth)
{
    block_result r;
    if (b->type == TC_BOX_REPLACED) {
        layout_replaced(l, b, cbw, -1);
        return;
    }
    layout_block(l, b, cbw, -1, -2, &r, depth);
}

static void add_ink(tc_box *b, float x0, float y0, float x1, float y1)
{
    b->ink[0] = TC_MIN(b->ink[0], x0);
    b->ink[1] = TC_MIN(b->ink[1], y0);
    b->ink[2] = TC_MAX(b->ink[2], x1);
    b->ink[3] = TC_MAX(b->ink[3], y1);
}

/* The first line's baseline in a block's subtree, from its border box top
   (or -1), for an outside list marker. */
static float first_baseline(const tc_box *b, int depth)
{
    const tc_box *c;
    uint32_t i;
    if (depth > TC_LAYOUT_MAX_DEPTH + 8)
        return -1;
    for (i = 0; i < b->nfrags; i++)
        if (b->frags[i].type == TC_FRAG_TEXT)
            return b->frags[i].y;
        else if (b->frags[i].type == TC_FRAG_ATOMIC)
            return b->frags[i].y + b->frags[i].h;
    if (b->has_lines)
        return -1;
    for (c = b->first; c; c = c->next)
        if (c->type == TC_BOX_BLOCK) {
            float f = first_baseline(c, depth + 1);
            if (f >= 0)
                return c->y + f;
        }
    return -1;
}

/* forced_w: -1 for a block's width, -2 for an inline-block's (sized to fit). */
static void layout_block(tc_layout *l, tc_box *b, float cbw, float cbh, float forced_w, block_result *res, int depth)
{
    const tc_style *s = b->style;
    float content_w, content_h = 0, w, h, cursor = 0, child_cbh;
    int top_collapse, bottom_collapse, escaping, any = 0;
    margins pending = { 0, 0 };
    tc_box *c;

    memset(res, 0, sizeof *res);
    box_model(b, cbw);
    /* The width. */
    w = content_size(b, &s->width, cbw, 1);
    if (w < 0 && (forced_w == -2 || s->width.kind == TC_SIZE_FIT_CONTENT || s->width.kind == TC_SIZE_MIN_CONTENT ||
                  s->width.kind == TC_SIZE_MAX_CONTENT)) {
        float mn, mx, avail = cbw - b->margin[LEFT] - b->margin[RIGHT] - mbp_h(b);
        if (b->element && tc_dom_is_html(&b->element->node, TC_TAG_TEXTAREA)) {
            const char *cols = tc_dom_get_attr(b->element, "cols");
            mn = mx = (cols && atoi(cols) > 0 ? atoi(cols) : 20) * s->font_size * 0.55f;
        } else {
            intrinsic(l, b, &mn, &mx, depth);
        }
        w = s->width.kind == TC_SIZE_MIN_CONTENT ? mn : s->width.kind == TC_SIZE_MAX_CONTENT ? mx
                                                                                             : TC_MIN(TC_MAX(mn, avail), mx);
    }
    if (w < 0)
        w = cbw - b->margin[LEFT] - b->margin[RIGHT] - mbp_h(b);
    w = clamp_size(b, w, &s->min_width, &s->max_width, cbw, 1);
    if (w < 0)
        w = 0;
    content_w = w;
    b->w = w + mbp_h(b);
    /* Auto margins centre (blocks with a width only). */
    if (forced_w == -1 && b->parent) {
        float room = cbw - b->w - b->margin[LEFT] - b->margin[RIGHT];
        int al = s->margin[LEFT].kind == TC_SIZE_AUTO, ar = s->margin[RIGHT].kind == TC_SIZE_AUTO;
        if (room > 0) {
            if (al && ar)
                b->margin[LEFT] = b->margin[RIGHT] = room / 2;
            else if (al)
                b->margin[LEFT] = room;
            else if (ar)
                b->margin[RIGHT] = room;
        }
    }
    /* The height, if it's known now (children's percentages need it). */
    h = content_size(b, &s->height, cbh, 0);
    if (!b->parent)
        h = l->vh;
    child_cbh = h >= 0 ? clamp_size(b, h, &s->min_height, &s->max_height, cbh, 0) : -1;

    top_collapse = !new_context(b) && b->border[TOP] == 0 && b->padding[TOP] == 0;
    bottom_collapse = !new_context(b) && b->border[BOTTOM] == 0 && b->padding[BOTTOM] == 0 && h < 0;
    escaping = top_collapse;
    madd(&res->top, b->margin[TOP]);
    b->ink[0] = b->ink[1] = 0;
    b->ink[2] = b->w;
    b->ink[3] = 0;

    if (b->has_lines) {
        content_h = layout_lines(l, b, content_w, depth);
        any = content_h > 0 || b->nfrags > 0;
        cursor = content_h;
    } else if (depth < TC_LAYOUT_MAX_DEPTH + 16) {
        float ox = b->border[LEFT] + b->padding[LEFT], oy = b->border[TOP] + b->padding[TOP];
        for (c = b->first; c && !l->nomem; c = c->next) {
            block_result r;
            if (c->type == TC_BOX_REPLACED) {
                layout_replaced(l, c, content_w, child_cbh);
                memset(&r, 0, sizeof r);
                madd(&r.top, c->margin[TOP]);
                madd(&r.bottom, c->margin[BOTTOM]);
                /* Auto margins centre a block-level replaced box too. */
                if (c->style->margin[LEFT].kind == TC_SIZE_AUTO || c->style->margin[RIGHT].kind == TC_SIZE_AUTO) {
                    float room = content_w - c->w - c->margin[LEFT] - c->margin[RIGHT];
                    if (room > 0) {
                        if (c->style->margin[LEFT].kind == TC_SIZE_AUTO && c->style->margin[RIGHT].kind == TC_SIZE_AUTO)
                            c->margin[LEFT] += room / 2;
                        else if (c->style->margin[LEFT].kind == TC_SIZE_AUTO)
                            c->margin[LEFT] += room;
                    }
                }
            } else {
                layout_block(l, c, content_w, child_cbh, -1, &r, depth + 1);
            }
            c->x = ox + c->margin[LEFT];
            if (r.self_collapse) {
                margins m = mmerge(r.top, r.bottom);
                if (escaping) {
                    res->top = mmerge(res->top, m);
                    c->y = oy;
                } else {
                    pending = mmerge(pending, m);
                    c->y = oy + cursor + msum(pending);
                }
            } else {
                if (escaping) {
                    res->top = mmerge(res->top, r.top);
                    c->y = oy;
                    escaping = 0;
                } else {
                    c->y = oy + cursor + msum(mmerge(pending, r.top));
                }
                cursor = c->y - oy + c->h;
                pending = r.bottom;
                any = 1;
            }
            /* Relative positioning moves a box, but not the others. */
            if (c->style->position == TC_POSITION_RELATIVE || c->style->position == TC_POSITION_STICKY) {
                const tc_style *cs = c->style;
                if (cs->inset[LEFT].kind == TC_SIZE_LENGTH)
                    c->x += lp(&cs->inset[LEFT].length, content_w);
                else if (cs->inset[RIGHT].kind == TC_SIZE_LENGTH)
                    c->x -= lp(&cs->inset[RIGHT].length, content_w);
                if (cs->inset[TOP].kind == TC_SIZE_LENGTH && (child_cbh >= 0 || !has_percent(&cs->inset[TOP].length)))
                    c->y += lp(&cs->inset[TOP].length, child_cbh);
                else if (cs->inset[BOTTOM].kind == TC_SIZE_LENGTH &&
                         (child_cbh >= 0 || !has_percent(&cs->inset[BOTTOM].length)))
                    c->y -= lp(&cs->inset[BOTTOM].length, child_cbh);
            }
            add_ink(b, c->x + c->ink[0], c->y + c->ink[1], c->x + c->ink[2], c->y + c->ink[3]);
        }
    }

    if (!any && top_collapse && bottom_collapse && h <= 0 &&
        (s->min_height.kind != TC_SIZE_LENGTH || lp(&s->min_height.length, cbh) <= 0)) {
        res->self_collapse = 1;
        madd(&res->bottom, b->margin[BOTTOM]);
        content_h = 0;
    } else if (bottom_collapse && !b->has_lines) {
        madd(&pending, b->margin[BOTTOM]);
        res->bottom = pending;
        content_h = cursor;
    } else {
        content_h = b->has_lines ? content_h : cursor + msum(pending);
        madd(&res->bottom, b->margin[BOTTOM]);
    }
    if (h < 0)
        h = content_h;
    h = clamp_size(b, h, &s->min_height, &s->max_height, cbh, 0);
    if (h < 0)
        h = 0;
    b->h = h + mbp_v(b);
    if (!b->parent && b->h < l->vh)
        b->h = l->vh;
    add_ink(b, 0, 0, b->w, b->h);
    if (b->has_lines) {
        uint32_t i;
        for (i = 0; i < b->nfrags; i++) {
            const tc_frag *f = &b->frags[i];
            if (f->type == TC_FRAG_TEXT) {
                const tc_font_metrics *m = tc_font_get_metrics(f->font);
                add_ink(b, f->x, f->y - m->ascent, f->x + f->w, f->y + m->descent);
            } else if (f->type == TC_FRAG_ATOMIC) {
                add_ink(b, f->x + f->box->ink[0], f->y + f->box->ink[1], f->x + f->box->ink[2],
                        f->y + f->box->ink[3]);
            } else {
                add_ink(b, f->x, f->y, f->x + f->w, f->y + f->h);
            }
        }
    } else if (b->type == TC_BOX_BLOCK || b->type == TC_BOX_INLINE_BLOCK) {
        /* A block's baseline is its last line's. */
        b->baseline = -1;
        for (c = b->first; c; c = c->next)
            if (c->baseline >= 0 && c->type == TC_BOX_BLOCK)
                b->baseline = c->y + c->baseline;
    }
    /* An inline-block with overflow other than visible sits on its bottom
       margin edge (-1), as does one without lines. */
    if (b->type == TC_BOX_INLINE_BLOCK && s->overflow_x != TC_OVERFLOW_VISIBLE)
        b->baseline = -1;
    if (b->has_marker) {
        float fb = first_baseline(b, depth);
        const tc_font_metrics *m = tc_font_get_metrics(tc_layout_font(l, s));
        b->marker_x = b->border[LEFT] + b->padding[LEFT];
        b->marker_y = fb >= 0 ? fb : b->border[TOP] + b->padding[TOP] + m->ascent;
        add_ink(b, b->marker_x - 4 * s->font_size, b->marker_y - m->ascent, b->marker_x, b->marker_y + m->descent);
    }
}

/* ---- The document ---- */

int tc_layout_document(const tc_document *doc, const tc_styled *styled, tc_font_set *fonts, float w, float h,
                       tc_layout **out)
{
    tc_layout *l;
    block_result r;
    *out = NULL;
    l = calloc(1, sizeof *l);
    if (!l)
        return TC_ERR_NOMEM;
    tc_arena_init(&l->arena);
    l->doc = doc;
    l->styled = styled;
    l->fonts = fonts;
    l->vw = w;
    l->vh = h;
    if (tc_box_build(l) != TC_OK) {
        tc_layout_free(l);
        return TC_ERR_NOMEM;
    }
    layout_block(l, l->root, w, h, -1, &r, 0);
    if (l->root->first) {
        /* The root element's margins don't collapse with anything. */
        tc_box *html = l->root->first;
        html->y = html->margin[TOP];
        l->root->h = TC_MAX(l->root->h, html->y + html->h + html->margin[BOTTOM]);
        add_ink(l->root, 0, 0, l->root->w, l->root->h);
    }
    /* Each length is bounded, but many add up: keep the document's size to
       something its viewers can turn into ints. */
    l->doc_w = TC_MIN(TC_MAX(w, l->root->ink[2]), 1e8f);
    l->doc_h = TC_MIN(TC_MAX(h, TC_MAX(l->root->h, l->root->ink[3])), 1e8f);
    l->stats.bytes = l->arena.total;
    if (l->nomem) {
        tc_layout_free(l);
        return TC_ERR_NOMEM;
    }
    *out = l;
    return TC_OK;
}

void tc_layout_free(tc_layout *l)
{
    if (!l)
        return;
    tc_arena_free(&l->arena);
    free(l);
}

const tc_box *tc_layout_root(const tc_layout *l)
{
    return l->root;
}

void tc_layout_size(const tc_layout *l, float *w, float *h)
{
    *w = l->doc_w;
    *h = l->doc_h;
}

void tc_layout_stats_of(const tc_layout *l, tc_layout_stats *out)
{
    *out = l->stats;
}
