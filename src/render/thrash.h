/* THRASH: EA's 3D driver interface between the game and a rendering back end. The original ships it as
   a DLL per API (Drivers/dx7z.dll for Direct3D 7, voodoo2z.dll for Glide), loaded by Porsche.exe
   (THRASH_opendll 0x574d50 fills the function table at 0x6bd850..0x6bd8f8 with GetProcAddress); this
   port implements the same entry points on OpenGL ES 3.0 (gasm:gl, WebGL 2 in browsers). Behaviour
   follows dx7z.dll (image base 0x60000000; each function cites its address). Notes: docs/re/thrash.md.

   Coordinates are the game's screen pixels in the current video mode (640x480 etc.); the driver renders
   at an integer multiple of that and scales to the drawable. Vertices are pre-transformed (D3D FVF
   XYZRHW | DIFFUSE | SPECULAR | TEX1, or TEX2 after setstate(THRASH_STATE_VERTEXFORMAT, 1)). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* FVF 0x1c4: 32 bytes. Colours are D3DCOLOR (0xAARRGGBB); the specular alpha is the vertex fog factor. */
typedef struct { float x, y, z, rhw; uint32_t diffuse, specular; float u, v; } ThrashVertex;
/* FVF 0x2c4: 40 bytes, a second texture coordinate set for stage 1. */
typedef struct { float x, y, z, rhw; uint32_t diffuse, specular; float u, v, u2, v2; } ThrashVertex2;

/* A texture handle from THRASH_talloc (values below 0x10 are the special settexture arguments). */
typedef uintptr_t ThrashTexture;

/* lockwindow's answer (0x600216d8): the selected window's pixels while locked. format is a THRASH
   texture format code (4 = RGB565, 6 = XRGB8888; for the z-buffer window the bit depth). */
typedef struct { void *pixels; int32_t pitch, format, width, height; } ThrashSurface;

/* setstate(THRASH_STATE_DRAWPRIMITIVE, &p) 0x1b3: a Direct3D DrawIndexedPrimitive (DrawPrimitive when
   there are no indices) passed straight to the device: no software culling, depth bias or fog table. */
typedef struct {
    uint32_t prim;              /* 1 points, 2 lines, 3 line strip, 4 triangles, 5 strip, 6 fan */
    uint32_t fvf;               /* 0x1c4 or 0x2c4 */
    const void *verts;
    uint16_t nverts;
    const uint16_t *indices;
    uint16_t nindices;
} ThrashPrimitive;

/* Display modes as listed by THRASH_about (entry 0 unused; setvideomode takes the index). */
typedef struct { int32_t width, height, bpp, unknown3, unknown4, unknown5, unknown6, pad[3]; } ThrashMode;

/* THRASH_about's answer (0x600555a0, 0xd0 bytes): the driver and the card. */
typedef struct {
    uint32_t signature;         /* '7D3D' 0x44334437 (Glide drivers: '3DFX' / '2FD3') */
    uint32_t size;              /* 0xd0 */
    uint32_t version;           /* setstate 0x1f, 0x68..0x6b */
    uint32_t flags;             /* THRASH_ABOUT_* */
    int32_t texwidthmin, texwidthmax, texwidthmultiple;
    int32_t texheightmin, texheightmax, texheightmultiple;
    int32_t clipalign;
    int32_t ntexformats;        /* 0x10 */
    const uint32_t *texformats; /* per format code: bit 0 supported, 5 = only without alpha (1555 as 555) */
    int32_t ncluts;             /* 4 */
    const uint32_t *cluts;
    int32_t nmodes;
    const ThrashMode *modes;
    int32_t numstages;
    uint32_t unknown48;
    char name[12];              /* "DX7 3rash" */
    uint32_t unknown58[5];
    int32_t subtype;            /* card id from the PCI vendor/device (0x12 = GeForce 256) */
    int32_t textureramsize;
    int32_t textureramtype;
    const char *author;
    int32_t dxversion;          /* 7 */
    char device[80];
} ThrashAbout;

enum {
    THRASH_ABOUT_LINEWIDTH = 1 << 0, THRASH_ABOUT_SQUARE = 1 << 1, THRASH_ABOUT_POW2W = 1 << 2,
    THRASH_ABOUT_POW2H = 1 << 3, THRASH_ABOUT_SOFTWARE = 1 << 4, THRASH_ABOUT_WINDOWED = 1 << 5,
    THRASH_ABOUT_GLOBALCLUT = 1 << 6, THRASH_ABOUT_TRILINEAR2PASS = 1 << 7,
};

