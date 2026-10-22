#include "tc_woff.h"

#include <stdlib.h>
#include <string.h>

#include "tc_inflate.h"

#define MAX_FONT (32u << 20)
#define MAX_TABLES 256

typedef struct out_table {
    uint32_t tag;
    const uint8_t *data;   /* NULL: built in `built` */
    uint32_t len;
    tc_buf built;
} out_table;

static uint32_t u16(const uint8_t *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int put16(tc_buf *b, uint32_t v)
{
    uint8_t x[2];
    x[0] = (uint8_t)(v >> 8), x[1] = (uint8_t)v;
    return tc_buf_append(b, x, 2);
}

static int put32(tc_buf *b, uint32_t v)
{
    uint8_t x[4];
    x[0] = (uint8_t)(v >> 24), x[1] = (uint8_t)(v >> 16), x[2] = (uint8_t)(v >> 8), x[3] = (uint8_t)v;
    return tc_buf_append(b, x, 4);
}

int tc_woff_sniff(const void *data, size_t len)
{
    return len >= 4 && (memcmp(data, "wOFF", 4) == 0 || memcmp(data, "wOF2", 4) == 0);
}

static int compare_tags(const void *a, const void *b)
{
    uint32_t x = ((const out_table *)a)->tag, y = ((const out_table *)b)->tag;
    return x < y ? -1 : x > y;
}

/* An sfnt from its tables: the directory, then each table 4-byte aligned. */
static int write_sfnt(uint32_t flavor, out_table *t, uint32_t n, tc_buf *out)
{
    uint32_t i, range = 1, selector = 0, offset = 12 + 16 * n;
    uint64_t total = offset;
    int err;
    qsort(t, n, sizeof *t, compare_tags);
    for (i = 0; i < n; i++)
        total += (t[i].len + 3u) & ~3u;
    if (total > MAX_FONT)
        return TC_ERR_TOO_LARGE;
    while (range * 2 <= n)
        range *= 2, selector++;
    err = tc_buf_reserve(out, (size_t)total);
    if (!err)
        err = put32(out, flavor);
    if (!err)
        err = put16(out, n);
    if (!err)
        err = put16(out, range * 16);
    if (!err)
        err = put16(out, selector);
    if (!err)
        err = put16(out, n * 16 - range * 16);
    for (i = 0; i < n && !err; i++) {
        err = put32(out, t[i].tag);
        if (!err)
            err = put32(out, 0); /* checksums: nothing reads them */
        if (!err)
            err = put32(out, offset);
        if (!err)
            err = put32(out, t[i].len);
        offset += (t[i].len + 3u) & ~3u;
    }
    for (i = 0; i < n && !err; i++) {
        static const uint8_t zeros[3] = { 0, 0, 0 };
        err = tc_buf_append(out, t[i].data ? t[i].data : (const uint8_t *)t[i].built.data, t[i].len);
        if (!err)
            err = tc_buf_append(out, zeros, (4 - t[i].len % 4) % 4);
    }
    return err;
}

/* ---- WOFF ---- */

static int decode_woff(const uint8_t *d, size_t len, tc_buf *out)
{
    out_table t[MAX_TABLES];
    tc_buf raw[MAX_TABLES];
    uint32_t n, i, nraw = 0;
    uint64_t total = 0;
    int err = TC_OK;

    if (len < 44)
        return TC_ERR_DECODE;
    n = u16(d + 12);
    if (!n || n > MAX_TABLES || 44 + (size_t)20 * n > len)
        return TC_ERR_DECODE;
    memset(t, 0, sizeof t);
    for (i = 0; i < n && !err; i++) {
        const uint8_t *e = d + 44 + 20 * i;
        uint32_t off = u32(e + 4), clen = u32(e + 8), olen = u32(e + 12);
        t[i].tag = u32(e);
        t[i].len = olen;
        total += olen;
        if (off > len || clen > len - off || clen > olen || total > MAX_FONT) {
            err = total > MAX_FONT ? TC_ERR_TOO_LARGE : TC_ERR_DECODE;
        } else if (clen == olen) {
            t[i].data = d + off;
        } else {
            tc_buf_init(&raw[nraw]);
            err = tc_inflate(TC_CODING_DEFLATE, d + off, clen, olen, &raw[nraw]);
            if (!err && raw[nraw].len != olen)
                err = TC_ERR_DECODE;
            t[i].data = (const uint8_t *)raw[nraw].data;
            nraw++;
        }
    }
    if (!err)
        err = write_sfnt(u32(d + 4), t, n, out);
    for (i = 0; i < nraw; i++)
        tc_buf_free(&raw[i]);
    return err;
}

/* ---- WOFF2 ---- */

static const char known_tags[] = "cmapheadhheahmtxmaxpnameOS/2postcvt fpgmglyflocaprepCFF VORGEBDTEBLCgasphdmxkern"
                                 "LTSHPCLTVDMXvheavmtxBASEGDEFGPOSGSUBEBSCJSTFMATHCBDTCBLCCOLRCPALSVG sbixacntavar"
                                 "bdatblocbslncvarfdscfeatfmtxfvargvarhstyjustlcarmortmorxopbdproptrakZapfSilfGlat"
                                 "GlocFeatSill";

#define TAG(s) ((uint32_t)(s)[0] << 24 | (uint32_t)(s)[1] << 16 | (uint32_t)(s)[2] << 8 | (uint32_t)(s)[3])

/* A stream to read, which can't be read past. */
typedef struct rd {
    const uint8_t *p, *end;
    int bad;
} rd;

static uint32_t rd8(rd *r)
{
    if (r->p >= r->end) {
        r->bad = 1;
        return 0;
    }
    return *r->p++;
}

static uint32_t rd16(rd *r)
{
    uint32_t hi = rd8(r);
    return hi << 8 | rd8(r);
}

static uint32_t rd32(rd *r)
{
    uint32_t hi = rd16(r);
    return hi << 16 | rd16(r);
}

static const uint8_t *rdn(rd *r, uint32_t n)
{
    const uint8_t *p = r->p;
    if ((size_t)(r->end - r->p) < n) {
        r->bad = 1;
        return NULL;
    }
    r->p += n;
    return p;
}

static uint32_t base128(rd *r)
{
    uint32_t v = 0, i;
    for (i = 0; i < 5; i++) {
        uint32_t b = rd8(r);
        if ((i == 0 && b == 0x80) || (v & 0xFE000000u)) {
            r->bad = 1; /* a leading zero, or too big */
            return 0;
        }
        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80))
            return v;
    }
    r->bad = 1;
    return 0;
}

