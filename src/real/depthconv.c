/* \real\cmn\depthconv.c and its pixel kernels: see depthconv.h. */
#include "real/depthconv.h"
#include "exe.h"
#include "real/mem.h"
#include "real/pack.h"
#include "real/real.h"
#include "real/shape.h"
#include "real/window.h"
#include <stdio.h>
#include <string.h>

static const char depthconv_c[] = "\\real\\cmn\\depthconv.c";

uint32_t DEPTH_clutbuf[256];                     /* 0x6bd960 */
uint32_t REAL_syspalette[256];                   /* 0x6b4a60: the 8-bit display palette (palette.c) */
const uint32_t *DEPTH_clut = REAL_syspalette;    /* 0x5de2f8 */
uint32_t DEPTH_colourkey;                        /* 0x6a6368 */

/* ---- little helpers: pixel access without alignment assumptions ---- */

static uint16_t ld16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t ld32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void st16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void st32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Channel widening as the kernels do it: the top bits repeated into the new low bits. */
static uint32_t x5(uint32_t c) { return c << 3 | c >> 2; }
static uint32_t x6(uint32_t c) { return c << 2 | c >> 4; }

/* 0x572ce0: ARGB8888 -> RGB565 (truncating). */
static uint32_t to565(uint32_t v) { return ((v & 0xfc00) >> 5) + ((v & 0xf80000) >> 8) + (v >> 3 & 0x1f); }
/* 0x572d0b: ARGB8888 -> ARGB1555, alpha set; 0 stays 0 (transparent black). */
static uint32_t to1555(uint32_t v)
{
    return v ? ((v & 0xf800) >> 6) + 0x8000 + ((v & 0xf80000) >> 9) + (v >> 3 & 0x1f) : 0;
}

/* ---- the kernels (dst, src, n pixels), named by what they convert ---- */

static void k_copy4(void *d, const void *s, int n) { MEM_copy(d, s, (size_t)((n + 1) >> 1)); }          /* 0x56fe80 */
static void k_copy8(void *d, const void *s, int n) { MEM_copy(d, s, (size_t)n); }                       /* 0x56ff40 */
static void k_copy16(void *d, const void *s, int n) { MEM_copy(d, s, (size_t)n * 2); }                  /* 0x570220 */
static void k_copy24(void *d, const void *s, int n) { MEM_copy(d, s, (size_t)n * 3); }                  /* 0x5704b0 */
static void k_copy32(void *d, const void *s, int n) { MEM_copy(d, s, (size_t)n * 4); }                  /* 0x570830 */

/* 0x56fea0: 8-bit indices into 4-bit nibbles (even pixel in the low nibble), keeping the other nibble. */
static void k_p8_p4(void *d, const void *s, int n)
{
    uint8_t *dst = d;
    const uint8_t *src = s;
    for (int i = 0; i < n; i++) {
        uint8_t *b = dst + (i >> 1);
        *b = (i & 1) ? (uint8_t)(src[i] << 4 | (*b & 0x0f)) : (uint8_t)((src[i] & 0x0f) | (*b & 0xf0));
    }
}

/* 0x56ff00: 4-bit nibbles into 8-bit indices. */
static void k_p4_p8(void *d, const void *s, int n)
{
    uint8_t *dst = d;
    const uint8_t *src = s;
    for (int i = 0; i < n; i++) dst[i] = (i & 1) ? src[i >> 1] >> 4 : src[i >> 1] & 0x0f;
}

/* 0x537ba0: the palette entry nearest an ARGB colour (squared distance over r, g, b; the first of equals),
   among those the flags allow (0x5de790: all but 254 and 255), in the system palette by default. */
static uint8_t nearest(const uint32_t *pal, uint32_t argb)
{
    const uint8_t *ok = exe_data(0x5de790, 256);
    if (!pal) pal = REAL_syspalette;
    int best = 99999, pick = 0;
    for (int i = 0; i < 256; i++) {
        if (ok && !ok[i]) continue;
        int db = (int)(argb & 0xff) - (int)(pal[i] & 0xff), dg = (int)(argb >> 8 & 0xff) - (int)(pal[i] >> 8 & 0xff),
            dr = (int)(argb >> 16 & 0xff) - (int)(pal[i] >> 16 & 0xff);
        int d = dr * dr + dg * dg + db * db;
        if (d < best) best = d, pick = i;
    }
    return (uint8_t)pick;
}

uint8_t RGB_nearest(uint32_t argb) { return nearest(NULL, argb); }   /* 0x572d40 (no inverse table) */

