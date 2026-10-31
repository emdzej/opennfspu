/* EAGL2: EA's graphics layer between the game's renderer and the THRASH driver (docs/re/eagl.md).

   The pieces, as in Porsche.exe:
   - matrices and matrix stacks (row vectors: v' = v * M, translation in elements 12..14);
   - the vertex transformer of EA's real library (EAGL_Transform*: matrix, strides, clip codes);
   - views: camera stacks, projection, viewport, scene begin/end;
   - the context (one per device, 0x1182c bytes): the vertex work area every draw goes through;
   - materials: a chain of layers run once per draw; each layer fills part of the context (positions,
     colours, texture coordinates, textures, render states) and draw layers run the primitive kernels,
     which clip, project and hand the result to THRASH (setstate 0x1b3).

   Layouts keep the original's offsets (wasm32 is 32-bit like the original): the game's own layers (the
   renderer's R3D / RCustomLayers / Headlights classes) read and write the context by those offsets. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "render/thrash.h"

/* ---- matrices ---- */

typedef struct { float m[16]; } EaglMatrix;

void EAGL_MatIdentity(EaglMatrix *m);                                            /* 0x539a00 */
void EAGL_MatMul(const EaglMatrix *a, const EaglMatrix *b, EaglMatrix *out);    /* 0x539620 (out = a * b; out may not alias) */
void EAGL_MatMulInto(const EaglMatrix *a, const EaglMatrix *b, EaglMatrix *out);/* 0x539600 (thiscall wrapper of 0x539620) */
void EAGL_MatPostMul(EaglMatrix *m, const EaglMatrix *b);                        /* 0x539840: m = m * b */
void EAGL_MatPreMul(EaglMatrix *m, const EaglMatrix *b);                         /* 0x539870: m = b * m */
void EAGL_MatScale(EaglMatrix *m, float sx, float sy, float sz, float sw);       /* 0x5398a0 */
void EAGL_MatTranslation(EaglMatrix *m, float x, float y, float z);              /* 0x5398f0 */
void EAGL_MatFromRotTrans(EaglMatrix *m, const float rot[9], const float t[3]); /* 0x539a40 */
void EAGL_MatToRotTrans(const EaglMatrix *m, float rot[9], float t[3]);         /* 0x539ab0 */
void EAGL_MatCopy(EaglMatrix *m, const EaglMatrix *src);                         /* 0x539b00 */
void EAGL_MatInvertOrtho(EaglMatrix *m);
/* The general inverse, by cofactors over the determinant; false (dst untouched) when |det| < 1e-5. 0x53a1c0 */
bool EAGL_MatInvert(const EaglMatrix *src, EaglMatrix *dst);                                          /* 0x539b20: transpose 3x3, inverse translation */
/* n 4-component points (strides in bytes, 0 = 16) through m; in place when src == dst. */
void EAGL_MatTransform4(const EaglMatrix *m, int n, const float *src, int sstride, float *dst, int dstride); /* 0x539cc0 */

/* A matrix stack (0x1c bytes): the top is the current matrix; serial counts changes (views compare it). */
typedef struct {
    EaglMatrix *base;     /* +0x00 */
    EaglMatrix *top;      /* +0x04 */
    int32_t index;        /* +0x08 */
    int32_t capacity;     /* +0x0c */
    int32_t serial;       /* +0x10 */
    int32_t seen;         /* +0x14: the serial a view last combined */
    uint8_t dirty;        /* +0x18 */
} EaglMatStack;

void EAGL_StackInit(EaglMatStack *s, int capacity);                              /* 0x539bc0 */
void EAGL_StackFree(EaglMatStack *s);                                             /* 0x539c20 */
void EAGL_StackPush(EaglMatStack *s);                                             /* 0x539c30 */
void EAGL_StackPop(EaglMatStack *s);                                              /* 0x539c70 */
EaglMatrix *EAGL_StackTop(EaglMatStack *s);                                       /* 0x539ca0 */
EaglMatrix *EAGL_StackTopForWrite(EaglMatStack *s);                               /* 0x539cb0: marks it changed */

