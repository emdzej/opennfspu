/* THRASH, ported from Drivers/dx7z.dll ("DX7 3rash", \3rash\dx7\*.c, built 2000-03-01): the state
   machine, the vertex batcher with its software culling, the texture records, windows and locks. The
   Direct3D 7 device underneath is thrash_dev.c. See thrash.h and docs/re/thrash.md.

   The card the driver reports is a fixed profile (a GeForce 256-class DX7 part: two texture stages,
   stencil, table fog, no W-buffer), since every capability is available on GLES 3. Where the DLL's
   answer depends on the card, the profile's value is used and noted. */
#include "render/thrash_internal.h"
#include "platform.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the card (caps 0x60033f40) */

/* The 47 capability words the DLL fills from the device caps (DX7_createD3D) and setstate 0x193 copies
   out, in their order. */
enum {
    CAP_HWTNL, CAP_ZDEPTH, CAP_RENDERDEPTH, CAP_3, CAP_ZVIDMEM, CAP_ZBUFFER, CAP_RGB565, CAP_7,
    CAP_DITHER, CAP_NOVSYNC, CAP_WBUFFER, CAP_FOGTABLE, CAP_WINDOWED, CAP_TRILINEAR, CAP_14,
    CAP_PERSPECTIVE, CAP_16, CAP_17, CAP_TEXALPHA, CAP_MODULATEALPHA, CAP_20, CAP_ANTIALIAS,
    CAP_SPECULARSHADE, CAP_ANISOTROPY, CAP_GAMMA, CAP_SPECULAR, CAP_STENCIL, CAP_27, CAP_28,
    CAP_ADDRESSUV, CAP_LODBIAS, CAP_TEXWMIN, CAP_TEXWMAX, CAP_TEXHMIN, CAP_TEXHMAX, CAP_TEXWMULT,
    CAP_TEXPOW2W, CAP_TEXHMULT, CAP_TEXPOW2H, CAP_NUMSTAGES, CAP_TEXSQUARE, CAP_WIREFRAME,
    CAP_GUARDLEFT, CAP_GUARDRIGHT, CAP_GUARDTOP, CAP_GUARDBOTTOM, CAP_MAXREPEAT, CAP_COUNT
};
static uint32_t caps[CAP_COUNT];

static void caps_init(void)
{
    static const struct { int i; uint32_t v; } on[] = {
        {CAP_HWTNL, 1}, {CAP_ZDEPTH, 24}, {CAP_RENDERDEPTH, 0x500}, {CAP_ZVIDMEM, 1}, {CAP_ZBUFFER, 1},
        {CAP_RGB565, 1}, {CAP_DITHER, 1}, {CAP_NOVSYNC, 1}, {CAP_FOGTABLE, 1}, {CAP_WINDOWED, 1},
        {CAP_TRILINEAR, 1}, {CAP_PERSPECTIVE, 1}, {CAP_16, 1}, {CAP_17, 1}, {CAP_TEXALPHA, 1},
        {CAP_MODULATEALPHA, 1}, {CAP_20, 1}, {CAP_SPECULARSHADE, 1}, {CAP_ANISOTROPY, 1}, {CAP_GAMMA, 1},
        {CAP_SPECULAR, 1}, {CAP_STENCIL, 1}, {CAP_27, 1}, {CAP_ADDRESSUV, 1}, {CAP_LODBIAS, 1},
        {CAP_TEXWMIN, 1}, {CAP_TEXWMAX, 2048}, {CAP_TEXHMIN, 1}, {CAP_TEXHMAX, 2048}, {CAP_TEXWMULT, 1},
        {CAP_TEXPOW2W, 1}, {CAP_TEXHMULT, 1}, {CAP_TEXPOW2H, 1}, {CAP_NUMSTAGES, DEV_STAGES},
        {CAP_WIREFRAME, 1},
    };
    memset(caps, 0, sizeof caps);
    for (size_t i = 0; i < sizeof on / sizeof *on; i++) caps[on[i].i] = on[i].v;
    caps[CAP_GUARDLEFT] = (uint32_t)THRASH_f2u(-2048.0f);
    caps[CAP_GUARDRIGHT] = (uint32_t)THRASH_f2u(2047.0f);
    caps[CAP_GUARDTOP] = (uint32_t)THRASH_f2u(-2048.0f);
    caps[CAP_GUARDBOTTOM] = (uint32_t)THRASH_f2u(2047.0f);
    caps[CAP_MAXREPEAT] = (uint32_t)THRASH_f2u(8192.0f);
}

enum { CARD_SUBTYPE = 0x12 };   /* FUN_6000a040: PCI 10de:0100..0103 (GeForce 256) */

static float u2f(uintptr_t u) { union { uint32_t u; float f; } c = {(uint32_t)u}; return c.f; }

/* ---------------------------------------------------------------- driver state (dx7z globals) */

enum { WIN_NONE, WIN_FRONT, WIN_BACK, WIN_ZBUFFER };

static bool device_open;              /* DAT_60031f90 (the D3D device) */
static bool in_scene;                 /* DAT_600216ec */
static int window;                    /* DAT_60021b8c */
static bool locked;                   /* DAT_600216f0 */
static int lock_window;               /* DAT_600216f4 */
static bool lock_write;
static ThrashSurface surf;            /* 0x600216d8 */
static uint8_t *lock_pixels;
static int clip_x, clip_y, clip_w, clip_h;   /* DAT_600216f8..704 */
static int mode_w = 640, mode_h = 480, mode_bpp = 16;   /* DAT_60033f20, DAT_60033df4, DAT_60031f6c */

static uint32_t cull = 0x80000000u;   /* DAT_60031fec: 1 = none, else the culled sign bit */
static int gouraud = 1;               /* DAT_600180fc: 0 = flat (the batcher whitens the diffuse) */
static float depth_bias;              /* _DAT_60021d54 */
static uint32_t zfunc = 4;            /* DAT_600180e4 */
static uint32_t alphafunc = 5;        /* DAT_600180e8 */
static uint32_t fog_colour = 0xff0000;/* DAT_600180f4 */
static uint32_t fog_density = 0x3f800000;  /* DAT_600180f8 (float bits) */
static uint32_t fog_start = 0, fog_end = 0x3f800000;   /* DAT_60021d44, DAT_600180ec: never set */
static int fog_custom;                /* DAT_60021d4c: the custom table drives the specular alpha */
static uint32_t fog_mode;             /* DAT_60021d48: 0, 2, 4, 8, or 0x10 (custom table) */
static uint8_t fog_table[256];        /* 0x60021c38 */
static uint32_t background = 0xffffffffu;  /* DAT_60018128 (also the material colour) */
static uint32_t clear_z = 0x3f800000; /* DAT_600180f0 */
static int windowed;                  /* DAT_60021b48 */
static uintptr_t hooks[8];            /* DAT_60054640: [5] is called around locks */
static uintptr_t sprite_stride = 32;  /* DAT_60018138 */
static uintptr_t version = 0x6b;      /* DAT_60018134 */
static uintptr_t misc[16];            /* values FUN_6000a430 only stores */
/* Kept as the DLL keeps them, without an effect here: the window handle (DAT_60054660), the Flip and
   Blt flags of the vsync state (DAT_600180dc / DAT_600180e0), the texture memory choice (DAT_60018104),
   the gamma value, the texture count (DAT_60031a74). */
static struct {
    uintptr_t hwnd;
    uint32_t flip_flags, blit_flags, texmem;
    float gamma;
    int ntextures;
} kept = {0, 1, 0x1000000, 1, 1.0f, 0};
static void (*state_callback)(uint32_t state, uintptr_t value);   /* DAT_60031fe0 (setstate 0x27) */

/* The batcher (dx7wrap.c): pre-transformed vertices collected into one buffer of 0x1fa0 bytes and
   drawn with DrawIndexedPrimitive when the primitive type changes, it fills, or any state changes. */
enum { BATCH_BYTES = 0x1fa0 };
static uint8_t batch[BATCH_BYTES + 64];   /* PTR_DAT_60018118 */
static uint16_t batch_idx[1024];          /* PTR_DAT_60018124 */
static int batch_nv, batch_ni;            /* DAT_60031a64, DAT_60031a68 */
static int batch_cap = BATCH_BYTES / 32;  /* DAT_60018110 */
static int batch_prim = PRIM_TRIANGLELIST;/* DAT_60018114 */
static uint32_t stride = 32;              /* DAT_6001810c */
static uint32_t fvf = 0x1c4;              /* DAT_60018100 */