/* RGB_colourmatch 0x537c40: an ARGB colour in the current window's format. 8 bits and below: the nearest
   palette entry (0x572d40; its inverse table 0x69e0ac is never made); 15: 1555 with the alpha bit, 0 for
   0 (0x572d0b); 16: 4444 for the 'm' type (0x572db0), else 565 (0x572ce0); 24 and up: as it is; others 0. */
uint32_t RGB_colourmatch(uint32_t argb)
{
    unsigned depth = WINDOW_cur.depth;
    if (depth < 9) return nearest(NULL, argb);
    if (depth == 15) return argb ? ((argb & 0xf800) >> 6) + 0x8000 + ((argb & 0xf80000) >> 9) + (argb >> 3 & 0x1f) : 0;
    if (depth != 16) return depth < 24 ? 0 : argb;
    if (WINDOW_cur.type == 'm')
        return (((argb >> 20 & 0xf) | (argb >> 28) << 4) << 4 | (argb >> 12 & 0xf)) << 4 | (argb >> 4 & 0xf);
    return ((argb & 0xfc00) >> 5) + ((argb & 0xf80000) >> 8) + (argb >> 3 & 0x1f);
}

uint32_t RGB_pixelcolour(uint32_t px)
{
    switch (WINDOW_cur.depth) {
    case 15:
        return (((((px & 0x8000) ? 0xffu : 0) << 5 | (px >> 10 & 0x1f)) << 8 | (px >> 5 & 0x1f)) << 8 | (px & 0x1f)) << 3;
    case 16:
        if (WINDOW_cur.type == 'm')
            return (((px >> 12 & 0xf) << 4 << 8 | (px >> 8 & 0xf) << 4) << 8 | (px >> 4 & 0xf) << 4) << 8 | (px & 0xf) << 4;
        return (((px ? 0xffu : 0) << 8 | (px >> 11 & 0x1f) << 3) << 8 | (px >> 5 & 0x3f) << 2) << 8 | (px & 0x1f) << 3;
    case 24: case 32:
        return px;
    default:
        return REAL_syspalette[px & 0xff];
    }
}

/* 0x56ff60: RGB565 -> 8-bit index, through the inverse table (0x69e0ac; never made, in the original too) or the
   colour match. */
static void k_565_p8(void *d, const void *s, int n)
{
    uint8_t *dst = d;
    const uint8_t *src = s;
    for (int i = 0; i < n; i++) {
        uint32_t v = ld16(src + 2 * i);
        dst[i] = (uint8_t)RGB_colourmatch(0xff000000u | x5(v >> 11) << 16 | (v & 0x7e0) << 5 | (v & 0x1f) << 3);
    }
}

/* 0x570010: 15-bit with red and blue swapped; the top bit is dropped. */
static void k_swap555(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld16((const uint8_t *)s + 2 * i);
        st16((uint8_t *)d + 2 * i, (v >> 10 & 0x1f) | (v & 0x1f) << 10 | (v & 0x3e0));
    }
}

/* 0x570050: RGB565 -> ARGB1555, opaque except black (0), which stays transparent. */
static void k_565_1555a(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld16((const uint8_t *)s + 2 * i);
        st16((uint8_t *)d + 2 * i, v ? ((v >> 1 & 0xffe0) | (v & 0x1f) | 0x8000) : 0);
    }
}

/* 0x5700a0: RGB565 -> RGB555 (no alpha). */
static void k_565_555(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld16((const uint8_t *)s + 2 * i);
        st16((uint8_t *)d + 2 * i, (v >> 1 & 0x7fe0) | (v & 0x1f));
    }
}

/* 0x5700e0: ARGB8888 -> ARGB1555, transparent below alpha 0x40. */
static void k_8888_1555t(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld32((const uint8_t *)s + 4 * i);
        st16((uint8_t *)d + 2 * i, v > 0x3fffffffu ? to1555(v) : 0);
    }
}

/* 0x570120: ARGB8888 -> ARGB1555. */
static void k_8888_1555(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) st16((uint8_t *)d + 2 * i, to1555(ld32((const uint8_t *)s + 4 * i)));
}

/* 0x570150 / 0x5701a0: 4-bit / 8-bit indices through the 16-bit palette in the work buffer. */
static void k_p4_16(void *d, const void *s, int n)
{
    const uint8_t *src = s;
    const uint16_t *pal = (const uint16_t *)(const void *)DEPTH_clutbuf;
    for (int i = 0; i < n; i++) st16((uint8_t *)d + 2 * i, pal[(i & 1) ? src[i >> 1] >> 4 : src[i >> 1] & 0x0f]);
}