/* ---- the vertex transformer (EA real library, 0x537ed0..0x5385d0 and PACODE 0x5b05c0..0x5b0960) ---- */

/* Flags of EAGL_Transform (low bits as the original's): */
enum {
    EAGL_TF_MATRIX = 0x001,     /* multiply by the matrix (else copy) */
    EAGL_TF_XYZ = 0x002,        /* input has 3 components (w = 1), else 4 */
    EAGL_TF_RHW = 0x008,        /* out[3] = 1 / z */
    EAGL_TF_W1 = 0x010,         /* out[3] = 1 */
    EAGL_TF_CODES_SHIFT = 8,    /* bits 8..10: which clip-code routine */
};
const EaglMatrix *EAGL_SetTransformMatrix(const EaglMatrix *m);     /* 0x537ed0 (NULL = identity) */
const float *EAGL_SetNearFar(const float *nearfar);                  /* 0x537ef0 (NULL = 1.0, 65535) */
uint32_t *EAGL_SetCodeOutput(uint32_t *codes);                        /* 0x537f20 */
int EAGL_SetGuardBand(int shift);                                     /* 0x537f40: |z| scaled by 2^shift for the outer codes */
int EAGL_SetSourceStride(int bytes);                                  /* 0x537fa0 (0 = 16) */
int EAGL_SetDestStride(int bytes);                                    /* 0x537fc0 (0 = 16) */
int EAGL_SetCodeStride(int bytes);                                    /* 0x537fe0 (0 = 4) */
uint32_t EAGL_Transform(uint32_t flags, int n, const void *src, void *dst); /* 0x538320 */

/* Clip codes (per vertex, from the transformer): 1 nearer than near, 2 farther than far, 0x04..0x20 outside
   the guard band (one bit per side plane), 0x0400..0x2000 outside the view frustum (docs/re/eagl.md). A
   primitive is rejected when the AND of its codes & 0x3c03 is set and needs clipping when the OR & 0x3f is. */
enum { EAGL_CODE_REJECT = 0x3c03, EAGL_CODE_CLIP = 0x3f };

/* ---- views ---- */

typedef struct EaglView {
    EaglMatStack view;        /* +0x000: the camera stack */
    uint8_t pad01c[0x40];
    EaglMatStack model;       /* +0x05c: the object stack */
    uint8_t pad078[0x40];
    uint32_t background;      /* +0x0b8: clear colour (setstate 3) */
    uint8_t pad0bc[0x10];
    float znear;              /* +0x0cc */
    float zfar;               /* +0x0d0 */
    float fov;                /* +0x0d4: degrees */
    float aspect;             /* +0x0d8 */
    float zscale;             /* +0x0dc: far / (far - near) */
    float zoffset;            /* +0x0e0: -zscale * near */
    float ortho_height;       /* +0x0e4 */
    float ortho_far;          /* +0x0e8 */
    float ortho_near;         /* +0x0ec */
    float ortho_zscale;       /* +0x0f0: 1 */
    float ortho_zoffset;      /* +0x0f4: 0 */
    float cull_tanx, cull_cosx, cull_tany, cull_cosy; /* +0x0f8..0x104: sphere test (EAGL_ViewSphereVisible) */
    EaglMatrix combined;      /* +0x108: model * view * projection */
    EaglMatrix proj;          /* +0x148 */
    EaglMatrix modelview;     /* +0x188: model * view */
    void *owner;              /* +0x1c8: the device (owner+0x14 = current view) */
    uint32_t pad1cc;
    int32_t proj_serial;      /* +0x1d0 */
    int32_t proj_seen;        /* +0x1d4 */
    float vp_w, vp_h;         /* +0x1d8, +0x1dc: viewport size in pixels */
    float vp_x, vp_y;         /* +0x1e0, +0x1e4 */
    float vp_minz, vp_maxz;   /* +0x1e8, +0x1ec */
    float vp_sx, vp_sy;       /* +0x1f0, +0x1f4: half sizes (screen scale) */
    float vp_cx, vp_cy;       /* +0x1f8, +0x1fc: centre */
    float proj_sx, proj_sy;   /* +0x200, +0x204: screen-size / viewport-size (off-centre viewports) */
    float proj_ox, proj_oy;   /* +0x208, +0x20c */
    float ortho_sx, ortho_sy; /* +0x210, +0x214 */
    float ortho_ox, ortho_oy; /* +0x218, +0x21c */
    int32_t projection;       /* +0x220: 0 perspective, 1 orthographic */
} EaglView;

