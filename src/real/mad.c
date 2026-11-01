/* EA's MAD video codec (see mad.h). The coefficient tables, the quantiser matrix, the zigzag order and
   the IDCT's and the colour conversion's constants are read from the exe. */
#include "real/mad.h"
#include "real/window.h"
#include "exe.h"
#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- the bit reader */

static uint32_t s_bits;            /* 0x6b4ea0: the next 32 bits, the first in the top bit */
static const uint8_t *s_word;      /* 0x6b4fc0: the next 16-bit word */
static const uint8_t *s_end;       /* (the chunk's end: words past it read as 0) */
static int s_count;                /* 0x6b4fc4: the bits in s_bits that are real */
static int s_inter;                /* 0x6a6340 */

static uint32_t next_word(void)
{
    if (s_word + 2 > s_end) return 0;
    uint32_t w = (uint32_t)s_word[0] | (uint32_t)s_word[1] << 8;
    s_word += 2;
    return w;
}

/* 0x56cdf0 (and the same inline in 0x593010) */
static void consume(int n)
{
    s_bits <<= n & 31;
    s_count -= n;
    if (s_count < 16) {
        s_bits |= next_word() << ((16 - s_count) & 31);
        s_count += 16;
    }
}

/* ---------------------------------------------------------------- tables */

static int s_ready;                /* 0x6a5f3c */
static int32_t s_q[64];            /* 0x6b4ec0: the quantiser in raster order (DC first) */
static uint8_t s_clamp[256];       /* 0x6b4fe0: a signed sample byte -> 0..127 (index 0x7f unset: 0) */
static uint32_t s_t1[512];         /* 0x6b50e0: the next 9 bits */
static uint32_t s_t2[256];         /* 0x6b58e0: 8 bits after nine zeros */
static uint32_t s_t3[256];         /* 0x6b5ce0: 8 bits after six zeros */
static uint32_t s_mv[64];          /* 0x6b60e0: motion and offset codes, 6 bits */
static uint8_t s_zigzag[64];       /* 0x5e2940: raster positions (byte offsets / 4) */
static int32_t s_k;                /* 0x5e2608: sqrt(2) / 2 in 0.32 */
static float s_c1, s_c2, s_c3, s_magic;   /* 0x5e260c, 0x5e2610, 0x5e2614, 0x5e2618 */

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* An entry of the coefficient tables: the run (bits 16..21), the level (signed, from bit 22) and the
   bits to consume (the low byte). */
static uint32_t entry(uint32_t v, uint32_t len) { return ((v & 0xfc00) | v << 16) << 6 | len; }

static void fill(uint32_t *t, int count, uint32_t e)
{
    for (int i = 0; i < count; i++) t[i] = e;
}

/* 0x56c8b0 */
static void tables(void)
{
    for (int v = -0x80; v < 0x7f; v++) s_clamp[v & 0xff] = (uint8_t)((v < -0x40 ? -0x40 : v > 0x3f ? 0x3f : v) + 0x40);
    s_t1[0] = 0xf;
    fill(s_t1 + 1, 7, 0x1f);
    fill(s_t1 + 8, 8, 0x2f);
    fill(s_t1 + 0x100, 0x80, 0x3f);
    /* The codes: 16 bytes each (the length, the value, -, the code left-aligned in 16 bits); the first
       set's codes from the stream's start, the second's after eight zeros. */
    for (uint32_t a = 0x5bdf90; a < 0x5be570; a += 16) {
        uint32_t len = exe_u32(a), v = exe_u32(a + 4), code = exe_u32(a + 12);
        if ((code & 0xfc00) == 0) {
            int32_t n = (int32_t)(1u << ((14 - len) & 31));
            if (0 < n) fill(s_t3 + ((int32_t)code >> 2), n, entry(v, len - 6));
        } else {
            int32_t n = (int32_t)(1u << ((9 - len) & 31));
            if (0 < n) fill(s_t1 + ((int32_t)code >> 7), n, entry(v, len));
        }
    }
    for (uint32_t a = 0x5be570; a < 0x5bed70; a += 16) {
        uint32_t len = exe_u32(a), v = exe_u32(a + 4), code = exe_u32(a + 12);
        uint32_t total = (uint8_t)(len + 8);
        if ((code & 0x8000) == 0) {
            int32_t n = (int32_t)(1u << ((17 - total) & 31));
            if (0 < n) fill(s_t2 + ((int32_t)code >> 7), n, entry(v, len - 1));
        } else {
            int32_t n = (int32_t)(1u << ((14 - total) & 31));
            if (0 < n) fill(s_t3 + ((int32_t)code >> 10), n, entry(v, len + 2));
        }
    }
    /* Motion: '0' is 0; '1' and five bits 1..16 or -16..-1. */
    fill(s_mv, 0x20, 1);
    for (int i = 0; i < 16; i++) {
        uint32_t v = (uint32_t)(i - 16) << 22;
        s_mv[0x20 + i] = (v + 0x4400000u) | 6;
        s_mv[0x30 + i] = v | 6;
    }
    for (int i = 0; i < 64; i++) s_zigzag[i] = (uint8_t)(exe_u32(0x5e2940 + 4 * (uint32_t)i) >> 2 & 63);
    s_k = (int32_t)exe_u32(0x5e2608);
    s_c1 = exe_f32(0x5e260c);
    s_c2 = exe_f32(0x5e2610);
    s_c3 = exe_f32(0x5e2614);
    s_magic = exe_f32(0x5e2618);
    s_ready = 1;
}

