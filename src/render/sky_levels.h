/* The sky's pieces (the Sky object's layers): the base class SkyLevel_Base (vtable 0x5b3e70; ctor
   0x45a960), the horizon (SkyHorizon, vtable 0x5b3e54, 0x58 bytes) and the levels made from
   Sky\<name>.txt: SkyLevel_BasicSky (render/sky_basic.c); _MultiSky and _FreeClouds are pending. */
#pragma once
#include "render/rmesh.h"
#include "render/sky.h"
#include "render/sky_tex.h"
#include "render/sky_text.h"
#include <stdbool.h>
#include <stdint.h>

struct SkyLevel;

typedef struct {
    /* posA, posB: the eye in weather region A's and B's wind frames (x, -, z: [1] unused) */
    void (*update)(struct SkyLevel *l, const float posA[3], const float posB[3], float blend, int ticks); /* 0 */
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

/* ---- the dome levels (render/sky_basic.c) ---- */

/* The dome's grid (0x28 bytes; 0x460d80). */
typedef struct {
    int32_t count;            /* +0x00: (n+1)^2 */
    float (*verts)[4];        /* +0x04 */
    int32_t n, n1;            /* +0x08, +0x0c */
    float size;               /* +0x10 */
    float curve;              /* +0x14 */
    float height;             /* +0x18 */
    float step;               /* +0x1c: size / n */
    float angle;              /* +0x20: turns */
    float scale;              /* +0x24 */
} SkyGrid;
SkyGrid *SkyGrid_ctor(SkyGrid *g, int n, float size, float curve, float height, float angle, float scale);
void SkyGrid_build(SkyGrid *g, float size, float curve, float height, float angle, float scale);   /* 0x460e00 */

/* The quads' vertex indices (0x10 bytes; 0x460c30). */
typedef struct {
    int32_t nbytes;           /* +0x00 */
    uint8_t *idx;             /* +0x04 */
    int32_t n, n1;            /* +0x08, +0x0c */
} SkyQuads;

/* A sky map's source (vtable 0x5b3ee8): a shape's pixels (SkyMapSource_Texture 0x5b3ef4, 0x10 bytes) or
   noise values, one int each (SkyMapSource_Noise 0x5b3f08, 0x14 bytes). */
struct SkyMapSource;
typedef struct {
    void (*dtor)(struct SkyMapSource *s, int free_self);   /* 0 */
    void (*update)(struct SkyMapSource *s, float t);       /* 1 */
} SkyMapSourceVtbl;
typedef struct SkyMapSource {
    const SkyMapSourceVtbl *vt;   /* +0x00 */
    int32_t size;                 /* +0x04 */
    uint8_t *pixels;              /* +0x08 */
    const uint8_t *shape;         /* +0x0c: the texture's shape */
    float speed;                  /* the noise's +0x0c (the port keeps both fields) */
    float offset;                 /* the noise's +0x10 */
} SkyMapSource;

/* A lightning bolt (0x20 bytes): a pixel of the map brightened up to the peak and back. */
typedef struct {
    int32_t peak;             /* +0x00 */
    int32_t level;            /* +0x04: -1 idle */
    uint8_t *pixel;           /* +0x08 */
    int32_t step;             /* +0x0c */
    int32_t rgb[4];           /* +0x10: the colour grid's cell (a, r, g, b): the channels' caps */
} SkyBolt;

/* The map's storm (0x1930 bytes). */
typedef struct {
    SkyBolt bolt[200];        /* +0x0000 */
    uint8_t *map;             /* +0x1900 */
    int32_t size;             /* +0x1904 */
    int32_t cx, cy;           /* +0x1908, +0x190c: the flash */
    int32_t count;            /* +0x1910: the strikes left */
    int32_t f1914;
    uint8_t active;           /* +0x1918 */
    int32_t nbolts;           /* +0x191c */
    float spread;             /* +0x1920 */
    int32_t strength;         /* +0x1924 */
    int32_t rate;             /* +0x1928: bolts lit a tick */
    int32_t level;            /* +0x192c */
} SkyStorm;

/* The sky map (0x24 bytes; 0x45dc50): the dome's vertex colours, size x size. */
typedef struct {
    SkyStorm *storm;          /* +0x00 */
    SkyMapSource *src[2];     /* +0x04: regions A and B */
    SkyMapSource *alpha[2];   /* +0x0c */
    uint8_t *out;             /* +0x14 */
    uint8_t *saved;           /* +0x18: the map under a storm */
    int32_t count;            /* +0x1c */
    int32_t size;             /* +0x20 */
} SkyMap;

/* Scroller_BasicSky (0x170 bytes; vtable 0x5b3f28): the cloud texture's coordinates over the grid. Per
   stage, pairs: [0], [1] region A, [2], [3] region B, [4], [5] the blend of the two. */
typedef struct {
    const void *vt;           /* +0x000 */
    float (*uv[3])[2];        /* +0x004: per stage */
    int32_t nverts;           /* +0x010 */
    int32_t nidx;             /* +0x014 */
    uint8_t *quads;           /* +0x018 */
    int32_t n;                /* +0x01c */
    int32_t nstages;          /* +0x020 */
    int32_t block;            /* +0x024 */
    float offset[3][2];       /* +0x028: from the eye's position by the move ratio */
    float scroll[3][2];       /* +0x040: by the wind */
    int32_t nwarp;            /* +0x058 */
    float (*warp)[2];         /* +0x05c */
    int32_t ntarget;          /* +0x060 */
    float (*target)[2];       /* +0x064 */
    int32_t nvel;             /* +0x068 */
    float (*vel)[2];          /* +0x06c */
    uint8_t moving[3];        /* +0x070: the stage's wind above 0.25 */
    uint8_t follow;           /* +0x073: a move ratio above 1 */
    uint8_t warping;          /* +0x074: a warp speed above 0.01 */
    int32_t n1;               /* +0x078 */
    float warpspeed[3];       /* +0x07c */
    float warpamount[3];      /* +0x088 */
    float wind[3][6];         /* +0x094 */
    float scale[3][6];        /* +0x0dc: 1 / the scale factors */
    float ratio[3];           /* +0x124: 1 / the move ratio */
    int32_t retarget;         /* +0x130 */
    int32_t steer;            /* +0x134 */
    float rnd[13];            /* +0x138 */
    int32_t rndi;             /* +0x16c */
} Scroller;
Scroller *Scroller_ctor(Scroller *s, int n, int block, int nstages, const float *rec);   /* 0x45e7a0 */
void Scroller_dtor(Scroller *s);
void Scroller_update(Scroller *s, const float pos[3], float blend, int ticks);           /* 0x45ef60 */

/* SkyLevel_BasicSky (0x5c bytes). */
typedef struct {
    SkyLevel base;            /* +0x00 */
    float plane[4];           /* +0x08 (+0x08, +0x10, +0x18, +0x20 in the original): the view's sides */
    uint8_t pad18[0x10];
    SkyConfig *config;        /* +0x28 */
    SkyGrid *grid;            /* +0x2c */
    SkyMap *map;              /* +0x30 */
    Scroller *scroller;       /* +0x34 */
    SkyQuads *quads;          /* +0x38 */
    SkyMapSource *source[4];  /* +0x3c: the maps A, B, their alphas A, B */
    RMesh *mesh;              /* +0x4c */
    Texture *tex;             /* +0x50 */
    SkyBlend *blend;          /* +0x54 */
    uint8_t water;            /* +0x58 */
} SkyBasic;
SkyBasic *SkyBasic_ctor(SkyBasic *s, SkyRecord *rec, SkyTextures *tex, SkyConfig *config, bool water);   /* 0x45acb0 */

/* The sky's lightning (render/sky_draw.c): whether a flash is on (0x627e38 +0), the flash's place on the
   sky's grid and a storm record (0x5cf018, 5 words: strikes, bolts, spread, strength, rate) picked at
   random for its level (0x45cdb0), and the colour grid's cell at offset (a, b) from its middle (7 x 7,
   0x45cae0 of 0x627e3c). */
bool Sky_LightningActive(void);
void Sky_LightningFlash(float pos[2], int32_t rec[5], int *level);
const int32_t *Sky_ColourCell(int a, int b);
