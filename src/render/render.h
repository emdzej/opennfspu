/* The game's render system (render module 0x464000-0x469000; no source file name in the exe): the
   driver choice (the 3D Setup registry values, NFSRegistryManager), the render system object at 0x628130
   with its graphics device and render-state object, the display mode and the render quality, the cameras
   (0x138-byte objects with their own EAGL view) and the per-frame view begin / end. docs/re/render-system.md. */
#pragma once
#include "eagl/eagl.h"
#include "real/set3d.h"
#include "real/texture.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- the 3D Setup choice (NFSRegistryManager, vtable 0x5b3fbc) ---- */

/* Values of HKLM\Software\Electronic Arts\Need For Speed - Porsche Unleashed, by index into the exe's table
   0x5d6f28 (name, type, default; a missing value is written with its default, as the original does).
   The port has no registry: a value comes from the launch parameter of its name in lower case with '_'
   for spaces (thrash_driver, d3d_device, thrash_resolution, hardware_acceleration, triple_buffer), else the
   table's default. */
enum { RREG_DRIVER = 0, RREG_DEVICE = 1, RREG_RESOLUTION = 2, RREG_HWACCEL = 3, RREG_TRIPLEBUFFER = 4 };
void RRegistry_GetString(int key, char *dst, int cap);   /* 0x4b7030 */
int RRegistry_GetInt(int key);                           /* 0x4b7130 */

/* ---- the options the render system reads (the option records of Options_Parse 0x4b0dc0) ---- */

/* The render options of fe.txt, the settings file the game reads at start (Options_Read 0x4b6450, not
   ported yet: render_options.c reads these keys from it, with the original's defaults for missing ones). */
typedef struct {
    char driver[32];            /* 0x657a38 DRIVER: "registry" = the 3D Setup's choice */
    int width, height, depth;   /* 0x657a48, 0x657a4c, 0x657a50 */
    int window;                 /* 0x657a54 */
    int renderdevice;           /* 0x657a58 */
    int widescreen;             /* 0x657d5c */
    int carchrome;              /* 0x657d60: reflections 0 / 1 / 2 */
    int cardetail;              /* 0x657d68 */
    int specialfx;              /* 0x657d6c */
    int adjustableviewdistance; /* 0x657d70 */
    int headlights;             /* 0x657d78 */
    int lensflare;              /* 0x657d7c */
    int gammacorrection;        /* 0x657d80 */
    int skyrendering;           /* 0x657d9c SKYRENDERING: the sky's level (0 low .. 3 amazing: which blocks of
                                   Sky\<name>.txt count, and the light slot); a front-end option, so not in
                                   fe.txt: the port uses 2 (high) unless fe.txt or --param sky_detail says */
    int drawsync;               /* 0x657da0 */
    int nomip;                  /* 0x657da4 NOMIP: no mip levels (Render_DetectCard) */
    int viewdistance;
} RenderOptions;
extern RenderOptions g_renderopts;
void RenderOptions_Load(void);

/* ---- the display mode (0x619780..) ---- */

typedef struct {
    int32_t stamp;              /* 0x619780: changes with every mode set (cameras reset their viewport) */
    int32_t width, height;      /* 0x619784, 0x619788 */
    int32_t bpp;                /* 0x61978c (15 counts as 16) */
} RenderDisplay;
extern RenderDisplay g_display;

/* ---- the render-state object (0x38 bytes, 0x537470) ---- */

