/* The vertex transformer of EA's real library (0x537ed0..0x5385d0, with the hand-written x87 routines of
   the PACODE section, 0x5b05c0..0x5b0960): points through a matrix, 1/z, and clip codes computed from the
   bit patterns of the results.

   EAGL_Transform (0x538320) picks the routine from the flags, as the original does:
     (flags & 0xffff) == 1: 4x4, no codes (0x538470)      == 3: xyz with w = 1 (0x538430)
     (flags & 0xff) == 0x11 / 0x13: xyz(w) with w out = 1 and codes (0x538540 / 0x5384b0)
     matrix + 1/z without bit 7, codes routine 1..3: the PACODE routine (0x5b0730)
     anything else: the C routine 0x5385d0.
   The perspective transform of a draw (flags 0x20b) takes the PACODE routine, the orthographic one
   (0x613) the C routine.

   Clip codes are built from the IEEE bit patterns: the sign bits of x, y, z, and |z| - |x|, |z| - |y|
   subtracted as integers (for non-negative floats the integer order is the float order) select an entry
   of a 32-word table; near and far come from the same trick against the near/far values. The tables are
   the exe's (0x5de94c, 0x5de9cc for the C routines, 0x5dfb90, 0x5dfc10 for PACODE), read at runtime. */
#include "eagl/eagl.h"
#include "exe.h"
#include <string.h>

static EaglMatrix identity_matrix = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};   /* 0x5bc660 */
static const EaglMatrix *xf_matrix = &identity_matrix;                                     /* 0x5dea58 */
static float default_nearfar[2] = {1.0f, 65535.0f};                                        /* 0x5dea50 */
static const float *nearfar_ptr = default_nearfar;                                         /* 0x5dea5c */
static uint32_t near_bits = 0x3f800000u, far_bits = 0x477fff00u;                           /* 0x5dea70, 0x5dea74 */
static uint32_t *code_out_base;                                                            /* 0x69e0d0 */
static uint32_t *code_out;                                                                 /* 0x69e0b4 */
static uint32_t guard_add = 0x800000u;                                                     /* 0x5dea60: 2^shift in exponent units */
static int src_stride = 16, dst_stride = 16, code_stride = 4;                              /* 0x5dea64, 0x5dea68, 0x5dea6c */
static uint32_t cached_flags = 0xffffffffu;                                                /* 0x5dea78 */
static uint32_t active_flags;                                                              /* 0x69e0b0 */

/* The code tables (32 words each) and whether they were read. */
static uint32_t tab_c[32], tab_cgb[32], tab_pa[32], tab_pagb[32];
static bool tables_loaded;

static void load_tables(void)
{
    if (tables_loaded) return;
    for (int i = 0; i < 32; i++) {
        tab_c[i] = exe_u32(0x5de94c + 4 * i);
        tab_cgb[i] = exe_u32(0x5de9cc + 4 * i);
        tab_pa[i] = exe_u32(0x5dfb90 + 4 * i);
        tab_pagb[i] = exe_u32(0x5dfc10 + 4 * i);
    }
    tables_loaded = exe_loaded();
}

static uint32_t fbits(float f) { union { float f; uint32_t u; } c = {f}; return c.u; }

const EaglMatrix *EAGL_SetTransformMatrix(const EaglMatrix *m)
{
    const EaglMatrix *old = xf_matrix;
    xf_matrix = m ? m : &identity_matrix;
    return old;
}

const float *EAGL_SetNearFar(const float *nf)
{
    const float *old = nearfar_ptr;
    if (!nf) nf = default_nearfar;
    nearfar_ptr = nf;
    near_bits = fbits(nf[0]);
    far_bits = fbits(nf[1]);
    return old;
}

uint32_t *EAGL_SetCodeOutput(uint32_t *codes)
{
    uint32_t *old = code_out_base;
    code_out_base = codes;
    cached_flags = 0xffffffffu;
    return old;
}

int EAGL_SetGuardBand(int shift)
{
    int old = (int)(guard_add >> 23);
    if (shift < 0) guard_add = 0;
    else if (shift > 16) guard_add = 0x8000000u;
    else guard_add = (uint32_t)shift << 23;
    cached_flags = 0xffffffffu;
    return old;
}

