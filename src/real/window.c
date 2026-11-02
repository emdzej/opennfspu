/* 2D windows (see window.h). */
#include "real/window.h"
#include "real/depthconv.h"
#include "real/mem.h"
#include "real/movdfl.h"
#include "pending.h"
#include "platform.h"
#include "real/real.h"
#include "real/set3d.h"
#include "real/shape.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

Window WINDOW_cur;                 /* 0x5de620 (0x30 bytes copied) */
int WINDOW_defaultdepth;           /* 0x5dead0 */
/* The display's size (0x5deac8, 0x5deacc): the clip of a window without a size. */
static int display_w, display_h;
static ThrashSurface *locked;      /* 0x69dd18: the driver surface locked for the current window */
static Window *standin;            /* 0x69dd14: the 8x8 window current when a lock fails */

enum { HEADER = (int)(sizeof(Window)) };   /* 0x40 in the original */

/* log2 of the bytes per pixel as the original computes it: the exponent of (float)bits plus 126, kept
   in a byte (8 -> 0, 16 -> 1, 32 -> 2; 24 gives 1, 4 gives 0xff). */
static uint8_t shift_of(int bits)
{
    union { float f; uint32_t u; } c = {(float)bits};
    return (uint8_t)((int32_t)c.u >> 23) + 126;
}

Window *WINDOW_create(int width, int height, int depth, unsigned memflags, int abort)
{
    if (width < 0 || width > 0x4000 || height < 0 || height > 0x2000) {
        if (abort)
            REAL_abort("\\real\\cmn\\createw.c", 0x55,
                       "WINDOW_create - INVALID WINDOW DIMENSIONS\nrequested: %dx%dx%d maximum: %dx%d\n",
                       width, height, depth, 0x4000, 0x2000);
        return NULL;
    }
    if (depth == 0 && (depth = WINDOW_defaultdepth) == 0) depth = 16;
    uint8_t rec[4] = {(uint8_t)DEPTH_bpptotype(depth)};
    int bits = SHAPE_typevalid(rec), stored = bits == 15 ? 16 : bits;
    if (rec[0] == 0) {
        if (abort)
            REAL_abort("\\real\\cmn\\createw.c", 0x66,
                       "WINDOW_create - INVALID DEPTH\nrequested: %dx%dx%d BPP MUST BE 1,4,8,15,16,24,OR 32\n",
                       width, height, depth);
        return NULL;
    }
    /* Rows rounded up to whole bytes through a 64-bit division, as the original does. */
    int stride = (int)((((int64_t)stored * width + 7) & ~(int64_t)7) / stored) * stored >> 3;
    char name[32];
    snprintf(name, sizeof name, "%dx%dx%d", width, height, depth);
    Window *w = MEM_alloc(name, stride * height + HEADER, memflags);
    if (!w) {
        if (abort)
            REAL_abort("\\real\\cmn\\createw.c", 0x83,
                       "WINDOW_create - OUT OF MEMORY\nrequested: %dx%dx%d bytes: %d available: %d\n", width,
                       height, depth, stride * height + HEADER, 0);
        return NULL;
    }
    memset(w, 0, HEADER);
    w->rec[0] = rec[0];
    w->rec[4] = (uint8_t)width;
    w->rec[5] = (uint8_t)(width >> 8);
    w->rec[6] = (uint8_t)height;
    w->rec[7] = (uint8_t)(height >> 8);
    w->shape = w->rec;
    w->magic = 0x57494e44;   /* 'WIND' */
    w->type = rec[0];
    w->shift = shift_of(stored);
    w->depth = (uint8_t)bits;
    w->stride = stride;
    w->pixels = (uint8_t *)w + HEADER;
    w->width = width;
    w->height = height;
    w->lock = 0;
    w->bpp = (uint8_t)stored;
    w->clip[0] = w->clip[1] = 0;
    w->clip[2] = width;
    w->clip[3] = height;
    return w;
}

Window *WINDOW_new(int width, int height, int depth, unsigned memflags)
{
    return WINDOW_create(width, height, depth, memflags, 0);
}

/* The original allocates a whole window and shrinks the block to its 0x30-byte header (0x568ac0); the port
   allocates the header alone. The window's record and pixels are the shape's. */