static uint32_t uint255(rd *r)
{
    uint32_t code = rd8(r);
    if (code == 253)
        return rd16(r);
    if (code == 255)
        return rd8(r) + 253;
    if (code == 254)
        return rd8(r) + 506;
    return code;
}

static rd sub_stream(rd *r, uint32_t len)
{
    rd s;
    s.p = rdn(r, len);
    s.end = s.p ? s.p + len : NULL;
    s.bad = !s.p;
    return s;
}

/* One point's move, from its flag and 1 to 4 bytes (WOFF2, 5.2). */
static int triplet(rd *g, uint32_t flag, int32_t *dx, int32_t *dy)
{
    uint32_t f = flag & 0x7F, b0, b1, b2;
#define SIGN(fl, v) (((fl) & 1) ? (int32_t)(v) : -(int32_t)(v))
    if (f < 10) {
        *dx = 0;
        *dy = SIGN(f, ((f & 14) << 7) + rd8(g));
    } else if (f < 20) {
        *dx = SIGN(f, (((f - 10) & 14) << 7) + rd8(g));
        *dy = 0;
    } else if (f < 84) {
        b0 = f - 20;
        b1 = rd8(g);
        *dx = SIGN(f, 1 + (b0 & 0x30) + (b1 >> 4));
        *dy = SIGN(f >> 1, 1 + ((b0 & 0x0C) << 2) + (b1 & 0x0F));
    } else if (f < 120) {
        b0 = f - 84;
        b1 = rd8(g);
        b2 = rd8(g);
        *dx = SIGN(f, 1 + ((b0 / 12) << 8) + b1);
        *dy = SIGN(f >> 1, 1 + (((b0 % 12) >> 2) << 8) + b2);
    } else if (f < 124) {
        b0 = rd8(g);
        b1 = rd8(g);
        b2 = rd8(g);
        *dx = SIGN(f, (b0 << 4) + (b1 >> 4));
        *dy = SIGN(f >> 1, ((b1 & 0x0F) << 8) + b2);
    } else {
        b0 = rd16(g);
        b1 = rd16(g);
        *dx = SIGN(f, b0);
        *dy = SIGN(f >> 1, b1);
    }
#undef SIGN
    return !g->bad;
}

