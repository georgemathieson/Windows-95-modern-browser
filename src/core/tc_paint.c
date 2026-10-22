/* Painting (CSS 2.1 appendix E, without stacking contexts until Sprint 7):
 * the canvas, backgrounds, borders, text and its decorations, list markers
 * and replaced elements' placeholders, in tree order; and hit testing over
 * the same boxes. Boxes outside the area being painted are skipped. */
#include <math.h>
#include <string.h>

#include "tc_layout_internal.h"

enum { TOP, RIGHT, BOTTOM, LEFT };

typedef struct painter {
    tc_layout *l;
    tc_surface *s;
    tc_rect clip;
    const tc_box *propagated;  /* the box whose background is the canvas's */
} painter;

static int px(float v)
{
    return (int)floor(v + 0.5f);
}

static void fill(painter *p, int x, int y, int w, int h, uint32_t argb)
{
    if (w > 0 && h > 0 && (argb >> 24))
        tc_surface_fill(p->s, tc_rect_intersect(p->clip, tc_rect_make(x, y, w, h)), argb);
}

static uint32_t shade(uint32_t c, int light)
{
    uint32_t a = c & 0xFF000000u, r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (light) {
        r = (r + 255) / 2, g = (g + 255) / 2, b = (b + 255) / 2;
    } else {
        r = r * 2 / 3, g = g * 2 / 3, b = b * 2 / 3;
    }
    return a | r << 16 | g << 8 | b;
}

/* One border side as rows (or columns) of pixels, mitred at the corners. */
static void border_side(painter *p, int side, int x0, int y0, int x1, int y1, const int bw[4], int style,
                        uint32_t color)
{
    int t = bw[side], i, len, along0, along1;
    int before = side == TOP || side == BOTTOM ? bw[LEFT] : bw[TOP];
    int after = side == TOP || side == BOTTOM ? bw[RIGHT] : bw[BOTTOM];
    if (t <= 0 || !(color >> 24) || style == TC_BORDER_NONE || style == TC_BORDER_HIDDEN)
        return;
    len = side == TOP || side == BOTTOM ? x1 - x0 : y1 - y0;
    for (i = 0; i < t; i++) {
        /* Row i from the outside in. */
        uint32_t c = color;
        int a = i * before / t, b = i * after / t, pos;
        int outer = i < (t + 1) / 2;
        switch (style) {
        case TC_BORDER_INSET: c = shade(color, side == BOTTOM || side == RIGHT); break;
        case TC_BORDER_OUTSET: c = shade(color, side == TOP || side == LEFT); break;
        case TC_BORDER_GROOVE: c = shade(color, (side == BOTTOM || side == RIGHT) == outer); break;
        case TC_BORDER_RIDGE: c = shade(color, (side == TOP || side == LEFT) == outer); break;
        case TC_BORDER_DOUBLE:
            if (t >= 3 && i >= t / 3 && i < t - t / 3)
                continue;
            break;
        default: break;
        }
        along0 = a;
        along1 = len - b;
        if (style == TC_BORDER_DOTTED || style == TC_BORDER_DASHED) {
            int seg = style == TC_BORDER_DOTTED ? t : 3 * t, period = style == TC_BORDER_DOTTED ? 2 * t : 5 * t;
            for (pos = along0; pos < along1; pos += period) {
                int e = TC_MIN(pos + seg, along1);
                switch (side) {
                case TOP: fill(p, x0 + pos, y0 + i, e - pos, 1, c); break;
                case BOTTOM: fill(p, x0 + pos, y1 - 1 - i, e - pos, 1, c); break;
                case LEFT: fill(p, x0 + i, y0 + pos, 1, e - pos, c); break;
                default: fill(p, x1 - 1 - i, y0 + pos, 1, e - pos, c); break;
                }
            }
            continue;
        }
        switch (side) {
        case TOP: fill(p, x0 + along0, y0 + i, along1 - along0, 1, c); break;
        case BOTTOM: fill(p, x0 + along0, y1 - 1 - i, along1 - along0, 1, c); break;
        case LEFT: fill(p, x0 + i, y0 + along0, 1, along1 - along0, c); break;
        default: fill(p, x1 - 1 - i, y0 + along0, 1, along1 - along0, c); break;
        }
    }
}

