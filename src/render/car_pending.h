/* What the car code calls that isn't ported yet (car_pending.c; docs/internals/porting.md). Each logs
   once ("pending: Name 0x...") and does nothing, except R3DMaterial_Draw, which stands in for the
   render methods (see below). */
#pragma once
#include "render/r3dcar.h"

/* R3DCar_LoadStyles' tail 0x437b00..: the stripe colour and the hood / trunk / headlight extents eased
   over time (the car's animation controller at +0x64, GetTickCount). */
void R3DCar_PendingStyleExtents(R3DCar *car);
/* R3DCar_v20 0x43b040: after the LODs (headlights, the animation slots' timings). */
void R3DCar_PendingPostLOD(R3DCar *car);
/* 0x44deb0 on the car's texture info (+0x480): whether textures load with the geometry (true here). */
bool R3DCar_PendingTexturesWanted(R3DCar *car);
/* 0x44d1f0 + 0x440870: the shared textures 'SPC1'..'SPC3' (race) / 'FEDU', 'FE  ', 'FESH' (front end). */
void R3DCar_PendingGlobalTextures(R3DCar *car);
/* LoadCrp's fallback when the .crp can't be read: the stand-in car ("%seden2", 0x5cdc28). */
void R3DCar_PendingFallback(R3DCar *car, const char *name);
/* 0x447b80: the decal / livery shapes of the chosen options from <car>d.fsh / <car>l.fsh. */
void R3DCar_PendingDecals(R3DCar *car);
/* R3DObject_LoadHeader 0x4426c0: the texture pages ([tpageN.details], [fileN.details]) composed from the
   .fsh files (R3DCar_LoadDetails 0x443e90, TEXTURE 0x5356c0, DTEX). */
void R3DCar_PendingTexturePages(R3DCar *car);
/* 0x44ba40: a material's render method ('RMth', the layer chain it draws with); NULL until ported. */
void *R3DMaterial_PendingMethod(R3DCar *car, R3DMaterial *m);
/* 0x44a090 (the material set's per-draw reset) and 0x467ed0 (the render system's cull state). */
void R3DLOD_PendingBegin(R3DCar *car);
void R3DObject_PendingRenderState(R3DCar *car);

/* One primitive list of a part through its material: the original binds the 'pr' item's streams into
   the material's render method (0x44a190) and draws with it (the method's EAGL material, slot 1). Until
   render methods are ported this resolves the streams the same way and draws them with a port stand-in:
   positions (kind 0) through their index list, shaded grey from the normals (kind 1) against a fixed
   light, untextured. The stand-in is not the original's look; it exists so the geometry can be seen. */
void R3DMaterial_Draw(R3DCar *car, R3DPart *part, R3DMaterial *mat, const uint32_t *pr, uint32_t n,
                      EaglMatStack *model);