/* Textures (dx7tex.c). */
static DevTexture *textures;          /* DAT_60031ffc */
static int texture_oom;               /* DAT_60031a78 */
static DevTexture *first_opaque;      /* DAT_6003401c */

/* Recorded states (DAT_60054680): getstate's answers, 8 stages per id. */
enum { STATE_SLOTS = 160 };
static intptr_t recorded[STATE_SLOTS][8];

static void error(const char *msg)    /* FUN_6000b1b0: the DLL's abort-message box */
{
    char m[256];
    snprintf(m, sizeof m, "THRASH: %s", msg);
    plat_log(m);
}

/* ---------------------------------------------------------------- state record */

/* FUN_60001c30: the id -> slot map over the recorded ranges 0-0x46, 0x64-0x6e, 0xc8-0xd7, 0x12c-0x13b,
   0x190-0x19f, 0x2bc-0x2cb. Each range starts where the previous one's last id was (the running sum
   adds hi - lo, not hi - lo + 1): neighbouring ranges share a slot, as in the original. */
static int state_slot(int id)
{
    static const int ranges[6][2] = {{0, 0x46}, {0x64, 0x6e}, {0xc8, 0xd7}, {0x12c, 0x13b}, {0x190, 0x19f}, {0x2bc, 0x2cb}};
    int base = 0;
    for (int i = 0; i < 6; i++) {
        if (id >= ranges[i][0] && id <= ranges[i][1]) return base + id - ranges[i][0];
        base += ranges[i][1] - ranges[i][0];
    }
    return -1;
}

/* The stage in bits 16-18 (only exact values 0x10000..0x70000 count; anything else is stage 0). */
static int state_stage(uint32_t state)
{
    uint32_t hi = state & 0xffff0000u;
    return hi <= 0x70000 && !(hi & 0xffff) ? (int)(hi >> 16) : 0;
}

/* FUN_60001c80 0x60001c80 */
static void record(uint32_t state, uintptr_t value)
{
    int slot = state_slot((int)(state & 0xffff));
    if (slot >= 0) recorded[slot][state_stage(state)] = (intptr_t)value;
    if (state_callback) state_callback(state & 0xffff, value);
}

/* _THRASH_getstate@4 0x60001b90 */
intptr_t THRASH_getstate(uint32_t state)
{
    int slot = state_slot((int)(state & 0xffff));
    return slot < 0 ? slot : recorded[slot][state_stage(state)];
}

/* ---------------------------------------------------------------- scene and batch */

/* FUN_60007e70 0x60007e70: BeginScene (unlocking a window first, with a complaint). */
static void begin_scene(void)
{
    if (locked) {
        error("D3D startrender called in while locked");
        THRASH_unlockwindow(NULL);
    }
    dev_begin_scene();
    in_scene = true;
}

static int fvf_stride(uint32_t f) { return f == 0x2c4 ? 40 : 32; }

/* FUN_60006a50 0x60006a50 */
static void batch_draw(void)
{
    if (!in_scene) begin_scene();
    dev_draw(batch_prim, batch, batch_nv, fvf_stride(fvf), batch_idx, batch_ni);
    batch_nv = batch_ni = 0;
}

/* FUN_60006a40 0x60006a40 */
static void batch_flush(void)
{
    if (batch_nv > 0) batch_draw();
}

/* FUN_60007ee0 0x60007ee0: EndScene (the depth bias resets with it). */
static bool end_scene(void)
{
    batch_flush();
    if (in_scene) {
        depth_bias = 0;
        dev_end_scene();
        in_scene = false;
    }
    return true;
}

/* FUN_60008090 / FUN_60008050: every device state change happens inside a scene, after the batch. */
static void rs(int state, uint32_t value)
{
    if (!in_scene) begin_scene();
    batch_flush();
    dev_set_render_state(state, value);
}

static void tss(int stage, int state, uint32_t value)
{
    if (!in_scene) begin_scene();
    batch_flush();
    if (stage < DEV_STAGES) dev_set_tss(stage, state, value);
}

/* One vertex into the batch (the copy loop of FUN_60006af0 and its siblings): z gets the depth bias, a
   flat-shaded vertex gets a white diffuse, and with the custom fog table the specular word becomes the
   table's fog factor in its alpha (its colour bits are lost). */
static void batch_vertex(const void *v)
{
    uint8_t *d = batch + (size_t)stride * batch_nv;
    memcpy(d, v, stride > 32 ? 40 : 32);
    float z;
    memcpy(&z, d + 8, 4);
    z += depth_bias;
    memcpy(d + 8, &z, 4);
    if (!gouraud) memset(d + 16, 0xff, 4);
    if (fog_custom == 1 && fog_mode == 0x10) {
        long i = lrintf(z * 255.0f);   /* the original indexes the table unchecked */
        uint32_t s = (uint32_t)fog_table[i < 0 ? 0 : i > 255 ? 255 : i] << 24;
        memcpy(d + 20, &s, 4);
    }
    batch_nv++;
}

static void batch_begin(int prim, int need)
{
    if (batch_prim != prim) batch_flush();
    batch_prim = prim;
    if (batch_cap - need < batch_nv) batch_flush();
}

/* The software back-face test of drawtri / drawquad / the meshes: the sign bit of the screen-space
   cross product against the cull state (1 draws everything). */
static bool visible(const void *pa, const void *pb, const void *pc)
{
    if (cull == 1) return true;
    const float *a = pa, *b = pb, *c = pc;
    float cr = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
    uint32_t bits;
    memcpy(&bits, &cr, 4);
    return (bits & 0x80000000u) != cull;
}

/* FUN_60006af0 0x60006af0 */
static void batch_tri(const void *a, const void *b, const void *c)
{
    batch_begin(PRIM_TRIANGLELIST, 3);
    const void *v[3] = {a, b, c};
    for (int i = 0; i < 3; i++) {
        batch_idx[batch_ni++] = (uint16_t)batch_nv;
        batch_vertex(v[i]);
    }
}

/* FUN_60007520 0x60007520 */
static void batch_quad(const void *a, const void *b, const void *c, const void *d)
{
    batch_begin(PRIM_TRIANGLELIST, 4);
    int base = batch_nv;
    batch_vertex(a);
    batch_vertex(b);
    batch_vertex(c);
    batch_vertex(d);
    static const uint8_t order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) batch_idx[batch_ni++] = (uint16_t)(base + order[i]);
}

/* FUN_60007980 0x60007980 */
static void batch_line(const void *a, const void *b)
{
    batch_begin(PRIM_LINELIST, 2);
    batch_idx[batch_ni++] = (uint16_t)batch_nv;
    batch_vertex(a);
    batch_idx[batch_ni++] = (uint16_t)batch_nv;
    batch_vertex(b);
}

/* FUN_60007880 / FUN_60007900: strips and fans skip the batch: the indices go straight to the device
   (no culling, bias, flat white or fog table), the vertex count is the highest index + 1. */
static void direct_indexed(int prim, const void *verts, int nidx, const int *idx)
{
    static uint16_t ix[2048];
    int maxi = 0;
    batch_flush();
    if (nidx > (int)(sizeof ix / sizeof *ix)) nidx = (int)(sizeof ix / sizeof *ix);
    for (int i = 0; i < nidx; i++) {
        ix[i] = (uint16_t)idx[i];
        if (maxi < idx[i]) maxi = idx[i];
    }
    if (!in_scene) begin_scene();
    dev_draw(prim, verts, maxi + 1, fvf_stride(fvf), ix, nidx);
}

/* ---------------------------------------------------------------- draw entry points */

void THRASH_drawtri(const void *a, const void *b, const void *c)
{
    if (visible(a, b, c)) batch_tri(a, b, c);
}

/* FUN_60006da0 0x60006da0: three ints per triangle, vertices at the current stride. */
void THRASH_drawtrimesh(int count, const void *verts, const int *idx)
{
    const uint8_t *v = verts;
    if (batch_prim != PRIM_TRIANGLELIST) batch_flush();
    batch_prim = PRIM_TRIANGLELIST;
    for (int i = 0; i < count; i++, idx += 3) {
        if (batch_cap - 3 < batch_nv) batch_flush();
        const void *a = v + (size_t)idx[0] * stride, *b = v + (size_t)idx[1] * stride, *c = v + (size_t)idx[2] * stride;
        if (!visible(a, b, c)) continue;
        for (int k = 0; k < 3; k++) {
            batch_idx[batch_ni++] = (uint16_t)batch_nv;
            batch_vertex(v + (size_t)idx[k] * stride);
        }
    }
}

