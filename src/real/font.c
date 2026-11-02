/* EA real library: fonts (see font.h). */
#include "real/font.h"
#include "exe.h"
#include "pending.h"
#include "real/depthconv.h"
#include "real/set3d.h"
#include "real/real.h"
#include "real/window.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

#define K(va) exe_f32(va)

/* n bytes little-endian (0x56e058). */
static uint32_t rdn(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--) v = v << 8 | p[i];
    return v;
}

/* ---- the strings' codings (+0x30) ---- */

static uint32_t next16(const uint8_t **s)   /* 0x573840 */
{
    uint32_t c = rdn(*s, 2);
    *s += 2;
    return c;
}

static uint32_t next8(const uint8_t **s)   /* 0x573860 */
{
    return *(*s)++;
}

/* Two bytes (high first) from a lead byte of 0x80 or more; else the byte, those from 0x20 to 0x7f through
   the table at 0x5dfc88. 0x573890 (0x573870) */
static uint32_t nextmb(const uint8_t **s)
{
    const uint8_t *p = *s;
    if (p[0] & 0x80) {
        *s = p + 2;
        return (uint32_t)p[0] << 8 | p[1];
    }
    uint32_t c = p[0];
    if (c > 0x1f && c < 0x80) c = exe_u32(0x5dfc88 + 2 * c) & 0xffff;
    *s = p + 1;
    return c;
}

/* ---- the glyphs ---- */

const uint8_t *FONT_glyph(const Font *f, uint32_t c)
{
    int n = f->nglyphs;
    const uint8_t *g = f->glyphs;
    if ((int)c - 0x20 < n) {
        const uint8_t *d = g + ((int)c - 0x20) * 11;
        if (rdn(d, 2) == c) return d;
    }
    /* A halving search (0x53d6e0). */
    while (n != 0) {
        const uint8_t *m = g + (n >> 1) * 11;
        uint32_t mc = rdn(m, 2);
        if (c == mc) return m;
        if (c != mc && -1 < (int)(c - mc)) {
            g = m + 11;
            n--;
        }
        n >>= 1;
    }
    return NULL;
}

/* The other case of a Latin-1 letter, 0 for none. 0x53dfe0 */
static uint32_t other_case(uint32_t c)
{
    if (c > 0x40 && c < 0x5b) return c + 0x20;
    if (c > 0x60 && c < 0x7b) return c - 0x20;
    if (c > 0xbf && c < 0xd7) return c + 0x20;
    if (c > 0xd7 && c < 0xdf) return c + 0x20;
    if (c > 0xdf && c < 0xf7) return c - 0x20;
    if (c > 0xf7 && c < 0xff) return c - 0x20;
    return 0;
}

/* The kerning of the pair (prev, c), from the sorted 8-byte table (by its second character, then its
   first). 0x53df90 */