/* Background and borders of a border box. edges: which of the left and
   right sides to draw (inline boxes split over lines). */
static void paint_box_decorations(painter *p, const tc_box *b, float x, float y, float w, float h, int edges)
{
    const tc_style *s = b->style;
    int x0 = px(x), y0 = px(y), x1 = px(x + w), y1 = px(y + h), bw[4], i;
    if (b != p->propagated)
        fill(p, x0, y0, x1 - x0, y1 - y0, s->background_color);
    for (i = 0; i < 4; i++)
        bw[i] = s->border_width[i] > 0 && s->border_width[i] < 1 ? 1 : px(s->border_width[i]);
    if (!(edges & 1))
        bw[LEFT] = 0;
    if (!(edges & 2))
        bw[RIGHT] = 0;
    for (i = 0; i < 4; i++)
        border_side(p, i, x0, y0, x1, y1, bw, s->border_style[i], s->border_color[i]);
}

static void paint_decorations(painter *p, const tc_style *s, const tc_font *font, float x, float baseline, float w)
{
    const tc_font_metrics *m;
    int x0, x1, t;
    if (!s->decorations || !(s->decorations_color >> 24))
        return;
    m = tc_font_get_metrics(font);
    x0 = px(x);
    x1 = px(x + w);
    t = px(m->underline_thickness) > 0 ? px(m->underline_thickness) : 1;
    if (s->decorations & TC_DECORATION_UNDERLINE)
        fill(p, x0, px(baseline + m->underline_offset), x1 - x0, t, s->decorations_color);
    if (s->decorations & TC_DECORATION_OVERLINE)
        fill(p, x0, px(baseline - m->ascent), x1 - x0, t, s->decorations_color);
    if (s->decorations & TC_DECORATION_LINE_THROUGH)
        fill(p, x0, px(baseline - m->strikeout_offset), x1 - x0, t, s->decorations_color);
}

static void paint_text(painter *p, const tc_frag *f, float ox, float oy)
{
    const tc_style *s = f->box->style;
    if (s->visibility != TC_VISIBILITY_VISIBLE)
        return;
    if (f->len)
        tc_font_draw(f->font, p->s, p->clip, ox + f->x, oy + f->y, f->text, f->len, s->letter_spacing, s->color);
    paint_decorations(p, s, f->font, ox + f->x, oy + f->y, f->w);
}