static void k_p8_16(void *d, const void *s, int n)
{
    const uint8_t *src = s;
    const uint16_t *pal = (const uint16_t *)(const void *)DEPTH_clutbuf;
    for (int i = 0; i < n; i++) st16((uint8_t *)d + 2 * i, pal[src[i]]);
}

/* 0x5701d0: ARGB1555 -> RGB565 (alpha dropped, green widened). */
static void k_1555_565(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld16((const uint8_t *)s + 2 * i);
        uint32_t g = (v & 0x3e0) >> 5;
        st16((uint8_t *)d + 2 * i, (v & 0x7c00) << 1 | (g << 1 | g >> 4) << 5 | (v & 0x1f));
    }
}

/* 0x570240: the low 16 bits of 32-bit entries. */
static void k_low16(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) st16((uint8_t *)d + 2 * i, ld32((const uint8_t *)s + 4 * i));
}

/* 0x570270: ARGB8888 -> RGB565 for palettes: transparent (alpha < 0x40) entries become 0, others never 0
   (pure black becomes 1, so 0 stays the transparent colour). */
static void k_8888_565t(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld32((const uint8_t *)s + 4 * i), o = 0;
        if (v > 0x3fffffffu && (o = to565(v)) == 0) o = 1;
        st16((uint8_t *)d + 2 * i, o);
    }
}

/* 0x5702c0: ARGB8888 -> RGB565. */
static void k_8888_565(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) st16((uint8_t *)d + 2 * i, to565(ld32((const uint8_t *)s + 4 * i)));
}

/* 0x5702f0: ARGB8888 -> ARGB4444 (each channel's top nibble). */
static void k_8888_4444(void *d, const void *s, int n)
{
    const uint8_t *src = s;
    for (int i = 0; i < n; i++, src += 4)
        st16((uint8_t *)d + 2 * i, (uint32_t)(src[3] >> 4) << 12 | (src[2] >> 4) << 8 | (src[1] >> 4) << 4 | src[0] >> 4);
}

/* 0x570350: RGB565 -> ARGB4444, opaque; the colour key becomes 0. */
static void k_565_4444(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld16((const uint8_t *)s + 2 * i);
        uint32_t o = v == DEPTH_colourkey ? 0 : 0xf000 | (v >> 12) << 8 | (v >> 7 & 0xf) << 4 | (v >> 1 & 0xf);
        st16((uint8_t *)d + 2 * i, o);
    }
}

/* 0x5703c0: ARGB8888 -> RGB565 in the low half, the alpha byte kept (palette type 0x2c). */
static void k_8888_565a(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld32((const uint8_t *)s + 4 * i);
        st32((uint8_t *)d + 4 * i, to565(v) | (v & 0xff000000u));
    }
}

/* 0x570400: 8-bit indices -> 24-bit through the current 32-bit palette. */
static void k_p8_888(void *d, const void *s, int n)
{
    uint8_t *dst = d;
    const uint8_t *src = s;
    for (int i = 0; i < n; i++, dst += 3) {
        uint32_t c = DEPTH_clut[src[i]];
        dst[0] = (uint8_t)c;
        dst[1] = (uint8_t)(c >> 8);
        dst[2] = (uint8_t)(c >> 16);
    }
}

/* 0x570440: RGB565 -> 24-bit (blue, green, red). */
static void k_565_888(void *d, const void *s, int n)
{
    uint8_t *dst = d;
    for (int i = 0; i < n; i++, dst += 3) {
        uint32_t v = ld16((const uint8_t *)s + 2 * i);
        dst[0] = (uint8_t)x5(v & 0x1f);
        dst[1] = (uint8_t)x6(v >> 5 & 0x3f);
        dst[2] = (uint8_t)x5(v >> 11);
    }
}

/* 0x5704d0: ARGB8888 -> 24-bit (alpha dropped). */
static void k_8888_888(void *d, const void *s, int n)
{
    uint8_t *dst = d;
    const uint8_t *src = s;
    for (int i = 0; i < n; i++, dst += 3, src += 4) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
    }
}

/* 0x570500: 8-bit indices -> ARGB8888 through the current 32-bit palette (not the work buffer: a
   conversion that just expanded its palette into the buffer still reads the pointer, as the original). */
