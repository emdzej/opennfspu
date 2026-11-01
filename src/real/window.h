/* EA real library: 2D windows (\real\cmn\createw.c and the window selection, clip and fill of the 2D
   library, 0x533cf0-0x5342c0, 0x535470, 0x537b70, 0x53d2e0-0x53d4f0, 0x561b00, 0x573e30-0x573e50).

   A window is a 0x40-byte header ('WIND') and its pixels, which follow as a shape record: the header ends
   with a 16-byte shape record (+0x30) whose pixels are the window's, so a window's shape can be drawn or
   made into a texture like any shape of an FSH file. The library draws into the current window, a copy of
   the selected window's first 0x30 bytes (0x5de620). A window may instead stand for one of the driver's
   (lock = a THRASH window id): selecting it locks the driver's surface. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct Window {
    uint32_t magic;      /* +0x00: 'WIND' (0x57494e44); 'WINk' once destroyed */
    int32_t width;       /* +0x04 */
    int32_t height;      /* +0x08 */
    uint8_t type;        /* +0x0c: the shape type of its pixels (0x7e 1555, 0x6d 4444, 0x7d 8888, ...) */
    uint8_t depth;       /* +0x0d: bits per pixel (15 for 1555) */
    uint8_t bpp;         /* +0x0e: bits per pixel as stored (15 -> 16) */
    uint8_t shift;       /* +0x0f: log2 of the bytes per pixel */
    int8_t lock;         /* +0x10: 0 a memory window; else the driver's window to lock (-1 a locked copy) */
    uint8_t pad11[3];
    int32_t clip[4];     /* +0x14: x0, y0, x1, y1 */
    int32_t stride;      /* +0x24: bytes per row */
    uint8_t *shape;      /* +0x28: the shape record (rec, or an external shape's) */
    uint8_t *pixels;     /* +0x2c */
    uint8_t rec[16];     /* +0x30: the shape record; the pixels follow (+0x40) */
} Window;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(Window) == 0x40, "the window header of createw.c");
#endif

/* The current window (0x5de620): the selected window's header up to the record. */
extern Window WINDOW_cur;
/* The display's window (0x5de650): it stands for the driver's front buffer (lock 1). */
extern Window WINDOW_display;
/* The back buffer's window (0x5de680, lock 2). */
extern Window WINDOW_back;
int WINDOW_selectdisplay(void);   /* 0x5342a0 */
int WINDOW_selectback(void);      /* 0x5342b0 */
/* The display's window locked once after the mode is set: the display's size and depth, the default
   depth of new windows (the setup after setvideomode, GLIDE_env 0x556910 and the EAGL device's 0x5388a0). */
void WINDOW_displayinit(void);
/* The default depth of new windows (0x5dead0: the display's), 0 = 16 bits. */
extern int WINDOW_defaultdepth;

/* A window of width x height (at most 0x4000 x 0x2000) at a depth: bits (1, 4, 8, 15, 16, 24, 32) or a
   DirectDraw-style mask code (0x613 1555, 0x115c 4444, ...; 0 = the default). The header is cleared and
   the clip is the whole window; the pixels aren't cleared. NULL (after REAL_abort when abort) if the size
   or the depth is bad or there's no memory. WINDOW_create 0x535470 (\real\cmn\createw.c) */
Window *WINDOW_create(int width, int height, int depth, unsigned memflags, int abort);
/* WINDOW_create with the aborts of the release build (0x5deb88 = 0: none). 0x5356c0 */
Window *WINDOW_new(int width, int height, int depth, unsigned memflags);
/* A window over a shape's own pixels (no copy; the window owns only its header). 0x534310 */
Window *WINDOW_fromshape(uint8_t *shape, unsigned memflags);
/* Marks the window destroyed and frees it (deselected first if it's current). 0x533cf0 */
void WINDOW_destroy(Window *w);

/* Makes w the current window (NULL: none); a driver window is locked first. 0 if it couldn't be locked
   (then an 8x8 stand-in is current). 0x5341f0 */
int WINDOW_select(Window *w);
void WINDOW_deselect(void);                     /* 0x5342c0 */
/* Byte offset of pixel (x, y) in the current window. 0x573e30 */
int WINDOW_offset(int x, int y);
/* The current window's clip rectangle, clamped to it (an empty window: to the display, 0x5deac8). 0x573e50 */
void WINDOW_setclip(int x0, int y0, int x1, int y1);
void WINDOW_clipall(void);                      /* 0x53d4f0: the whole window */
/* Fills the current window's clip rectangle with a colour in its pixel format. 0x537b70 */
void WINDOW_clear(uint32_t colour);
/* Fills a rectangle of the current window. 0x561b00 */
void WINDOW_fillrect(int x, int y, int w, int h, uint32_t colour);
/* Draws a shape at (x, y) into the current window, converted to its type and clipped. 0x53d2e0 */
void WINDOW_drawshape(const uint8_t *shape, int x, int y);
/* A shape scaled to w x h at (x, y) on the current window (a negative size mirrors). 0x5628c0 */
void WINDOW_drawscaled(const uint8_t *shape, int x, int y, int w, int h);
