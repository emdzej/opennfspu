/* The cars' light halos (0x43cd00): a light of a car ('ef' type 5 of its parts) as a quad facing the eye,
   turned and stretched by the light's axes as they look on screen, in the light type's two textures and
   colours, and in a race its glow on the road (Sprite_DrawCarGlow). */
#pragma once
#include "eagl/eagl.h"

/* A light record ('ef' +8: x, y, z, w; the axes +0x10 and +0x20; the colours +0x40, +0x44; the type
   +0x4c) under the matrix m (the part's on the car's), at level (0 .. 1) of its light; ground the car's
   road height (the glow's). 0x43cd00 */
void R3DCar_LightHalo(const float *light, float ground, const EaglMatrix *m, float level);