/* A list item's outside marker. */
static void paint_marker(painter *p, const tc_box *b, float x, float y)
{
    const tc_element_styles *es = b->element ? tc_styled_element(p->l->styled, b->element) : NULL;
    const tc_style *ms = es && es->marker ? es->marker : b->style;
    const tc_font *font = tc_layout_font(p->l, ms);
    const tc_font_metrics *m = tc_font_get_metrics(font);
    float fs = ms->font_size, mx = x + b->marker_x, base = y + b->marker_y;
    char text[64];
    size_t len;

    if (ms->visibility != TC_VISIBILITY_VISIBLE)
        return;
    switch (b->style->list_style_type) {
    case TC_LIST_DISC:
    case TC_LIST_CIRCLE:
    case TC_LIST_SQUARE:
    case TC_LIST_DISCLOSURE_OPEN:
    case TC_LIST_DISCLOSURE_CLOSED: {
        int d = px(fs * 0.35f), cx, cy, row;
        if (d < 3)
            d = 3;
        cx = px(mx - fs * 0.55f - d / 2.0f);
        cy = px(base - m->x_height / 2);
        for (row = 0; row < d; row++) {
            /* A circle's span on this row. */
            float r = d / 2.0f, dy = row + 0.5f - r, half = (float)sqrt(r * r - dy * dy);
            int left = px(cx - half), right = px(cx + half);
            switch (b->style->list_style_type) {
            case TC_LIST_SQUARE:
                fill(p, cx - d / 2, cy - d / 2 + row, d, 1, ms->color);
                break;
            case TC_LIST_CIRCLE:
                if (row == 0 || row == d - 1) {
                    fill(p, left, cy - d / 2 + row, right - left, 1, ms->color);
                } else {
                    fill(p, left, cy - d / 2 + row, 1, 1, ms->color);
                    fill(p, right - 1, cy - d / 2 + row, 1, 1, ms->color);
                }
                break;
            default:
                fill(p, left, cy - d / 2 + row, right - left, 1, ms->color);
                break;
            }
        }
        return;
    }
    default:
        len = tc_marker_text(b->style, b->number, text, sizeof text);
        if (len) {
            float w = tc_font_measure(font, text, len, 0);
            tc_font_draw(font, p->s, p->clip, mx - w, base, text, len, 0, ms->color);
        }
        return;
    }
}

/* Replaced elements, until Sprint 4 draws pictures. */
static void paint_replaced(painter *p, const tc_box *b, float x, float y)
{
    const tc_style *s = b->style;
    float cx = x + b->border[LEFT] + b->padding[LEFT], cy = y + b->border[TOP] + b->padding[TOP];
    float cw = b->w - b->border[LEFT] - b->border[RIGHT] - b->padding[LEFT] - b->padding[RIGHT];
    float ch = b->h - b->border[TOP] - b->border[BOTTOM] - b->padding[TOP] - b->padding[BOTTOM];
    tc_rect saved = p->clip;
    const tc_font *font = tc_layout_font(p->l, s);
    const tc_font_metrics *m = tc_font_get_metrics(font);
    int x0 = px(cx), y0 = px(cy), w = px(cx + cw) - x0, h = px(cy + ch) - y0;

    switch (b->replaced) {
    case TC_REPLACED_IMAGE:
        if (w >= 4 && h >= 4 && (s->width.kind == TC_SIZE_LENGTH || s->height.kind == TC_SIZE_LENGTH)) {
            /* A frame where the picture will go. */
            fill(p, x0, y0, w, 1, 0xFFA0A0A0u);
            fill(p, x0, y0, 1, h, 0xFFA0A0A0u);
            fill(p, x0, y0 + h - 1, w, 1, 0xFFE0E0E0u);
            fill(p, x0 + w - 1, y0, 1, h, 0xFFE0E0E0u);
        }
        if (b->len) {
            p->clip = tc_rect_intersect(p->clip, tc_rect_make(x0, y0, w, h));
            tc_font_draw(font, p->s, p->clip, cx + 2, cy + 2 + m->ascent, b->text, b->len, 0, s->color);
            p->clip = saved;
        }
        break;
    case TC_REPLACED_TEXT_FIELD:
        if (b->len) {
            int placeholder = !b->element || !tc_dom_get_attr(b->element, "value") ||
                              !*tc_dom_get_attr(b->element, "value");
            p->clip = tc_rect_intersect(p->clip, tc_rect_make(x0, y0, w, h));
            tc_font_draw(font, p->s, p->clip, cx, y + b->baseline, b->text, b->len, 0,
                         placeholder ? 0xFF808080u : s->color);
            p->clip = saved;
        }
        break;
    case TC_REPLACED_CHECKBOX: {
        /* Windows 95's: sunken and white, with a tick. */
        static const uint8_t tick[7] = { 0x01, 0x03, 0x47, 0x6E, 0x7C, 0x38, 0x10 };
        int i, k;
        fill(p, x0, y0, 12, 1, 0xFF808080u);
        fill(p, x0, y0, 1, 12, 0xFF808080u);
        fill(p, x0 + 1, y0 + 1, 10, 1, 0xFF000000u);
        fill(p, x0 + 1, y0 + 1, 1, 10, 0xFF000000u);
        fill(p, x0, y0 + 12, 13, 1, 0xFFFFFFFFu);
        fill(p, x0 + 12, y0, 1, 13, 0xFFFFFFFFu);
        fill(p, x0 + 1, y0 + 11, 11, 1, 0xFFDFDFDFu);
        fill(p, x0 + 11, y0 + 1, 1, 11, 0xFFDFDFDFu);
        fill(p, x0 + 2, y0 + 2, 9, 9, 0xFFFFFFFFu);
        if (b->checked)
            for (i = 0; i < 7; i++)
                for (k = 0; k < 7; k++)
                    if (tick[i] & (0x40 >> k))
                        fill(p, x0 + 3 + k, y0 + 3 + i, 1, 1, 0xFF000000u);
        break;
    }
    case TC_REPLACED_RADIO: {
        /* A sunken circle: dark above, light below, white inside. */
        int row, col;
        for (row = 0; row < 12; row++)
            for (col = 0; col < 12; col++) {
                float dx = col - 5.5f, dy = row - 5.5f, d = (float)sqrt(dx * dx + dy * dy);
                uint32_t c;
                if (d > 6)
                    continue;
                if (d > 5)
                    c = dx + dy < 0 ? 0xFF808080u : 0xFFFFFFFFu;
                else if (d > 4)
                    c = dx + dy < 0 ? 0xFF000000u : 0xFFDFDFDFu;
                else
                    c = b->checked && d < 2 ? 0xFF000000u : 0xFFFFFFFFu;
                fill(p, x0 + col, y0 + row, 1, 1, c);
            }
        break;
    }
    default:
        break; /* frames and media: the space they take */
    }
}

