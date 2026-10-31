/* The sun and its lens flare (the sky module's 0x4600b0 .. 0x460640; the global 0x627e5c, 0x1a0 bytes):
   the sun sprite ("anus" of flare.dat) drawn at the sun's place among the sky's levels, then after the
   world thirteen flare sprites ("bnus" .. "nnus") on the line from it through the screen's centre, at
   flarepos.dat's distances, as bright as the share of the sun's pixels the world left as they were. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "render/rmesh.h"
#include "render/sky.h"
#include "render/sky_levels.h"
#include "render/sprite.h"

enum { SUN_SPRITES = 14 };

typedef struct {
    RMesh *mesh[SUN_SPRITES];       /* +0x000 (built, not drawn with: the sprites use the shared mesh) */
    SkyBlend *blend[SUN_SPRITES];   /* +0x038 */
    Sprite *sprite[SUN_SPRITES];    /* +0x070: 0 the sun, 1..13 the flare */
    uint8_t tried[SUN_SPRITES];     /* +0x0a8: looked up */
    uint8_t pad0b6[2];
    float *flarepos;                /* +0x0b8: 14 distances along the line (1 the sun, 0 the centre) */
    float pos[4][4];                /* +0x0bc: the meshes' positions */
    float uv[8];                    /* +0x0fc */
    float world[4];                 /* +0x11c: the sun's place around the eye (Sky_SunPosition) */
    float screen[4];                /* +0x12c: projected */
    const SkyConfig *config;        /* +0x13c */
    uint8_t enabled;                /* +0x140: the sun is off the zenith (offsets outside +-0.02) */
    uint8_t pad141[3];
    int32_t state;                  /* +0x144: 0 the sun's pixels not read yet, 1 read, 2 no flare */
    uint32_t ref[16];               /* +0x148: the 4 x 4 pixels round the sun after its draw */
    int32_t bpp;                    /* +0x188: their size (2, or 4 when the last word got data) */
    uint8_t seen;                   /* +0x18c: some of them unchanged at the last read */
    uint8_t onscreen;               /* +0x18d */
    uint8_t pad18e[2];
    float unchanged;                /* +0x190: how many */
    int32_t tick;                   /* +0x194: the last read (ms) */
    float reads;                    /* +0x198 */
    float readms;                   /* +0x19c: the time they took */
} Sun;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(Sun) == 0x1a0, "the original's layout");
#endif

extern Sun *g_sun;   /* 0x627e5c */

void Sun_Create(const SkyConfig *config);   /* 0x4600b0 -> 0x460640 */
void Sun_Free(void);                        /* 0x4600e0 */
/* Among the sky's levels (0x460150): the sun sprite at its projected place, if it is on the screen and
   in front; the first time, its pixels read back as the reference. */
void Sun_Draw(Sun *s);
/* After the world (0x460320 from 0x45d060): every 200 ms the pixels read again and compared; the flare. */
void Sun_DrawFlare(Sun *s);