/* Texture formats (talloc's format; FSH shape type in brackets, Porsche.exe 0x5565e0). */
enum {
    THRASH_TEX_P4 = 1,          /* [0x7a] 4-bit palettised */
    THRASH_TEX_P8 = 2,          /* [0x7b] 8-bit palettised */
    THRASH_TEX_ARGB1555 = 3,    /* [0x7e] */
    THRASH_TEX_RGB565 = 4,      /* [0x78] */
    THRASH_TEX_RGB888 = 5,      /* [0x7f] never supported by dx7z */
    THRASH_TEX_ARGB8888 = 6,    /* [0x7d] */
    THRASH_TEX_ARGB4444 = 7,    /* [0x6d] */
    THRASH_TEX_DXT1 = 12,       /* [0xf8] data after an 8-byte header */
    THRASH_TEX_DXT3 = 13,       /* [0xfd] data after an 8-byte header */
};

/* State ids used by Porsche.exe (low 16 bits; bits 16-18 select the texture stage). */
enum {
    THRASH_STATE_TEXTURE = 0x01, THRASH_STATE_CULL = 0x02, THRASH_STATE_BACKGROUND = 0x03,
    THRASH_STATE_ZBUFFER = 0x04, THRASH_STATE_DITHER = 0x05, THRASH_STATE_SHADE = 0x06,
    THRASH_STATE_FILTER = 0x07, THRASH_STATE_LODBIAS = 0x08, THRASH_STATE_ANTIALIAS = 0x09,
    THRASH_STATE_BLEND = 0x0a, THRASH_STATE_MIPMAP = 0x0b, THRASH_STATE_ADDRESS = 0x0d,
    THRASH_STATE_FOGDENSITY = 0x0e, THRASH_STATE_FOGCOLOUR = 0x0f, THRASH_STATE_WINDOWED = 0x12,
    THRASH_STATE_HOOKS = 0x13, THRASH_STATE_FOGTABLE = 0x14, THRASH_STATE_FOGMODE = 0x15,
    THRASH_STATE_DEPTHBIAS = 0x18, THRASH_STATE_HWND = 0x19, THRASH_STATE_SPRITESTRIDE = 0x1e,
    THRASH_STATE_VERSION = 0x1f, THRASH_STATE_LOST = 0x23, THRASH_STATE_ALPHATEST = 0x24,
    THRASH_STATE_CALLBACK = 0x27, THRASH_STATE_ZFUNC = 0x28, THRASH_STATE_TEXBLEND = 0x29,
    THRASH_STATE_VERTEXFORMAT = 0x2a, THRASH_STATE_GAMMA = 0x2e, THRASH_STATE_STENCIL = 0x2f,
    THRASH_STATE_TRANSPARENCY = 0x38, THRASH_STATE_ZWRITE = 0x39, THRASH_STATE_VSYNC = 0x3c,
    THRASH_STATE_GUARDBAND = 0x3f, THRASH_STATE_ALPHAFUNC = 0x40, THRASH_STATE_ADDRESSU = 0x41,
    THRASH_STATE_ADDRESSV = 0x42, THRASH_STATE_CLEARZ = 0x43, THRASH_STATE_CAPS = 0x193,
    THRASH_STATE_DRAWPRIMITIVE = 0x1b3, THRASH_STATE_MODULATEALPHA = 0x1c3,
};
#define THRASH_STAGE(n) ((uint32_t)(n) << 16)

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(ThrashAbout) == 0xd0, "THRASH_about's layout is the DLL's");
_Static_assert(sizeof(ThrashPrimitive) == 0x18 && sizeof(ThrashMode) == 0x28, "the DLL's layouts");
#endif

/* Float state values travel as their bits (the original passes them in the pointer-sized argument). */
static inline uintptr_t THRASH_f2u(float f) { union { float f; uint32_t u; } c = {f}; return c.u; }

const ThrashAbout *THRASH_about(void);                                  /* _THRASH_about@0 0x60001000 */
int THRASH_init(void);                                                  /* _THRASH_init@0 0x60003450 */
int THRASH_is(void);                                                    /* _THRASH_is@0 0x600038b0 */
int THRASH_selectdisplay(int display);                                  /* _THRASH_selectdisplay@4 0x60001ee0 */
bool THRASH_setvideomode(int mode, int backbuffers, int bpp);           /* _THRASH_setvideomode@12 0x60003510 */
int THRASH_restore(void);                                               /* _THRASH_restore@0 0x600034a0 */
void THRASH_idle(void);                                                 /* _THRASH_idle@0 0x6000b270 */