Window *WINDOW_fromshape(uint8_t *shape, unsigned memflags)
{
    Window *full = WINDOW_new((int16_t)(shape[4] | shape[5] << 8), (int16_t)(shape[6] | shape[7] << 8),
                              SHAPE_typevalid(shape), memflags);
    if (!full) return NULL;
    Window *w = MEM_alloc("window", HEADER, memflags);
    if (w) memcpy(w, full, HEADER);
    MEM_purge(full);
    if (!w) return NULL;
    w->shape = shape;
    w->pixels = shape + 0x10;
    w->type = shape[0];
    return w;
}

/* The driver surface of the current window, unlocked. 0x5341a0 */
static void unlock_current(void)
{
    if (!locked) return;
    THRASH.fn.unlockwindow(locked);
    WINDOW_cur.pixels = NULL;
    locked = NULL;
    THRASH.fn.window(0);
}

/* The shape type of a THRASH texture format. THRASH_FormatToShapeType 0x5565e0 */
static uint8_t format_type(uint32_t format)
{
    switch (format & 0x7fffffffu) {
    case 1: return 0x7a;
    case 2: return 0x7b;
    case 3: case 0xb: return 0x7e;
    case 4: return 0x78;
    case 5: return 0x7f;
    case 6: return 0x7d;
    case 7: return 0x6d;
    case 0xc: return 0xf8;
    case 0xd: return 0xfd;
    }
    return 0;
}

/* Locks the driver's window a window stands for and fills its header from the surface. 0x5340d0 */
static int lock_window(Window *w)
{
    int8_t id = w->lock;
    THRASH.fn.window(id);
    locked = THRASH.fn.lockwindow();
    if (!locked) return 0;
    w->magic = 0x57494e44;   /* 'WIND' */
    w->width = locked->width;
    w->height = locked->height;
    w->type = format_type((uint32_t)locked->format);
    uint8_t bits = (uint8_t)SHAPE_typevalid(&w->type);
    w->depth = bits;
    w->bpp = bits == 15 ? 16 : bits;
    w->lock = id;
    w->clip[0] = w->clip[1] = 0;
    w->clip[2] = locked->width;
    w->shift = shift_of(w->bpp);
    w->clip[3] = locked->height;
    w->stride = locked->pitch;
    w->shape = NULL;
    w->pixels = locked->pixels;
    return 1;
}

/* The display's window (0x5de650): the driver's front buffer (lock 1), 640 x 480 until the mode's setup
   locks it. */
Window WINDOW_display = {.lock = 1, .clip = {0, 0, 640, 480}};
/* The back buffer's (0x5de680, lock 2). */
Window WINDOW_back = {.lock = 2, .clip = {0, 0, 640, 480}};

int WINDOW_selectdisplay(void) { return WINDOW_select(&WINDOW_display); }   /* 0x5342a0 */
int WINDOW_selectback(void) { return WINDOW_select(&WINDOW_back); }         /* 0x5342b0 */

void WINDOW_displayinit(void)
{
    int ok = WINDOW_select(&WINDOW_display);
    WINDOW_deselect();
    if (ok) {
        display_w = WINDOW_display.width;
        display_h = WINDOW_display.height;
        WINDOW_defaultdepth = WINDOW_display.depth;
    }
}

void WINDOW_destroy(Window *w)
{
    if (w->shape && w->shape == WINDOW_cur.shape) WINDOW_deselect();
    w->type = 0;
    w->depth = 0;
    w->magic = 0x57494e6b;   /* 'WINk' */
    MEM_purge(w);
}

int WINDOW_select(Window *w)
{
    int ok = 1;
    unlock_current();
    if (!w) {
        memset(&WINDOW_cur, 0, offsetof(Window, rec));
        WINDOW_cur.type = WINDOW_display.type;     /* (0x5de65c, 0x5de65d) */
        WINDOW_cur.depth = WINDOW_display.depth;
        return 1;
    }
    if (w->lock != 0 && w->lock != -1) ok = lock_window(w);
    if (ok) {
        memcpy(&WINDOW_cur, w, offsetof(Window, rec));
        return ok;
    }
    if (!standin) standin = WINDOW_new(8, 8, WINDOW_defaultdepth, 0);
    if (standin) memcpy(&WINDOW_cur, standin, offsetof(Window, rec));
    return 0;
}

void WINDOW_deselect(void) { WINDOW_select(NULL); }

int WINDOW_offset(int x, int y) { return (WINDOW_cur.bpp * x >> 3) + WINDOW_cur.stride * y; }

