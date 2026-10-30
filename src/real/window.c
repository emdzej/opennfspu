/* 2D windows (see window.h). */
#include "real/window.h"
#include "real/depthconv.h"
#include "real/mem.h"
#include "real/movdfl.h"
#include "pending.h"
#include "real/real.h"
#include "real/set3d.h"
#include "real/shape.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

Window WINDOW_cur;                 /* 0x5de620 (0x30 bytes copied) */
int WINDOW_defaultdepth;           /* 0x5dead0 */
/* The display window's type and depth (0x5de65c, 0x5de65d: bytes +0xc, +0xd of the display's window at
   0x5de650), which the current window keeps when none is selected. Set by the display's setup (not
   ported: zero). */
static uint8_t display_type, display_depth;
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

/* Locks the driver's window a window stands for and fills its header from the surface. 0x5340d0 */
static int lock_window(Window *w)
{
    PENDING_NOTE(0x5340d0, "WINDOW_lock");
    (void)w;
    return 0;
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
        WINDOW_cur.type = display_type;
        WINDOW_cur.depth = display_depth;
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