static int kerning(const Font *f, uint32_t c, uint32_t prev)
{
    int lo = 0, hi = f->nkern - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const uint8_t *e = f->kern + 8 * mid;
        int d = (int)rdn(e + 2, 2) != (int)c ? (int)rdn(e + 2, 2) - (int)c : (int)rdn(e, 2) - (int)prev;
        if (d == 0) return (int8_t)e[4];
        if (d < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

/* ---- the states ---- */

void FONT_setstatef(Font *f, int state, float v)
{
    switch (state) {
    case 1: f->slant = v; break;
    case 2: f->sx = v; break;
    case 3: f->sy = v; break;
    case 4: f->lineheight = (int32_t)lrintf(v / f->sy); break;
    case 0x12: f->du = v; break;
    case 0x13: f->dv = v; break;
    case 0x14: f->dx = v; break;
    case 0x15: f->dy = v; break;
    case 0x16: f->z = v; break;
    case 0x17: f->rhw = v; break;
    default: break;
    }
}

void FONT_setstate(Font *f, int state, uint32_t v)
{
    float fix = (float)v * K(0x5b253c);   /* 16.16 */
    switch (state) {
    case 0:
        FONT_setstatef(f, 1, 0);
        FONT_setstatef(f, 2, 1.0f);
        FONT_setstatef(f, 3, 1.0f);
        break;
    case 1: case 2: case 3: case 4: FONT_setstatef(f, state, fix); break;
    case 5: f->flags = v == 1 ? f->flags | 1 : f->flags & ~1u; break;
    case 6: f->flags = v == 1 ? f->flags | 2 : f->flags & ~2u; break;
    case 7: f->flags = v == 1 ? f->flags | 4 : f->flags & ~4u; break;
    case 8: case 9: case 10: case 11:
        if (f->colour[state - 8] != v) {
            f->colour[state - 8] = v;
            f->dirty = 1;
        }
        break;
    case 0xc: f->corner[0] = f->corner[1] = f->corner[2] = f->corner[3] = v; break;
    case 0xd: case 0xe: case 0xf: case 0x10: f->corner[state - 0xd] = v; break;
    default: break;
    }
}

float FONT_getstatef(const Font *f, int state)
{
    switch (state) {
    case 1: return f->slant;
    case 2: return f->sx;
    case 3: return f->sy;
    case 4: return (float)f->lineheight * f->sy;
    case 0x12: return f->du;
    case 0x13: return f->dv;
    case 0x14: return f->dx;
    case 0x15: return f->dy;
    case 0x16: return f->z;
    case 0x17: return f->rhw;
    default: return 0;
    }
}

uint32_t FONT_getstate(const Font *f, int state)
{
    uint32_t ff = rdn(f->file + 0xc, 4);
    switch (state) {
    case 1: case 2: case 3: case 4: return (uint32_t)lrintf(FONT_getstatef(f, state) * K(0x5b3e68));
    case 5: return f->flags & 1;
    case 6: return f->flags >> 1 & 1;
    case 7: return f->flags >> 2 & 1;
    case 8: case 9: case 10: case 11: return f->colour[state - 8];
    case 0xc: return f->corner[0];
    case 0xd: case 0xe: case 0xf: case 0x10: return f->corner[state - 0xd];
    case 0x11: return (ff & 2) ? 3 : (ff & 4) ? 4 : 2;   /* the colours the palette uses */
    case 0x18: return ff >> 16 & 3;
    default: return 0;
    }
}

void FONT_setclip(Font *f, float x, float y, float w, float h)
{
    f->clip[0] = x;
    f->clip[2] = y;
    f->clip[1] = x + w;
    f->clip[3] = y + h;
    FONT_setstate(f, 6, 1);
}

/* ---- the palettes (0x594730) ---- */

/* The colours a and b mixed wa : wb, each channel at most 255. 0x5947e0 */
static uint32_t mix(uint32_t a, int wa, uint32_t b, int wb)
{
    int t = wa + wb;
    uint32_t out = 0;
    for (int sh = 24; sh >= 0; sh -= 8) {
        int v = (int)(((b >> sh & 0xff) * (uint32_t)wb + (a >> sh & 0xff) * (uint32_t)wa)) / t;
        if (v > 255) v = 255;
        if (v < 0) v = 0;
        out = out << 8 | (uint32_t)v;
    }
    return out;
}

static void make_palette(Font *f)
{
    uint32_t ff = rdn(f->file + 0xc, 4), *p = f->palette, c = f->colour[0], bg = f->colour[1];
    if (ff & 2) {
        /* Background, outline, then the face (0x594900). */
        uint32_t o = f->colour[2];
        if (!(bg & 0xff000000u)) bg = 0;
        p[0] = bg;
        p[1] = o;
        p[2] = mix(o, 3, bg, 1);
        p[3] = mix(o, 2, bg, 2);
        p[4] = mix(o, 1, bg, 3);
        p[5] = mix(bg, 4, c, 1);
        p[6] = mix(bg, 3, c, 2);
        p[7] = mix(bg, 2, c, 3);
        p[8] = mix(bg, 1, c, 4);
        p[9] = c;
        p[10] = mix(c, 4, o, 1);
        p[11] = mix(c, 3, o, 2);
        p[12] = mix(c, 2, o, 3);
        p[13] = mix(c, 1, o, 4);
        p[14] = p[15] = 0;
    } else if (ff & 4) {
        /* Background to shadow, shadow to face (0x5949e0). */
        uint32_t sh = f->colour[3];
        if (!(bg & 0xff000000u)) bg = sh & 0xffffff;
        for (int i = 0; i < 8; i++) p[i] = mix(bg, 8 - i, sh, i);
        for (int i = 0; i < 8; i++) p[8 + i] = mix(sh, 7 - i, c, i);
    } else {
        /* Background to face in sixteen steps (0x594790). */
        if (!(bg & 0xff000000u)) bg = c & 0xffffff;
        for (int i = 0; i < 16; i++) p[i] = mix(c, i, bg, 15 - i);
    }
}

/* ---- the DTEX font driver (0x5dfb60) ---- */

/* The glyph image as square DTEX pages (0x9c bytes, 0x571940): the page size (32 doubled up to the
   width) and up to ten pages down the image. */
struct FontDtex {
    int32_t width, height;       /* +0x00 */
    uint32_t format;             /* +0x08: 1 (4-bit) or 6 (8-bit) */
    int32_t bits;                /* +0x0c */
    int32_t size;                /* +0x10 */
    int32_t npages;              /* +0x14 */
    struct { int32_t y, h; } page[10];   /* +0x18 */
    DtexObject *tex[10];         /* +0x68 */
    int32_t generation;          /* +0x90: DTEX_destroycount when made */
};

static FontDtex *g_current;   /* 0x6a6374 */

static void dtex_pages(FontDtex *t)   /* 0x571f70 */
{
    for (int i = 0; i < t->npages; i++) t->tex[i] = DTEX_new(t->size, t->page[i].h, t->format, 4, 0, exe_str(0x5c0778));   /* "FONT" */
    t->generation = DTEX_destroycount;
}

static FontDtex *dtex_create(int w, int h, uint32_t type)   /* 0x571940 */
{
    FontDtex *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->width = w;
    t->height = h;
    t->size = 0x20;
    while (t->size < w) t->size *= 2;   /* 0x571870 */
    for (int y = 0; t->npages < 10 && y < h; y += t->size) {
        t->page[t->npages].y = y;
        t->page[t->npages].h = t->size;
        t->npages++;
    }
    switch (type) {
    case 1: case 0x7a: t->format = 1; t->bits = 4; break;
    case 5: case 0x7d: t->format = 6; t->bits = 8; break;
    default:
        PENDING_NOTE(0x571940, "FONT - Invalid shape type, font data not supported by DTEX font driver");
        t->bits = 8;
    }
    dtex_pages(t);
    return t;
}

/* The image's rows into each page (4-bit pixels with their nibbles swapped), then the palette. 0x571ac0 */
static bool dtex_update(FontDtex *t, const uint8_t *pixels, const uint32_t *palette)
{
    if (t->generation != DTEX_destroycount) dtex_pages(t);
    int dst_row = t->size * t->bits / 8;
    int src_row = t->format == 1 ? (t->width + 1) / 2 : t->bits * t->width / 8;
    for (int i = 0; i < t->npages; i++) {
        uint8_t *buf = calloc((size_t)t->page[i].h, (size_t)dst_row);
        if (!buf) return false;
        const uint8_t *src = pixels + t->page[i].y * src_row;
        int rows = t->height - t->page[i].y;
        if (t->page[i].h <= rows) rows = t->page[i].h;
        for (int r = 0; r < rows; r++, src += src_row) {
            uint8_t *d = buf + r * dst_row;
            if (t->format == 1) {
                int n = (t->width < t->size ? t->width : t->size) / 2;   /* 0x571d10 */
                for (int k = 0; k < n; k++) d[k] = (uint8_t)(src[k] >> 4 | src[k] << 4);
            } else {
                memcpy(d, src, (size_t)(src_row < dst_row ? src_row : dst_row));
            }
        }
        DTEX_load(t->tex[i], buf, palette);
        free(buf);
    }
    return true;
}

static void dtex_settexture(FontDtex *t)   /* 0x572040 */
{
    if (t->generation != DTEX_destroycount) dtex_pages(t);
    g_current = t;
}

/* The quads by page: their texture coordinates into the page's (in place), each page's quads drawn in
   one call. 0x572070 */
static void dtex_draw(int nquads, float *v, int *idx)
{
    FontDtex *t = g_current;
    if (!t || nquads <= 0) return;
    float inv = 1.0f / (float)t->size;
    if (t->npages == 1) {
        for (int q = 0; q < nquads; q++) {
            float *p = v + 32 * q;
            for (int k = 0; k < 4; k++) {
                idx[4 * q + k] = 4 * q + k;
                p[8 * k + 6] *= inv;
                p[8 * k + 7] *= inv;
            }
        }
        DTEX_settexture((uintptr_t)t->tex[0]);
        THRASH.fn.drawquadmesh(nquads, v, idx);
        return;
    }
    int *pg = malloc(sizeof *pg * (size_t)nquads);
    if (!pg) return;
    for (int q = 0; q < nquads; q++) {   /* 0x5718e0: the page holding the quad's first v */
        pg[q] = -1;
        float y = v[32 * q + 7];
        for (int i = 0; i < t->npages; i++)
            if ((float)t->page[i].y <= y && y < (float)(t->page[i].y + t->page[i].h)) {
                pg[q] = i;
                break;
            }
    }
    for (;;) {
        int page = -1, n = 0;
        float pinv = 0, py = 0;
        for (int q = 0; q < nquads; q++) {
            if (pg[q] == -1) continue;
            if (page == -1) {
                page = pg[q];
                pinv = 1.0f / (float)t->page[page].h;
                py = (float)t->page[page].y;
            } else if (pg[q] != page) {
                continue;
            }
            float *p = v + 32 * q;
            for (int k = 0; k < 4; k++) {
                idx[n + k] = 4 * q + k;
                p[8 * k + 6] *= inv;
                p[8 * k + 7] = (p[8 * k + 7] - py) * pinv;
            }
            n += 4;
            pg[q] = -1;
        }
        if (page == -1) break;
        DTEX_settexture((uintptr_t)t->tex[page]);
        THRASH.fn.drawquadmesh(n / 4, v, idx);
    }
    free(pg);
}

/* ---- the software font driver (0x5dec18) ---- */

/* The glyph image and its palette in the window's terms (0xd4 bytes, 0x53c880). */
struct FontSoft {
    int32_t width, height;   /* +0x00 */
    int depth;               /* +0x08: the current window's when the palette was made */
    const uint8_t *pixels;   /* +0x0c: the image's 4-bit pixels (the font's own) */
    uint8_t *table8;         /* +0x10: for 8-bit windows, each colour over each palette entry (0x53c930) */
    uint32_t colours[16];    /* +0x14: the palette */
    uint32_t pre[16];        /* +0x54: times its alpha (0x53cac0) */
    uint32_t p565[16];       /* +0x94: those as 565 under the alpha (0x53cb60) */
};

static FontSoft *g_soft;   /* 0x69eb98 */
static FontDriver g_driver = FONT_DRIVER_DTEX;   /* 0x5dec08 (FONT_init's) */

FontDriver FONT_setdriver(FontDriver d)
{
    FontDriver old = g_driver;
    g_driver = d;
    return old;
}

static FontSoft *soft_create(int w, int h)   /* 0x53c880 */
{
    FontSoft *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->width = w;
    t->height = h;
    return t;
}

/* 0x53c930: for each of the 16 premultiplied colours (b g r a) and each system palette entry, the entry
   when the colour's alpha is 0, else the nearest match (RGB_colourmatch) of the entry's channels times
   255 - alpha (/ 255) plus the colour's, each at most 255, with the colour's alpha: table[colour << 8 |
   entry]. */
static void soft_table8(const uint32_t *pre, uint8_t *table)
{
    for (int e = 0; e < 256; e++) {
        uint32_t pal = REAL_syspalette[e];
        int r = (int)(pal >> 16 & 0xff), g = (int)(pal >> 8 & 0xff), b = (int)(pal & 0xff);
        for (int k = 0; k < 16; k++) {
            uint32_t c = pre[k], a = c >> 24, v = (uint32_t)e;
            if (a) {
                int inv = 0xff - (int)a;
                int nr = inv * r / 0xff + (int)(c >> 16 & 0xff), ng = inv * g / 0xff + (int)(c >> 8 & 0xff),
                    nb = inv * b / 0xff + (int)(c & 0xff);
                if (nr > 0xff) nr = 0xff;
                if (ng > 0xff) ng = 0xff;
                if (nb > 0xff) nb = 0xff;
                v = RGB_colourmatch(((a << 8 | (uint32_t)nr) << 8 | (uint32_t)ng) << 8 | (uint32_t)nb);
            }
            table[k << 8 | e] = (uint8_t)v;
        }
    }
}

/* 0x53c8b0: the window's depth, the pixels, the palette and its two forms. */
static bool soft_update(FontSoft *t, const uint8_t *pixels, const uint32_t *palette)
{
    t->depth = WINDOW_cur.depth;
    t->pixels = pixels;
    memcpy(t->colours, palette, sizeof t->colours);
    for (int i = 0; i < 16; i++) {   /* 0x53cac0 */
        uint32_t c = palette[i], a = c >> 24;
        t->pre[i] = (((c >> 16 & 0xff) * a + 0x80) / 0xff | a << 8) << 8 | ((c >> 8 & 0xff) * a + 0x80) / 0xff;
        t->pre[i] = t->pre[i] << 8 | ((c & 0xff) * a + 0x80) / 0xff;
    }
    for (int i = 0; i < 16; i++) {   /* 0x53cb60 */
        uint32_t c = t->pre[i];
        t->p565[i] = (((c >> 16 & 0xff) * 0x1f + 0x80) / 0xff << 6 | ((c >> 8 & 0xff) * 0x3f + 0x80) / 0xff) << 5 |
                     ((c & 0xff) * 0x1f + 0x80) / 0xff | (c & 0xff000000u);
    }
    if (t->depth < 9) {
        if (!t->table8) t->table8 = malloc(0x1000);   /* (0x53d0d0) */
        if (t->table8) soft_table8(t->pre, t->table8);
    }
    return true;
}

/* The 2D library's pixel writers: a premultiplied colour over the pixel, nothing under alpha 0x10, the
   colour itself from 0xfc (0x573c70 565, 0x573d5f 1555), or over a 32-bit pixel (0x53c6c0). */
static uint16_t *put565(uint16_t *d, uint32_t c)
{
    if (c > 0x0fffffffu) {
        if (c < 0xfc000000u) {
            uint32_t dv = *d, e = ((dv << 16 | (dv & 0xff)) & 0xf800001fu) + (dv & 0x7e0) * 0x100;
            uint64_t l = (uint64_t)e * (~c >> 24);
            uint32_t lo = (uint32_t)l, hi = (uint32_t)(l >> 32);
            c = c + (hi & 0xf8) * 0x100 + (lo >> 16 & 0x7e0) + (lo >> 8 & 0x1f);
        }
        *d = (uint16_t)c;
    }
    return d + 1;
}

static uint16_t *put1555(uint16_t *d, uint32_t c)
{
    if (c > 0x0fffffffu) {
        if (c < 0xfc000000u) {
            uint32_t dv = *d;
            uint64_t l = (uint64_t)((dv & 0x7c00) * 0x20000u + (dv & 0x3e0) * 0x200u + (dv & 0x1f)) * ((c >> 24) ^ 0xff);
            uint32_t lo = (uint32_t)l;
            c = c + (uint32_t)(l >> 32) * 0x10000u + (lo >> 5 & 0xff) + (lo >> 0xb & 0xff00);
        }
        *d = (uint16_t)(((c & 0xf800) >> 6) + 0x8000 + ((c & 0xf80000) >> 9) + (c >> 3 & 0x1f));
    }
    return d + 1;
}

static uint32_t *put8888(uint32_t *d, uint32_t c)
{
    uint32_t o = *d, a = c >> 24;
    if (a == 0xff || o >> 24 == 0) {
        *d = c;
    } else if (a != 0) {
        uint32_t k = 0xff - a;
        *d = ((((o >> 8 & 0xff) * k) >> 8) + (c >> 8 & 0xff)) * 0x100 | ((((o >> 16 & 0xff) * k) >> 8) + (c >> 16 & 0xff)) * 0x10000 |
             ((((o & 0xff) * k) >> 8) + (c & 0xff)) | (0xff - ((0xff - (o >> 24)) * k) / 0xff) * 0x1000000u;
    }
    return d + 1;
}

static uint8_t *at(int x, int y)   /* 0x573e00 */
{
    return WINDOW_cur.pixels + WINDOW_cur.stride * y + ((WINDOW_cur.bpp * x) >> 3);
}

/* A glyph's w x h pixels from (u, v) of the image at (x, y) (0x53cd50): a glyph wholly inside the clip
   two pixels a byte through the depth's writer, every pixel (0 too, its colour's alpha deciding); else
   pixel by pixel, the zero ones left. */
static void soft_glyph(const FontSoft *t, int x, int y, int u, int v, int w, int h)
{
    int row = t->width / 2;
    const uint8_t *s = t->pixels + row * v + u / 2;
    int n = (w + 1) >> 1;
    if (n == 0 || h == 0) return;
    const int32_t *c = WINDOW_cur.clip;   /* x0 y0 x1 y1 */
    int depth = WINDOW_cur.depth;
    bool inside = c[0] <= x && x < c[2] && c[1] <= y && y < c[3] && c[0] <= x + w && x + w < c[2] && c[1] <= y + h && y + h < c[3];
    if (inside && (depth == 16 || depth == 15 || depth == 32)) {
        for (; h != 0; h--, y++, s += row - n) {
            uint8_t *d = at(x, y);
            for (int i = 0; i < n; i++, s++) {
                if (depth == 16) {
                    d = (uint8_t *)put565((uint16_t *)d, t->p565[*s >> 4]);
                    d = (uint8_t *)put565((uint16_t *)d, t->p565[*s & 0xf]);
                } else if (depth == 15) {
                    d = (uint8_t *)put1555((uint16_t *)d, t->pre[*s >> 4]);
                    d = (uint8_t *)put1555((uint16_t *)d, t->pre[*s & 0xf]);
                } else {   /* (0x5deba0 0: the premultiplied blend) */
                    d = (uint8_t *)put8888((uint32_t *)d, t->pre[*s >> 4]);
                    d = (uint8_t *)put8888((uint32_t *)d, t->pre[*s & 0xf]);
                }
            }
        }
        return;
    }
    if (inside && depth == 8) {   /* 0x573b60: each pixel through the table by its colour */
        if (!t->table8) return;
        uint8_t *d = at(x, y);
        for (; h != 0; h--, s += row - n, d += WINDOW_cur.stride - 2 * n)
            for (int i = 0; i < n; i++, s++, d += 2) {
                d[0] = t->table8[(*s >> 4) << 8 | d[0]];
                d[1] = t->table8[(*s & 0xf) << 8 | d[1]];
            }
        return;
    }
    /* (the pixels one by one through the 2D library's plot 0x5738d0, which blends by alpha in the
       window's format; the port puts each through the depth's writer) */
    for (; h > 0; h--, y++, s += row - n) {
        for (int i = 0; i < w; i += 2, s++) {
            for (int k = 0; k < 2; k++) {
                int nib = k ? *s & 0xf : *s >> 4, px = x + i + k;
                if (!nib || px < c[0] || px >= c[2] || y < c[1] || y >= c[3]) continue;
                if (depth == 16) put565((uint16_t *)at(px, y), t->p565[nib]);
                else if (depth == 15) put1555((uint16_t *)at(px, y), t->pre[nib]);
                else if (depth == 32) put8888((uint32_t *)at(px, y), t->pre[nib]);
            }
        }
    }
}

static void soft_settexture(FontSoft *t) { g_soft = t; }   /* 0x53cc30 */

/* FONT_drawtext 0x53cc40: each quad's glyph by its top left corner and its texture rectangle. */
static void soft_draw(int nquads, const float *v)
{
    if (!WINDOW_cur.pixels) {
        REAL_abort("\\real\\pc\\fontpc.c", 0x1c3,
                   "FONT_drawtext - Using software font driver to draw into a THRASH window.\nEither call "
                   "WINDOW_set, WINDOW_hidden, WINDOW_display, or install a THRASH font driver\n");
        return;
    }
    if (!g_soft) return;
    for (int q = 0; q < nquads; q++) {
        const float *p = v + 32 * q;
        soft_glyph(g_soft, (int)lrintf(p[0]), (int)lrintf(p[1]), (int)lrintf(p[6]), (int)lrintf(p[7]),
                   (int)lrintf(p[14] - p[6]), (int)lrintf(p[23] - p[7]));
    }
}

/* ---- the fonts ---- */

static bool g_batch;          /* 0x69eba8 */
static void *g_last;         /* 0x69ebac */
static int *g_index;          /* 0x69eba4 */
static int g_nindex;          /* 0x69eba0 */

static void index_room(int n)   /* 0x53d5b0, 0x53d610 */
{
    if (n <= g_nindex && g_index) return;
    if (n < 2 * g_nindex) n = 2 * g_nindex;
    if (n < 400) n = 400;
    free(g_index);
    g_index = malloc(sizeof *g_index * (size_t)n);
    g_nindex = g_index ? n : 0;
    for (int i = 0; i < g_nindex; i++) g_index[i] = i;
}

Font *FONT_open(const void *file)
{
    const uint8_t *d = file;
    Font *f = calloc(1, sizeof *f);
    if (!f) return NULL;
    f->f20 = 100;
    f->file = d;
    f->penx = (int8_t)d[0x10];
    f->peny = (int8_t)d[0x11];
    f->ascent = (int8_t)d[0x12];
    f->descent = (int8_t)d[0x13];
    f->lineheight = f->lineheight0 = f->ascent + f->descent;
    f->image = d + rdn(d + 0x1c, 4);
    f->glyphs = d + rdn(d + 0x14, 4);
    f->nglyphs = (int32_t)rdn(d + 0xa, 2);
    uint32_t coding = rdn(d + 0xc, 4) >> 16 & 3;
    if (coding == 1) f->next = next16;
    else if (coding != 2 && f->nglyphs <= 0xff && rdn(d + 0x20, 2) < 0x100) f->next = next8;
    else f->next = nextmb;
    FONT_setstate(f, 0, 0);
    FONT_setstate(f, 5, 0);
    FONT_setstate(f, 6, 0);
    FONT_setstate(f, 7, 0);
    FONT_setstate(f, 0xc, 0xffffffffu);
    FONT_setstate(f, 8, 0xffffffffu);
    FONT_setstate(f, 9, 0xffffff);
    FONT_setstate(f, 10, 0xff000000u);
    FONT_setstate(f, 11, 0xff000000u);
    for (int s = 0x12; s <= 0x16; s++) FONT_setstatef(f, s, 0);
    FONT_setstatef(f, 0x17, 1.0f);
    f->dirty = 1;
    /* The ascent from the first capital A .. E there is: its height and offset. */
    const uint8_t *g = NULL;
    for (uint32_t c = 'A'; c <= 'E' && !g; c++) g = FONT_glyph(f, c);
    if (g) {
        f->descent = (f->ascent - g[3]) - (int8_t)g[10];
        f->ascent = g[3] + (int8_t)g[10];
    }
    return f;
}

void FONT_close(Font *f)
{
    if (!f) return;
    free(f->soft);   /* 0x53cc00 */
    if (f->tex) {
        for (int i = 0; i < f->tex->npages; i++) DTEX_free(f->tex->tex[i]);
        free(f->tex);
    }
    free(f);
}

/* A glyph's quad (four vertices; none when clipping leaves nothing). 0x53da90 */
static int quad(float *v, const Font *f, float x, float y, float w, float h, float xo, float yo, int gu, int gv, int gw, int gh)
{
    for (int k = 0; k < 4; k++) {
        v[8 * k + 2] = f->z;
        v[8 * k + 3] = f->rhw;
        memcpy(&v[8 * k + 4], &f->corner[k], 4);
        v[8 * k + 5] = 0;
    }
    float a = (float)f->ascent * f->sy - yo;   /* the glyph's top above the baseline */
    float xt = f->slant * a + f->dx + x, xb = -f->slant * (h - a) + f->dx + x;
    float top = y + f->dy + yo, bottom = (y + f->dy) + h + yo;
    v[0] = xt + xo, v[1] = top;
    v[8] = xt + w + xo, v[9] = top;
    v[16] = xb + w + xo, v[17] = bottom;
    v[24] = xb + xo, v[25] = bottom;
    v[6] = v[30] = (float)gu + f->du;
    v[7] = v[15] = (float)gv + f->dv;
    v[14] = v[22] = (float)(gu + gw) + f->du;
    v[23] = v[31] = (float)(gv + gh) + f->dv;
    if (!(f->flags & 2)) return 4;
    const float *c = f->clip;   /* x0 x1 y0 y1 */
    if (v[16] <= c[1] && v[8] <= c[1] && c[2] <= v[1] && c[0] <= v[0] && c[0] <= v[24] && v[17] <= c[3]) return 4;
    if (!(c[0] < v[16] && c[0] < v[8] && v[1] < c[3] && v[0] < c[1] && v[24] < c[1] && c[2] < v[17])) return 0;
    /* Partly out: each corner into the rectangle, its texture coordinate along the edge with it
       (0x53dd00, corner by corner as the original). */
    float x0 = c[0], x1 = c[1], y0 = c[2], y1 = c[3];
    if (v[0] < x0) {
        v[6] = v[14] - (v[14] - v[6]) * ((v[8] - x0) / (v[8] - v[0]));
        v[0] = x0;
    } else if (x1 < v[0]) {
        v[0] = x1;
    }
    if (v[1] < y0) {
        v[7] = v[31] - (v[31] - v[7]) * ((v[25] - y0) / (v[25] - v[1]));
        v[1] = y0;
    } else if (y1 < v[1]) {
        v[1] = y1;
    }
    if (x1 < v[8]) {
        v[14] = (v[14] - v[6]) * ((x1 - v[0]) / (v[8] - v[0])) + v[6];
        v[8] = x1;
    } else if (v[8] < x0) {
        v[8] = x0;
    }
    if (v[9] < y0) {
        v[15] = v[23] - (v[23] - v[15]) * ((v[17] - y0) / (v[17] - v[9]));
        v[9] = y0;
    } else if (y1 < v[9]) {
        v[9] = y1;
    }
    if (x1 < v[16]) {
        v[22] = (v[22] - v[30]) * ((x1 - v[24]) / (v[16] - v[24])) + v[30];
        v[16] = x1;
    } else if (v[16] < x0) {
        v[16] = x0;
    }
    if (y1 < v[17]) {
        v[23] = (v[23] - v[15]) * ((y1 - v[9]) / (v[17] - v[9])) + v[15];
        v[17] = y1;
    } else if (v[17] < y0) {
        v[17] = y0;
    }
    if (v[24] < x0) {
        v[30] = v[22] - (v[22] - v[30]) * ((v[16] - x0) / (v[16] - v[24]));
        v[24] = x0;
    } else if (x1 < v[24]) {
        v[24] = x1;
    }
    if (y1 < v[25]) {
        float old = v[25];
        v[25] = y1;
        v[31] = (v[31] - v[7]) * ((y1 - v[1]) / (old - v[1])) + v[7];
    } else if (v[25] < y0) {
        v[25] = y0;
    }
    return 4;
}

int FONT_build(Font *f, float *out, float x, float y, const char *s, float *endx, float *minx)
{
    const uint8_t *p = (const uint8_t *)s;
    int count = 0;
    float start = (float)(int32_t)lrintf(x);
    float least = x + K(0x5b2428);
    float px = (float)f->penx * f->sx + x, py = (float)f->peny * f->sy + y;
    const uint8_t *sp = FONT_glyph(f, ' ');
    int space = sp ? (int8_t)sp[8] : 0;
    uint32_t c = 0;
    for (;;) {
        uint32_t prev = c;
        c = f->next(&p);
        if (c < 0x21 && !(c == 0x20 && (f->colour[1] & 0xff000000u))) {
            if (c == 0) {
                if (endx) *endx = px;
                if (minx) *minx = least;
                return count;
            }
            if (px <= least) least = px;
            if (c == 10) {
                px = start;
                py = (float)f->lineheight * f->sy + py;
            } else if (f->tabs && c == 9) {
                for (int i = 0; i < f->ntabs; i++)
                    if (px < (float)f->tabs[i]) {
                        px = (float)f->tabs[i];
                        break;
                    }
            } else {
                px = (float)space * f->sx + px;
            }
            continue;
        }
        const uint8_t *g = FONT_glyph(f, c);
        if (!g) {
            c = other_case(c);
            if (c) g = FONT_glyph(f, c);
        }
        if (!g) continue;
        int gu = (int)rdn(g + 4, 2), gv = (int)rdn(g + 6, 2);
        if ((f->flags & 4) && f->kern) px = (float)kerning(f, c, prev) * f->sx + px;
        int n = quad(out, f, px, py, (float)g[2] * f->sx, (float)g[3] * f->sy, (float)(int8_t)g[9] * f->sx,
                     (float)(int8_t)g[10] * f->sy, gu, gv, g[2], g[3]);
        out += 8 * n;
        count += n;
        px = (float)(int8_t)g[8] * f->sx + px;
    }
}

/* The characters of s. 0x53e050 */
static int length(const Font *f, const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    int n = 0;
    while (f->next(&p)) n++;
    return n;
}

/* 0x5717c0: the font's DTEX texture made (the image's size and type) if it has none, then its pixels
   in the font's palette; whether that worked. */
static int font_texture(Font *f)
{
    if (!f->tex) {
        const uint8_t *im = f->image;
        f->driver = FONT_DRIVER_DTEX;
        f->tex = dtex_create((int16_t)rdn(im + 4, 2), (int16_t)rdn(im + 6, 2), im[0]);
        f->dirty = 1;
        if (!f->tex) return 0;
    }
    make_palette(f);   /* 0x594730 */
    int ok = dtex_update(f->tex, f->image + 0x10, f->palette);
    f->dirty = !ok;
    return ok;
}

/* The system font (0x69eb90): the FNTF in the executable at 0x5dfd8c (a 4-bit 128 x 91 glyph image),
   opened once with the index list for 400 glyphs (0x53c650). The debug frame-rate text draws
   with it (0x569cf0). */
static Font *g_sysfont;      /* 0x69eb90 */

Font *FONT_sysfont(void)
{
    if (!g_sysfont) {
        index_room(0);   /* 0x53d5b0 */
        const uint8_t *h = exe_data(0x5dfd8c, 0x20);
        if (!h) return NULL;
        const uint8_t *im = h + rdn(h + 0x1c, 4);
        int bits = im[0] == 0x7a ? 4 : 8;
        size_t len = (size_t)rdn(h + 0x1c, 4) + 0x10 +
                     (size_t)((int16_t)rdn(im + 4, 2) * bits + 7) / 8 * (size_t)(int16_t)rdn(im + 6, 2);
        g_sysfont = FONT_open(exe_data(0x5dfd8c, len));
    }
    return g_sysfont;
}

/* FONT_init 0x5365e0, once: the DTEX driver current (0x53c640, 0x5dfb60), the system font and its texture.
   The device's texel-centre test (0x556a50) decides whether the font's text moves by half a pixel (states
   0x14, 0x15 = -0.5); the port's driver samples at D3D's centres, the test's no-offset answer. (The
   original also wraps the font with a texture-state record, 0x2c bytes, vtable 0x5bb708, freed with the
   device: nothing reads it.) */
void FONT_init(void)
{
    static bool done;   /* 0x69de40 */
    if (done) return;
    FONT_setdriver(FONT_DRIVER_DTEX);
    Font *f = FONT_sysfont();
    done = true;
    if (f) font_texture(f);
}

/* The quads with the font's texture: made or recoloured first. 0x53d7b0 (0x5717c0) */
static void draw_quads(Font *f, int nverts, float *v)
{
    if (f->soft || (!f->tex && g_driver == FONT_DRIVER_WINDOW)) {   /* (the software driver's texture) */
        const uint8_t *im = f->image;
        if (!f->soft) {
            f->driver = FONT_DRIVER_WINDOW;
            f->soft = soft_create((int16_t)rdn(im + 4, 2), (int16_t)rdn(im + 6, 2));
            f->dirty = 1;
            if (!f->soft) return;
        }
        if (f->dirty) {
            make_palette(f);   /* 0x594730 */
            f->dirty = !soft_update(f->soft, im + 0x10, f->palette);
        }
        if (!g_batch || g_last != (void *)f->soft) {
            soft_settexture(f->soft);
            g_last = (void *)f->soft;
        }
        soft_draw(nverts >> 2, v);
        return;
    }
    if (f->dirty || !f->tex) font_texture(f);
    if (f->tex && (!g_batch || g_last != (void *)f->tex)) {
        dtex_settexture(f->tex);
        g_last = f->tex;
    }
    dtex_draw(nverts >> 2, v, g_index);
}

void FONT_print(Font *f, float x, float y, const char *s)
{
    int n = length(f, s) * 4;
    index_room(n);
    float *v = malloc(sizeof *v * 8 * (size_t)(n ? n : 1));
    if (!v) return;
    int nv = FONT_build(f, v, x, y, s, NULL, NULL);
    if (nv > 0) draw_quads(f, nv, v);
    free(v);
}

FontMesh *FONT_makemesh(Font *f, float x, float y, const char *s)
{
    int n = length(f, s);
    index_room(n * 4);
    FontMesh *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->verts = malloc(sizeof *m->verts * 32 * (size_t)(n ? n : 1));
    if (!m->verts) {
        free(m);
        return NULL;
    }
    m->font = f;
    m->nverts = FONT_build(f, m->verts, x, y, s, &m->endx, &m->minx);
    m->x = x;
    m->y = y;
    m->z = f->z;
    return m;
}

void FONT_drawmesh(const FontMesh *m)
{
    if (!m) {
        PENDING_NOTE(0x53e230, "FONT_drawmesh - NULL mesh");
        return;
    }
    if (m->nverts <= 0) return;
    float *v = malloc(sizeof *v * 8 * (size_t)m->nverts);   /* the driver scales a copy */
    if (!v) return;
    memcpy(v, m->verts, sizeof *v * 8 * (size_t)m->nverts);
    draw_quads(m->font, m->nverts, v);
    free(v);
}

void FONT_freemesh(FontMesh *m)
{
    if (!m) return;
    free(m->verts);
    free(m);
}

void FONT_extent(Font *f, const char *s, float *x, float *y, float *w, float *h)
{
    float x0 = 10000.0f, y0 = 10000.0f, x1 = -10000.0f, y1 = -10000.0f;
    if (*s) {
        uint32_t clip = FONT_getstate(f, 6);
        FONT_setstate(f, 6, 0);
        FontMesh *m = FONT_makemesh(f, 0, 0, s);
        FONT_setstate(f, 6, clip);
        if (m) {
            for (int i = 0; i < m->nverts; i++) {
                const float *p = m->verts + 8 * i;
                if (p[0] <= x0) x0 = p[0];
                if (p[1] <= y0) y0 = p[1];
                if (x1 <= p[0]) x1 = p[0];
                if (y1 <= p[1]) y1 = p[1];
            }
            if (x1 <= m->endx) x1 = m->endx;
            if (m->minx <= x0) x0 = m->minx;
            FONT_freemesh(m);
        }
        if (x0 == K(0x5b2764)) x0 = 0;
    } else {
        x0 = 0;
    }
    if (y0 == K(0x5b2764)) y0 = 0;
    if (x1 == K(0x5b2760)) x1 = 0;
    if (y1 == K(0x5b2760)) y1 = 0;
    if (x) *x = x0;
    if (y) *y = y0;
    if (w) *w = x1 - x0;
    if (h) *h = y1 - y0;
}

void FONT_begin(void)
{
    g_last = NULL;
    g_batch = true;
}

void FONT_end(void) { g_batch = false; }

int FONT_pages(const Font *f) { return f && f->tex ? f->tex->npages : 0; }