static void k_p8_8888(void *d, const void *s, int n)
{
    const uint8_t *src = s;
    for (int i = 0; i < n; i++) st32((uint8_t *)d + 4 * i, DEPTH_clut[src[i]]);
}

/* 0x570530: ARGB1555 -> ARGB8888 (alpha 0xff or 0); the colour key becomes 0. */
static void k_1555_8888(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld16((const uint8_t *)s + 2 * i), o = 0;
        if (v != DEPTH_colourkey)
            o = (v & 0x8000 ? 0xff000000u : 0) | x5(v >> 10 & 0x1f) << 16 | x5(v >> 5 & 0x1f) << 8 | x5(v & 0x1f);
        st32((uint8_t *)d + 4 * i, o);
    }
}

/* 0x5705d0: RGB565 -> ARGB8888, opaque; the colour key becomes 0. */
static void k_565_8888(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld16((const uint8_t *)s + 2 * i), o = 0;
        if (v != DEPTH_colourkey) o = 0xff000000u | x5(v >> 11) << 16 | x6(v >> 5 & 0x3f) << 8 | x5(v & 0x1f);
        st32((uint8_t *)d + 4 * i, o);
    }
}

/* 0x570650: ARGB4444 -> ARGB8888 (nibbles doubled). */
static void k_4444_8888(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld16((const uint8_t *)s + 2 * i), o = 0;
        for (int c = 3; c >= 0; c--) {
            uint32_t q = v >> (4 * c) & 0xf;
            o = o << 8 | q << 4 | q;
        }
        st32((uint8_t *)d + 4 * i, o);
    }
}

/* 0x5706d0: 32-bit entries holding an alpha byte over a 555 colour -> ARGB8888 (palette type 0x2e). */
static void k_a555_8888(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld32((const uint8_t *)s + 4 * i);
        st32((uint8_t *)d + 4 * i, (v >> 24) << 24 | x5(v >> 10 & 0x1f) << 16 | x5(v >> 5 & 0x1f) << 8 | x5(v & 0x1f));
    }
}

/* 0x570740: 32-bit entries holding an alpha byte over a 565 colour -> ARGB8888 (palette type 0x2c). */
static void k_a565_8888(void *d, const void *s, int n)
{
    for (int i = 0; i < n; i++) {
        uint32_t v = ld32((const uint8_t *)s + 4 * i);
        st32((uint8_t *)d + 4 * i, (v >> 24) << 24 | x5(v >> 11 & 0x1f) << 16 | x6(v >> 5 & 0x3f) << 8 | x5(v & 0x1f));
    }
}

/* 0x5707b0: 24-bit -> ARGB8888, opaque, bytes in order. */
static void k_888_8888(void *d, const void *s, int n)
{
    const uint8_t *src = s;
    for (int i = 0; i < n; i++, src += 3)
        st32((uint8_t *)d + 4 * i, 0xff000000u | (uint32_t)src[2] << 16 | src[1] << 8 | src[0]);
}

/* 0x570f10: palette of 24-bit (red, green, blue) entries -> ARGB8888; entry 255 transparent, the others
   opaque. Arguments (count, src, dst), as the original. */
static void pal888(int count, const uint8_t *src, uint8_t *dst)
{
    for (int i = 0; i < count; i++, src += 3, dst += 4) {
        dst[3] = i != 0xff ? 0xff : 0;
        dst[2] = src[0];
        dst[1] = src[1];
        dst[0] = src[2];
    }
}

/* 0x570da0: the same for 6-bit (VGA DAC) components, scaled to 8 bits with rounding. */
static void pal666(int count, const uint8_t *src, uint8_t *dst)
{
    for (int i = 0; i < count; i++, src += 3, dst += 4) {
        dst[3] = i != 0xff ? 0xff : 0;
        dst[2] = (uint8_t)((src[0] * 0xffu + 0x20) / 0x3f);
        dst[1] = (uint8_t)((src[1] * 0xffu + 0x20) / 0x3f);
        dst[0] = (uint8_t)((src[2] * 0xffu + 0x20) / 0x3f);
    }
}