void WINDOW_setclip(int x0, int y0, int x1, int y1)
{
    int wmax = WINDOW_cur.width ? WINDOW_cur.width : display_w;
    int hmax = WINDOW_cur.height ? WINDOW_cur.height : display_h;
    if (x0 < 0) x0 = 0;
    if (x1 < 0) x1 = 0;
    if (x0 > wmax) x0 = wmax;
    if (x1 > wmax) x1 = wmax;
    if (y0 < 0) y0 = 0;
    if (y1 < 0) y1 = 0;
    if (y0 > hmax) y0 = hmax;
    if (y1 > hmax) y1 = hmax;
    WINDOW_cur.clip[0] = x0;
    WINDOW_cur.clip[1] = y0;
    WINDOW_cur.clip[2] = x1;
    WINDOW_cur.clip[3] = y1;
    /* The original keeps float copies of the clip for the 3D path (0x6a6358, 0x6a635c, 0x5dfa14,
       0x5dfa18); the port's 3D path is pending. */
}

void WINDOW_clipall(void) { WINDOW_setclip(0, 0, WINDOW_cur.width, WINDOW_cur.height); }

/* Whether the current window is drawn through the driver (a locked driver window without pixels). */
static bool through_driver(void)
{
    return WINDOW_cur.lock != 0 && WINDOW_cur.lock != -1 && WINDOW_cur.pixels == NULL;
}

void WINDOW_fillrect(int x, int y, int w, int h, uint32_t colour)
{
    if (through_driver()) {
        /* 0x561b00's driver path: an untextured quad (0x574920, 0x56d8a0). */
        PENDING_NOTE(0x56d8a0, "WINDOW_fillrect (driver window)");
        return;
    }
    switch (WINDOW_cur.depth) {
    case 4: colour = (colour & 0xf) | (colour & 0xf) << 4; break;
    case 8:
        colour = (colour & 0xff) | (colour & 0xff) << 8;
        colour |= colour << 16;
        break;
    case 15:
    case 16: colour = (colour & 0xffff) | colour << 16; break;
    default: break;
    }
    /* A shape record standing for the rectangle (its type the window's; its pixels never read). */
    uint8_t rec[16] = {WINDOW_cur.type, 0, 0, 0, (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)h, (uint8_t)(h >> 8)};
    MOVDFL_fillcolour = colour;
    movdfl(rec, x, y, &MOVDFL_fill);
}

void WINDOW_clear(uint32_t colour)
{
    WINDOW_fillrect(WINDOW_cur.clip[0], WINDOW_cur.clip[1], WINDOW_cur.clip[2] - WINDOW_cur.clip[0],
                    WINDOW_cur.clip[3] - WINDOW_cur.clip[1], colour);
}

void WINDOW_drawshape(const uint8_t *shape, int x, int y)
{
    if (through_driver()) {
        /* 0x53d2e0's driver path: the shape as a texture on a quad (0x572840, 0x5728f0). */
        PENDING_NOTE(0x5728f0, "WINDOW_drawshape (driver window)");
        return;
    }
    movdfl(shape, x, y, &MOVDFL_copy);
}

/* ---- the faded draw (0x53fe10) ---- */

static uint8_t s_fade[256];        /* 0x69ebb8: i * alpha / 256 */
static int s_fade_alpha = -1;      /* 0x69ecb8 */
static const uint32_t *s_fade_clut;   /* 0x69ebb4 */

static void fade_table(int alpha)   /* 0x53ffe0 */
{
    if (s_fade_alpha == alpha) return;
    for (int i = 0, v = 0; i < 256; i++, v += alpha) s_fade[i] = (uint8_t)((unsigned)v >> 8);
    s_fade_alpha = alpha;
}

static uint32_t faded(const uint8_t *p)   /* 0x53fc50: each byte of an 8888 pixel through the table */
{
    return (uint32_t)s_fade[p[0]] | (uint32_t)s_fade[p[1]] << 8 | (uint32_t)s_fade[p[2]] << 16 | (uint32_t)s_fade[p[3]] << 24;
}

/* An 8888 colour (its channels already times its alpha) over a 565 pixel: none under alpha 0x10, the
   colour itself from 0xfc, else the pixel's channels times 255 - alpha added in one product (the 565
   fields spread over a word, so a channel's overflow carries into the next). 0x573cda */