/* The bytes of a composite glyph's parts, and whether it has instructions. */
static int composite_size(rd *c, uint32_t *size, int *instructions)
{
    const uint8_t *start = c->p;
    uint32_t flags, count = 0;
    *instructions = 0;
    do {
        uint32_t skip = 2;
        flags = rd16(c);
        if (flags & 0x100)
            *instructions = 1;
        skip += (flags & 1) ? 4 : 2;
        if (flags & 8)
            skip += 2;
        else if (flags & 0x40)
            skip += 4;
        else if (flags & 0x80)
            skip += 8;
        rdn(c, skip);
        if (c->bad || ++count > 1024)
            return 0;
    } while (flags & 0x20);
    *size = (uint32_t)(c->p - start);
    return 1;
}

typedef struct glyf_out {
    tc_buf glyf, loca;
    int16_t *xmin;     /* each glyph's, for hmtx */
} glyf_out;

/* glyf and loca from their transformed form (WOFF2, 5.1). */
static int rebuild_glyf(rd *in, uint32_t nglyphs_maxp, glyf_out *o)
{
    rd ncontours, npoints, flags, glyphs, composites, bboxes, instructions, bitmap;
    uint32_t nglyphs, index_format, option, i, offset = 0, sizes[7];
    int32_t *xs = NULL, *ys = NULL;
    uint8_t *on = NULL;
    uint32_t cap = 0;
    const uint8_t *overlap = NULL;
    int err = TC_OK;

    rd16(in);
    option = rd16(in);
    nglyphs = rd16(in);
    index_format = rd16(in);
    for (i = 0; i < 7; i++)
        sizes[i] = rd32(in);
    if (in->bad || nglyphs != nglyphs_maxp || index_format > 1)
        return TC_ERR_DECODE;
    ncontours = sub_stream(in, sizes[0]);
    npoints = sub_stream(in, sizes[1]);
    flags = sub_stream(in, sizes[2]);
    glyphs = sub_stream(in, sizes[3]);
    composites = sub_stream(in, sizes[4]);
    bboxes = sub_stream(in, sizes[5]);
    instructions = sub_stream(in, sizes[6]);
    if (in->bad || ncontours.bad || npoints.bad || flags.bad || glyphs.bad || composites.bad || bboxes.bad ||
        instructions.bad)
        return TC_ERR_DECODE;
    /* The bbox stream starts with a bitmap: which glyphs have one given. */
    bitmap = sub_stream(&bboxes, ((nglyphs + 31) / 32) * 4);
    if (option & 1) {
        overlap = rdn(in, (nglyphs + 7) / 8);
        if (!overlap)
            return TC_ERR_DECODE;
    }
    if (bitmap.bad || !(o->xmin = calloc(nglyphs ? nglyphs : 1, sizeof *o->xmin)))
        return bitmap.bad ? TC_ERR_DECODE : TC_ERR_NOMEM;

    for (i = 0; i < nglyphs && !err; i++) {
        int has_bbox = bitmap.p[i / 8] >> (7 - i % 8) & 1;
        int32_t n = (int16_t)rd16(&ncontours), xmin = 0, ymin = 0, xmax = 0, ymax = 0;
        tc_buf *g = &o->glyf;
        if (ncontours.bad)
            err = TC_ERR_DECODE;
        else if (index_format) /* each glyph's offset */
            err = put32(&o->loca, offset);
        else
            err = put16(&o->loca, offset / 2);
        if (err)
            break;
        if (n == 0) {
            if (has_bbox)
                err = TC_ERR_DECODE;
        } else if (n == -1) { /* a composite */
            uint32_t size, ilen = 0;
            const uint8_t *parts = composites.p, *ins = NULL;
            int has_ins;
            if (!has_bbox || !composite_size(&composites, &size, &has_ins)) {
                err = TC_ERR_DECODE;
                break;
            }
            xmin = (int16_t)rd16(&bboxes), ymin = (int16_t)rd16(&bboxes);
            xmax = (int16_t)rd16(&bboxes), ymax = (int16_t)rd16(&bboxes);
            if (has_ins) {
                ilen = uint255(&glyphs);
                ins = rdn(&instructions, ilen);
            }
            if (bboxes.bad || glyphs.bad || (has_ins && !ins)) {
                err = TC_ERR_DECODE;
                break;
            }
            err = put16(g, 0xFFFF);
            err = err ? err : put16(g, (uint32_t)xmin & 0xFFFF);
            err = err ? err : put16(g, (uint32_t)ymin & 0xFFFF);
            err = err ? err : put16(g, (uint32_t)xmax & 0xFFFF);
            err = err ? err : put16(g, (uint32_t)ymax & 0xFFFF);
            err = err ? err : tc_buf_append(g, parts, size);
            if (!err && has_ins) {
                err = put16(g, ilen);
                err = err ? err : tc_buf_append(g, ins, ilen);
            }
        } else if (n > 0) { /* a simple glyph */
            uint32_t c, p, total = 0, ilen, ends_at = (uint32_t)g->len + 10;
            int32_t x = 0, y = 0;
            const uint8_t *ins;
            err = put16(g, (uint32_t)n);
            for (p = 0; p < 4 && !err; p++)
                err = put16(g, 0); /* the box, filled in below */
            for (c = 0; c < (uint32_t)n && !err; c++) {
                uint32_t k = uint255(&npoints);
                total += k;
                if (npoints.bad || !k || total > 0xFFFF)
                    err = TC_ERR_DECODE;
                else
                    err = put16(g, total - 1);
            }
            if (err)
                break;
            if (total > cap) {
                int32_t *nx = realloc(xs, total * sizeof *xs), *ny = nx ? realloc(ys, total * sizeof *ys) : NULL;
                uint8_t *no = ny ? realloc(on, total) : NULL;
                if (nx)
                    xs = nx;
                if (ny)
                    ys = ny;
                if (no)
                    on = no;
                if (!no) {
                    err = TC_ERR_NOMEM;
                    break;
                }
                cap = total;
            }
            for (p = 0; p < total; p++) {
                uint32_t fl = rd8(&flags);
                int32_t dx, dy;
                if (flags.bad || !triplet(&glyphs, fl, &dx, &dy)) {
                    err = TC_ERR_DECODE;
                    break;
                }
                x += dx;
                y += dy;
                if (x < -32768 || x > 32767 || y < -32768 || y > 32767) {
                    err = TC_ERR_DECODE;
                    break;
                }
                xs[p] = x, ys[p] = y, on[p] = !(fl & 0x80);
                if (!p || x < xmin)
                    xmin = x;
                if (!p || x > xmax)
                    xmax = x;
                if (!p || y < ymin)
                    ymin = y;
                if (!p || y > ymax)
                    ymax = y;
            }
            if (err)
                break;
            ilen = uint255(&glyphs);
            ins = rdn(&instructions, ilen);
            if (glyphs.bad || !ins) {
                err = TC_ERR_DECODE;
                break;
            }
            if (has_bbox) {
                xmin = (int16_t)rd16(&bboxes), ymin = (int16_t)rd16(&bboxes);
                xmax = (int16_t)rd16(&bboxes), ymax = (int16_t)rd16(&bboxes);
                if (bboxes.bad) {
                    err = TC_ERR_DECODE;
                    break;
                }
            }
            /* Flags (on curve, and overlap on the first), then every move as
               a 16-bit delta. */
            err = put16(g, ilen);
            err = err ? err : tc_buf_append(g, ins, ilen);
            for (p = 0; p < total && !err; p++) {
                uint8_t fl = (uint8_t)(on[p] | (p == 0 && overlap && (overlap[i / 8] >> (7 - i % 8) & 1) ? 0x40 : 0));
                err = tc_buf_append(g, &fl, 1);
            }
            for (p = 0; p < total && !err; p++)
                err = put16(g, (uint32_t)(xs[p] - (p ? xs[p - 1] : 0)) & 0xFFFF);
            for (p = 0; p < total && !err; p++)
                err = put16(g, (uint32_t)(ys[p] - (p ? ys[p - 1] : 0)) & 0xFFFF);
            if (!err) {
                uint8_t *box = (uint8_t *)g->data + ends_at - 8;
                box[0] = (uint8_t)(xmin >> 8), box[1] = (uint8_t)xmin;
                box[2] = (uint8_t)(ymin >> 8), box[3] = (uint8_t)ymin;
                box[4] = (uint8_t)(xmax >> 8), box[5] = (uint8_t)xmax;
                box[6] = (uint8_t)(ymax >> 8), box[7] = (uint8_t)ymax;
            }
        } else {
            err = TC_ERR_DECODE;
        }
        o->xmin[i] = (int16_t)xmin;
        while (!err && g->len % 4) /* padded, so short offsets stay exact */
            err = tc_buf_append(g, "", 1);
        if (!err && g->len > MAX_FONT)
            err = TC_ERR_TOO_LARGE;
        offset = (uint32_t)g->len;
        if (!err && !index_format && offset / 2 > 0xFFFF)
            err = TC_ERR_DECODE;
    }
    if (!err)
        err = index_format ? put32(&o->loca, offset) : put16(&o->loca, offset / 2);
    free(xs);
    free(ys);
    free(on);
    return err;
}