static int culled(const painter *p, const tc_box *b, float x, float y)
{
    return x + b->ink[2] < p->clip.x || x + b->ink[0] > p->clip.x + p->clip.w || y + b->ink[3] < p->clip.y ||
           y + b->ink[1] > p->clip.y + p->clip.h;
}

static void paint_box(painter *p, const tc_box *b, float ox, float oy, int depth)
{
    const tc_style *s = b->style;
    float x = ox + b->x, y = oy + b->y;
    int visible = s->visibility == TC_VISIBILITY_VISIBLE;
    tc_rect saved = p->clip;
    uint32_t i;

    if (depth > TC_LAYOUT_MAX_DEPTH + 16 || culled(p, b, x, y))
        return;
    if (visible && b->type != TC_BOX_INLINE && b->type != TC_BOX_TEXT)
        paint_box_decorations(p, b, x, y, b->w, b->h, 3);
    if (visible && b->type == TC_BOX_REPLACED)
        paint_replaced(p, b, x, y);
    if (b->has_marker)
        paint_marker(p, b, x, y);
    if (s->overflow_x != TC_OVERFLOW_VISIBLE && b->parent && b->parent->parent) {
        /* Clip to the padding box. */
        p->clip = tc_rect_intersect(p->clip, tc_rect_make(px(x + b->border[LEFT]), px(y + b->border[TOP]),
                                                          px(b->w - b->border[LEFT] - b->border[RIGHT]),
                                                          px(b->h - b->border[TOP] - b->border[BOTTOM])));
        if (tc_rect_is_empty(p->clip)) {
            p->clip = saved;
            return;
        }
    }
    if (b->has_lines) {
        for (i = 0; i < b->nfrags; i++) {
            const tc_frag *f = &b->frags[i];
            switch (f->type) {
            case TC_FRAG_BOX:
                if (f->box->style->visibility == TC_VISIBILITY_VISIBLE && y + f->y < p->clip.y + p->clip.h &&
                    y + f->y + f->h > p->clip.y)
                    paint_box_decorations(p, f->box, x + f->x, y + f->y, f->w, f->h, f->edges);
                break;
            case TC_FRAG_TEXT:
                if (y + f->y + 2 * f->box->style->font_size > p->clip.y &&
                    y + f->y - 2 * f->box->style->font_size < p->clip.y + p->clip.h)
                    paint_text(p, f, x, y);
                break;
            case TC_FRAG_ATOMIC:
                paint_box(p, f->box, x, y, depth + 1);
                break;
            default:
                break;
            }
        }
    } else {
        const tc_box *c;
        for (c = b->first; c; c = c->next)
            paint_box(p, c, x, y, depth + 1);
    }
    p->clip = saved;
}