int EAGL_SetSourceStride(int b) { int o = src_stride; src_stride = b ? b : 16; return o; }
int EAGL_SetDestStride(int b) { int o = dst_stride; dst_stride = b ? b : 16; return o; }
int EAGL_SetCodeStride(int b) { int o = code_stride; code_stride = b ? b : 4; return o; }

/* ---- near / far bits (both families): 1 = nearer than near (or z negative), 2 = farther than far ---- */
static uint32_t nearfar_code(uint32_t z)
{
    return (((z - near_bits) | z) >> 1 | ((far_bits - z) & ~z & 0xbfffffffu)) >> 30;
}

static void emit_code(uint32_t code)
{
    *code_out = code;
    code_out = (uint32_t *)((uint8_t *)code_out + code_stride);
}

/* ---- the C code routines (0x538010..0x538240), on the last point's bits ---- */
static uint32_t cx, cy, cz;   /* 0x69e0c0.. as bits */

static uint32_t cidx(uint32_t zabs)
{
    return ((((((cy & 0xbfffffffu) | (cz >> 1)) >> 1 | (cx & 0x9fffffffu)) >> 1 |
              ((zabs - (cy & 0x7fffffffu)) & 0x8fffffffu)) >> 1 |
             ((zabs - (cx & 0x7fffffffu)) & 0x87ffffffu)) >> 27);
}

/* Index of the guard band variant: the sign part, and |z| - |x|, |z| - |y| of a given |z|. */
static uint32_t cidx_gb_signs(void) { return (((cy & 0xbfffffffu) | (cz >> 1)) >> 1 | (cx & 0x9fffffffu)) >> 29; }
static uint32_t cidx_gb(uint32_t zabs)
{
    return (((zabs - (cy & 0x7fffffffu)) >> 1 & 0x40000000u) | ((zabs - (cx & 0x7fffffffu)) & 0x87ffffffu)) >> 27;
}

static void code_none(void) {}                                                        /* 0x538000 */
static void code_persp(void) { emit_code(tab_c[cidx(cz & 0x7fffffffu)] | nearfar_code(cz)); } /* 0x538010 */
static void code_persp_gb(void)                                                       /* 0x5380c0 */
{
    uint32_t za = cz & 0x7fffffffu, s = cidx_gb_signs();
    emit_code(tab_cgb[cidx_gb(za) | s] << 8 | tab_cgb[cidx_gb(guard_add + za) | s] | nearfar_code(cz));
}
static void code_ortho(void)                                                          /* 0x5381a0 */
{
    uint32_t i = (((((cy >> 1) & 0x40000000u) | (cx & 0x9fffffffu)) >> 1 |
                   ((0x3f800000u - (cy & 0x7fffffffu)) & 0x8fffffffu)) >> 1 |
                  ((0x3f800000u - (cx & 0x7fffffffu)) & 0x87ffffffu)) >> 27;
    emit_code(tab_c[i] | nearfar_code(cz));
}
static void code_ortho_gb(void)                                                       /* 0x538240 */
{
    uint32_t s = (((cy >> 1) & 0x40000000u) | (cx & 0x9fffffffu)) >> 29;
    emit_code(tab_cgb[cidx_gb(guard_add + 0x3f800000u) | s] | tab_cgb[cidx_gb(0x3f800000u) | s] << 8 |
              nearfar_code(cz));
}
static void (*const c_codes[8])(void) = {                                             /* 0x5de92c */
    code_none, code_persp, code_persp_gb, code_persp_gb, code_ortho, code_ortho, code_ortho_gb, code_ortho_gb,
};