void THRASH_drawtristrip(int count, const void *verts, const int *idx) { direct_indexed(PRIM_TRIANGLESTRIP, verts, count + 2, idx); }
void THRASH_drawtrifan(int count, const void *verts, const int *idx) { direct_indexed(PRIM_TRIANGLEFAN, verts, count + 2, idx); }

void THRASH_drawquad(const void *a, const void *b, const void *c, const void *d)
{
    if (visible(a, b, c)) batch_quad(a, b, c, d);
}

/* FUN_600070f0 0x600070f0: four ints per quad, culled on the first three. */
void THRASH_drawquadmesh(int count, const void *verts, const int *idx)
{
    const uint8_t *v = verts;
    for (int i = 0; i < count; i++, idx += 4) {
        const void *a = v + (size_t)idx[0] * stride, *b = v + (size_t)idx[1] * stride;
        const void *c = v + (size_t)idx[2] * stride, *d = v + (size_t)idx[3] * stride;
        if (visible(a, b, c)) batch_quad(a, b, c, d);
    }
}

void THRASH_drawline(const void *a, const void *b) { batch_line(a, b); }

/* FUN_60007b60 0x60007b60: two ints per line. */
void THRASH_drawlinemesh(int count, const void *verts, const int *idx)
{
    const uint8_t *v = verts;
    for (int i = 0; i < count; i++, idx += 2) batch_line(v + (size_t)idx[0] * stride, v + (size_t)idx[1] * stride);
}

/* Lines and points by index use a fixed 32-byte stride, whatever the vertex format. */
void THRASH_drawlinestrip(int count, const void *verts, const int *idx)
{
    const uint8_t *v = verts;
    for (int i = 0; i < count; i++) THRASH_drawline(v + (size_t)idx[i] * 32, v + (size_t)idx[i + 1] * 32);
}

/* FUN_60007d90 0x60007d90: points are drawn from the caller's vertices, which it adjusts in place
   (white diffuse when flat, fog table, depth bias added to z every time). */
static void draw_points(void *verts, int n)
{
    uint8_t *v = verts;
    if (!in_scene) begin_scene();
    batch_flush();
    for (int i = 0; i < n; i++) {
        uint8_t *p = v + (size_t)stride * i;
        float z;
        memcpy(&z, p + 8, 4);
        if (!gouraud) memset(p + 16, 0xff, 4);
        if (fog_custom == 1 && fog_mode == 0x10) {
            long k = lrintf(z * 255.0f);
            uint32_t s = (uint32_t)fog_table[k < 0 ? 0 : k > 255 ? 255 : k] << 24;
            memcpy(p + 20, &s, 4);
        }
        z += depth_bias;
        memcpy(p + 8, &z, 4);
    }
    dev_draw(PRIM_POINTLIST, verts, n, fvf_stride(fvf), NULL, 0);
}

void THRASH_drawpoint(const void *a) { draw_points((void *)a, 1); }

void THRASH_drawpointmesh(int count, const void *verts, const int *idx)
{
    const uint8_t *v = verts;
    for (int i = 0; i < count; i++) THRASH_drawpoint(v + (size_t)idx[i] * 32);
}

/* A screen-aligned sprite from two corners: the quad (a, (b.x, a.y), b, (a.x, b.y)) with the texture
   coordinates following. Only 32 bytes of each corner are copied (a second texture coordinate is
   left zero here; uninitialised in the original). */
void THRASH_drawsprite(const void *pa, const void *pb)
{
    float c1[10] = {0}, c3[10] = {0};
    const float *a = pa, *b = pb;
    memcpy(c1, b, 32);
    memcpy(c3, b, 32);
    c1[1] = a[1];
    c1[7] = a[7];
    c3[0] = a[0];
    c3[6] = a[6];
    THRASH_drawquad(a, c1, b, c3);
}

void THRASH_drawspritemesh(int count, const void *verts, const int *idx)
{
    const uint8_t *v = verts;
    for (int i = 0; i < count; i++, idx += 2)
        THRASH_drawsprite(v + (size_t)idx[0] * sprite_stride, v + (size_t)idx[1] * sprite_stride);
}

/* Unindexed strips and fans: triangles one by one through drawtri (culled, batched), 32-byte stride.
   The strip alternates its winding: (0,1,2), (1,3,2), (2,3,4), (3,5,4), ... */
void THRASH_drawtristrip8(int count, const void *verts)
{
    const float *v = verts;
    if (count <= 0) return;
    THRASH_drawtri(v, v + 8, v + 16);
    const float *p = v + 16, *base = v;
    while (count > 1) {
        THRASH_drawtri(p - 8, p + 8, p);
        count -= 2;
        base += 16;
        if (count < 1) return;
        THRASH_drawtri(base, p + 8, p + 16);
        p += 16;
    }
}

void THRASH_drawtrifan8(int count, const void *verts)
{
    const float *v = verts, *p = v, *q = v + 16;
    for (; count > 0; count--, q += 8) {
        p += 8;
        THRASH_drawtri(v, p, q);
    }
}

void THRASH_drawlinestrip8(int count, const void *verts)
{
    const float *v = verts;
    for (; count > 0; count--, v += 8) THRASH_drawline(v, v + 8);
}

void THRASH_drawpointstrip8(int count, const void *verts)
{
    uint8_t *v = (uint8_t *)verts;
    for (; count > 0; count--, v += 32) THRASH_drawpoint(v);
}

/* ---------------------------------------------------------------- textures */

/* Bytes per pixel by format (0x600201c8). */
static const int format_bytes[16] = {0, 1, 1, 2, 2, 3, 4, 2, 1, 2, 2, 0, 0, 0, 0, 0};
/* The format table of THRASH_about (0x60018030, filled by FUN_6000b9c0): what the card takes. */
static uint32_t texformats[16];
static const uint32_t cluts[4] = {0, 0, 0, 0};   /* 0x60018078: no clut formats */

static bool format_supported(int f)
{
    return f >= 0 && f < 16 && texformats[f] != 0;
}

/* FUN_60007f30 0x60007f30: bind; handles below 0x10 unbind (0 every stage, 1 stage 0, 2 stage 1). */
static bool bind_texture(ThrashTexture t)
{
    if (!in_scene) begin_scene();
    batch_flush();
    if (t < 0x10) {
        if (t == 0)
            for (int s = 0; s < DEV_STAGES; s++) dev_set_texture(s, NULL);
        else if (t == 1)
            dev_set_texture(0, NULL);
        else if (t == 2)
            dev_set_texture(1, NULL);
        return true;
    }
    DevTexture *d = (DevTexture *)t;
    dev_set_texture(d->stage, d);
    return true;
}

/* _THRASH_settexture@4 0x60008000: a no-op when the handle is the one recorded for state 1 (stage 0,
   whatever stage the texture belongs to). */
int THRASH_settexture(ThrashTexture t)
{
    intptr_t cur = THRASH_getstate(THRASH_STATE_TEXTURE);
    if (!device_open) return 0;
    if ((ThrashTexture)cur == t) return 1;
    if (!bind_texture(t)) return 0;
    record(THRASH_STATE_TEXTURE, t);
    return 1;
}

/* _THRASH_talloc@20 0x60008420. mipstage: low 16 bits = extra mip levels, high 16 bits = stage (0x10000
   = stage 1). The record keeps the format's low byte; 1555 and 4444 count as "with alpha" (record +0x28). */
ThrashTexture THRASH_talloc(int width, int height, int format, int clut, uint32_t mipstage)
{
    if (!device_open || texture_oom) return 0;
    DevTexture *t = calloc(1, sizeof *t);
    if (!t) return 0;
    t->width = width;
    t->height = height;
    t->format = format & 0xff;
    t->bytespp = format >= 0 && format < 16 ? format_bytes[format] : 0;
    t->clut = clut;
    if ((int32_t)mipstage > 0) {
        if ((mipstage & 0xffff0000u) == 0x10000) t->stage = 1;
        if (mipstage & 0xffff) t->levels = (int)(mipstage & 0xffff) + 1;
    }
    bool with_alpha = t->format == THRASH_TEX_ARGB1555 || t->format == THRASH_TEX_ARGB4444;
    /* FUN_6000bdd0: CreateSurface fails for a format the card hasn't got (DDERR_INVALIDPIXELFORMAT:
       no out-of-memory flag). Video memory never runs out here. */
    if (!format_supported(t->format)) {
        free(t);
        return 0;
    }
    t->next = textures;
    textures = t;
    kept.ntextures++;
    if (!first_opaque && !with_alpha) first_opaque = t;
    return (ThrashTexture)t;
}

/* _THRASH_tfree@4 0x600085a0: walks a list (DAT_60021d50) that nothing ever fills, so it never finds
   the texture: textures are only released by treset. Kept. */