EaglView *EAGL_ViewInit(EaglView *v, void *owner);                              /* 0x538a80 */
void EAGL_ViewFree(EaglView *v);                                                 /* 0x538af0 */
/* Viewport in pixels (clamped to the display, EAGL_SetDisplaySize); minz/maxz the depth range. */
void EAGL_ViewSetViewport(EaglView *v, float w, float h, float x, float y, float minz, float maxz); /* 0x538b10 */
/* The display size the viewport is clamped to: the original asks its device object (*0x69ed0c, slot 6). */
void EAGL_SetDisplaySize(float w, float h);
void EAGL_ViewGetViewport(const EaglView *v, float *w, float *h, float *x, float *y, float *minz, float *maxz); /* 0x538df0 */
void EAGL_ViewSetPerspective(EaglView *v, float fov, float aspect, float znear, float zfar); /* 0x538f20 */
void EAGL_ViewSetOrtho(EaglView *v, float height, float znear, float zfar);    /* 0x539040 */
void EAGL_ViewBegin(EaglView *v);                                                /* 0x538e40 */
void EAGL_ViewEnd(EaglView *v);                                                  /* EAGL_EndScene 0x538ef0 */
void EAGL_ViewClear(EaglView *v);                                                /* 0x5390f0 */
void EAGL_ViewUpdate(EaglView *v);                                               /* 0x539110 */
/* Whether a world-space sphere can be seen (0x5391f0): the renderers' culling. */
bool EAGL_ViewSphereVisible(EaglView *v, const float *centre, float radius);
/* n points (xyz, 16-byte stride) to screen (x, y, z, 1) in dst (16-byte stride). */
void EAGL_ViewProject(EaglView *v, int n, const void *src, float *dst);         /* 0x539470 */

/* ---- the context ---- */

#define EAGL_MAX_VERTS 252     /* clip-code slots per draw (the CRP index lists are bytes) */


typedef struct EaglContext {
    EaglView *view;            /* +0x00000: the view between ViewBegin and ViewEnd */
    int32_t nverts;            /* +0x00004 */
    int32_t stamp_diffuse;     /* +0x00008: newest diffuse stream copied in this draw (layer position) */
    int32_t stamp_specular;    /* +0x0000c */
    int32_t stamp_uv;          /* +0x00010 */
    int32_t pad014;            /* +0x00014 */
    const float *coords;       /* +0x00018: positions (LayerCoords) */
    void *texture;             /* +0x0001c: stage-0 texture of the draw */
    int32_t prim;              /* +0x00020: 1 strip, 2 line list, 3 triangle list, 4 quad list */
    float znear, zfar;         /* +0x00024, +0x00028 */
    float sx, sy, sz;          /* +0x0002c..: viewport scale */
    float ox, oy, oz;          /* +0x00038..: viewport offset */
    uint32_t codes[EAGL_MAX_VERTS]; /* +0x00044: per-vertex clip codes */
    uint8_t verts[0xfc00];     /* +0x00434: work vertices, ThrashVertex (32 bytes) or ThrashVertex2 (40) */
    uint16_t indices[0x5e8];   /* +0x10034: 504 triangles */
    int32_t ntris;             /* +0x10c04: triangles of the immediate (non-DX7) path */
    void *list_head;           /* +0x10c08 */
    uint8_t pad10c0c[0x3f4];
    uint32_t primcodes[EAGL_MAX_VERTS][2]; /* +0x11000: per primitive {AND & 0x3c03, OR & 0x3f} */
    int32_t pad117e0;          /* +0x117e0 */
    uint32_t flags;            /* +0x117e4: EAGL_CF_* */
    float guardband;           /* +0x117e8: 1, or 2 with a guard band */
    uint8_t sort;              /* +0x117ec: sort this draw's triangles by depth */
    uint8_t pad117ed[3];
    ThrashPrimitive draw;      /* +0x117f0: the primitive passed to setstate 0x1b3 */
    uint16_t out_nverts;       /* +0x11808 */
    uint16_t out_nindices;     /* +0x1180a */
    float depthbias;           /* +0x1180c */
    int32_t twouv;             /* +0x11810: the current THRASH vertex format (setstate 0x2a) */
    void *stage_tex[4];        /* +0x11814: textures of the two-stage draw layer */
    int32_t blend0, blend1;    /* +0x11824, +0x11828: setstate 0x29 / 0x10029 as last set */
} EaglContext;