typedef struct {
    int32_t v00;                /* +0x00 */
    int32_t state28;            /* +0x04: setstate 0x28 (3) */
    int32_t state04;            /* +0x08: setstate 4 (1) */
    int32_t state39;            /* +0x0c: setstate 0x39 (1) */
    int32_t cull;               /* +0x10: 1 counter-clockwise faces culled, 2 clockwise (setstate 2) */
    int32_t state09;            /* +0x14 */
    int32_t state15;            /* +0x18 */
    int32_t state0e;            /* +0x1c (alias) */
    int32_t state16;            /* +0x20 (alias) */
    uint32_t state17;           /* +0x24 (alias): 0.99999 */
    int32_t state0f;            /* +0x28: -1 */
    int32_t state3c;            /* +0x2c */
    int32_t biasbase;           /* +0x30: depth-bias base */
    uint32_t state43;           /* +0x34 (alias): 1.0 */
} RenderState;
RenderState *RenderState_ctor(RenderState *rs);          /* 0x537470 */
void RenderState_Send(RenderState *rs);                  /* 0x5374d0 */
void RenderState_SetZFunc(RenderState *rs, int zfunc);   /* 0x5375a0: setstate 0x28 (3 less-equal, 7 always) */
void RenderState_SetZWrite(RenderState *rs, int on);
/* The race's view distance (0x6197cc): the track's by road node (render.c), each frame. */
extern float g_render_viewdist;
extern int32_t g_render_falsesort;  /* 0x6197e8: the false sort layers' level (0x44eb50) */
extern int32_t g_render_specials;   /* 0x6197f4: the special effects' level 0..3 */
extern int32_t g_render_precip;     /* 0x6197f0: the weather's most drops (60, 140, 220, 300) */
/* The track's view distances by road node (<track>_forward.key / _backwards.key). 0x44f9b0, 0x44f9e0 */
void Render_LoadViewKeys(const char *track);
void Render_FreeViewKeys(void);     /* 0x5375e0: setstate 0x39 */
void RenderState_SetCull(RenderState *rs, int cull);     /* 0x537600 */
void RenderState_SetFogMode(RenderState *rs, int mode);          /* 0x537670: 0 off, 1 on, 4 exp, 8 exp2 */
void RenderState_SetFogDensity(RenderState *rs, float density);  /* 0x537690 */
void RenderState_SetFogColour(RenderState *rs, uint32_t argb);   /* 0x5376f0 */

/* ---- the graphics device (EAGL device, vtable 0x5bc41c; the windowed kind 0x5bc3f0) ---- */

struct RDevice;
typedef struct {
    void (*destroy)(struct RDevice *d, int free_it);     /* 0x556e50 */
    bool (*open)(struct RDevice *d);                     /* 0x556eb0 */
    void (*begin)(struct RDevice *d);                    /* 0x556f80: drawing to the back buffer */
    void (*present)(struct RDevice *d);                  /* 0x556f90: page flip */
    void (*clear)(struct RDevice *d);                    /* 0x556f50 */
} RDeviceVtbl;

typedef struct RDevice {
    const RDeviceVtbl *vt;      /* +0x00 */
    RenderState *rs;            /* +0x04 */
    uint32_t caps;              /* +0x08: the driver's about block +0x40 */
    int32_t mode;               /* +0x0c: 1 */
    const ThrashAbout *about;   /* +0x10 */
    EaglView *current;          /* +0x14: the view between begin and end (EAGL writes it) */
    uint32_t pad18[2];
    TexGraphics *graphics;      /* +0x20 */
} RDevice;

/* ---- cameras (0x138 bytes, Render_CreateCamera 0x467230) ---- */

/* A camera's frame (0x60 bytes at +0xb8): the input is the orientation (rows: right, up, back) and the
   eye; RFrame_ToView 0x4661a0 makes the world-to-camera rotation and translation from them. */
typedef struct {
    float rot[9];               /* +0x00: world to camera */
    float trans[3];             /* +0x24 */
    float orient[9];            /* +0x30 */
    float eye[3];               /* +0x54 */
} RFrame;