void tc_paint(const tc_layout *l, tc_surface *s, tc_rect clip, float scroll_x, float scroll_y)
{
    painter p;
    const tc_box *html = l->root ? l->root->first : NULL, *body = NULL;
    uint32_t canvas = 0xFFFFFFFFu;

    p.l = (tc_layout *)l;
    p.s = s;
    p.clip = tc_rect_intersect(clip, tc_rect_make(0, 0, s->width, s->height));
    p.propagated = NULL;
    if (tc_rect_is_empty(p.clip))
        return;
    /* The root's background covers the canvas, or failing that body's. */
    if (html && html->element && tc_dom_is_html(&html->element->node, TC_TAG_HTML)) {
        const tc_box *c;
        for (c = html->first; c; c = c->next)
            if (c->element && tc_dom_is_html(&c->element->node, TC_TAG_BODY)) {
                body = c;
                break;
            }
    }
    if (html && html->style->background_color >> 24) {
        canvas = html->style->background_color;
        p.propagated = html;
    } else if (body && body->style->background_color >> 24) {
        canvas = body->style->background_color;
        p.propagated = body;
    }
    if ((canvas >> 24) != 255)
        tc_surface_fill(s, p.clip, 0xFFFFFFFFu);
    tc_surface_fill(s, p.clip, canvas);
    if (l->root)
        paint_box(&p, l->root, -scroll_x, -scroll_y, 0);
}

/* ---- Hit testing ---- */

static const tc_box *hit_box(const tc_box *b, float ox, float oy, float hx, float hy, int depth)
{
    float x = ox + b->x, y = oy + b->y;
    const tc_box *found;
    uint32_t i;

    if (depth > TC_LAYOUT_MAX_DEPTH + 16 || hx < x + b->ink[0] || hx >= x + b->ink[2] || hy < y + b->ink[1] ||
        hy >= y + b->ink[3])
        return NULL;
    if (b->has_lines) {
        for (i = b->nfrags; i-- > 0;) {
            const tc_frag *f = &b->frags[i];
            if (f->type == TC_FRAG_ATOMIC) {
                if ((found = hit_box(f->box, x, y, hx, hy, depth + 1)) != NULL)
                    return found;
            } else if (f->type == TC_FRAG_TEXT) {
                const tc_font_metrics *m = tc_font_get_metrics(f->font);
                if (hx >= x + f->x && hx < x + f->x + f->w && hy >= y + f->y - m->ascent && hy < y + f->y + m->descent)
                    return f->box;
            } else if (f->type == TC_FRAG_BOX) {
                if (hx >= x + f->x && hx < x + f->x + f->w && hy >= y + f->y && hy < y + f->y + f->h)
                    return f->box;
            }
        }
    } else {
        const tc_box *c, *last = NULL;
        for (c = b->first; c; c = c->next)
            if ((found = hit_box(c, x, y, hx, hy, depth + 1)) != NULL)
                last = found; /* later boxes paint on top */
        if (last)
            return last;
    }
    if (hx >= x && hx < x + b->w && hy >= y && hy < y + b->h)
        return b;
    return NULL;
}