int THRASH_tfree(ThrashTexture t)
{
    (void)t;
    return 0;
}

/* Level byte sizes: rows of (bits * w + 7) / 8 bytes; DXT in 4x4 blocks of 8 (DXT1) or 16 bytes. */
static size_t level_size(const DevTexture *t, int w, int h)
{
    if (t->format == THRASH_TEX_DXT1 || t->format == THRASH_TEX_DXT3)
        return (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * (t->format == THRASH_TEX_DXT1 ? 8 : 16);
    int bits = t->format == THRASH_TEX_P4 ? 4 : t->bytespp * 8;
    return (size_t)((bits * w + 7) >> 3) * (size_t)h;
}

/* FUN_6000c230 0x6000c230 (+ FUN_6000c390 for the mip levels, which follow level 0 in the data). The
   clut is 256 words 0x00RRGGBB; its top byte is dropped. Palettised pixels are kept (a later clut-only
   update re-expands them); others are converted at once. */
static bool texture_update(DevTexture *t, const void *pixels, const uint32_t *clut)
{
    if (pixels) {
        const uint8_t *p = pixels;
        if (t->format == THRASH_TEX_DXT1 || t->format == THRASH_TEX_DXT3) p += 8;
        size_t total = 0;
        int levels = t->levels > 0 ? t->levels : 1;
        for (int l = 0, w = t->width, h = t->height; l < levels; l++, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1)
            total += level_size(t, w, h);
        free((void *)t->pixels);
        uint8_t *copy = malloc(total ? total : 1);
        if (!copy) return false;
        memcpy(copy, p, total);
        t->pixels = copy;
        if (in_scene) end_scene();
    }
    if (clut && t->clut)
        for (int i = 0; i < 256; i++) t->palette[i] = clut[i] & 0xffffff;
    if (t->pixels) dev_texture_upload(t);
    return true;
}

/* _THRASH_tupdate@12 0x60008610 */
ThrashTexture THRASH_tupdate(ThrashTexture t, const void *pixels, const uint32_t *clut)
{
    if (!t) return 0;
    return texture_update((DevTexture *)t, pixels, clut) ? t : 0;
}

/* _THRASH_treset@0 0x60008640: every texture goes; the state record keeps the last handle bound. */
int THRASH_treset(void)
{
    kept.ntextures = 0;
    if (!device_open) return 0;
    bind_texture(0);
    if (in_scene) {
        THRASH_flushwindow();
        THRASH_sync(0);
        THRASH_idle();
        in_scene = false;
    }
    while (textures) {
        DevTexture *t = textures;
        textures = t->next;
        dev_texture_free(t);
        free((void *)t->pixels);
        free(t);
    }
    texture_oom = 0;
    first_opaque = NULL;
    return 1;
}

/* ---------------------------------------------------------------- windows, locks, rectangles */

/* _THRASH_window@4 0x600011b0: which surface lockwindow / readrect / clearwindow mean. 3 and 5 are the
   z-buffer; 4 and anything above 5 have no surface. */
bool THRASH_window(int n)
{
    record(0x3b, (uintptr_t)n);
    if (n == 3 || n == 5) {
        window = WIN_ZBUFFER;
        return true;
    }
    window = n == 1 ? WIN_FRONT : n == 2 ? WIN_BACK : WIN_NONE;
    return window != WIN_NONE;
}

/* _THRASH_clip@16 0x60001300: the viewport (z 0..0.99998). */
int THRASH_clip(int x0, int y0, int x1, int y1)
{
    clip_x = x0;
    clip_y = y0;
    clip_w = x1 - x0;
    clip_h = y1 - y0;
    if (in_scene) end_scene();
    dev_set_viewport(x0, y0, clip_w, clip_h);
    return 1;
}

/* _THRASH_clearwindow@0 0x60001230 / FUN_60008380: Clear on the render target over the clip rectangle
   (all of it before the first clip), colour unless the z-buffer window is selected. */
void THRASH_clearwindow(void)
{
    int x0 = clip_x, y0 = clip_y, x1 = clip_w + clip_x, y1 = clip_y + clip_h;
    int flags = window == WIN_ZBUFFER ? 0 : CLEAR_TARGET;
    if (caps[CAP_ZBUFFER] && mode_bpp) flags |= CLEAR_ZBUFFER;
    if (caps[CAP_STENCIL]) flags |= CLEAR_STENCIL;
    if (x1 == 0) {
        x0 = 0;
        y0 = 0;
        x1 = mode_w;
        y1 = mode_h;
    }
    /* No batch flush: triangles still queued are drawn after the clear, over it, as in the DLL. */
    dev_clear(x0, y0, x1, y1, flags, background, u2f(clear_z), 0);
}

/* _THRASH_flushwindow@0 0x60001270 */
bool THRASH_flushwindow(void)
{
    return end_scene();
}

/* _THRASH_pageflip@0 0x60001280 / FUN_600080d0: Flip, or a blit of the back buffer when windowed. */
void THRASH_pageflip(void)
{
    THRASH_idle();
    end_scene();
    if (locked) error("D3D pageflip called in while locked");
    dev_flip(windowed != 0);
}

/* _THRASH_sync@4 0x600012c0: 0 waits for the card (a lock and unlock), 2 for the vertical blank. Neither
   changes what is drawn. */
int THRASH_sync(int how)
{
    (void)how;
    return 0;
}

static int window_surface(int w) { return w == WIN_FRONT ? 1 : 2; }

static int surface_format(void) { return mode_bpp == 32 ? 6 : mode_bpp == 24 ? 5 : 4; }

/* FUN_60001590 0x60001590 (lockwindow's body; lockwindow passes no flags). The z-buffer can't be read
   back on GLES 3 / WebGL 2: locking it fails, as a lost surface would. */
static ThrashSurface *lock(bool read, bool write)
{
    if (window == WIN_NONE) return &surf;
    if (locked) error("D3D lock called while locked");
    if (in_scene) end_scene();
    void (*hook)(int) = (void (*)(int))hooks[5];
    if (hook) hook(1);
    if (window == WIN_ZBUFFER) {
        if (hook) hook(0);
        return NULL;
    }
    int bpp = surface_format() == 6 ? 4 : 2;
    free(lock_pixels);
    lock_pixels = calloc((size_t)mode_w * mode_h, (size_t)bpp);
    if (!lock_pixels) {
        if (hook) hook(0);
        return NULL;
    }
    if (read) dev_read_pixels(window_surface(window), 0, 0, mode_w, mode_h, lock_pixels, mode_w * bpp);
    lock_window = window;
    lock_write = write;
    surf.pixels = lock_pixels;
    surf.pitch = mode_w * bpp;
    surf.format = surface_format();
    surf.width = mode_w;
    surf.height = mode_h;
    locked = true;
    return &surf;
}

/* _THRASH_lockwindow@0 0x600013c0 */
ThrashSurface *THRASH_lockwindow(void) { return lock(true, true); }

/* _THRASH_unlockwindow@4 0x60001790 */
int THRASH_unlockwindow(ThrashSurface *s)
{
    (void)s;
    if (locked && lock_window != WIN_NONE) {
        if (lock_write) dev_write_pixels(window_surface(lock_window), 0, 0, mode_w, mode_h, lock_pixels, surf.pitch);
        lock_window = WIN_NONE;
        locked = false;
        void (*hook)(int) = (void (*)(int))hooks[5];
        if (hook) hook(0);
    }
    return 1;
}

/* _THRASH_readrect@20 0x60003990: rows of w pixels in the surface's format (4 bytes for format 6,
   3 for 5, else 2) at (x, y). Read straight from the surface; the same pixels a full lock gives. */
int THRASH_readrect(int x, int y, int w, int h, void *dst)
{
    if (window == WIN_NONE || window == WIN_ZBUFFER) return 0;
    if (in_scene) end_scene();
    int f = surface_format(), bpp = f == 6 ? 4 : f == 5 ? 3 : 2;
    dev_read_pixels(window_surface(window), x, y, w, h, dst, w * bpp);
    return 1;
}

/* _THRASH_writerect@20 0x60003a30: like readrect, but 2 bytes a pixel unless the format is 5 (then 4),
   so in 32-bit modes each row copies half its bytes. Kept: the 16-bit case writes the rectangle, the
   others go through a whole lock. */
int THRASH_writerect(int x, int y, int w, int h, const void *src)
{
    int f = surface_format(), bpp = f != 5 ? 2 : 4;
    if (window == WIN_NONE || window == WIN_ZBUFFER) return 0;
    if (f == 4) {
        if (in_scene) end_scene();
        dev_write_pixels(window_surface(window), x, y, w, h, src, w * bpp);
        return 1;
    }
    ThrashSurface *s = lock(true, true);
    if (!s) return 0;
    const uint8_t *p = src;
    for (int r = 0; r < h; r++)
        if (y + r >= 0 && y + r < s->height)
            memcpy((uint8_t *)s->pixels + (size_t)(y + r) * s->pitch + (size_t)x * bpp, p + (size_t)r * w * bpp, (size_t)w * bpp);
    return THRASH_unlockwindow(s);
}

/* ---------------------------------------------------------------- about, init, modes */

static ThrashMode modes[6];
static ThrashAbout about;
static const char author[] = "AUTHOR: Daniel Kennett, Wednesday 07:51PM Mar 01, 2000";

/* FUN_6000b9c0 0x6000b9c0: the texture formats the card takes (1 each; 1555 would be 5 if only 555 were
   there; 888 never). */
static void texformats_init(void)
{
    memset(texformats, 0, sizeof texformats);
    static const int ok[] = {THRASH_TEX_P4, THRASH_TEX_P8, THRASH_TEX_ARGB1555, THRASH_TEX_RGB565,
                             THRASH_TEX_ARGB8888, THRASH_TEX_ARGB4444, THRASH_TEX_DXT1, THRASH_TEX_DXT3};
    for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) texformats[ok[i]] = 1;
}

