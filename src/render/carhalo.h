/* The cars' light halos (0x43cd00): a light of a car ('ef' type 5 of its parts) as a quad facing the eye,
   turned and stretched by the light's axes as they look on screen, in the light type's two textures and
   colours, and in a race its glow on the road (Sprite_DrawCarGlow). */
#pragma once
#include "eagl/eagl.h"

/* A light record ('ef' +8: x, y, z, w; the axes +0x10 and +0x20; the colours +0x40, +0x44; the type
   +0x4c) under the matrix m (the part's on the car's), at level (0 .. 1) of its light; ground the car's
   road height (the glow's). 0x43cd00 */
void R3DCar_LightHalo(const float *light, float ground, const EaglMatrix *m, float level);

/* A headlight's beam at night (+0x57 of the record): a fan of nine pairs round the light, near ones 0.3 of
   its axes out in its colour at alpha 0x10, far ones five times as wide and pushed 8 forward along m2's
   -z, transparent; one triangle strip, untextured. m1 the light's matrix, m2 the same without the hinge's
   x turn. 0x43d420 */
void R3DCar_LightBeam(const float *light, const EaglMatrix *m1, const EaglMatrix *m2);