/* Context flags (+0x117e4): */
enum {
    EAGL_CF_PERSPECTIVE = 0x1, EAGL_CF_ORTHO = 0x2,      /* set by the viewport setup (0x540580) */
    EAGL_CF_TRANSFORMED = 0x8,     /* positions transformed in this draw */
    EAGL_CF_DIFFUSE = 0x10, EAGL_CF_SPECULAR = 0x20, EAGL_CF_UV = 0x40, EAGL_CF_UV2 = 0x80, /* set by a layer */
    EAGL_CF_INSIDE = 0x100,        /* every vertex inside the guard band: no clipping */
    EAGL_CF_REJECT = 0x200,        /* every vertex outside one plane: nothing to draw */
    EAGL_CF_GUARDBAND = 0x10000,   /* the driver's guard band covers 1.5x the screen */
    EAGL_CF_DX7 = 0x20000,         /* the driver is "DX7": draw through setstate 0x1b3 with u16 indices */
};

extern EaglContext *g_eagl;     /* 0x69e0d4 */

EaglContext *EAGL_ContextCreate(void);                                          /* 0x5387a0 (the context part) */
void EAGL_ContextFree(void);                                                     /* 0x538820 */
void EAGL_ContextInit(EaglContext *c, int mode);                                 /* 0x548f80: guard band, driver kind */
void EAGL_ContextResetStages(EaglContext *c);                                    /* 0x540350 */
void EAGL_ContextSetVertexFormat(EaglContext *c, int twouv);                     /* 0x549050 */
void EAGL_ContextViewport(EaglContext *c);                                       /* 0x540580 */
void EAGL_ContextPrimCodes(EaglContext *c);                                      /* 0x540400 */
/* Transforms c->nverts positions (c->coords, stride bytes) with the view's combined matrix into the work
   vertices, with clip codes; sets EAGL_CF_TRANSFORMED / INSIDE / REJECT. */
void EAGL_ContextTransform(EaglContext *c, int stride);                          /* 0x540650 */
/* The same for a caller's arrays (the game's transform layers). */
void EAGL_TransformToContext(EaglContext *c, int n, const void *src, void *dst, uint32_t *codes, int dstride); /* 0x540920 */

/* ---- render states (0x5401e0..0x540320): the THRASH state table set by LayerState ---- */

void EAGL_RSSet(uint32_t state, uintptr_t value);              /* 0x5401e0 */
void EAGL_RSSetDepthBias(uint32_t state, int value);            /* 0x540200 */
void EAGL_RSSetAlias(uint32_t state, uintptr_t value);          /* 0x540240 */
void EAGL_RSSetRelativeBias(uint32_t state, int value);         /* 0x540260 */
void EAGL_RSSetBlend(uint32_t state, int value);                /* 0x5402b0 (-1 = src 7, dest 6) */
void EAGL_RSSetCull(uint32_t state, int value);                 /* 0x5402f0 */
void EAGL_RSProbe(uint32_t state);                              /* 0x540320 */
void EAGL_RSApply(const int32_t values[6]);                     /* 0x5403c0: 6 states, only changes sent */
/* The render-state object's cull mode ([1]+0x10) and depth-bias base ([1]+0x30) (*0x69ed0c). */
void EAGL_RSSetDevice(int cull, int biasbase);
void EAGL_RSReset(void);                                         /* 0x538860: send the whole table */

