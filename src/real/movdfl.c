/* \real\pc\movdfl.c and its blitter tables: see movdfl.h. */
#include "real/movdfl.h"
#include "real/depthconv.h"
#include "real/mem.h"
#include "real/real.h"
#include "real/shape.h"
#include "real/window.h"
#include <string.h>

uint32_t MOVDFL_fillcolour;          /* 0x6a5b00 */

/* ---- copy kernels (table 0x5dec38) ---- */

/* 0x5b0000 (PACODE): n bytes, aligning the destination first; the same bytes as a memcpy. */
static void copy8(void *dst, const void *src, int n) { if (n > 0) memcpy(dst, src, (size_t)n); }
static void copy16(void *dst, const void *src, int n) { copy8(dst, src, n * 2); }   /* 0x53d100 */
static void copy24(void *dst, const void *src, int n) { copy8(dst, src, n * 3); }   /* 0x53d120 */
static void copy32(void *dst, const void *src, int n) { copy8(dst, src, n * 4); }   /* 0x53d140 */

const BlitTable MOVDFL_copy = {{copy8, copy16, copy16, copy24, copy32}, NULL, 2, 0};

/* ---- read-back kernels (table 0x5de5f0): the window's pixels copied into the shape ---- */

static void read8(void *dst, const void *src, int n) { copy8((void *)src, dst, n); }        /* 0x5351b0 */
static void read16(void *dst, const void *src, int n) { copy8((void *)src, dst, n * 2); }   /* 0x5351d0 */
static void read24(void *dst, const void *src, int n) { copy8((void *)src, dst, n * 3); }   /* 0x5351f0 */
static void read32(void *dst, const void *src, int n) { copy8((void *)src, dst, n * 4); }   /* 0x535210 */

const BlitTable MOVDFL_read = {{read8, read16, read16, read24, read32}, NULL, 0, 0};

/* ---- fill kernels (table 0x5df880) ---- */

/* 0x5b0980 (PACODE): n bytes of the 32-bit pattern, by address (a byte or halfword to align, then
   words, then the tail as a word, a halfword and a byte of the pattern's low end). */
static void fill_bytes(uint8_t *d, uint32_t c, int n)
{
    if ((uintptr_t)d & 1 && n > 0) { *d++ = (uint8_t)c; n--; }
    if ((uintptr_t)d & 2 && n > 1) { memcpy(d, &c, 2); d += 2; n -= 2; }
    while (n >= 4) { memcpy(d, &c, 4); d += 4; n -= 4; }
    if (n > 1) { memcpy(d, &c, 2); d += 2; n -= 2; }
    if (n > 0) *d = (uint8_t)c;
}
static void fill8(void *dst, const void *src, int n) { fill_bytes(dst, MOVDFL_fillcolour, n); }       /* 0x561980 */
static void fill16(void *dst, const void *src, int n) { fill_bytes(dst, MOVDFL_fillcolour, n * 2); }  /* 0x5619a0 */
static void fill32(void *dst, const void *src, int n) { fill_bytes(dst, MOVDFL_fillcolour, n * 4); }  /* 0x5619f0 */
/* 0x5619c0: reads each 24-bit pixel and writes it back unchanged (it never takes the fill colour), so a
   24-bit fill leaves the window as it was. */
static void fill24(void *dst, const void *src, int n)
{
    uint8_t *d = dst, p[3];
    for (; n > 0; n--, d += 3) {
        memcpy(p, d, 3);
        memcpy(d, p, 3);
    }
}

const BlitTable MOVDFL_fill = {{fill8, fill16, fill16, fill24, fill32}, NULL, 0, 0};

/* ---- the blitter ---- */

static DepthKernel convert_row;      /* 0x6a636c: the conversion of a two-step row (0x571770) */
static BlitKernel copy_row;          /* 0x6a6370 */

/* 0x571770: a row converted into a buffer on the stack (0x1900 bytes), then copied. */
static void convert_then_copy(void *dst, const void *src, int n)
{
    uint8_t buf[0x1900];
    convert_row(buf, src, n);
    copy_row(dst, buf, n);
}

static int16_t ld16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

void movdfl(const uint8_t *shape, int x, int y, const BlitTable *table)
{
    uint8_t *converted = NULL;
    if (!SHAPE_typevalid(shape))
        REAL_abort("\\real\\pc\\movdfl.c", 0x2a, "movdfl - INVALID SHAPE TYPE ($%02x)\n", shape[0]);
    convert_row = NULL;
    BlitKernel kernel = table->forced;
    uint32_t target = table->target ? table->target : WINDOW_cur.type;
    if (!kernel && shape[0] != target) {
        int conv = table->conversion;
        DepthKernel k = DEPTH_rowkernel(shape, target, conv != 2);
        kernel = (BlitKernel)k;
        if (!k) {
            converted = SHAPE_converttype(shape, target, conv == 1, conv != 2);
            shape = converted;
        } else if (conv != 2) {
            /* A row kernel that only converts: the copy of the window's depth goes after it. */
            kernel = NULL;
            convert_row = k;
        }
    }
    int w = ld16s(shape + 4), h = ld16s(shape + 6);
    const uint8_t *src = shape + 0x10;
    int bits = (SHAPE_typevalid(shape) + 1) & ~1;
    int rowbytes = bits * w >> 3;
    int srcskip = SHAPE_rowbytes(shape) - rowbytes;
    const int *clip = WINDOW_cur.clip;
    int d = clip[1] - y;
    if (d > 0) {
        /* The rows skipped at the top advance by the visible bytes of a row, without the row padding. */
        src += rowbytes * d;
        h -= d;
        y += d;
    }
    d = h - clip[3] + y;
    if (d > 0) h -= d;
    d = clip[0] - x;
    if (d > 0) {
        int skip = d * bits >> 3;
        src += skip;
        w -= d;
        x += d;
        srcskip += skip;
    }
    d = w - clip[2] + x;
    if (d > 0) {
        srcskip += d * bits >> 3;
        w -= d;
    }
    if (w > 0 && h > 0) {
        int dstrow = WINDOW_cur.bpp * w >> 3;
        uint8_t *dst = WINDOW_cur.pixels + WINDOW_offset(x, y);
        int dstskip = WINDOW_cur.stride - dstrow;
        if (!kernel) {
            switch (WINDOW_cur.depth) {
            case 8: kernel = table->kernel[0]; break;
            case 15: kernel = table->kernel[1]; break;
            case 16: kernel = table->kernel[2]; break;
            case 24: kernel = table->kernel[3]; break;
            case 32: kernel = table->kernel[4]; break;
            default: break;
            }
            if (convert_row) {
                copy_row = kernel;
                kernel = convert_then_copy;
            }
        }
        if (kernel) {
            if (dstskip == 0 && srcskip == 0 && kernel != convert_then_copy)
                kernel(dst, src, h * w);
            else
                for (; h > 0; h--) {
                    kernel(dst, src, w);
                    src += (bits * w >> 3) + srcskip;
                    dst += dstskip + dstrow;
                }
        }
    }
    if (converted) MEM_purge(converted);
}