typedef struct {
    int32_t stamp;              /* +0x000: the display mode its viewport was set for */
    int32_t type;               /* +0x004: 0 main, 1 .., 3 overlay, 4 mirror, 5/6 front end */
    int32_t v008;
    EaglView *view;             /* +0x00c */
    float x0, y0;               /* +0x010, +0x014: the viewport, fractions of the screen */
    float znear;                /* +0x018 */
    float x1, y1;               /* +0x01c, +0x020 */
    float zfar;                 /* +0x024 */
    uint32_t clear;             /* +0x028: clear colour (0 = no clear) */
    int32_t update[2];          /* +0x02c: type, + 0x030 (camera update, 0x461060) */
    uint8_t pad034[0x84];
    RFrame frame;               /* +0x0b8 (eye at +0x10c) */
    uint8_t flags[5];           /* +0x118..0x11c */
    uint8_t pad11d[3];
    float range;                /* +0x120 */
    float viewdist, viewdist2;  /* +0x124, +0x128 */
    int32_t v12c;               /* +0x12c */
    float halffov;              /* +0x130: degrees (45 = 90 degrees across) */
    uint8_t active;             /* +0x134: between View_Begin and View_End */
    uint8_t pad135[3];
} RCamera;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(RenderState) == 0x38 && sizeof(RCamera) == 0x138 && sizeof(RFrame) == 0x60, "the original's layouts");
#endif

/* ---- the render system (0x80 bytes at *0x628130) ---- */

typedef struct {
    int32_t v00;                /* +0x00 */
    float znear;                /* +0x04: the cameras' near plane, 0.15 */
    float zfar;                 /* +0x08: far plane, 700 */
    float depthfactor;          /* +0x0c: View_Begin: far / (far - near) * near */
    RCamera *camera;            /* +0x10: the camera between View_Begin and View_End */
    uint8_t pad14[0x44];
    uint32_t cpu;               /* +0x58: CPU_detect */
    int32_t v5c;
    uint8_t windowed;           /* +0x60: 0 (the game always builds the full-screen device) */
    uint8_t pad61[3];
    int32_t v64;                /* +0x64 */
    int32_t mode;               /* +0x68: the video mode set last */
    TexGraphics *graphics;      /* +0x6c */
    RDevice *device;            /* +0x70 */
    TexManager *textures;       /* +0x74 */
    int32_t v78;
    EaglView *view2d;           /* +0x7c: the 2D (orthographic) view */
} RenderSystem;

extern RenderSystem *g_render;      /* 0x628130 */
extern uint8_t g_render_skiplod0;   /* 0x6197e5: draw objects start at their second LOD */
extern uint8_t g_render_carlights, g_render_carenv;   /* 0x6197ca, 0x6197cb (the chrome option 2) */
int Render_CardType(void);           /* 0x6197b0: Render_DetectCard's card (the about block's subtype) */
/* What Render_DetectCard (0x44e760) found the driver takes: each a state set to a test value and back
   (setstate's answer), and the about block's figures. */
typedef struct {
    uint32_t vram;              /* 0x619790: texture memory less three display pages (all of it on Glide) */
    uint32_t vramtotal;         /* 0x619794 */
    uint8_t glide;              /* 0x619798 */
    uint8_t b799;               /* 0x619799: 0 */
    uint8_t fogtable;           /* 0x61979b: state 4 = 2 */
    uint8_t colourkey;          /* 0x61979c: state 0x24 = 0x80 */
    uint8_t b79d;               /* 0x61979d: 1 */
    uint8_t state2f;            /* 0x61979e: state 0x2f = 1 */
    uint8_t state6;             /* 0x61979f: state 6 = 2 */
    uint8_t noglobalclut;       /* 0x6197a0: the about flags' bit 6 clear */
    uint8_t state15;            /* 0x6197a1: state 0x15 = 1 */
    uint8_t state7;             /* 0x6197a2: state 7 = 1 */
    uint8_t b7a3, b7a4;         /* 0x6197a3, 0x6197a4: 0 */
    uint8_t mipmap;             /* 0x6197a5: state 0xb = 1 */
    uint8_t trilinear;          /* 0x6197a6: state 0xb = 3 */
    uint8_t state38;            /* 0x6197a7: state 0x38 = 1 */
    uint8_t b7a9;               /* 0x6197a9 */
    uint8_t trident;            /* 0x6197ad: a "Trident Blade" (state 0x3c set) */
    int32_t texmax;             /* 0x6197b4: the smaller of the largest texture width and height */
    int32_t stages;             /* 0x6197b8 */
} RenderCaps;
extern RenderCaps g_rcaps;
void Render_DetectCard(void);        /* 0x44e760 */
extern uint8_t g_render_modalpha;   /* 0x6197aa: the driver modulates alpha (Render_DetectCard) */
extern uint8_t g_render_extras;     /* 0x6197e6: the extra 'pr' lists (0x40000) and the effects lists */
extern uint8_t g_render_skidmarks;  /* 0x6197e7: skid marks (special effects 2 and up) */
extern uint32_t g_render_skidmask;  /* 0x6197ec: 7 with the marks, else 0xf */
extern RenderState *g_racerstate;   /* 0x62814c: the race's copy of the device's render states */