/* ---- point transforms (0x572f00..0x5730a0) ---- */
static void xf_xyz4(const float *p, const float *m, float *o)         /* 0x572f00: xyz, w = 1, 4 out */
{
    o[0] = p[2] * m[8] + p[1] * m[4] + p[0] * m[0] + m[12];
    o[1] = p[2] * m[9] + p[1] * m[5] + p[0] * m[1] + m[13];
    o[2] = p[2] * m[10] + p[1] * m[6] + p[0] * m[2] + m[14];
    o[3] = p[2] * m[11] + p[1] * m[7] + p[0] * m[3] + m[15];
}
static void xf_xyz3(const float *p, const float *m, float *o)         /* 0x572f90 */
{
    o[0] = p[2] * m[8] + p[1] * m[4] + p[0] * m[0] + m[12];
    o[1] = p[2] * m[9] + p[1] * m[5] + p[0] * m[1] + m[13];
    o[2] = p[2] * m[10] + p[1] * m[6] + p[0] * m[2] + m[14];
}
static void xf_xyzw4(const float *p, const float *m, float *o)        /* 0x573000 */
{
    o[0] = p[3] * m[12] + p[2] * m[8] + p[1] * m[4] + p[0] * m[0];
    o[1] = p[3] * m[13] + p[2] * m[9] + p[1] * m[5] + p[0] * m[1];
    o[2] = p[3] * m[14] + p[2] * m[10] + p[1] * m[6] + p[0] * m[2];
    o[3] = p[3] * m[15] + p[2] * m[11] + p[1] * m[7] + p[0] * m[3];
}
static void xf_xyzw3(const float *p, const float *m, float *o)        /* 0x5730a0 */
{
    o[0] = p[3] * m[12] + p[2] * m[8] + p[1] * m[4] + p[0] * m[0];
    o[1] = p[3] * m[13] + p[2] * m[9] + p[1] * m[5] + p[0] * m[1];
    o[2] = p[3] * m[14] + p[2] * m[10] + p[1] * m[6] + p[0] * m[2];
}

static void store_bits(const float *v)
{
    cx = fbits(v[0]);
    cy = fbits(v[1]);
    cz = fbits(v[2]);
}

#define NEXT(p, stride) ((p) = (void *)((uint8_t *)(p) + (stride)))

static void run_points4(uint32_t f, int n, const float *s, float *d)            /* 0x538470 */
{
    for (; n > 0; n--, NEXT(s, src_stride), NEXT(d, dst_stride)) xf_xyzw4(s, xf_matrix->m, d);
}
static void run_points3(uint32_t f, int n, const float *s, float *d)            /* 0x538430 */
{
    for (; n > 0; n--, NEXT(s, src_stride), NEXT(d, dst_stride)) xf_xyz4(s, xf_matrix->m, d);
}
static void run_w1(uint32_t f, int n, const float *s, float *d, bool xyz)       /* 0x538540 / 0x5384b0 */
{
    void (*codes)(void) = c_codes[f >> 8 & 7];
    code_out = code_out_base;
    for (; n > 0; n--, NEXT(s, src_stride), NEXT(d, dst_stride)) {
        float v[3];
        if (xyz) xf_xyz3(s, xf_matrix->m, v);
        else xf_xyzw3(s, xf_matrix->m, v);
        store_bits(v);
        d[0] = v[0];
        d[1] = v[1];
        d[2] = v[2];
        d[3] = 1.0f;
        codes();
    }
}

static void run_c(uint32_t f, int n, const float *s, float *d)                  /* 0x5385d0 */
{
    void (*codes)(void) = c_codes[f >> 8 & 7];
    bool any_w = (f & 0x1c) != 0;
    code_out = code_out_base;
    for (; n > 0; n--, NEXT(s, src_stride), NEXT(d, dst_stride)) {
        float v[4];
        if (!(f & EAGL_TF_MATRIX)) {
            v[0] = s[0];
            v[1] = s[1];
            v[2] = s[2];
            v[3] = f & EAGL_TF_XYZ ? 1.0f : s[3];
        } else if (!any_w) {
            if (f & EAGL_TF_XYZ) xf_xyz4(s, xf_matrix->m, v);
            else xf_xyzw4(s, xf_matrix->m, v);
        } else {
            if (f & EAGL_TF_XYZ) xf_xyz3(s, xf_matrix->m, v);
            else xf_xyzw3(s, xf_matrix->m, v);
        }
        store_bits(v);
        d[0] = v[0];
        d[1] = v[1];
        d[2] = v[2];
        if (!any_w) d[3] = v[3];
        else if (f & EAGL_TF_RHW) d[3] = 1.0f / v[2];
        else if (f & EAGL_TF_W1) d[3] = 1.0f;
        codes();
    }
}