const tc_element *tc_layout_hit(const tc_layout *l, float x, float y)
{
    const tc_box *b = l->root ? hit_box(l->root, 0, 0, x, y, 0) : NULL;
    for (; b; b = b->parent)
        if (b->element)
            return b->element;
    return NULL;
}

static int find_box(const tc_box *b, const tc_element *el, float ox, float oy, float *fx, float *fy, int depth)
{
    float x = ox + b->x, y = oy + b->y;
    const tc_box *c;
    uint32_t i;
    if (depth > TC_LAYOUT_MAX_DEPTH + 16)
        return 0;
    if (b->element == el && b->type != TC_BOX_INLINE) {
        *fx = x;
        *fy = y;
        return 1;
    }
    for (i = 0; i < b->nfrags; i++) {
        const tc_frag *f = &b->frags[i];
        const tc_box *k;
        for (k = f->box; k && k != b; k = k->parent)
            if (k->element == el) {
                *fx = x + f->x;
                *fy = y + (f->type == TC_FRAG_TEXT ? f->y - tc_font_get_metrics(f->font)->ascent : f->y);
                return 1;
            }
        if (f->type == TC_FRAG_ATOMIC && find_box(f->box, el, x, y, fx, fy, depth + 1))
            return 1;
    }
    if (!b->has_lines)
        for (c = b->first; c; c = c->next)
            if (find_box(c, el, x, y, fx, fy, depth + 1))
                return 1;
    return 0;
}

int tc_layout_find(const tc_layout *l, const tc_element *el, float *x, float *y)
{
    return l->root && el ? find_box(l->root, el, 0, 0, x, y, 0) : 0;
}

/* ---- Debugging ---- */

static void dump_box(const tc_box *b, int depth, tc_buf *out)
{
    static const char *const types[] = { "block", "inline", "inline-block", "text", "br", "replaced" };
    static const char *const frags[] = { "text", "box", "atomic", "marker" };
    const tc_box *c;
    uint32_t i;
    int k;
    if (depth > TC_LAYOUT_MAX_DEPTH + 16)
        return;
    for (k = 0; k < depth; k++)
        tc_buf_append_str(out, "  ");
    tc_buf_printf(out, "%s", types[b->type]);
    if (b->element)
        tc_buf_printf(out, " <%s>", b->element->name);
    else if (b->type != TC_BOX_TEXT)
        tc_buf_append_str(out, " (anonymous)");
    if (b->type == TC_BOX_TEXT) {
        tc_buf_append_str(out, " \"");
        tc_buf_append(out, b->text, b->len > 40 ? 40 : b->len);
        tc_buf_append_str(out, "\"\n");
        return;
    }
    tc_buf_printf(out, " %g,%g %gx%g", b->x, b->y, b->w, b->h);
    if (b->has_marker)
        tc_buf_printf(out, " marker %d", (int)b->number);
    tc_buf_append_char(out, '\n');
    for (i = 0; i < b->nfrags; i++) {
        const tc_frag *f = &b->frags[i];
        for (k = 0; k <= depth; k++)
            tc_buf_append_str(out, "  ");
        tc_buf_printf(out, "| %s %g,%g w %g", frags[f->type], f->x, f->y, f->w);
        if (f->type == TC_FRAG_TEXT) {
            tc_buf_append_str(out, " \"");
            tc_buf_append(out, f->text, f->len);
            tc_buf_append_char(out, '"');
        } else if (f->box->element) {
            tc_buf_printf(out, " <%s>", f->box->element->name);
        }
        tc_buf_append_char(out, '\n');
        if (f->type == TC_FRAG_ATOMIC)
            dump_box(f->box, depth + 2, out);
    }
    if (!b->has_lines)
        for (c = b->first; c; c = c->next)
            dump_box(c, depth + 1, out);
}

int tc_layout_dump(const tc_layout *l, tc_buf *out)
{
    if (l->root)
        dump_box(l->root, 0, out);
    return out->data ? TC_OK : TC_ERR_NOMEM;
}