void Render_Init(void);                                  /* 0x4673b0 */
RenderSystem *Render_Construct(RenderSystem *r, const char *driver, int device, int windowed); /* 0x467720 */
void Render_Shutdown(void);                              /* 0x467680 */
/* The driver opened, its display chosen, the video mode set (GLIDE_env 0x556910, the real library's). */
bool GLIDE_env(bool windowed, const char *driver, int display, int mode);
void Render_SetMode(RenderSystem *r, int mode);         /* 0x467f70: the video mode, when not the one set */
void Render_SetVideoMode(RenderSystem *r, int w, int h, int bpp, int buffers);   /* 0x467f00 */
void Render_DisplayInit(void);                           /* 0x44e190 (RTex's display init) */
void Render_SetupQuality(void);                          /* 0x44ebe0: method key, specials, gamma */
void Render_SetCull(RenderSystem *r);                    /* 0x467ed0: flips the device's cull */
void Render_BeginFrame(RenderSystem *r);                 /* 0x468010 */
void Render_EndFrame(RenderSystem *r);                   /* 0x468030 */

RCamera *Render_CreateCamera(RenderSystem *r, int type, float x0, float y0, float x1, float y1,
                             uint32_t clear, float znear, float zfar);   /* 0x467230 */
void RCamera_Free(RCamera *c);
void RFrame_Init(RFrame *f);                             /* 0x4660a0 */
void RFrame_ToView(RFrame *f);                           /* 0x4661a0 */
/* The camera's viewport (when the display mode changed), view matrix and projection. 0x466e70 */
void RCamera_Setup(RCamera *c);
/* The camera's view as the screen in pixels (x right, y down; z 0..10 orthographic). 0x466f70 */
void RCamera_Setup2D(RCamera *c);
void View_Begin(RenderSystem *r, RCamera *c);            /* 0x467170 */
void View_End(RenderSystem *r);                          /* 0x4671e0 */

/* One race frame (0x468900) for the race renderer: the views, the HUD, the flip. What isn't ported is a
   pending note (render_pending.h). */
typedef struct {
    int32_t v00;
    RCamera *main;              /* +0x04 */
    RCamera *overlay;           /* +0x08 */
    RCamera *mirror;            /* +0x0c */
} RaceViews;
RaceViews *RaceViews_ctor(RaceViews *v);                 /* 0x468da0 */
void Render_MainViews(RaceViews *v);                     /* 0x468ae0 */
void Render_Frame(RaceViews *v);                         /* 0x468900 */
/* The rear-view mirror's camera (type 1: the car's eye looking back) over a rectangle of the screen
   (fractions), made again at each call. 0x469110 */
void Render_SetRearView(float x0, float y0, float x1, float y1, float znear, float zfar);
/* The mirror's view, drawn over the HUD when the HUD shows it. 0x468be0 */
void Render_RearView(void);