DepthKernel DEPTH_kernel(uint32_t address)
{
    switch (address) {
    case 0x56fe80: return k_copy4;
    case 0x56fea0: return k_p8_p4;
    case 0x56ff00: return k_p4_p8;
    case 0x56ff40: return k_copy8;
    case 0x56ff60: return k_565_p8;
    case 0x570010: return k_swap555;
    case 0x570050: return k_565_1555a;
    case 0x5700a0: return k_565_555;
    case 0x5700e0: return k_8888_1555t;
    case 0x570120: return k_8888_1555;
    case 0x570150: return k_p4_16;
    case 0x5701a0: return k_p8_16;
    case 0x5701d0: return k_1555_565;
    case 0x570220: return k_copy16;
    case 0x570240: return k_low16;
    case 0x570270: return k_8888_565t;
    case 0x5702c0: return k_8888_565;
    case 0x5702f0: return k_8888_4444;
    case 0x570350: return k_565_4444;
    case 0x5703c0: return k_8888_565a;
    case 0x570400: return k_p8_888;
    case 0x570440: return k_565_888;
    case 0x5704b0: return k_copy24;
    case 0x5704d0: return k_8888_888;
    case 0x570500: return k_p8_8888;
    case 0x570530: return k_1555_8888;
    case 0x5705d0: return k_565_8888;
    case 0x570650: return k_4444_8888;
    case 0x5706d0: return k_a555_8888;
    case 0x570740: return k_a565_8888;
    case 0x5707b0: return k_888_8888;
    case 0x570830: return k_copy32;
    default: return NULL;
    }
}

/* ---- type tables ---- */

int DEPTH_shapeindex(uint32_t type)
{
    switch (type) {
    case 0x01: case 0x40: case 0x7a: return 0;
    case 0x02: case 0x41: case 0x7b: return 1;
    case 0x03: case 0x7e: return 2;
    case 0x04: case 0x78: return 3;
    case 0x05: case 0x43: case 0x7f: return 4;
    case 0x7d: return 5;
    case 0x6d: return 6;
    case 0x42: return 7;
    default: return -1;
    }
}

int DEPTH_texindex(uint32_t format)
{
    return format < 14 ? (int)exe_u32(0x5de574 + 4 * format) : -1;
}

uint32_t DEPTH_indextex(int index)
{
    return index >= 0 && index < 7 ? exe_u32(0x5de5ac + 4 * (uint32_t)index) : 0;
}

uint32_t DEPTH_bpptotype(int bpp)
{
    switch (bpp) {
    case 1: return 0x79;
    case 4: return 0x7a;
    case 8: return 0x7b;
    case 15: case 0x22b: case 0x613: return 0x7e;
    case 16: case 0x235: return 0x78;
    case 24: case 0x378: return 0x7f;
    case 32: case 0x22b8: return 0x7d;
    case 0x115c: return 0x6d;
    default: return 0;
    }
}

/* The kernel of a conversion step, and its intermediate type, from the exe's tables (index = the source
   type's row, the target's column; either index -1 reads outside the tables in the original: none). */
static DepthKernel step_kernel(int si, int ti, int variant)
{
    if (si < 0 || ti < 0) return NULL;
    return DEPTH_kernel(exe_u32((variant ? 0x5de2fc : 0x5de3fc) + 4 * (uint32_t)(si * 8 + ti)));
}

static uint32_t step_type(int si, int ti)
{
    if (si < 0 || ti < 0) return 0;
    const uint8_t *t = exe_data(0x5de4fc + (uint32_t)(si * 8 + ti), 1);
    return t ? *t : 0;
}

/* ---- palettes ---- */

static int clut_count(const uint8_t *clut) { return (int16_t)ld16(clut + 4); }

const uint32_t *getclutptr(const uint8_t *shape, uint32_t *buf)
{
    const uint8_t *clut = SHAPE_clut(shape);
    if (!clut) return REAL_syspalette;
    uint32_t type = clut[0];
    if (type == 0x2a) return (const uint32_t *)(const void *)(clut + 0x10);
    int n = clut_count(clut);
    const uint8_t *data = clut + 0x10;
    buf[0xff] = 0;
    switch (type) {
    case 0x24: pal888(n, data, (uint8_t *)buf); break;
    case 0x29: k_565_8888(buf, data, n); break;
    case 0x2d: k_1555_8888(buf, data, n); break;
    case 0x2c: k_a565_8888(buf, data, n); break;
    case 0x2e: k_a555_8888(buf, data, n); break;
    case 0x22: pal666(n, data, (uint8_t *)buf); break;
    case 0x23: {
        uint16_t tmp[256];
        k_swap555(tmp, data, 0x100);
        k_1555_8888(buf, tmp, 0x100);
        break;
    }
    default:
        REAL_abort(depthconv_c, 0xca, "getclutptr - UNSUPPORTED CLUT TYPE 0x%x\n", type);
    }
    return buf;
}