static void over565(uint16_t *d, uint32_t c)
{
    if (c < 0x10000000u) return;
    if (c < 0xfc000000u) {
        uint32_t px = *d;
        uint32_t packed = ((px << 16) & 0xf8000000u) | (px & 0x1f) | ((px & 0x7e0) << 8);
        uint64_t m = (uint64_t)packed * (~c >> 24 & 0xff);
        uint32_t lo = (uint32_t)m, hi = (uint32_t)(m >> 32);
        c += hi << 16;
        c += lo >> 5 & 0xff;
        c += lo >> 11 & 0xff00;
    }
    *d = (uint16_t)(((c & 0xf80000) >> 8) + (c >> 3 & 0x1f) + ((c & 0xfc00) >> 5));
}

/* The same over a 1555 pixel, which comes out opaque. 0x573d5f */
static void over1555(uint16_t *d, uint32_t c)
{
    if (c < 0x10000000u) return;
    if (c < 0xfc000000u) {
        uint32_t px = *d;
        uint32_t packed = ((px & 0x7c00) << 17) + ((px & 0x3e0) << 9) + (px & 0x1f);
        uint64_t m = (uint64_t)packed * ((c >> 24) ^ 0xff);
        uint32_t lo = (uint32_t)m, hi = (uint32_t)(m >> 32);
        c += hi << 16;
        c += lo >> 5 & 0xff;
        c += lo >> 11 & 0xff00;
    }
    *d = (uint16_t)(((c & 0xf80000) >> 9) + (c >> 3 & 0x1f) + ((c & 0xf800) >> 6) + 0x8000);
}

/* 0x53fb80: over an 8-bit pixel, its palette colour times 255 - alpha (/ 256) plus the faded colour's,
   each channel a byte that wraps, then the nearest palette entry. */
static void fade8(void *dst, const void *src, int n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (; n > 0; n--, s += 4, d++) {
        uint32_t f = faded(s), inv = (uint8_t)~(f >> 24), pal = RGB_pixelcolour(*d);
        uint8_t r = (uint8_t)(((pal >> 16 & 0xff) * inv >> 8) + (f >> 16 & 0xff));
        uint8_t g = (uint8_t)(((pal >> 8 & 0xff) * inv >> 8) + (f >> 8 & 0xff));
        uint8_t b = (uint8_t)(((pal & 0xff) * inv >> 8) + (f & 0xff));
        *d = (uint8_t)RGB_colourmatch((uint32_t)r << 16 | (uint32_t)g << 8 | b);
    }
}
static void fade1555(void *dst, const void *src, int n)   /* 0x53fc90 */
{
    uint16_t *d = dst;
    const uint8_t *s = src;
    for (; n > 0; n--, s += 4) over1555(d++, faded(s));
}
static void fade565(void *dst, const void *src, int n)   /* 0x53fcd0 */
{
    uint16_t *d = dst;
    const uint8_t *s = src;
    for (; n > 0; n--, s += 4) over565(d++, faded(s));
}
/* 24 and 32 bits: each channel times 255 - alpha (/ 256) plus the faded colour's, byte arithmetic; 32-bit
   pixels come out opaque. 0x53fd10, 0x53fd90 */
static void fade_bytes(uint8_t *d, uint32_t c)
{
    unsigned k = (unsigned)(uint8_t)(0xff - (c >> 24));
    d[2] = (uint8_t)((d[2] * k >> 8) + (c >> 16));
    d[1] = (uint8_t)((d[1] * k >> 8) + (c >> 8));
    d[0] = (uint8_t)((d[0] * k >> 8) + c);
}
static void fade24(void *dst, const void *src, int n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (; n > 0; n--, s += 4, d += 3) fade_bytes(d, faded(s));
}
static void fade32(void *dst, const void *src, int n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (; n > 0; n--, s += 4, d += 4) {
        uint32_t c = faded(s);
        d[3] = 0xff;
        fade_bytes(d, c);
    }
}
/* An 8-bit shape on a 16-bit window: its palette's 8888 colours (faded below 0xff) over each pixel.
   0x540060 */
static void fade_clut(void *dst, const void *src, int n)
{
    uint16_t *d = dst;
    const uint8_t *s = src;
    for (; n > 0; n--) over565(d++, s_fade_clut[*s++]);
}

