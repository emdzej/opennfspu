/* Inside the THRASH port: thrash.c is dx7z.dll's logic (state machine, batching, culling, textures);
   it talks to a Direct3D 7 device the way the DLL does, through the small device model below
   (thrash_dev.c), which carries out the D3D render and texture-stage states on OpenGL ES 3.0. Values are
   Direct3D 7's (D3DRENDERSTATETYPE, D3DTEXTURESTAGESTATETYPE, D3DCMPFUNC, D3DBLEND, D3DTOP, ...). */
#pragma once
#include "render/thrash.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* D3DRENDERSTATETYPE values the DLL sets. */
enum {
    RS_ANTIALIAS = 2, RS_ZENABLE = 7, RS_FILLMODE = 8, RS_SHADEMODE = 9, RS_ZWRITEENABLE = 14,
    RS_ALPHATESTENABLE = 15, RS_SRCBLEND = 19, RS_DESTBLEND = 20, RS_CULLMODE = 22, RS_ZFUNC = 23,
    RS_ALPHAREF = 24, RS_ALPHAFUNC = 25, RS_DITHERENABLE = 26, RS_ALPHABLENDENABLE = 27,
    RS_FOGENABLE = 28, RS_SPECULARENABLE = 29, RS_FOGCOLOR = 34, RS_FOGTABLEMODE = 35,
    RS_FOGSTART = 36, RS_FOGEND = 37, RS_FOGDENSITY = 38, RS_EDGEANTIALIAS = 40, RS_STENCILENABLE = 52,
    RS_STENCILFAIL = 53, RS_STENCILZFAIL = 54, RS_STENCILPASS = 55, RS_STENCILFUNC = 56,
    RS_TEXTUREFACTOR = 60, RS_FOGVERTEXMODE = 140,
    RS_COUNT = 160,
};

/* D3DTEXTURESTAGESTATETYPE values. */
enum {
    TSS_COLOROP = 1, TSS_COLORARG1 = 2, TSS_COLORARG2 = 3, TSS_ALPHAOP = 4, TSS_ALPHAARG1 = 5,
    TSS_ALPHAARG2 = 6, TSS_TEXCOORDINDEX = 11, TSS_ADDRESS = 12, TSS_ADDRESSU = 13, TSS_ADDRESSV = 14,
    TSS_MAGFILTER = 16, TSS_MINFILTER = 17, TSS_MIPFILTER = 18, TSS_MIPMAPLODBIAS = 19,
    TSS_COUNT = 24,
};

enum { DEV_STAGES = 2 };   /* the card's simultaneous textures (THRASH numstages) */

/* D3DPRIMITIVETYPE */
enum { PRIM_POINTLIST = 1, PRIM_LINELIST = 2, PRIM_LINESTRIP = 3, PRIM_TRIANGLELIST = 4,
       PRIM_TRIANGLESTRIP = 5, PRIM_TRIANGLEFAN = 6 };

/* D3DCLEAR_* */
enum { CLEAR_TARGET = 1, CLEAR_ZBUFFER = 2, CLEAR_STENCIL = 4 };

/* A texture: talloc's record (dx7z keeps w, h, format, clut flag, mip levels, stage, ... in a 0xb8-byte
   block); the pixels the game gave (kept: a clut update re-expands them) and the GL texture. */
typedef struct DevTexture {
    int width, height, format, clut, levels, stage, bytespp;
    struct DevTexture *next;      /* talloc's list (record +0x18) */
    const uint8_t *pixels;        /* the caller's data (the DLL's staging surface aliases it) */
    uint32_t palette[256];        /* clut as given: 0x00RRGGBB */
    unsigned gl;
    int uploaded_levels;
} DevTexture;

/* The device (an IDirect3DDevice7 on its render target). */
int dev_open(int width, int height, int bpp);          /* the mode's surfaces; 0 on failure */
void dev_close(void);
void dev_begin_scene(void);
void dev_end_scene(void);
void dev_set_render_state(int rs, uint32_t value);
uint32_t dev_render_state(int rs);
void dev_set_tss(int stage, int tss, uint32_t value);
void dev_set_texture(int stage, DevTexture *t);
void dev_set_viewport(int x, int y, int w, int h);
void dev_clear(int x0, int y0, int x1, int y1, int flags, uint32_t colour, float z, uint32_t stencil);
/* stride: 32 or 40 (FVF 0x1c4 / 0x2c4). indices may be NULL (DrawPrimitive). */
void dev_draw(int prim, const void *verts, int nverts, int stride, const uint16_t *indices, int nindices);
void dev_flip(bool copy);    /* back <-> front (a flip chain), or a copy of the back buffer (windowed) */
void dev_set_gamma(float g);                            /* the gamma ramp: desktop ramp * g, clamped */
/* 1 front, 2 back. Reads/writes the mode's pixels (format 4 = RGB565, 6 = XRGB8888). */
void dev_read_pixels(int surface, int x, int y, int w, int h, void *dst, int pitch);
void dev_write_pixels(int surface, int x, int y, int w, int h, const void *src, int pitch);
void dev_present(void);
void dev_texture_upload(DevTexture *t);                 /* (re)expand pixels + clut into the GL texture */
void dev_texture_free(DevTexture *t);
