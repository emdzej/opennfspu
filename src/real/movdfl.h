/* EA real library: the 2D blitter (\real\pc\movdfl.c 0x5713e0). movdfl draws a shape into the current
   window (real/window.h) at a position: clipped to the window's clip rectangle, converted to the window's
   type when they differ, row by row through a kernel of a blitter table. The tables come in five variants
   by CPU (0x5decdc: plain, two MMX flavours, ...); the kernels of each produce the same bytes, and the
   port has the plain ones. */
#pragma once
#include <stdint.h>

/* A row kernel: n pixels from src to dst (fill kernels ignore src). */
typedef void (*BlitKernel)(void *dst, const void *src, int n);

/* A blitter table (8 words): the kernels for 8, 15, 16, 24 and 32-bit windows, a kernel that replaces
   them (with no conversion), how a type conversion is done (0 a whole-shape copy with its palette, 1
   without, 2 a row kernel), and a type to convert to instead of the window's (0: the window's). */
typedef struct {
    BlitKernel kernel[5];   /* +0x00 */
    BlitKernel forced;      /* +0x14 */
    int32_t conversion;     /* +0x18 */
    uint32_t target;        /* +0x1c */
} BlitTable;

extern const BlitTable MOVDFL_copy;   /* 0x5dec38: plain copies (0x5b0000, 0x53d100, 0x53d120, 0x53d140) */
extern const BlitTable MOVDFL_fill;   /* 0x5df880: fills with MOVDFL_fillcolour (0x561980 ...) */
extern uint32_t MOVDFL_fillcolour;    /* 0x6a5b00: the replicated fill colour */

void movdfl(const uint8_t *shape, int x, int y, const BlitTable *table);   /* 0x5713e0 */