void WINDOW_drawshapealpha(const uint8_t *shape, int x, int y, int alpha)
{
    if (WINDOW_cur.lock != 0 && WINDOW_cur.lock != -1) {
        if (!WINDOW_cur.pixels) {
            /* the driver's path: the shape as a texture on a quad with the alpha in its colour */
            PENDING_NOTE(0x5728f0, "WINDOW_drawshapealpha (driver window)");
        }
        if (WINDOW_cur.lock != -1 && !WINDOW_cur.pixels) return;
    }
    BlitTable t = {{fade8, fade1555, fade565, fade24, fade32}, NULL, 3, 0x7d};   /* 0x5ded08 */
    static uint32_t clut[256];
    if (SHAPE_typevalid(shape) == 8 && WINDOW_cur.depth == 16) {
        s_fade_clut = getclutptr(shape, clut);   /* 0x534440 */
        if (alpha < 0xff) {
            const uint8_t *c = (const uint8_t *)s_fade_clut;
            static uint32_t faded_clut[256];
            for (int i = 0; i < 256; i++) {   /* 0x540010: each byte times alpha / 256 */
                uint8_t b[4];
                for (int k = 0; k < 4; k++) b[k] = (uint8_t)((unsigned)c[4 * i + k] * (unsigned)alpha >> 8);
                memcpy(&faded_clut[i], b, 4);
            }
            s_fade_clut = faded_clut;
        }
        t.forced = fade_clut;
        t.target = 0x7b;
    } else {
        fade_table(alpha);
    }
    movdfl(shape, x, y, &t);
}

/* A shape scaled to w x h at (x, y) on the current window (negative sizes mirror), sampled at the pixel
   centres; an 8-bit shape on a 15- or 16-bit window through its palette (0x58af95), any other converted
   to the window's type first and copied (0x58ae13). The translucent row drawers of 0x5deb14 aren't
   ported (it is never set where the port draws). 0x5628c0 */
void WINDOW_drawscaled(const uint8_t *shape, int x, int y, int w, int h)
{
    if (!w || !h) return;
    int sw = (int16_t)(shape[4] | shape[5] << 8), sh = (int16_t)(shape[6] | shape[7] << 8);
    uint8_t *converted = NULL;
    uint16_t clut[256];
    bool paletted = false;
    if (shape[0] != WINDOW_cur.type) {
        if (SHAPE_typevalid(shape) == 8 && (WINDOW_cur.depth == 16 || WINDOW_cur.depth == 15)) {
            paletted = true;
            const uint16_t *c = getclut16ptr(shape, clut);
            if (c != clut) memcpy(clut, c, sizeof clut);
            if (WINDOW_cur.depth == 15) {   /* 565 -> 1555, black left clear (0x570050) */
                for (int i = 0; i < 256; i++)
                    clut[i] = clut[i] ? (uint16_t)((clut[i] >> 1 & 0xffe0) | (clut[i] & 0x1f) | 0x8000) : 0;
            }
        } else {
            converted = SHAPE_converttype(shape, WINDOW_cur.type, 0, 0);
            if (!converted) return;
            shape = converted;
        }
    }
    int x1 = WINDOW_cur.clip[2], y1 = WINDOW_cur.clip[3], cx0 = WINDOW_cur.clip[0], cy0 = WINDOW_cur.clip[1];
    if (WINDOW_cur.pixels && x < x1 && y < y1 && cx0 < (w < 1 ? -w : w) + x && cy0 < (h < 1 ? -h : h) + y) {
        int rowbytes = SHAPE_rowbytes(shape);
        int xstep = (sw << 16) / w, ystep = (sh << 16) / h;
        int yacc = ystep / 2, xacc = xstep / 2;
        if (h < 0) {
            h = -h;
            yacc = ystep / 2 + (sh << 16);
        }
        if (w < 0) {
            w = -w;
            xacc = xstep / 2 + (sw << 16);
        }
        if (y < cy0) {
            yacc += (cy0 - y) * ystep;
            h += y - cy0;
            y = cy0;
        }
        if (y1 < y + h) h = y1 - y;
        if (x < cx0) {
            xacc += (cx0 - x) * xstep;
            w += x - cx0;
            x = cx0;
        }
        if (x1 < x + w) w = x1 - x;
        const uint8_t *src = shape + 0x10;
        int bytes = WINDOW_cur.bpp >> 3;
        uint8_t *row = WINDOW_cur.pixels + WINDOW_offset(x, y);
        for (; h > 0; h--, row += WINDOW_cur.stride, yacc += ystep) {
            const uint8_t *s = src + (yacc >> 16) * rowbytes;
            for (int i = 0, xa = xacc; i < w; i++, xa += xstep) {
                int col = xa >> 16;
                if (paletted) {
                    uint16_t v = clut[s[col]];
                    memcpy(row + i * 2, &v, 2);
                } else {
                    memcpy(row + i * bytes, s + col * bytes, (size_t)bytes);
                }
            }
        }
    }
    if (converted) MEM_purge(converted);
}