/* A 16.16 product rounded (bit 15 up), as the original's 64-bit products. */
static int32_t round16(int32_t a, int32_t b)
{
    int64_t p = (int64_t)a * b;
    uint32_t lo = (uint32_t)p;
    return (int32_t)((uint32_t)(p >> 16) + ((lo >> 15) & 1));
}

void MAD_begin(const uint8_t *bits, const uint8_t *end, int inter, int quality)
{
    if (!s_ready) tables();
    s_word = bits;
    s_end = end;
    uint32_t w0 = next_word(), w1 = next_word();
    s_bits = w0 << 16 | w1;
    s_count = 0x20;
    s_inter = inter;
    /* The quantiser: the intra matrix (0x5bed70) times the quality and the AAN scales (0x5e283c). */
    s_q[0] = round16((int32_t)(exe_u32(0x5bed70) << 15), (int32_t)exe_u32(0x5e283c));
    for (uint32_t i = 1; i < 64; i++)
        s_q[i] = round16((int32_t)(exe_u32(0x5bed70 + 4 * i) * (uint32_t)quality * 0x1000u), (int32_t)exe_u32(0x5e283c + 4 * i));
}

/* ---------------------------------------------------------------- blocks */

static int32_t s_blk[64];          /* 0x5e273c: the coefficients */
static int32_t s_tmp[72];          /* 0x5e261c: the IDCT's columns (9 to a row) */
static int32_t s_luma[256];        /* 0x6a5f40: 16 x 16 samples (the value in bits 16..23) */
static int32_t s_chroma[128];      /* 0x6a5d3c: U 8 x 8, then V (0x6a5e3c) */

/* A motion code (0x56ce50). */
static int32_t motion(void)
{
    uint32_t e = s_mv[s_bits >> 26];
    consume((int)(e & 0xff));
    return (int32_t)e >> 22;
}

/* The coefficients of a block (0x593010): the DC (eight bits), then runs and levels to the end code. The
   number of positions passed (1: the DC alone). */
static int coefficients(void)
{
    s_blk[0] = (int32_t)((uint32_t)((int32_t)s_bits >> 24) * (uint32_t)s_q[0]);
    consume(8);
    memset(s_blk + 1, 0, 63 * sizeof *s_blk);
    int idx = 1;
    for (;;) {
        uint32_t e = s_t1[s_bits >> 23];
        if ((int8_t)e > 9) {
            uint8_t l = (uint8_t)e;
            if (l < 0x10) {
                consume(9);
                e = s_t2[s_bits >> 24];
            } else if (l < 0x20) {
                consume(6);
                e = s_t3[s_bits >> 24];
            } else if (l < 0x30) {   /* escape: six bits of run, ten of level */
                consume(6);
                e = (s_bits & 0xffff0000u) | 0x10;
            } else {
                consume(2);
                return idx;
            }
        }
        idx += (int)(e >> 16 & 0x3f);
        consume((int)(e & 0xff));
        /* (Past 63 the original reads past its zigzag table and writes where it points; the port
           drops those, and stops a stream that has gone wrong.) */
        if (idx < 64) s_blk[s_zigzag[idx]] = (int32_t)((uint32_t)((int32_t)e >> 22) * (uint32_t)s_q[s_zigzag[idx]]);
        idx++;
        if (idx > 0x1000) return idx;
    }
}