const uint16_t *getclut16ptr(const uint8_t *shape, uint16_t *buf)
{
    const uint8_t *clut = SHAPE_clut(shape);
    uint32_t tmp[256];
    if (!clut) {
        k_8888_565t(buf, REAL_syspalette, 0x100);
        return buf;
    }
    uint32_t type = clut[0];
    if (type == 0x29) return (const uint16_t *)(const void *)(clut + 0x10);
    int n = clut_count(clut);
    const uint8_t *data = clut + 0x10;
    buf[0xff] = 0;
    switch (type) {
    case 0x24: pal888(n, data, (uint8_t *)tmp); k_8888_565t(buf, tmp, n); break;
    case 0x2c: k_low16(buf, data, n); break;
    case 0x2e: k_low16(tmp, data, n); k_1555_565(buf, tmp, n); break;
    case 0x2d: k_1555_565(buf, data, n); break;
    case 0x2a: k_8888_565t(buf, data, n); break;
    case 0x22: pal666(n, data, (uint8_t *)tmp); k_8888_565t(buf, tmp, n); break;
    case 0x23: k_swap555(buf, data, n); break;
    default:
        REAL_abort(depthconv_c, 0x137, "getclut16ptr - UNSUPPORTED CLUT TYPE 0x%x\n", type);
    }
    return buf;
}

const uint32_t *getclut16aptr(const uint8_t *shape, uint32_t *buf)
{
    const uint8_t *clut = SHAPE_clut(shape);
    uint32_t tmp[256];
    if (!clut) {
        k_8888_565a(buf, REAL_syspalette, 0x100);
        return buf;
    }
    uint32_t type = clut[0];
    if (type == 0x2c) return (const uint32_t *)(const void *)(clut + 0x10);
    int n = clut_count(clut);
    const uint8_t *data = clut + 0x10;
    buf[0xff] = 0;
    switch (type) {
    case 0x24: pal888(n, data, (uint8_t *)tmp); break;
    case 0x29: k_565_8888(tmp, data, n); break;
    case 0x2d: k_1555_8888(tmp, data, n); break;
    case 0x2e: k_a555_8888(tmp, data, n); break;
    case 0x2a: k_8888_565a(buf, data, n); return buf;
    case 0x22: pal666(n, data, (uint8_t *)tmp); break;
    case 0x23:
        /* 15-bit swapped into the buffer, expanded as 565 (sic) into the temporary, then back. */
        k_swap555(buf, data, n);
        k_565_8888(tmp, buf, n);
        break;
    default:
        REAL_abort(depthconv_c, 0x17e, "getclut16aptr - UNSUPPORTED CLUT TYPE 0x%x\n", type);
        return buf;
    }
    k_8888_565a(buf, tmp, n);
    return buf;
}

/* ---- shapes ---- */

int SHAPE_rowbytes(const uint8_t *shape)
{
    int w = (int16_t)ld16(shape + 4);
    if (shape[0] == '{') return w;
    int bpp = SHAPE_typevalid(shape);
    if (bpp == 15) bpp = 16;
    return (bpp * w + 7) >> 3;
}

uint8_t *SHAPE_unpack(const uint8_t *shape)
{
    uint32_t h = ld32(shape);
    if (!(h & 0x80)) return (uint8_t *)shape;
    uint32_t size = UNPACK_size(shape + SHAPE_HEADER);
    uint8_t *out = MEM_alloc("unshp", (int)size + SHAPE_HEADER, 0);
    MEM_copy(out, shape, SHAPE_HEADER);
    UNPACK_dispatch(shape + SHAPE_HEADER, out + SHAPE_HEADER);
    /* The original keeps the chain: it points the copy's next-record offset at the record after the
       original (adding the distance between the blocks), so the copy's palette is still found. That needs
       the blocks within 24 bits of each other, which a 64-bit build can't promise: the copy is a single
       record here, and SHAPE_converttype takes the palette from the original record (the same one). */
    st32(out, h & 0x7f);
    return out;
}

/* Expands the palette of a palettised source for a step that leaves palettes behind, into the work buffer
   in the layout of the step's kernel (part of SHAPE_converttype). */