/* ---- pixels read back and the TGA writer (0x570b00, 0x535230, 0x534010) ---- */

/* The bytes per pixel the driver paths use: 32 over the default depth (2 at 16 bits, but 1 at 32: the
   original's arithmetic). */
static int driver_bytes(void) { return WINDOW_defaultdepth ? 32 / WINDOW_defaultdepth : 0; }

uint32_t WINDOW_getpixel(int x, int y)
{
    const uint8_t *p = NULL;
    bool inside = x >= WINDOW_cur.clip[0] && x < WINDOW_cur.clip[2] && y >= WINDOW_cur.clip[1] && y < WINDOW_cur.clip[3];
    ThrashSurface *s = NULL;
    if (!through_driver()) {
        if (inside) p = WINDOW_cur.pixels + WINDOW_offset(x, y);   /* 0x573e00 */
    } else {
        s = THRASH.fn.lockwindow();
        if (s && inside) p = (const uint8_t *)s->pixels + driver_bytes() * x + s->pitch * y;
    }
    /* Outside the clip the original reads through y taken as an address; the port reads nothing. */
    uint32_t v = 0;
    if (p) switch (WINDOW_cur.depth) {
        case 4: v = x & 1 ? *p >> 4 : *p & 0xf; break;
        case 8: v = *p; break;
        case 15: case 16: v = (uint32_t)(p[0] | p[1] << 8); break;
        case 24: v = (uint32_t)(p[0] | p[1] << 8 | p[2] << 16); break;   /* 0x56e058 */
        case 32: v = (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); break;
        default: break;
        }
    /* The driver's surface is unlocked after the read (only when it was inside, as the original). */
    if (s && inside) THRASH.fn.unlockwindow(s);
    return v;
}

void WINDOW_readshape(uint8_t *shape)
{
    uint32_t pos = (uint32_t)(shape[0xc] | shape[0xd] << 8 | shape[0xe] << 16 | (uint32_t)shape[0xf] << 24);
    int x = (int32_t)(pos << 20) >> 20, y = (int32_t)(pos << 4) >> 20;
    if (!through_driver()) {
        movdfl(shape, x, y, &MOVDFL_read);
        pos = (pos & 0xf000f000u) ^ ((uint32_t)x & 0xfff) ^ ((uint32_t)y & 0xfff) << 16;   /* (0x535230) */
        shape[0xc] = (uint8_t)pos, shape[0xd] = (uint8_t)(pos >> 8), shape[0xe] = (uint8_t)(pos >> 16);
        shape[0xf] = (uint8_t)(pos >> 24);
        return;
    }
    if (x > display_w || x < 0 || y > display_h || y < 0) return;
    ThrashSurface *s = THRASH.fn.lockwindow();
    if (!s) return;   /* (the original reads through it regardless) */
    int sw = (int16_t)(shape[4] | shape[5] << 8), sh = (int16_t)(shape[6] | shape[7] << 8);
    int cols = display_w - x < sw ? display_w - x : sw;
    int end = display_h - y < sh ? display_h - y : sh;
    /* Rows y .. end - 1: the shape's height counted from the screen's top, not from y (the original's). */
    int off = 0;
    for (int row = y; row < end; row++) {
        int n = driver_bytes() * cols;
        memcpy(shape + 0x10 + off, (const uint8_t *)s->pixels + driver_bytes() * x + s->pitch * row, (size_t)n);
        off += n;
    }
    THRASH.fn.unlockwindow(s);
}

static void put_le(uint8_t *p, uint32_t v, int n)   /* 0x56e0b1 */
{
    for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> 8 * i);
}