/* x * sqrt(2) as the assembly has it: the high half of x * 0x5a82799a, doubled. */
static int32_t mul_k(int32_t x) { return (int32_t)((uint32_t)(int32_t)(((int64_t)x * s_k) >> 32) << 1); }

/* The low 32 bits of a double rounded through the 1.5 * 2^52 constant (to nearest, ties even). */
static int32_t round_low(double v)
{
    double t = v + (double)s_magic;
    uint64_t u;
    memcpy(&u, &t, 8);
    return (int32_t)(uint32_t)u;
}

/* One row of the AAN IDCT (0x592c10 the first pass, columns out at step 9 with the shortcut for a row
   of zeros after the DC; 0x592d6d the second, rows out at step 1). */
static void idct_row(const int32_t *in, int32_t *out, int step, int shortcut)
{
#define U(x) ((uint32_t)(x))
    if (shortcut && !(in[2] | in[5] | in[6] | in[3] | in[4] | in[1] | in[7])) {
        for (int i = 0; i < 8; i++) out[i * step] = in[0];
        return;
    }
    int32_t z13 = (int32_t)(U(in[3]) + U(in[5])), z10 = (int32_t)(U(in[5]) - U(in[3]));
    int32_t z11 = (int32_t)(U(in[7]) + U(in[1])), z12 = (int32_t)(U(in[1]) - U(in[7]));
    int32_t t7 = (int32_t)(U(z13) + U(z11));
    double z5 = ((double)z12 + (double)z10) * (double)s_c3;
    double fa = (double)z10 * (double)s_c1;
    fa = fa + z5;
    double fb = (double)z12 * (double)s_c2;
    fb = fb - z5;
    int32_t t11 = mul_k((int32_t)(U(z11) - U(z13)));
    int32_t a5 = round_low(fa), b5 = round_low(fb);
    int32_t p = (int32_t)(U(a5) + U(t11)), q = (int32_t)(U(t11) + U(b5)), r = (int32_t)(U(b5) + U(t7));
    int32_t m = mul_k((int32_t)(U(in[2]) - U(in[6])));
    int32_t s = (int32_t)(U(in[6]) + U(in[2]) + U(m));
    int32_t d04 = (int32_t)(U(in[0]) - U(in[4])), s04 = (int32_t)(U(in[4]) + U(in[0]));
    int32_t a = (int32_t)(U(m) + U(d04)), c = (int32_t)(U(d04) - U(m));
    int32_t d = (int32_t)(U(s) + U(s04)), b = (int32_t)(U(s04) - U(s));
    out[0 * step] = (int32_t)(U(d) + U(r));
    out[1 * step] = (int32_t)(U(q) + U(a));
    out[2 * step] = (int32_t)(U(c) + U(p));
    out[3 * step] = (int32_t)(U(b) + U(a5));
    out[4 * step] = (int32_t)(U(b) - U(a5));
    out[5 * step] = (int32_t)(U(c) - U(p));
    out[6 * step] = (int32_t)(U(a) - U(q));
    out[7 * step] = (int32_t)(U(d) - U(r));
#undef U
}

/* The IDCT into a block of a buffer stride samples wide. 0x592e77 */
static void idct(int32_t *dest, int stride)
{
    for (int k = 0; k < 8; k++) idct_row(s_blk + 8 * k, s_tmp + k, 9, 1);
    for (int j = 0; j < 8; j++) idct_row(s_tmp + 9 * j, dest + j * stride, 1, 0);
}

/* A DC-only block: the DC everywhere. 0x56ce80 */
static void flat(int32_t *dest, int stride)
{
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) dest[j * stride + i] = s_blk[0];
}

static void intra(int32_t *dest, int stride)
{
    if (coefficients() == 1) flat(dest, stride);
    else idct(dest, stride);
}

/* A luma block from the reference plus an offset: pixel n of a row is byte 3 (even n) or 2 (odd) of
   word n / 2; odd moves it a pixel right. 0x56cec0 */
