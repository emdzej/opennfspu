/* What the car code calls that isn't ported yet (car_pending.c; docs/internals/porting.md). Each logs
   once ("pending: Name 0x...") and does nothing. */
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
/* 0x467ed0: the render system's cull state. */
void R3DObject_PendingRenderState(R3DCar *car);