/* ---- materials and layers ---- */

typedef struct { uint32_t info; void *data; } EaglStream;     /* a material's input slot (8 bytes) */

struct EaglLayer;
struct EaglMaterial;

typedef struct {
    struct EaglLayer *(*execute)(struct EaglLayer *self, EaglStream *s0, EaglStream *s1); /* slot 0: returns the next layer (NULL stops) */
    void (*destroy)(struct EaglLayer *self, int free_it);       /* slot 1 */
    void (*setparam)(struct EaglLayer *self, uint32_t param);   /* slot 2 */
    const uint32_t *(*deps)(struct EaglLayer *self, uint32_t *count); /* slot 3: {type, id} pairs it reads */
    void (*reset)(struct EaglLayer *self);                     /* slot 4 */
    bool (*wants_stream0)(struct EaglLayer *self);             /* slot 5 */
    bool (*wants_stream1)(struct EaglLayer *self);             /* slot 6 */
    uint32_t (*provides)(struct EaglLayer *self);              /* slot 7: what it fills (4 colour, 8 uv, 0x10 coords) */
    uint32_t (*slot8)(struct EaglLayer *self);                 /* slot 8 */
} EaglLayerVtbl;

typedef struct EaglLayer {
    const EaglLayerVtbl *vt;        /* +0x00 */
    struct EaglLayer *prev;         /* +0x04 */
    struct EaglLayer *next;         /* +0x08 */
    uint32_t type;                  /* +0x0c: 1 coords, 2 uv, 3 texture, 5 colours, 0x10 specular, ... */
    uint32_t id;                    /* +0x10 */
    uint32_t param;                 /* +0x14 */
    uint32_t slot0, slot1;          /* +0x18, +0x1c: input stream indices */
    struct EaglMaterial *material;  /* +0x20 */
    int16_t *dep_pos;               /* +0x24: position of each dependency in the chain (-1 none) */
    struct EaglLayer **dep;         /* +0x28 */
    uint32_t ndeps;                 /* +0x2c */
} EaglLayer;

typedef struct {
    void (*destroy)(struct EaglMaterial *self, int free_it);    /* slot 0 (the game's 0x44c250) */
    void (*draw)(struct EaglMaterial *self, EaglStream *streams, int prim, int n); /* slot 1: 0x53ef20 */
} EaglMaterialVtbl;

typedef struct EaglMaterial {
    const EaglMaterialVtbl *vt;     /* +0x0 */
    EaglLayer *layers;              /* +0x4 */
    EaglStream *streams;            /* +0x8 */
} EaglMaterial;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(EaglMatStack) == 0x1c, "matrix stack layout");
_Static_assert(offsetof(EaglView, projection) == 0x220, "view layout");
_Static_assert(offsetof(EaglContext, codes) == 0x44 && offsetof(EaglContext, verts) == 0x434, "context layout");
_Static_assert(offsetof(EaglContext, indices) == 0x10034 && offsetof(EaglContext, primcodes) == 0x11000, "context layout");
_Static_assert(offsetof(EaglContext, flags) == 0x117e4 && offsetof(EaglContext, draw) == 0x117f0, "context layout");
_Static_assert(sizeof(EaglContext) == 0x1182c, "context size");
_Static_assert(sizeof(EaglLayer) == 0x30 && sizeof(EaglMaterial) == 0xc, "layer layout");
#endif

void EAGL_MaterialInit(EaglMaterial *m);                                         /* the inline ctor (vtable 0x5b3c94) */
void EAGL_MaterialDraw(EaglMaterial *m, EaglStream *streams, int prim, int n);   /* 0x53ef20 */
void EAGL_MaterialAddLayer(EaglMaterial *m, EaglLayer *l);                       /* 0x53eed0 */
void EAGL_MaterialBind(EaglMaterial *m);                                         /* 0x53f010: stream slots, dependencies */
/* The nearest layer before / after self with that type (id 0 = any). */
EaglLayer *EAGL_LayerFindPrev(EaglLayer *self, uint32_t type, uint32_t id);      /* 0x53f1a0 */
EaglLayer *EAGL_LayerFindNext(EaglLayer *self, uint32_t type, uint32_t id);      /* 0x53f1d0 */