/* hmtx from its transformed form: side bearings from the glyphs' boxes. */
static int rebuild_hmtx(rd *in, uint32_t nglyphs, uint32_t nlong, const int16_t *xmin, tc_buf *out)
{
    uint32_t fl = rd8(in), i;
    const uint8_t *adv = rdn(in, 2 * nlong), *lsb = NULL, *mono = NULL;
    int err = TC_OK;
    if (!(fl & 1))
        lsb = rdn(in, 2 * nlong);
    if (!(fl & 2))
        mono = rdn(in, 2 * (nglyphs - nlong));
    if (in->bad || (fl & 0xFC) || !xmin || nlong > nglyphs)
        return TC_ERR_DECODE;
    for (i = 0; i < nlong && !err; i++) {
        err = put16(out, u16(adv + 2 * i));
        err = err ? err : put16(out, lsb ? u16(lsb + 2 * i) : (uint32_t)xmin[i] & 0xFFFF);
    }
    for (i = nlong; i < nglyphs && !err; i++)
        err = put16(out, mono ? u16(mono + 2 * (i - nlong)) : (uint32_t)xmin[i] & 0xFFFF);
    return err;
}

static int decode_woff2(const uint8_t *d, size_t len, tc_buf *out)
{
    struct entry {
        uint32_t tag, orig, xlen;   /* xlen: its length in the stream */
        int transformed;
    } e[MAX_TABLES];
    out_table t[MAX_TABLES];
    tc_buf stream;
    rd r;
    uint32_t flavor, n, i, compressed, at;
    uint64_t total = 0;
    int glyf = -1, loca = -1, hmtx = -1, maxp = -1, hhea = -1, err = TC_OK;
    glyf_out g;

    if (len < 48)
        return TC_ERR_DECODE;
    flavor = u32(d + 4);
    n = u16(d + 12);
    compressed = u32(d + 20);
    if (flavor == TAG("ttcf") || !n || n > MAX_TABLES)
        return TC_ERR_DECODE;
    r.p = d + 48;
    r.end = d + len;
    r.bad = 0;
    for (i = 0; i < n; i++) {
        uint32_t fl = rd8(&r), version = fl >> 6;
        e[i].tag = (fl & 63) == 63 ? rd32(&r) : (fl & 63) * 4 < sizeof known_tags - 1 ? TAG(known_tags + (fl & 63) * 4)
                                                                                     : 0;
        e[i].orig = base128(&r);
        /* glyf and loca are transformed unless version 3; others only if not 0. */
        if (e[i].tag == TAG("glyf") || e[i].tag == TAG("loca"))
            e[i].transformed = version != 3;
        else
            e[i].transformed = version != 0;
        e[i].xlen = e[i].transformed ? base128(&r) : e[i].orig;
        if (r.bad || !e[i].tag || (e[i].transformed && e[i].tag != TAG("glyf") && e[i].tag != TAG("loca") &&
                                   !(e[i].tag == TAG("hmtx") && version == 1)))
            return TC_ERR_DECODE;
        total += e[i].xlen;
        if (e[i].tag == TAG("glyf"))
            glyf = (int)i;
        else if (e[i].tag == TAG("loca"))
            loca = (int)i;
        else if (e[i].tag == TAG("hmtx"))
            hmtx = (int)i;
        else if (e[i].tag == TAG("maxp"))
            maxp = (int)i;
        else if (e[i].tag == TAG("hhea"))
            hhea = (int)i;
    }
    if (total > MAX_FONT)
        return TC_ERR_TOO_LARGE;
    if (!total || (glyf >= 0) != (loca >= 0) || (glyf >= 0 && e[glyf].transformed != e[loca].transformed) ||
        compressed > (size_t)(r.end - r.p))
        return TC_ERR_DECODE;
    /* All the tables, compressed as one. */
    tc_buf_init(&stream);
    err = tc_inflate(TC_CODING_BR, r.p, compressed, (size_t)total, &stream);
    if (!err && stream.len != total)
        err = TC_ERR_DECODE;
    memset(t, 0, sizeof t);
    memset(&g, 0, sizeof g);
    tc_buf_init(&g.glyf);
    tc_buf_init(&g.loca);
    for (i = 0, at = 0; i < n && !err; i++) {
        t[i].tag = e[i].tag;
        t[i].data = (const uint8_t *)stream.data + at;
        t[i].len = e[i].xlen;
        at += e[i].xlen;
    }
    if (!err && glyf >= 0 && e[glyf].transformed) {
        rd gr;
        if (maxp < 0 || t[maxp].len < 6 || e[loca].xlen != 0) {
            err = TC_ERR_DECODE;
        } else {
            gr.p = t[glyf].data;
            gr.end = gr.p + t[glyf].len;
            gr.bad = 0;
            err = rebuild_glyf(&gr, u16(t[maxp].data + 4), &g);
            t[glyf].data = (const uint8_t *)g.glyf.data;
            t[glyf].len = (uint32_t)g.glyf.len;
            t[loca].data = (const uint8_t *)g.loca.data;
            t[loca].len = (uint32_t)g.loca.len;
        }
    }
    if (!err && hmtx >= 0 && e[hmtx].transformed) {
        rd hr;
        if (glyf < 0 || !e[glyf].transformed || hhea < 0 || t[hhea].len < 36) {
            err = TC_ERR_DECODE;
        } else {
            hr.p = t[hmtx].data;
            hr.end = hr.p + t[hmtx].len;
            hr.bad = 0;
            tc_buf_init(&t[hmtx].built);
            err = rebuild_hmtx(&hr, u16(t[maxp].data + 4), u16(t[hhea].data + 34), g.xmin, &t[hmtx].built);
            t[hmtx].data = NULL;
            t[hmtx].len = (uint32_t)t[hmtx].built.len;
        }
    }
    if (!err)
        err = write_sfnt(flavor, t, n, out);
    for (i = 0; i < n; i++)
        tc_buf_free(&t[i].built);
    tc_buf_free(&g.glyf);
    tc_buf_free(&g.loca);
    free(g.xmin);
    tc_buf_free(&stream);
    return err;
}

int tc_woff_decode(const void *data, size_t len, tc_buf *out)
{
    if (len >= 4 && memcmp(data, "wOFF", 4) == 0)
        return decode_woff(data, len, out);
    if (len >= 4 && memcmp(data, "wOF2", 4) == 0)
        return decode_woff2(data, len, out);
    return TC_ERR_DECODE;
}