/* The mode list (0x600211d8): the five standard modes first (FUN_60001fc0 sorts them there). */
static void modes_init(void)
{
    static const int wh[5][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1600, 1200}};
    memset(modes, 0, sizeof modes);
    for (int i = 0; i < 5; i++) {
        modes[i + 1].width = wh[i][0];
        modes[i + 1].height = wh[i][1];
        modes[i + 1].bpp = 32;
        modes[i + 1].unknown3 = 4;
        modes[i + 1].unknown4 = modes[i + 1].unknown5 = modes[i + 1].unknown6 = 1;
    }
}

/* _THRASH_about@0 0x60001000 */
const ThrashAbout *THRASH_about(void)
{
    about.signature = 0x44334437;
    about.size = 0xd0;
    about.version = (uint32_t)version;
    if (!caps[CAP_TEXWMIN]) {          /* no device yet */
        about.texwidthmin = about.texheightmin = 8;
        about.texwidthmax = about.texheightmax = 256;
        about.texwidthmultiple = about.texheightmultiple = 1;
        about.flags |= THRASH_ABOUT_SQUARE | THRASH_ABOUT_POW2W | THRASH_ABOUT_POW2H;
        about.numstages = 1;
    } else {
        about.texwidthmin = (int32_t)caps[CAP_TEXWMIN];
        about.texwidthmax = (int32_t)caps[CAP_TEXWMAX];
        about.texheightmin = (int32_t)caps[CAP_TEXHMIN];
        about.texheightmax = (int32_t)caps[CAP_TEXHMAX];
        about.texwidthmultiple = (int32_t)caps[CAP_TEXWMULT];
        about.texheightmultiple = (int32_t)caps[CAP_TEXHMULT];
        about.flags = (caps[CAP_TEXSQUARE] & 1) << 1 | (caps[CAP_TEXPOW2H] & 1) << 3 |
                      (caps[CAP_TEXPOW2W] & 1) << 2 | (about.flags & 0xfffffff1u);
        about.numstages = (int32_t)caps[CAP_NUMSTAGES];
    }
    about.author = author;
    about.flags = (about.flags & 0xffffff2fu) | THRASH_ABOUT_LINEWIDTH | THRASH_ABOUT_WINDOWED;   /* not software */
    about.clipalign = 0;
    about.dxversion = 7;
    about.ntexformats = 0x10;
    about.texformats = texformats;
    about.ncluts = 4;
    about.cluts = cluts;
    about.modes = modes;
    about.nmodes = 5;
    memcpy(about.name, "DX7 3rash", 10);
    about.subtype = CARD_SUBTYPE;
    about.textureramsize = 32 << 20;
    snprintf(about.device, sizeof about.device, "OpenGL ES 3.0 (OpenNFSPU THRASH)");
    return &about;
}

/* _THRASH_init@0 0x60003450: enumerates the displays (one here); the count. The caps arrive with the
   device (setvideomode); before that THRASH_about answers its defaults. */
int THRASH_init(void)
{
    texformats_init();
    modes_init();
    return 1;
}

/* _THRASH_is@0 0x600038b0: 0x55 when DirectDraw 7 is there and the desktop has 8 bits or more. */
int THRASH_is(void) { return 0x55; }

/* _THRASH_selectdisplay@4 0x60001ee0: one display; restores the old device first. */
int THRASH_selectdisplay(int display)
{
    (void)display;
    if (device_open) THRASH_restore();
    return 1;
}

/* _THRASH_restore@0 0x600034a0 */
int THRASH_restore(void)
{
    if (!device_open) return 1;
    if (locked) THRASH_unlockwindow(NULL);
    THRASH_treset();
    dev_close();
    device_open = false;
    in_scene = false;
    return 1;
}

void THRASH_idle(void) {}

/* FUN_6000a630 0x6000a630: the states every mode starts with (each through setstate, so they are
   recorded too). The z-buffer asks for a W-buffer first when bpp >= 1, falling back to z. */
static void default_states(int backbuffers, int bpp_arg)
{
    THRASH_setstate(0x130, 0);
    THRASH_setstate(THRASH_STATE_TEXTURE, 0);
    THRASH_setstate(THRASH_STATE_CULL, 1);
    THRASH_setstate(THRASH_STATE_FILTER, 1);
    THRASH_setstate(THRASH_STATE_SHADE, 1);
    THRASH_setstate(THRASH_STATE_BLEND, 2);
    THRASH_setstate(THRASH_STATE_ALPHATEST, 0x10);
    THRASH_setstate(THRASH_STATE_MIPMAP, 1);
    THRASH_setstate(THRASH_STATE_BACKGROUND, 0);
    THRASH_setstate(0x0c, 0);
    THRASH_setstate(THRASH_STATE_DITHER, 1);
    THRASH_setstate(THRASH_STATE_FOGTABLE, 0);
    THRASH_setstate(THRASH_STATE_FOGMODE, 0);
    THRASH_setstate(THRASH_STATE_FOGDENSITY, 0);
    THRASH_setstate(0x16, 0);
    THRASH_setstate(0x17, 0x3f800000);
    THRASH_setstate(THRASH_STATE_FOGCOLOUR, 0xffffffffu);
    THRASH_setstate(THRASH_STATE_TRANSPARENCY, 0);
    THRASH_setstate(THRASH_STATE_ADDRESS, 0);
    THRASH_setstate(THRASH_STATE_GAMMA, 0x3f800000);
    THRASH_setstate(THRASH_STATE_DEPTHBIAS, 0);
    THRASH_setstate(THRASH_STATE_LODBIAS, 0);
    THRASH_setstate(0x6d, (uintptr_t)(backbuffers - 2 < 0 ? 0 : backbuffers - 2));
    THRASH_setstate(THRASH_STATE_VERTEXFORMAT, 0);
    THRASH_setstate(THRASH_STATE_TEXBLEND, 0);
    THRASH_setstate(THRASH_STAGE(1) | THRASH_STATE_TEXBLEND, 0);
    THRASH_setstate(0x2b, 1);
    THRASH_setstate(0x11, 1);
    THRASH_setstate(0x10, 1);
    THRASH_setstate(THRASH_STATE_STENCIL, 0);
    THRASH_setstate(0x3a, 0);
    THRASH_setstate(0x131, 0);
    THRASH_setstate(0x3b, 0);
    THRASH_setstate(THRASH_STATE_CLEARZ, 0x3f800000);
    THRASH_setstate(THRASH_STATE_VSYNC, 1);
    THRASH_setstate(0x3d, 0);
    uintptr_t z = bpp_arg >= 1 ? 2 : 0;
    if (!THRASH_setstate(THRASH_STATE_ZBUFFER, z) && z == 2) THRASH_setstate(THRASH_STATE_ZBUFFER, 1);
    THRASH_setstate(THRASH_STATE_ZFUNC, 3);
}

/* _THRASH_setvideomode@12 0x60003510: mode = index into the about list; bpp 1 = 16, 2 = 32 (else as
   given), capped at the mode's depth. Then the defaults, the window size probe (window 1, lock, unlock,
   window 0: state 0x1d), and treset. */
