/* The sky's pieces (the Sky object's layers): the base class SkyLevel_Base (vtable 0x5b3e70; ctor
   0x45a960) and the horizon (SkyHorizon, vtable 0x5b3e54, 0x58 bytes). The sky levels made from
   Sky\<name>.txt (SkyLevel_BasicSky, _MultiSky, _FreeClouds) are pending. */
#pragma once
#include "render/rmesh.h"
#include "render/sky.h"
#include "render/sky_tex.h"
#include <stdint.h>

struct SkyLevel;

typedef struct {
    void (*update)(struct SkyLevel *l, const float posA[2], const float posB[2], float blend, int ticks); /* 0 */
    void (*draw)(struct SkyLevel *l, float heading);                                                   /* 1 */
    void (*dtor)(struct SkyLevel *l, int free_self);                                                  /* 2 */
    void (*rebuild)(struct SkyLevel *l);                                                              /* 3 */
} SkyLevelVtbl;

typedef struct SkyLevel {
    const SkyLevelVtbl *vt;   /* +0x00 */
    uint32_t flags;           /* +0x04: 0x80 drawn by the main view's flag +0x11c, 0x400 blocks the sun */
} SkyLevel;

/* The blend parameters an RMesh stage takes (0x18 bytes; ctor 0x537760: 1, 2, 0, 0, 0, -1). */
typedef struct { int32_t v[6]; } SkyBlend;
SkyBlend *SkyBlend_ctor(SkyBlend *b);   /* 0x537760 */

typedef struct {
    SkyLevel base;            /* +0x00 */
    uint8_t pad08[0x20];
    int32_t npos;             /* +0x28: 0x80 vertices (16 quads, twice) */
    float (*pos)[4];          /* +0x2c */
    int32_t nuv;              /* +0x30 */
    float (*uv)[2];           /* +0x34 */
    int32_t ncol;             /* +0x38 */
    uint32_t *col;            /* +0x3c */
    RMesh *mesh;              /* +0x40 */
    Texture *tex;             /* +0x44 */
    SkyConfig *config;        /* +0x48 */
    SkyTextures *textures;    /* +0x4c */
    SkyBlend *blend;          /* +0x50 */
    uint8_t active;           /* +0x54: the sky file has the horizon's shapes */
} SkyHorizon;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(SkyHorizon) == 0x58, "the original's layout");
#endif

/* The horizon (0x459a20): needs the shape 'hzcl' and 'horz' or 'hor<level>'. */
SkyHorizon *SkyHorizon_ctor(SkyHorizon *h, SkyTextures *textures, SkyConfig *config);
