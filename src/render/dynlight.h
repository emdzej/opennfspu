/* The track's dynamic lights (render module 0x455e30 .. 0x456230; the global 0x627dfc): a map from each
   light's record (an article effect of type 4, 'ef' +8) to its texture, filled as the effects are seen,
   never emptied during the race; and the nearest one to a point, which the cars' light block takes for
   the shadow-map layer (rlayers.c). */
#pragma once
#include <stdint.h>

#include "real/texture.h"

/* A light's record (0x40 bytes): its place, its reach, its texture's (id, name), then the two rows of
   five that project the world onto that texture. */
typedef struct {
    float pos[3];
    float radius;     /* +0x0c */
    uint32_t id;      /* +0x10 */
    uint32_t name;    /* +0x14 */
    float rows[10];   /* +0x18 */
} DynLightRec;

_Static_assert(sizeof(DynLightRec) == 0x40, "the light block's +0x14c");

void DynLights_Init(void);   /* 0x455e30 (Render_RaceInit) */
void DynLights_Free(void);   /* 0x455ec0 */
/* The record with its texture, unless it is there already (std::map insert). 0x455f60 */
void DynLights_Add(const DynLightRec *rec, Texture *tex);
/* The nearest light to p within 400 and its reach + 2.5, by the distance max + min / 4 of |dx| and |dz|;
   the first in the map's order on a tie; NULL if none. 0x456030 */
const DynLightRec *DynLights_Nearest(const float *p, Texture **tex);