bool THRASH_setvideomode(int mode, int backbuffers, int bpp)
{
    if (!modes[1].width) THRASH_init();
    int depth = bpp == 1 ? 16 : bpp == 2 ? 32 : bpp;
    if (in_scene) error("D3D Setting videomode in 3d scene !!!!");
    const ThrashMode *m = mode >= 1 && mode <= 5 ? &modes[mode] : &modes[1];
    if (m->bpp < depth) depth = 16;
    if (device_open) {
        THRASH_treset();
        dev_close();
    }
    mode_w = m->width;
    mode_h = m->height;
    mode_bpp = depth;
    device_open = dev_open(mode_w, mode_h, mode_bpp) != 0;
    if (!device_open) return false;
    caps_init();
    in_scene = false;
    batch_nv = batch_ni = 0;
    clip_x = clip_y = clip_w = clip_h = 0;
    default_states(backbuffers, bpp);
    THRASH_setstate(0x3e, misc[15] + 1);
    THRASH_window(1);
    ThrashSurface *s = THRASH_lockwindow();
    THRASH_unlockwindow(s);
    THRASH_window(0);
    static int32_t sizes[5];
    if (s) {
        sizes[1] = s->pitch;
        sizes[2] = s->format;
        sizes[3] = s->width;
        sizes[4] = s->height;
    }
    record(0x1d, (uintptr_t)sizes);
    THRASH_treset();
    return true;
}

/* ---------------------------------------------------------------- setstate */

/* FUN_600069a0 0x600069a0: the custom fog table, 256 entries interpolated from the caller's 64 (plus
   one), inverted (255 - value). */
static void build_fog_table(const uint8_t *src)
{
    for (int i = 0; i < 256; i++) {
        float f = (float)i * (1.0f / 255.0f) * 63.0f;
        int k = (int)lrintf(f);
        float frac = f - (float)k;
        uint8_t v = frac <= 0.0f ? src[k] : (uint8_t)lrintf((float)src[k] + (float)((int)src[k + 1] - (int)src[k]) * frac);
        fog_table[i] = (uint8_t)(255 - v);
    }
}

/* The texture-blend modes (setstate 0x29) as Direct3D texture-stage ops. Stage 0's mode 0 and 8 modulate
   colour and take the texture's alpha (modulated when the card can); later stages pass in mode 0
   (disabled with fewer than two stages) and blend by the texture alpha in mode 8. */
static int texblend(int stage, uintptr_t mode)
{
    enum { DIFFUSE = 0, CURRENT = 1, TEXTURE = 2, TFACTOR = 3, COMPLEMENT = 0x10 };
    uint32_t a2 = stage ? CURRENT : DIFFUSE;
    typedef struct { uint32_t cop, c1, c2, aop, a1, a2; bool set_a2; } Blend;
    Blend b;
    switch (mode) {
    case 0:
    case 8:
        if (stage == 0) {
            if (!caps[CAP_MODULATEALPHA]) b = (Blend){4, TEXTURE, DIFFUSE, 2, TEXTURE, 0, false};
            else b = (Blend){4, TEXTURE, DIFFUSE, 4, TEXTURE, DIFFUSE, true};
        } else if (mode == 8) {
            b = (Blend){13, TEXTURE, CURRENT, 1, TEXTURE, CURRENT, true};
        } else if (caps[CAP_NUMSTAGES] < 2) {
            b = (Blend){1, TEXTURE, CURRENT, 1, TEXTURE, CURRENT, true};
        } else {
            b = (Blend){2, TEXTURE, CURRENT, 2, TEXTURE, CURRENT, true};
        }
        break;
    case 1: b = (Blend){7, TEXTURE, CURRENT, 7, TEXTURE, a2, true}; break;
    case 2: b = (Blend){1, TEXTURE, CURRENT, 1, TEXTURE, CURRENT, true}; break;
    case 3: b = (Blend){4, TEXTURE, CURRENT, 4, TEXTURE, a2, true}; break;
    case 4: b = (Blend){10, TEXTURE, CURRENT, 2, 0, 0, false}; break;
    case 5: b = (Blend){3, TEXTURE, CURRENT, 8, TEXTURE | COMPLEMENT, CURRENT, true}; break;
    case 6: b = (Blend){5, TEXTURE, CURRENT, 5, TEXTURE, a2, true}; break;
    case 7: b = (Blend){6, TEXTURE, CURRENT, 6, TEXTURE, a2, true}; break;
    case 0x61: b = (Blend){8, TEXTURE, CURRENT, 8, TEXTURE, a2, true}; break;
    case 0x62: b = (Blend){7, TEXTURE, CURRENT, 14, TEXTURE, TFACTOR, true}; break;
    case 0x63: b = (Blend){14, TEXTURE, CURRENT, 2, TEXTURE, CURRENT, true}; break;
    default: return 0;
    }
    tss(stage, TSS_COLOROP, b.cop);
    tss(stage, TSS_COLORARG1, b.c1);
    tss(stage, TSS_COLORARG2, b.c2);
    tss(stage, TSS_ALPHAOP, b.aop);
    if (mode != 4) tss(stage, TSS_ALPHAARG1, b.a1);
    if (b.set_a2) tss(stage, TSS_ALPHAARG2, b.a2);
    return 1;
}

/* FUN_6000a430 0x6000a430: states the DLL only stores (recorded; 1), anything else 0. */
static uintptr_t store_state(uint32_t id, uintptr_t v)
{
    switch (id) {
    case 0x1a: case 0x1b: case 0x1c: case 0x20: case 0x21: case 0x37: case 0x3a: case 0x3d:
        misc[id & 15] = v;
        break;
    case 0x3e:
        misc[15] = v;
        break;
    case 0x3b:
        break;
    case 0x1e:
        sprite_stride = v;
        break;
    case 0x1f:
        version = (intptr_t)v < 0x68 || (intptr_t)v > 0x6b ? 0x6b : v;
        break;
    case 0x27:
        state_callback = (void (*)(uint32_t, uintptr_t))v;
        break;
    default:
        return 0;
    }
    record(id, v);
    return 1;
}

/* setstate 0x1b3: DrawIndexedPrimitive (or DrawPrimitive) straight to the device, inside a scene. The
   batch isn't flushed first (its triangles land after these). */
static uintptr_t draw_primitive(const ThrashPrimitive *p)
{
    if (!in_scene) begin_scene();
    int st = fvf_stride(p->fvf);
    if (p->nindices == 0 || !p->indices)
        dev_draw((int)p->prim, p->verts, p->nverts, st, NULL, 0);
    else
        dev_draw((int)p->prim, p->verts, p->nverts, st, p->indices, p->nindices);
    return 1;
}

/* _THRASH_setstate@8 0x60003ac0. Returns 0 when the state or value isn't supported; most successful
   changes are recorded for getstate. The cases follow the DLL's switch; ids it passes through to
   Direct3D's own transform and lighting (0x1a4-0x1c1 except 0x1b3) aren't used by Porsche.exe and
   answer 0 here. */
