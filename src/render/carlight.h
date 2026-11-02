/* The cars' light (R3DCar_LoadEnvMap 0x440900 .. 0x441110, R3DCar_v15 0x43a490): the track's environment
   map (the global 0x6115d8: <track>.map's colours and road slices, <track>.env's reflection textures) and
   each car's light block (the original's R3DObject +0x2dc, 0x194 bytes, which the car's draw makes the
   layers' light environment 0x6115cc: rlayers.h). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "game/car.h"

struct R3DCar;

struct Texture;

/* A slice of the road (0x48 bytes): its node range and its reflection textures (three levels, open and
   under a roof), their size and aspect. */
typedef struct {
    int32_t start, end;       /* +0x00 */
    uint32_t tex[6];          /* +0x08: texture1..3.open, +0x14: texture1..3.covered (ENV0 by default) */
    uint8_t pad[0x20];
    float size;               /* +0x40: texturesize (6) */
    float aspect;             /* +0x44 (1) */
} CarEnvSlice;

/* The environment map (0x60 bytes). */
typedef struct {
    float tint[4];            /* +0x00: the header's colours (a, r, g, b) */
    float ambient[4];         /* +0x10 */
    float interior[4];        /* +0x20 */
    float interiordiffuse[4]; /* +0x30 */
    float lightdecal[4];      /* +0x40 */
    void *bank;               /* +0x50: the .env textures (an RTexBank of class 2) */
    CarEnvSlice *slices;      /* +0x54 (a vector) */
    int nslices;
    const CarEnvSlice *cur;   /* +0x58: the slice selected last */
} CarEnvMap;

extern CarEnvMap *g_carenv;   /* 0x6115d8 */

/* The track's map and textures (0x440fa0 -> R3DCar_LoadEnvMap) and their end (0x440fd0). */
void CarEnv_Load(const char *track);
void CarEnv_Free(void);

/* A car's light block, by the original's offsets within it (rlayers.h's environment). */
enum { CARLIGHT_SIZE = 0x194 };
typedef struct {
    float f[CARLIGHT_SIZE / 4];
    struct Texture *tex[3];   /* (the original's +0xd0: the slice's reflection textures by level; +0xdc,
                                 the car's sphere maps, are R3DCar's globaltex) */
    struct Texture *lighttex; /* (the original's +0x18c: the nearest dynamic light's texture) */
} CarLight;

/* The car's light now (R3DCar_v15 0x43a490): the light under it on the road (the polygon's colours), the
   map's colours for its exterior, interior and light decals, the sun's direction (straight down under a
   roof), the slice's reflection textures, the road's axes for their projection; camtype the drawing
   camera's update type (+0x2c: 1 in the mirror leaves the reflection textures out). */
void CarLight_Update(CarLight *l, Car *car, int camtype);
/* The car's lights on and their levels from the simulation's car (R3DCar_v6 0x439290; render/r3dcar.h's
   lighton, lightlevel). */
void R3DCar_SetLights(struct R3DCar *r, Car *c);

/* The car's hinge angles from the simulation (R3DCar_Animate 0x438370's race part): wheels, steering wheel,
   needles, shadows. */
void CarAnim_Update(struct R3DCar *r, Car *car);