static void shape_expand_clut(const uint8_t *clut, int bpp, int bppnext, uint32_t target, int palette, int variant)
{
    int n = 1 << bpp;
    if (!palette) DEPTH_clut = getclutptr(clut, DEPTH_clutbuf);
    else {
        const uint8_t *remap = exe_data(0x5dfa50, (size_t)n);
        for (int i = 0; i < n; i++) DEPTH_clutbuf[i] = REAL_syspalette[remap ? remap[i] : i];
        DEPTH_clut = DEPTH_clutbuf;
    }
    if (target == 0x6d) k_8888_4444(DEPTH_clutbuf, DEPTH_clut, n);
    else if (bppnext == 15) (variant ? k_8888_1555t : k_8888_1555)(DEPTH_clutbuf, DEPTH_clut, n);
    else if (bppnext == 16) (variant ? k_8888_565t : k_8888_565)(DEPTH_clutbuf, DEPTH_clut, n);
}

uint8_t *SHAPE_converttype(const uint8_t *shape, uint32_t target, int palette, int variant)
{
    const uint8_t *cur = shape;
    uint8_t *temp = NULL, *out = NULL;
    const uint8_t *clut = SHAPE_clut(shape);   /* the original reads it from the unpacked copy (SHAPE_unpack) */
    if (shape[0] & 0x80) cur = temp = SHAPE_unpack(shape);
    for (int steps = 10;;) {
        int si = DEPTH_shapeindex(cur[0]), ti = DEPTH_shapeindex(target);
        DepthKernel k = step_kernel(si, ti, variant);
        uint32_t next = step_type(si, ti);
        if (!next)
            REAL_abort(depthconv_c, 0x201,
                       "SHAPE_converttype - UNABLE TO CONVERT TO TARGET SHAPE TYPE.\nInitial shape type ($%02x). Target shape type ($%02x)\n",
                       cur[0], target);
        uint8_t nt[4] = {(uint8_t)next};
        int bpp = SHAPE_typevalid(cur), bppnext = SHAPE_typevalid(nt);
        int w = (int16_t)ld16(cur + 4), h = (int16_t)ld16(cur + 6);
        int srcrow = SHAPE_rowbytes(cur);
        int dstrow = (int)(((uint32_t)(bppnext + 1) & ~1u) * (uint32_t)w + 7) >> 3;
        if (bpp < 9 && bppnext > 8) {
            shape_expand_clut(clut, bpp, bppnext, target, palette, variant);
            palette = 0;
        }
        char name[32];
        snprintf(name, sizeof name, "%dto%d", bpp, bppnext);
        out = MEM_alloc(name, dstrow * h + 0x14, 0);
        uint8_t *sentinel = out + dstrow * h + SHAPE_HEADER;   /* 0x43534850 big-endian: "CSHP" */
        memcpy(sentinel, "CSHP", 4);
        st32(out, next);
        st16(out + 4, (uint32_t)w);
        st16(out + 6, (uint32_t)h);
        st16(out + 8, ld16(cur + 8));
        st16(out + 10, ld16(cur + 10));
        /* The position word: x and y from the source; its other bits (flags, mip count) are whatever the
           new block held in the original. The port clears them, for reproducible output. */
        st32(out + 12, ld32(cur + 12) & 0x0fff0fffu);
        const uint8_t *s = cur + SHAPE_HEADER;
        uint8_t *d = out + SHAPE_HEADER;
        for (int y = 0; y < h; y++, s += srcrow, d += dstrow)
            if (k) k(d, s, w);
        if (memcmp(sentinel, "CSHP", 4) != 0)
            REAL_abort(depthconv_c, 0x255, "convshp - SENTINAL CORRUPTED. %dx%dx%d->%dx%dx%d\n", w, h, bpp, w, h, bppnext);
        if (temp) MEM_purge(temp);
        if (out[0] == (uint8_t)target || --steps == 0) break;
        cur = temp = out;
    }
    return out;
}

/* ---- textures ---- */

static uint32_t tex_bpp(uint32_t format) { return format < 14 ? exe_u32(0x5de53c + 4 * format) : 0; }
static uint32_t clut_format(uint32_t clutfmt) { return clutfmt < 8 ? exe_u32(0x5de5c8 + 4 * clutfmt) : 0; }