/* ---- PACODE 0x5b0730 with its code routines 0x5b05c0 / 0x5b0660 (selected by (flags >> 8) & 3) ---- */

static uint32_t pa_signs(uint32_t x, uint32_t y, uint32_t z) { return (x >> 29 & 4) | (y >> 30 & 2) | z >> 31; }
static uint32_t pa_planes(uint32_t zabs, uint32_t xabs, uint32_t yabs)
{
    return ((zabs - xabs) >> 27 & 0x10) | ((zabs - yabs) & 0x80000000u) >> 28;
}

static uint32_t pa_code(int which, uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t s = pa_signs(x, y, z), xa = x & 0x7fffffffu, ya = y & 0x7fffffffu, za = z & 0x7fffffffu;
    if (which == 1) return tab_pa[pa_planes(za, xa, ya) | s] | nearfar_code(z);           /* 0x5b05c0 */
    uint32_t inner = pa_planes(za, xa, ya) | s, outer = pa_planes(guard_add + za, xa, ya) | s;  /* 0x5b0660 */
    return tab_pagb[inner] << 8 | tab_pagb[outer] | nearfar_code(z);
}

static void run_pacode(uint32_t f, int n, const float *s, float *d)
{
    /* The routine sets the x87 to 24-bit precision itself: float arithmetic. Sums run x, then y, then z
       terms, then the translation (or the w terms), as its fadd sequence does. */
    int which = (int)(f >> 8 & 3);
    const float *m = xf_matrix->m;
    uint32_t *codes = code_out_base;
    for (; n > 0; n--, NEXT(s, src_stride), NEXT(d, dst_stride)) {
        float x, y, z;
        if (f & EAGL_TF_XYZ) {
            x = s[0] * m[0] + s[1] * m[4] + s[2] * m[8] + m[12];
            y = s[0] * m[1] + s[1] * m[5] + s[2] * m[9] + m[13];
            z = s[0] * m[2] + s[1] * m[6] + s[2] * m[10] + m[14];
        } else {
            x = s[0] * m[0] + s[1] * m[4] + s[2] * m[8] + s[3] * m[12];
            y = s[0] * m[1] + s[1] * m[5] + s[2] * m[9] + s[3] * m[13];
            z = s[0] * m[2] + s[1] * m[6] + s[2] * m[10] + s[3] * m[14];
        }
        d[0] = x;
        d[1] = y;
        d[2] = z;
        d[3] = 1.0f / z;
        /* Table entry 0 has no routine (the original would call address 0): only 1..3 are selected. */
        if (which) {
            *codes = pa_code(which, fbits(x), fbits(y), fbits(z));
            codes = (uint32_t *)((uint8_t *)codes + code_stride);
        }
    }
}

uint32_t EAGL_Transform(uint32_t flags, int n, const void *src, void *dst)
{
    load_tables();
    if (flags != cached_flags) {
        active_flags = flags & 0x1079f;
        if (!code_out_base) active_flags = (active_flags & 0xffff0000u) | (active_flags & 0xffu);
        else if (guard_add == 0 && (flags & 0x200)) active_flags = (flags & 0x1059f) | 0x100;
        cached_flags = flags;
    }
    uint32_t f = active_flags;
    if ((f & 0xffff) == 1) run_points4(f, n, src, dst);
    else if ((f & 0xffff) == 3) run_points3(f, n, src, dst);
    else if ((f & 0xff) == 0x11) run_w1(f, n, src, dst, false);
    else if ((f & 0xff) == 0x13) run_w1(f, n, src, dst, true);
    else if ((f & 0x89) == 9 && (f & 0x300) && !(f & 0x400)) run_pacode(f, n, src, dst);
    else run_c(f, n, src, dst);
    return f;
}