uintptr_t THRASH_setstate(uint32_t state, uintptr_t v)
{
    int stage = state_stage(state);
    uint32_t id = state & 0xffff;
    uintptr_t r = 1;

    if (id > 400) {
        switch (id) {
        case 0x191: misc[1] = v; break;
        case THRASH_STATE_CAPS:
            if (!v) return 0;
            memcpy((void *)v, caps, sizeof caps);
            return v;
        case 0x194:
        case 0x195: {
            static const uint32_t blend[10] = {2, 1, 5, 6, 7, 8, 3, 9, 4, 10};
            if (v < 10) rs(id == 0x194 ? RS_SRCBLEND : RS_DESTBLEND, blend[v]);
            break;
        }
        case 0x197: if (v <= 2) kept.texmem = (uint32_t)v; break;
        case 0x198: misc[2] = v != 0; break;
        case 0x199:
            if (!caps[CAP_WIREFRAME]) return 0;
            rs(RS_EDGEANTIALIAS, v == 1);
            rs(RS_FILLMODE, v == 1 ? 2 : 3);
            break;
        case 0x19a: rs(RS_TEXTUREFACTOR, (uint32_t)v); break;
        case 0x19c:
        case 0x19d:
            if (!v) return 0;
            *(uintptr_t *)v = 0;
            break;
        case THRASH_STATE_DRAWPRIMITIVE:
            r = draw_primitive((const ThrashPrimitive *)v);
            break;
        case 0x1be: return 0;
        case THRASH_STATE_MODULATEALPHA:
            r = caps[CAP_MODULATEALPHA];
            if (!r) return 0;
            break;
        default:
            if (id >= 0x1a4 && id <= 0x1c1) return 0;
            r = store_state(id, v);
            if (!r) return 0;
        }
        record(state, v);
        return r;
    }
    if (id == 400) {
        misc[0] = v;
        record(state, v);
        return 1;
    }

    switch (id) {
    case 0: return 0;
    case THRASH_STATE_TEXTURE:
        THRASH_settexture(v);
        break;
    case THRASH_STATE_CULL:
        if (v == 0) { rs(RS_CULLMODE, 1); cull = 1; }
        else if (v == 2) { rs(RS_CULLMODE, 2); cull = 0; }
        else if (v == 1) { rs(RS_CULLMODE, 3); cull = 0x80000000u; }
        else return 0;
        break;
    case THRASH_STATE_BACKGROUND:     /* FUN_600082c0: also SetMaterial (no effect on these vertices) */
        background = (uint32_t)v;
        break;
    case THRASH_STATE_ZBUFFER:
        if (!caps[CAP_ZBUFFER]) return 0;
        if (v == 0) {
            rs(RS_ZWRITEENABLE, 0);
            if (CARD_SUBTYPE != 0xd) rs(RS_ZENABLE, 0);
            rs(RS_ZFUNC, 8);
        } else if (v == 1) {
            rs(RS_ZWRITEENABLE, 1);
            rs(RS_ZENABLE, 1);
            rs(RS_ZFUNC, zfunc);
        } else if (v == 2) {
            if (!caps[CAP_WBUFFER]) {
                rs(RS_ZWRITEENABLE, 1);
                rs(RS_ZENABLE, 1);
                rs(RS_ZFUNC, zfunc);
                return 0;
            }
            rs(RS_ZENABLE, 1);
            rs(RS_ZWRITEENABLE, 1);
            rs(RS_ZFUNC, zfunc);
            rs(RS_ZENABLE, 2);
        } else return 0;
        break;
    case THRASH_STATE_DITHER:
        rs(RS_DITHERENABLE, v != 0);
        if (!caps[CAP_DITHER]) return 0;
        break;
    case THRASH_STATE_SHADE:
        if (v == 0) { rs(RS_SHADEMODE, 1); rs(RS_SPECULARENABLE, 0); gouraud = 0; }
        else if (v == 1) { rs(RS_SHADEMODE, 2); rs(RS_SPECULARENABLE, 0); gouraud = 1; }
        else if (v == 2) {
            if (!caps[CAP_SPECULAR]) return 0;
            rs(RS_SHADEMODE, 2);
            rs(RS_SPECULARENABLE, 1);
            gouraud = 1;
        } else return 0;
        break;
    case THRASH_STATE_FILTER:
        if (v == 0) { tss(stage, TSS_MAGFILTER, 1); tss(stage, TSS_MINFILTER, 1); }
        else if (v == 1) { tss(stage, TSS_MAGFILTER, 2); tss(stage, TSS_MINFILTER, 2); }
        else if (v == 2) {
            if (!caps[CAP_ANISOTROPY]) return 0;
            tss(stage, TSS_MAGFILTER, 5);
            tss(stage, TSS_MINFILTER, 3);
        } else return 0;
        break;
    case THRASH_STATE_LODBIAS:
        if (!caps[CAP_LODBIAS]) return 0;
        tss(stage, TSS_MIPMAPLODBIAS, (uint32_t)v);
        break;
    case THRASH_STATE_ANTIALIAS: {
        if ((intptr_t)v < 0) return 0;
        uint32_t aa;
        if ((intptr_t)v < 2) aa = 0;
        else if (v == 2) aa = caps[CAP_ANTIALIAS] > 1 ? 2 : caps[CAP_ANTIALIAS];
        else return 0;
        rs(RS_ANTIALIAS, aa);
        if (v != 0 && !caps[CAP_ANTIALIAS]) return 0;
        break;
    }
    case THRASH_STATE_BLEND:
        if (v == 0) rs(RS_ALPHABLENDENABLE, 0);
        else if (v == 2) rs(RS_ALPHABLENDENABLE, 1);
        else return 0;
        break;
    case THRASH_STATE_MIPMAP:
        if (v == 0) tss(stage, TSS_MIPFILTER, 1);
        else if (v == 1) tss(stage, TSS_MIPFILTER, 2);
        else if (v == 2) return 0;
        else if (v == 3) {
            if (!caps[CAP_TRILINEAR]) return 0;
            tss(stage, TSS_MIPFILTER, 3);
        } else return 0;
        break;
    case 0x0c: case 0x10: case 0x11: case 0x31: case 0x32: case 0x33:
        return 0;
    case THRASH_STATE_ADDRESS:
        if (v == 0) tss(stage, TSS_ADDRESS, 3);
        else if (v == 1) tss(stage, TSS_ADDRESS, 1);
        else if (v == 2) tss(stage, TSS_ADDRESS, 2);
        else return 0;
        break;
    case THRASH_STATE_FOGDENSITY:      /* a float, or a small integer n meaning 1/n */
        fog_density = v == 0 || (v & 0xfffff000u) ? (uint32_t)v : (uint32_t)THRASH_f2u(1.0f / (float)(int)v);
        rs(RS_FOGDENSITY, fog_density);
        break;
    case THRASH_STATE_FOGCOLOUR:
        fog_colour = (uint32_t)v & 0xffffff;
        rs(RS_FOGCOLOR, fog_colour);
        break;
    case THRASH_STATE_WINDOWED:
        windowed = (int)v;
        break;
    case THRASH_STATE_HOOKS:
        if (v) memcpy(hooks, (const void *)v, sizeof hooks);
        else memset(hooks, 0, sizeof hooks);
        break;
    case THRASH_STATE_FOGTABLE:
    case 0x69:
        if (v) {
            build_fog_table((const uint8_t *)v);
            rs(RS_FOGTABLEMODE, 0);
            fog_custom = 1;
            fog_mode = 0x10;
        }
        break;
    case THRASH_STATE_FOGMODE:
        if (v == 4 || v == 8 || v == 2) {
            if (!caps[CAP_FOGTABLE]) return 0;
            fog_mode = (uint32_t)v;
            fog_custom = 0;
            rs(RS_FOGTABLEMODE, v == 4 ? 1 : v == 8 ? 2 : 3);
            rs(RS_FOGVERTEXMODE, 0);
            rs(RS_FOGCOLOR, fog_colour);
            rs(RS_FOGDENSITY, fog_density);
            if (v == 2) {
                rs(RS_FOGSTART, fog_start);
                rs(RS_FOGEND, fog_end);
            }
        } else if (v == 0) {
            rs(RS_FOGENABLE, 0);
            fog_custom = 0;
        } else if (v == 1) {
            if (caps[CAP_FOGTABLE] || fog_mode == 0x10) {
                rs(RS_FOGENABLE, 1);
                if (fog_mode == 0x10) fog_custom = 1;
            }
        } else return 0;
        break;
    case 0x16:
    case 0x17:
        break;
    case THRASH_STATE_DEPTHBIAS:
    case 0x66:
        /* The test compares the scaled bias with the unscaled value (only 0 ever matches). */
        if (depth_bias != (float)(int)v) depth_bias = (float)(int)v * (1.0f / 32768.0f);
        break;
    case THRASH_STATE_HWND:
        kept.hwnd = v;
        break;
    case THRASH_STATE_LOST:            /* FUN_60008280: surfaces not lost */
        break;
    case THRASH_STATE_ALPHATEST:
        if (v == 0) {
            rs(RS_ALPHATESTENABLE, 0);
            rs(RS_ALPHAFUNC, 8);
        } else {
            rs(RS_ALPHATESTENABLE, 1);
            rs(RS_ALPHAFUNC, alphafunc);
            rs(RS_ALPHAREF, (uint32_t)v);
        }
        break;
    case 0x26:
        if (!v) return 0;
        *(uintptr_t *)v = 0;
        break;
    case THRASH_STATE_ZFUNC:
        if (v <= 6) {
            zfunc = (uint32_t)v + 1;
            rs(RS_ZFUNC, zfunc);
        } else if (v == 7) {
            zfunc = 8;
            rs(RS_ZFUNC, 8);
        }
        break;
    case THRASH_STATE_TEXBLEND:
        if (!texblend(stage, v)) return 0;
        break;
    case THRASH_STATE_VERTEXFORMAT:
        if (v == 0 || v == 1) {
            stride = v ? 40 : 32;
            batch_flush();
            batch_cap = BATCH_BYTES / (int)stride;
            fvf = v ? 0x2c4 : 0x1c4;
            tss(0, TSS_TEXCOORDINDEX, 0);
            tss(1, TSS_TEXCOORDINDEX, v ? 1 : 0);
        }
        break;
    case THRASH_STATE_GAMMA:
    case 0x65: {
        if (!caps[CAP_GAMMA]) return 0;
        float g = u2f(v);
        if (!(g > 0.0f)) g = 0.0f;
        else if (g >= 4.0f) g = 4.0f;
        kept.gamma = g;
        dev_set_gamma(g);
        break;
    }
    case THRASH_STATE_STENCIL:
        if (!caps[CAP_STENCIL]) return 0;
        if (v <= 1) rs(RS_STENCILENABLE, (uint32_t)v);
        break;
    case 0x30:
        if (!caps[CAP_STENCIL]) return 0;
        if (v <= 7) rs(RS_STENCILFUNC, (uint32_t)v + 1);
        break;
    case 0x34:
    case 0x35:
    case 0x36:
        if (!caps[CAP_STENCIL]) return 0;
        if (v <= 5 || v == 7 || v == 8)   /* 6 is skipped */
            rs(id == 0x34 ? RS_STENCILFAIL : id == 0x35 ? RS_STENCILZFAIL : RS_STENCILPASS, v <= 5 ? (uint32_t)v + 1 : (uint32_t)v);
        break;
    case THRASH_STATE_TRANSPARENCY:
    case 0x68:
        if (v == 0) { rs(RS_SRCBLEND, 5); rs(RS_DESTBLEND, 6); }
        else if (v == 1) {
            rs(RS_SRCBLEND, CARD_SUBTYPE == 1 || CARD_SUBTYPE == 3 ? 2 : 5);
            rs(RS_DESTBLEND, 2);
        } else if (v == 2) { rs(RS_SRCBLEND, 1); rs(RS_DESTBLEND, 6); }
        else if (v == 3) { rs(RS_SRCBLEND, 9); rs(RS_DESTBLEND, 1); }
        else return 0;
        break;
    case THRASH_STATE_ZWRITE:
    case 0x6a:
        rs(RS_ZWRITEENABLE, v != 0);
        break;
    case THRASH_STATE_VSYNC:
    case 0x67:
        if (!caps[CAP_NOVSYNC]) return 0;
        kept.flip_flags = v ? 1 : 9;
        kept.blit_flags = 0x1000000;
        break;
    case THRASH_STATE_GUARDBAND: {
        float *out = (float *)v;
        if (!out || !(int)u2f(caps[CAP_GUARDLEFT]) || !(int)u2f(caps[CAP_GUARDRIGHT]) ||
            !(int)u2f(caps[CAP_GUARDTOP]) || !(int)u2f(caps[CAP_GUARDBOTTOM]))
            return 0;
        out[0] = u2f(caps[CAP_GUARDLEFT]);
        out[1] = u2f(caps[CAP_GUARDRIGHT]);
        out[2] = u2f(caps[CAP_GUARDTOP]);
        out[3] = u2f(caps[CAP_GUARDBOTTOM]);
        return 1;
    }
    case THRASH_STATE_ALPHAFUNC:
        if (v <= 7) {
            alphafunc = (uint32_t)v + 1;
            rs(RS_ALPHAFUNC, alphafunc);
        }
        break;
    case THRASH_STATE_ADDRESSU:
    case THRASH_STATE_ADDRESSV:
        if (!caps[CAP_ADDRESSUV]) return 0;
        if (v > 2) return 0;
        tss(stage, id == THRASH_STATE_ADDRESSU ? TSS_ADDRESSU : TSS_ADDRESSV, v == 0 ? 3 : (uint32_t)v);
        break;
    case THRASH_STATE_CLEARZ:
        clear_z = (uint32_t)v;
        break;
    default:
        r = store_state(id, v);
        if (!r) return 0;
    }
    record(state, v);
    return r;
}