static void copy_luma(const uint8_t *p, int odd, int sw, int32_t *dest, int8_t add)
{
    for (int j = 0; j < 8; j++, p += sw * 4, dest += 16)
        for (int i = 0; i < 8; i++) {
            int n = i + odd;
            dest[i] = (int32_t)((uint32_t)(uint8_t)(p[(n >> 1) * 4 + ((n & 1) ? 2 : 3)] + add) << 16);
        }
}

/* A chroma block (byte 1 U, 0 V of every word, every second row). 0x56cfa0 */
static void copy_chroma(const uint8_t *p, int sw, int32_t *dest, int8_t add)
{
    for (int j = 0; j < 8; j++, p += sw * 8, dest += 8)
        for (int i = 0; i < 8; i++) dest[i] = (int32_t)((uint32_t)(uint8_t)(p[i * 4] + add) << 16);
}

/* A row of 16 luma samples and 8 chroma pairs as 8 frame words. 0x5931b9 */
static void pack(const int32_t *y, const int32_t *c, uint8_t *dest)
{
    for (int i = 0; i < 8; i++) {
        dest[i * 4 + 0] = s_clamp[(uint32_t)c[64 + i] >> 16 & 0xff];
        dest[i * 4 + 1] = s_clamp[(uint32_t)c[i] >> 16 & 0xff];
        dest[i * 4 + 2] = s_clamp[(uint32_t)y[2 * i + 1] >> 16 & 0xff];
        dest[i * 4 + 3] = s_clamp[(uint32_t)y[2 * i] >> 16 & 0xff];
    }
}

void MAD_macroblock(const uint8_t *ref, uint8_t *dest, int width)
{
    int sw = width >> 1;
    unsigned map = 0;
    int odd = 0;
    /* The predicted frames' header: '00' all six blocks coded, '1' all copied, '01' and six bits of
       which are copied; then the motion (x in half words, y in rows). */
    if (s_inter) {
        if ((s_bits & 0xc0000000u) == 0) {
            consume(2);
        } else {
            int n;
            if ((s_bits & 0x80000000u) == 0) {
                map = s_bits >> 24;
                n = 8;
            } else {
                map = 0x3ff;
                n = 1;
            }
            consume(n);
            int32_t mx = motion(), my = motion();
            odd = mx & 1;
            ref += (my * sw + (mx >> 1)) * 4;
        }
    }
    static const int luma_at[4] = {0, 8, 128, 136};
    for (int b = 0; b < 4; b++) {
        if (map & (1u << b)) {
            int8_t add = (int8_t)(motion() - 0x40);
            copy_luma(ref + (b & 1) * 0x10 + (b >> 1) * sw * 0x20, odd, sw, s_luma + luma_at[b], add);
        } else {
            intra(s_luma + luma_at[b], 16);
        }
    }
    for (int b = 0; b < 2; b++) {   /* U (byte 1), then V (byte 0) */
        if (map & (0x10u << b)) {
            int8_t add = (int8_t)(motion() - 0x40);
            copy_chroma(ref + (1 - b), sw, s_chroma + 64 * b, add);
        } else {
            intra(s_chroma + 64 * b, 8);
        }
    }
    for (int r = 0; r < 16; r++) pack(s_luma + 16 * r, s_chroma + 8 * (r >> 1), dest + r * sw * 4);
}

static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

void MAD_decode(const uint8_t *chunk, uint32_t size, const uint8_t *ref, uint8_t *dest)
{
    int w = rd16(chunk + 0x10), h = rd16(chunk + 0x12);
    int inter = memcmp(chunk, "MADk", 4) != 0;
    if (inter && !ref) return;   /* (no reference: the original would read from address 0) */
    MAD_begin(chunk + 0x18, chunk + size, inter, (int8_t)chunk[0x15]);
    for (int y = 0; y < h; y += 16)
        for (int x = 0; x < w; x += 16) {
            int idx = (w * y + x) / 2;
            MAD_macroblock(inter ? ref + idx * 4 : NULL, dest + idx * 4, w);
        }
}

/* ---------------------------------------------------------------- display */