/* Layer factories: a chain tried newest first; EAGL's own (types 0..0x13) is always last. */
typedef EaglLayer *(*EaglLayerFactory)(uint32_t type, uint32_t id);
void EAGL_RegisterLayerFactory(EaglLayerFactory f);                              /* 0x53ee20 */
EaglLayer *EAGL_CreateLayer(uint32_t type, uint32_t id, uint32_t param);         /* 0x53efa0 */
EaglLayer *EAGL_DefaultLayerFactory(uint32_t type, uint32_t id);                 /* 0x53f6f0 */

/* Base-class pieces for layers defined elsewhere (the game's): */
void EAGL_LayerInit(EaglLayer *l, const EaglLayerVtbl *vt, uint32_t type, uint32_t id); /* 0x53f090 */
void EAGL_LayerFree(EaglLayer *l);                                                       /* 0x53f0c0 */
void EAGL_LayerResolve(EaglLayer *l);                                                    /* 0x53f0f0 */
EaglLayer *EAGL_LayerNext(EaglLayer *l, EaglStream *s0, EaglStream *s1);                 /* 0x53f210 */
void EAGL_LayerSetParam(EaglLayer *l, uint32_t param);                                   /* 0x53f200 */
/* The stream a dependency layer exposes (its material's slot1 entry), or NULL. */
void *EAGL_LayerData(const EaglLayer *l);

/* ---- primitive kernels (DX7 path; docs/re/eagl.md) ---- */

void EAGL_DrawStrip(EaglContext *c, const uint32_t *diffuse, const uint32_t *specular, const float *uv,
                    int stamp_diffuse, int stamp_specular, int stamp_uv);        /* 0x540a40 */
void EAGL_DrawList(EaglContext *c, const uint32_t *diffuse, const uint32_t *specular, const float *uv,
                   int stamp_diffuse, int stamp_specular, int stamp_uv);         /* 0x543840 */
/* Pending (src/eagl/pending.c): lines 0x548120, quads 0x545830, the two-stage kernels 0x542090 (strip),
   0x544780 (list), 0x546bb0 (quads). */
void EAGL_DrawLines(EaglContext *c, const uint32_t *diffuse, const uint32_t *specular, const float *uv,
                    int stamp_diffuse, int stamp_specular, int stamp_uv);        /* 0x548120 */
void EAGL_DrawQuads(EaglContext *c, const uint32_t *diffuse, const uint32_t *specular, const float *uv,
                    int stamp_diffuse, int stamp_specular, int stamp_uv);        /* 0x545830 */
void EAGL_DrawTwoStage(EaglContext *c, const uint32_t *diffuse, const uint32_t *specular, int mode0, int mode1,
                       const int16_t *stamps);                                   /* 0x542090 / 0x544780 / 0x546bb0 */
/* Clips triangle (i0, i1, i2) of verts against the planes in codes; the polygon (32-byte vertices) and
   its vertex count come back through out / nout. */
typedef void (*EaglClipFn)(int i0, int i1, int i2, uint32_t codes, float guardband, const float *verts,
                           int diffuse, int specular, int uv, int uv2, float znear, float zfar,
                           float **out, int *nout);
void EAGL_ClipPerspective(int i0, int i1, int i2, uint32_t codes, float guardband, const float *verts,
                          int diffuse, int specular, int uv, int uv2, float znear, float zfar,
                          float **out, int *nout);                               /* 0x54bd90 */
void EAGL_SortTriangles(EaglContext *c);                                         /* 0x5490e0 */

/* Port addition: draws a test scene through EAGL (view, material, layers, the list kernel) and THRASH. */
void EAGL_selftest(void);