/* The TGA of the current window's clip rectangle: its size (0x533d30) and the file (0x533d80): an
   uncompressed true-colour image (type 2; 8-bit windows a colour-mapped one, type 1, with the system
   palette), top-left first; a 16-bit (565) window's pixels go to 1555 with the top bit set but on black
   (0x570050). */
static int tga_size(void)
{
    int bits = (WINDOW_cur.depth + 7) & ~7;
    int n = (WINDOW_cur.clip[3] - WINDOW_cur.clip[1]) * (WINDOW_cur.clip[2] - WINDOW_cur.clip[0]) * bits / 8;
    return bits == 8 ? n + 0x312 : n + 0x12;
}

static void tga_build(uint8_t *b)
{
    int w = WINDOW_cur.clip[2] - WINDOW_cur.clip[0], h = WINDOW_cur.clip[3] - WINDOW_cur.clip[1];
    int bits = (WINDOW_cur.depth + 7) & ~7;
    bool pal = bits == 8;
    put_le(b + 0, 0, 1);
    put_le(b + 1, pal, 1);
    put_le(b + 2, pal ? 1 : 2, 1);
    put_le(b + 3, 0, 2);
    put_le(b + 5, pal ? 0x100 : 0, 2);
    put_le(b + 7, pal ? 0x18 : 0, 1);
    put_le(b + 8, (uint32_t)WINDOW_cur.clip[0], 2);
    put_le(b + 10, (uint32_t)(WINDOW_cur.height - WINDOW_cur.clip[3]), 2);
    put_le(b + 12, (uint32_t)w, 2);
    put_le(b + 14, (uint32_t)h, 2);
    put_le(b + 16, (uint32_t)bits, 1);
    put_le(b + 17, 0x20, 1);
    uint8_t *p = b + 0x12;
    if (pal)   /* 0x5709f0: the palette's colours as their bytes 0 .. 2 (blue, green, red) */
        for (int i = 0; i < 256; i++, p += 3) put_le(p, REAL_syspalette[i], 3);
    uint8_t *px = p;
    for (int y = WINDOW_cur.clip[1]; y < WINDOW_cur.clip[1] + h; y++)   /* 0x533f20 */
        for (int x = WINDOW_cur.clip[0]; x < WINDOW_cur.clip[0] + w; x++) {
            uint32_t v = WINDOW_getpixel(x, y);
            switch (bits) {
            case 8: *p++ = (uint8_t)v; break;
            case 16: put_le(p, v, 2), p += 2; break;
            case 24: put_le(p, v, 3), p += 3; break;
            case 32: put_le(p, v, 4), p += 4; break;
            default: break;
            }
        }
    if (WINDOW_cur.depth == 16)
        for (int i = 0; i < w * h; i++) {
            uint16_t c = (uint16_t)(px[2 * i] | px[2 * i + 1] << 8);
            if (c) c = (uint16_t)((c >> 1 & 0xffe0) | (c & 0x1f) | 0x8000);
            put_le(px + 2 * i, c, 2);
        }
}

/* 0x534010: the TGA written as a file with the windows put back afterwards (0x570c80, 0x570cb0). */
static int tga_save(const char *name)
{
    int size = tga_size();
    uint8_t *b = MEM_alloc("BFILE", size, 0);   /* (BFILE_alloc) */
    if (!b) return 0;
    tga_build(b);
    Window cur, display;
    memcpy(&cur, &WINDOW_cur, offsetof(Window, rec));
    memcpy(&display, &WINDOW_display, offsetof(Window, rec));
    WINDOW_deselect();
    int ok = plat_save_user_file(name, b, (size_t)size);   /* (0x568a20: FILE_save through FILESYS_atomic) */
    memcpy(&WINDOW_display, &display, offsetof(Window, rec));
    memcpy(&WINDOW_cur, &cur, offsetof(Window, rec));
    if (WINDOW_cur.lock != 0 && WINDOW_cur.pixels == NULL) THRASH.fn.window(WINDOW_cur.lock);
    else WINDOW_select(&cur);
    MEM_purge(b);
    return ok;
}

int WINDOW_savetga(int flags, const char *name)
{
    if ((flags & 1) == 1) return tga_save(name);
    if ((flags & 2) == 2) {
        /* The image built in memory and dropped (the original keeps no pointer to it). */
        uint8_t *b = MEM_alloc("BFILE", tga_size(), 0);
        if (b) {
            tga_build(b);
            MEM_purge(b);
            return 1;
        }
    }
    return 0;
}