static int s_table_depth;          /* 0x6a5d34 */
static uint32_t s_y[128];          /* 0x6b6fa0 */
static uint32_t s_u[128];          /* 0x6b61e0: U's green and blue */
static uint32_t s_v[128];          /* 0x6b63e0: V's red and green */
static uint32_t s_r[306];          /* 0x6b71a0 */
static uint32_t s_g[262];          /* 0x6b6b80 */
static uint32_t s_b[354];          /* 0x6b65e0 */

/* The colour tables for a depth (15, 16, else 32). A sample's red, green and blue indices are 9-bit
   fields of one sum (bits 23.., 11..19, 0..8); the output tables hold a pixel pair's two values, the
   rounded one low (the first pixel) and the truncated one high. 0x56ab20 */
static void colour_tables(int depth)
{
    float half = exe_f32(0x5b23f0);
    float ug = exe_f32(0x5bd8b4), ub = exe_f32(0x5bd8b0), vr = exe_f32(0x5bd8ac), vg = exe_f32(0x5bd8a8);
    uint32_t y = 0x2c821871u;
    for (int k = 0; k < 128; k++, y += 0x800801u) {
        s_y[k] = y;
        float f = (float)(k - 64) + half;
        s_u[k] = ((uint32_t)(int32_t)lrint((double)f * ug) & 0x1ff) * 0x800 + ((uint32_t)(int32_t)lrint((double)f * ub) & 0x1ff);
        s_v[k] = (((uint32_t)(int32_t)lrint((double)f * vr) & 0x1ff) * 0x1000 + ((uint32_t)(int32_t)lrint((double)f * vg) & 0x1ff)) * 0x800;
    }
    int n = 0;
    for (int i = -0xb1; i < 0x1b3; i += 2, n++) {
        int v = i < 0 ? 0 : i > 0xff ? 0xff : i;
        int r = (v + 4) >> 3 > 0x1f ? 0x1f : (v + 4) >> 3;
        s_r[n] = depth == 15 ? (uint32_t)((v >> 3) * 0x10000 + r) * 0x400 : depth == 16 ? (uint32_t)((v >> 3) * 0x10000 + r) * 0x800 : (uint32_t)v << 16;
    }
    n = 0;
    for (int i = -0x85; i < 0x187; i += 2, n++) {
        int v = i < 0 ? 0 : i > 0xff ? 0xff : i;
        if (depth == 15) {
            int g = (v + 4) >> 3 > 0x1f ? 0x1f : (v + 4) >> 3;
            s_g[n] = (uint32_t)((v >> 3) * 0x10000 + g) * 0x20;
        } else if (depth == 16) {
            int g = (v + 2) >> 2 > 0x3f ? 0x3f : (v + 2) >> 2;
            s_g[n] = (uint32_t)((v >> 2) * 0x10000 + g) * 0x20;
        } else {
            s_g[n] = (uint32_t)v << 8;
        }
    }
    n = 0;
    for (int i = -0xe1; i < 0x1e3; i += 2, n++) {
        int v = i < 0 ? 0 : i > 0xff ? 0xff : i;
        int b = (v + 4) >> 3 > 0x1f ? 0x1f : (v + 4) >> 3;
        s_b[n] = depth == 15 || depth == 16 ? (uint32_t)((v >> 3) * 0x10000 + b) : (uint32_t)v;
    }
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* count words to pixel pairs (16 bits each). 0x5923c0 */
static void convert16(const uint32_t *src, uint8_t *dest, int count)
{
    while (count-- > 0) {
        uint32_t w = *src++;
        uint32_t cv = s_v[w & 0x7f], cu = s_u[w >> 8 & 0x7f];
        uint32_t s0 = cu + s_y[w >> 24 & 0x7f] + cv, c = cv + cu;
        uint32_t p0 = s_r[(s0 >> 23) % 306] + s_g[((s0 & 0xff800) >> 11) % 262] + s_b[(s0 & 0x1ff) % 354];
        uint32_t s1 = s_y[w >> 16 & 0x7f] + c;
        uint32_t p1 = s_r[(s1 >> 23) % 306] + s_g[((s1 & 0xff800) >> 11) % 262] + s_b[(s1 & 0x1ff) % 354];
        uint32_t out = (p1 & 0xffff0000u) | (p0 & 0xffff);
        memcpy(dest, &out, 4);
        dest += 4;
    }
}

/* A row doubled: each word and the one after it give two, the pixels between interpolated (7-bit bytes
   averaged in one add). 0x592660 */
static void double_row(const uint32_t *src, uint32_t *dest, int count)
{
    for (int i = 0; i < count; i++) {
        uint32_t cur = src[i], next = src[i + 1];
        uint32_t hi = (cur << 8) & 0x7f000000u;
        uint32_t avg = (((next & 0x7f7f) | hi | ((next >> 8) & 0x7f0000)) + cur) >> 1;
        dest[2 * i] = (cur & 0x7f007f7fu) | ((avg >> 8) & 0x7f0000);
        dest[2 * i + 1] = (avg & 0x7f7f7f) | hi;
    }
}

/* Two rows averaged, then doubled. 0x5926c8 */
static void double_rows(const uint32_t *a, const uint32_t *b, uint32_t *dest, int count)
{
    uint32_t avg[642];
    if (count + 1 > 642) count = 641;
    for (int i = 0; i <= count; i++) avg[i] = ((a[i] + b[i]) >> 1) & 0x7f7f7f7fu;
    double_row(avg, dest, count);
}

static void words(const uint8_t *p, uint32_t *out, int n)
{
    for (int i = 0; i < n; i++) out[i] = rd32(p + 4 * i);
}

int MAD_blit(int x, int y, const uint8_t *frame, int w, int h, int mode)
{
    int scale;
    if (mode == 0) scale = 1;
    else if (mode == 1 || mode == 2 || mode == 3) scale = 2;
    else return -1;
    int depth = WINDOW_cur.depth;
    if (depth != 15 && depth != 16 && (depth != 32 || mode != 0)) return -2;
    if (depth == 32 || mode == 1 || mode == 2) return -1;   /* (0x59249f, 0x592572: not ported) */
    if (s_table_depth != depth) {
        colour_tables(depth);
        s_table_depth = depth;
    }
    uint8_t *base = WINDOW_cur.pixels;
    if (!base) return 0;
    /* x rounded to the surface's 4-byte words. */
    int x0 = (int)(((uintptr_t)base >> 1) & 1) | (x & ~1);
    int bottom = scale * h + y, right = scale * w + x0;
    int sw = w / 2;
    const uint8_t *src = frame;
    if (x0 < WINDOW_cur.clip[0]) {
        int n = ((scale * 2 - x0) - 1 + WINDOW_cur.clip[0]) / (scale * 2);
        x0 += n * scale * 2;
        src += n * 4;
    }
    if (WINDOW_cur.clip[2] < right) right = WINDOW_cur.clip[2];
    if (y < WINDOW_cur.clip[1]) {
        int n = ((WINDOW_cur.clip[1] - y) - 1 + scale) / scale;
        y += n * scale;
        src += n * sw * 4;
    }
    if (WINDOW_cur.clip[3] < bottom) bottom = WINDOW_cur.clip[3];
    int span = right - x0;
    int n = span / (scale * 2);
    if (n < 2) return 0;
    uint32_t a[642], b[642], tmp[1284];
    if (n > 640) n = 640;
    if (mode == 0) {
        for (int row = y; row < bottom; row++, src += sw * 4) {
            words(src, a, n);
            convert16(a, base + WINDOW_cur.stride * row + (WINDOW_cur.bpp * x0 >> 3), n);
        }
        return 0;
    }
    const uint8_t *s = src;
    for (int row = y; row < bottom; row += 2, s += sw * 4) {
        words(s, a, n);
        double_row(a, tmp, n - 1);
        convert16(tmp, base + WINDOW_cur.stride * row + (WINDOW_cur.bpp * x0 >> 3), n * 2 - 2);
    }
    /* The rows between, from the two around them (the last row stays). The original first unlocks the
       surface here for a large frame (0x5649b0, then keeps writing through the pointer, which DirectDraw's
       primary surface lets it): the port keeps the lock so those rows reach the screen too. */
    for (int row = y + 1; row < bottom - 1; row += 2, src += sw * 4) {
        words(src, a, n);
        words(src + sw * 4, b, n);
        double_rows(a, b, tmp, n - 1);
        convert16(tmp, base + WINDOW_cur.stride * row + (WINDOW_cur.bpp * x0 >> 3), n * 2 - 2);
    }
    return 0;
}