uintptr_t THRASH_setstate(uint32_t state, uintptr_t value);            /* _THRASH_setstate@8 0x60003ac0 */
intptr_t THRASH_getstate(uint32_t state);                               /* _THRASH_getstate@4 0x60001b90 */

bool THRASH_window(int window);       /* 0 none, 1 front, 2 back, 3/5 z-buffer; _THRASH_window@4 0x600011b0 */
int THRASH_clip(int x0, int y0, int x1, int y1);                        /* _THRASH_clip@16 0x60001300 */
void THRASH_clearwindow(void);                                          /* _THRASH_clearwindow@0 0x60001230 */
bool THRASH_flushwindow(void);                                          /* _THRASH_flushwindow@0 0x60001270 */
void THRASH_pageflip(void);                                             /* _THRASH_pageflip@0 0x60001280 */
int THRASH_sync(int how);                                               /* _THRASH_sync@4 0x600012c0 */
ThrashSurface *THRASH_lockwindow(void);                                 /* _THRASH_lockwindow@0 0x600013c0 */
int THRASH_unlockwindow(ThrashSurface *s);                              /* _THRASH_unlockwindow@4 0x60001790 */
int THRASH_readrect(int x, int y, int w, int h, void *dst);             /* _THRASH_readrect@20 0x60003990 */
int THRASH_writerect(int x, int y, int w, int h, const void *src);      /* _THRASH_writerect@20 0x60003a30 */

ThrashTexture THRASH_talloc(int width, int height, int format, int clut, uint32_t mipstage); /* _THRASH_talloc@20 0x60008420 */
int THRASH_tfree(ThrashTexture t);                                      /* _THRASH_tfree@4 0x600085a0 */
ThrashTexture THRASH_tupdate(ThrashTexture t, const void *pixels, const uint32_t *clut); /* _THRASH_tupdate@12 0x60008610 */
int THRASH_treset(void);                                                /* _THRASH_treset@0 0x60008640 */
int THRASH_settexture(ThrashTexture t);   /* 0 all stages off, 1 stage 0 off, 2 stage 1 off; _THRASH_settexture@4 0x60008000 */

/* Vertices are pointers to ThrashVertex (or ThrashVertex2 with that format); meshes take an index array. */
void THRASH_drawtri(const void *a, const void *b, const void *c);       /* _THRASH_drawtri@12 0x600017f0 */
void THRASH_drawtrimesh(int count, const void *verts, const int *idx);  /* _THRASH_drawtrimesh@12 0x60001850 */
void THRASH_drawtristrip(int count, const void *verts, const int *idx); /* _THRASH_drawtristrip@12 0x60001870 */
void THRASH_drawtrifan(int count, const void *verts, const int *idx);   /* _THRASH_drawtrifan@12 0x60001890 */
void THRASH_drawquad(const void *a, const void *b, const void *c, const void *d); /* _THRASH_drawquad@16 0x600018b0 */
void THRASH_drawquadmesh(int count, const void *verts, const int *idx); /* _THRASH_drawquadmesh@12 0x60001910 */
void THRASH_drawline(const void *a, const void *b);                     /* _THRASH_drawline@8 0x60001930 */
void THRASH_drawlinemesh(int count, const void *verts, const int *idx); /* _THRASH_drawlinemesh@12 0x60001950 */
void THRASH_drawlinestrip(int count, const void *verts, const int *idx);/* _THRASH_drawlinestrip@12 0x60001970 */
void THRASH_drawpoint(const void *a);                                   /* _THRASH_drawpoint@4 0x600019b0 */
void THRASH_drawpointmesh(int count, const void *verts, const int *idx);/* _THRASH_drawpointmesh@12 0x600019d0 */
void THRASH_drawsprite(const void *a, const void *b);                   /* _THRASH_drawsprite@8 0x60001a00 */
void THRASH_drawspritemesh(int count, const void *verts, const int *idx); /* _THRASH_drawspritemesh@12 0x60001a60 */
void THRASH_drawtristrip8(int count, const void *verts);                /* _THRASH_drawtristrip@8 0x60001aa0 */
void THRASH_drawtrifan8(int count, const void *verts);                  /* _THRASH_drawtrifan@8 0x60001b00 */
void THRASH_drawlinestrip8(int count, const void *verts);               /* _THRASH_drawlinestrip@8 0x60001b40 */
void THRASH_drawpointstrip8(int count, const void *verts);              /* _THRASH_drawpointstrip@8 0x60001b70 */

/* Port additions. THRASH_present shows the front buffer on the drawable (the gasm default framebuffer
   isn't kept between frames): call it once at the end of every frame. THRASH_selftest draws a test
   scene through the API above and flips. */
void THRASH_present(void);
void THRASH_selftest(void);
