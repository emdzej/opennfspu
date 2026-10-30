/* A recording THRASH for the EAGL tests (included by one test file per executable): answers like the
   port's driver where EAGL asks (about: "DX7 3rash", mode 1 = 640x480, a +-2048 guard band) and keeps
   every primitive passed to setstate(0x1b3), so the tests can check the exact vertex and index stream. */
#pragma once
#include "real/set3d.h"
#include "real/texture.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t prim, fvf, nverts, nindices;
    ThrashVertex verts[512];
    uint16_t indices[2048];
} FakePrim;

static FakePrim fake_prims[16];
static int fake_nprims;
static int fake_states[0x400];      /* last value per state id (low 16 bits) */
static int fake_cliprect[4];

static ThrashMode fake_modes[6];
static ThrashAbout fake_about;
/* The GL driver's texture formats (P4, P8, 1555, 565, 8888, 4444, DXT1, DXT3), so DTEX can choose one. */
static uint32_t fake_texformats[17], fake_cluts[5];   /* one past: DTEX scans 1..n inclusive (see thrash.c) */

const ThrashAbout *fake_about_fn(void)
{
    if (!fake_about.size) {
        fake_about.signature = 0x44334437;
        fake_about.size = 0xd0;
        memcpy(fake_about.name, "DX7 3rash", 10);
        fake_modes[1].width = 640;
        fake_modes[1].height = 480;
        fake_modes[1].bpp = 16;
        fake_about.nmodes = 6;
        fake_about.modes = fake_modes;
        static const int ok[] = {1, 2, 3, 4, 6, 7, 12, 13};
        for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) fake_texformats[ok[i]] = 1;
        fake_about.ntexformats = 0x10;
        fake_about.texformats = fake_texformats;
        fake_about.ncluts = 4;
        fake_about.cluts = fake_cluts;
    }
    return &fake_about;
}

uintptr_t fake_setstate(uint32_t state, uintptr_t value)
{
    if (state == THRASH_STATE_GUARDBAND) {
        float *g = (float *)value;
        g[0] = -2048.0f;
        g[1] = 2047.0f;
        g[2] = -2048.0f;
        g[3] = 2047.0f;
        return 1;
    }
    if (state == THRASH_STATE_DRAWPRIMITIVE) {
        const ThrashPrimitive *p = (const ThrashPrimitive *)value;
        if (fake_nprims < 16) {
            FakePrim *r = &fake_prims[fake_nprims++];
            r->prim = p->prim;
            r->fvf = p->fvf;
            r->nverts = p->nverts;
            r->nindices = p->nindices;
            memcpy(r->verts, p->verts, (size_t)p->nverts * sizeof(ThrashVertex));
            if (p->indices) memcpy(r->indices, p->indices, (size_t)p->nindices * sizeof(uint16_t));
        }
        return 1;
    }
    fake_states[state & 0x3ff] = (int)value;
    return 1;
}

intptr_t fake_getstate(uint32_t state) { return fake_states[state & 0x3ff]; }

int fake_clip(int x0, int y0, int x1, int y1)
{
    fake_cliprect[0] = x0;
    fake_cliprect[1] = y0;
    fake_cliprect[2] = x1;
    fake_cliprect[3] = y1;
    return 1;
}

bool fake_flushwindow(void) { return true; }
void fake_clearwindow(void) {}

/* Textures (car loading uploads its pages): handles are numbered from 1; nothing is stored. */
static int fake_ntex;
ThrashTexture fake_talloc(int width, int height, int format, int clut, uint32_t mipstage)
{
    (void)width; (void)height; (void)format; (void)clut; (void)mipstage;
    return (ThrashTexture)(uintptr_t)++fake_ntex;
}
ThrashTexture fake_tupdate(ThrashTexture t, const void *pixels, const uint32_t *clut) { (void)pixels; (void)clut; return t; }
int fake_tfree(ThrashTexture t) { (void)t; return 1; }
int fake_treset(void) { return 1; }
int fake_settexture(ThrashTexture t) { (void)t; return 1; }
bool fake_window(int window) { (void)window; return true; }
int fake_unlockwindow(ThrashSurface *s) { (void)s; return 1; }
void fake_pageflip(void) {}

/* The fake as a driver in the original's pointer table (real/set3d.h): EAGL calls THRASH.fn.x(). */
static const ThrashDll fake_dll = {
    .name = "fake", .about = fake_about_fn, .clearwindow = fake_clearwindow, .clip = fake_clip,
    .flushwindow = fake_flushwindow, .getstate = fake_getstate, .pageflip = fake_pageflip, .setstate = fake_setstate,
    .talloc = fake_talloc, .tupdate = fake_tupdate, .tfree = fake_tfree, .treset = fake_treset,
    .settexture = fake_settexture, .window = fake_window, .unlockwindow = fake_unlockwindow,
};

/* Straight into the table: THRASH_opendll insists on a complete driver, as the original does. */
static inline void fake_install(void) { THRASH.fn = fake_dll; }

/* The graphics object and its texture manager, as Render_Init (0x4673b0) sets them up before anything
   loads: car loading makes managed textures. (eagl_test draws with stand-in texture values, so it runs
   without one and EAGL skips the texture selection.) */
static inline void fake_install_graphics(void)
{
    fake_install();
    static TexGraphics *g;
    static int device;
    if (!g) {
        g = calloc(1, sizeof *g);
        TexGraphics_register(g);
        TexGraphics_setdevice(g, &device);
    }
}