/* ---------------------------------------------------------------- port additions */

void THRASH_present(void)
{
    dev_present();
}

/* A test scene through the API: a shaded triangle, a culled one, a palettised texture on a quad with
   alpha blending over it, a fogged strip, lines, points. Flips and presents. */
void THRASH_selftest(void)
{
    static ThrashTexture tex;
    static uint8_t pix[64 * 64];
    static uint32_t pal[256];
    static int frame;
    if (!device_open) return;
    if (!tex) {
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) pix[y * 64 + x] = (uint8_t)(((x >> 3) ^ (y >> 3)) & 1 ? 1 + y * 2 : 200);
        for (int i = 0; i < 256; i++) pal[i] = (uint32_t)(i << 16 | (255 - i) << 8 | 0x40);
        tex = THRASH_talloc(64, 64, THRASH_TEX_P8, 1, 0);
        THRASH_tupdate(tex, pix, pal);
    }
    THRASH_window(2);
    THRASH_setstate(THRASH_STATE_BACKGROUND, 0xff203040);
    THRASH_clip(0, 0, mode_w, mode_h);
    THRASH_clearwindow();
    THRASH_setstate(THRASH_STATE_SHADE, 1);
    THRASH_setstate(THRASH_STATE_ZBUFFER, 1);
    THRASH_setstate(THRASH_STATE_CULL, 1);    /* counter-clockwise triangles culled */
    THRASH_settexture(0);
    float t = (float)(frame++ % 120) / 120.0f;
    ThrashVertex tri[3] = {
        {100, 400, 0.5f, 1, 0xffff0000, 0xff000000, 0, 0},
        {200 + 100 * t, 100, 0.5f, 1, 0xff00ff00, 0xff000000, 0, 0},
        {300, 400, 0.5f, 1, 0xff0000ff, 0xff000000, 0, 0},
    };
    THRASH_drawtri(&tri[0], &tri[1], &tri[2]);
    THRASH_drawtri(&tri[0], &tri[2], &tri[1]);          /* the other winding: culled */
    THRASH_settexture(tex);
    THRASH_setstate(THRASH_STATE_TEXBLEND, 0);
    THRASH_setstate(THRASH_STATE_FILTER, 1);
    THRASH_setstate(THRASH_STATE_ADDRESS, 1);   /* wrap (the default is clamp) */
    ThrashVertex q[4] = {
        {340, 80, 0.4f, 1, 0xffffffff, 0xff000000, 0, 0},
        {600, 80, 0.4f, 1, 0xffffffff, 0xff000000, 2, 0},
        {600, 300, 0.4f, 1, 0xffffffff, 0xff000000, 2, 2},
        {340, 300, 0.4f, 1, 0xffffffff, 0xff000000, 0, 2},
    };
    THRASH_drawquad(&q[0], &q[1], &q[2], &q[3]);
    THRASH_settexture(0);
    THRASH_setstate(THRASH_STATE_BLEND, 2);
    THRASH_setstate(THRASH_STATE_TRANSPARENCY, 0);
    ThrashVertex o[4] = {
        {300, 200, 0.3f, 1, 0x80ffff00, 0xff000000, 0, 0},
        {500, 200, 0.3f, 1, 0x80ffff00, 0xff000000, 0, 0},
        {500, 350, 0.3f, 1, 0x80ffff00, 0xff000000, 0, 0},
        {300, 350, 0.3f, 1, 0x80ffff00, 0xff000000, 0, 0},
    };
    THRASH_drawquad(&o[0], &o[1], &o[2], &o[3]);
    THRASH_setstate(THRASH_STATE_BLEND, 0);
    /* vertex fog: the specular alpha fades the strip to the fog colour (no culling: the strip starts
       counter-clockwise) */
    THRASH_setstate(THRASH_STATE_CULL, 0);
    THRASH_setstate(THRASH_STATE_FOGCOLOUR, 0x808080);
    THRASH_setstate(THRASH_STATE_FOGMODE, 1);
    ThrashVertex s[6];
    for (int i = 0; i < 6; i++) {
        s[i] = (ThrashVertex){40.0f + 110.0f * (float)(i / 2), 430.0f + 40.0f * (float)(i & 1), 0.2f, 1,
                              0xffff8000, (uint32_t)(255 - i * 50) << 24, 0, 0};
    }
    int si[6] = {0, 1, 2, 3, 4, 5};
    THRASH_drawtristrip(4, s, si);
    THRASH_setstate(THRASH_STATE_FOGMODE, 0);
    ThrashVertex l[2] = {{20, 20, 0.1f, 1, 0xffffffff, 0, 0, 0}, {620, 460, 0.1f, 1, 0xffffffff, 0, 0, 0}};
    THRASH_drawline(&l[0], &l[1]);
    ThrashVertex p = {320, 20, 0.1f, 1, 0xffffffff, 0, 0, 0};
    THRASH_drawpoint(&p);
    THRASH_pageflip();
    THRASH_present();
}