int TEXTURE_converttype(uint32_t srcfmt, uint32_t srcclutfmt, uint32_t dstfmt, uint32_t dstclutfmt,
                        const void *src, void *dst, const void *srcclut, void *dstclut, int n)
{
    uint32_t cur = srcfmt, next = 0;
    int bpp = 0, bppnext = 0;
    const void *in = src;
    bool own_in = false;
    if (srcfmt == 12) {
        if (dstfmt == 12) {
            MEM_copy(dst, src, (size_t)(n / 2));
            return 1;
        }
        /* DXT1 data is stored packed by an EA codec: unpacked to 565 first. */
        void *u = MEM_alloc("TEXto565", (int)UNPACK_size(src), 0);
        UNPACK_dispatch(src, u);
        cur = 4;
        in = u;
        own_in = true;
    }
    for (int steps = 10;;) {
        int si = DEPTH_texindex(cur), ti = DEPTH_texindex(dstfmt);
        DepthKernel k = step_kernel(si, ti, 1);
        next = DEPTH_indextex(DEPTH_shapeindex(step_type(si, ti)));
        if (!next)
            REAL_abort(depthconv_c, 0x350,
                       "TEXTURE_converttype - UNABLE TO CONVERT TO TARGET TEXTURE TYPE.\nInitial texture type ($%02x). Target texture type ($%02x)\n",
                       srcfmt, dstfmt);
        bpp = (int)tex_bpp(cur);
        bppnext = (int)tex_bpp(next);
        void *out = dst;
        if (next != dstfmt) {
            char name[32];
            snprintf(name, sizeof name, "%dto%d", bpp, bppnext);
            out = MEM_alloc(name, (int)(((uint32_t)bppnext + 1) & ~1u) * n / 8, 0);
        }
        if (bpp < 9 && bppnext > 8)
            TEXTURE_converttype(clut_format(srcclutfmt), 0, next, 0, srcclut, DEPTH_clutbuf, NULL, NULL, 1 << bpp);
        if (k) k(out, in, n);
        if (own_in) MEM_purge((void *)in);
        if (next == dstfmt) break;
        if (--steps == 0) return 0;
        cur = next;
        in = out;
        own_in = true;
    }
    if (bppnext < 9 && srcclutfmt && dstclutfmt && srcclut && dstclut)
        TEXTURE_converttype(clut_format(srcclutfmt), 0, clut_format(dstclutfmt), 0, srcclut, dstclut, NULL, NULL, 1 << bpp);
    return 1;
}

/* ---- \real\cmn\creates.c ---- */

uint8_t *SHAPE_create(int width, int height, int bpp, unsigned memflags)
{
    if (bpp == 0) bpp = 16;   /* the window depth (0x5dead0) when there is one; none in the port yet */
    uint8_t probe[4] = {(uint8_t)DEPTH_bpptotype(bpp)};
    int depth = SHAPE_typevalid(probe);
    int unit = depth == 15 ? 16 : depth;
    if (width < 0 || width > 0x4000 || height < 0 || height > 0x2000) return NULL;   /* (abort if diagnostics) */
    if (!DEPTH_bpptotype(depth) || !unit) return NULL;
    int row = (int)((((int64_t)unit * width + 7) & ~(int64_t)7) / unit * unit >> 3);
    char name[32];
    snprintf(name, sizeof name, "SHP%dx%dx%d", width, height, depth);
    uint8_t *s = MEM_alloc(name, row * height + SHAPE_HEADER, memflags);
    if (!s) return NULL;
    memset(s, 0, SHAPE_HEADER);
    st16(s + 4, (uint32_t)width);
    st16(s + 6, (uint32_t)height);
    s[0] = probe[0];
    return s;
}

/* ---- row kernels for the blitter ---- */

DepthKernel DEPTH_rowkernel(const uint8_t *shape, uint32_t target, int variant)
{
    int si = DEPTH_shapeindex(shape[0]), ti = DEPTH_shapeindex(target);
    if (step_type(si, ti) != target) return NULL;   /* more than one step: the whole shape is converted */
    DepthKernel k = step_kernel(si, ti, variant);
    uint8_t to[4] = {(uint8_t)target};
    int sbits = SHAPE_typevalid(shape), tbits = SHAPE_typevalid(to);
    if (sbits < 9 && tbits > 8) {
        /* A palette to a direct colour: the palette is converted once into the kernels' table. */
        int n = 1 << sbits;
        DEPTH_clut = getclutptr(shape, DEPTH_clutbuf);
        if (target == 0x6d) DEPTH_kernel(0x5702f0)(DEPTH_clutbuf, DEPTH_clut, n);
        else if (tbits == 15) DEPTH_kernel(variant ? 0x5700e0 : 0x570120)(DEPTH_clutbuf, DEPTH_clut, n);
        else if (tbits == 16) DEPTH_kernel(variant ? 0x570270 : 0x5702c0)(DEPTH_clutbuf, DEPTH_clut, n);
    }
    return k;
}
