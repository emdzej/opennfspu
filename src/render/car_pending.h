/* What the car code calls that isn't ported yet (car_pending.c; docs/internals/porting.md). Each logs
   once ("pending: Name 0x...") and does nothing. */
#pragma once
#include "render/r3dcar.h"

/* 0x44deb0 on the car's texture info (+0x480): whether textures load with the geometry (true here). */
bool R3DCar_PendingTexturesWanted(R3DCar *car);
/* 0x44d1f0 + 0x440870: the shared textures 'SPC1'..'SPC3' (race) / 'FEDU', 'FE  ', 'FESH' (front end). */
void R3DCar_PendingGlobalTextures(R3DCar *car);
/* LoadCrp's fallback when the .crp can't be read: the stand-in car ("%seden2", 0x5cdc28). */
void R3DCar_PendingFallback(R3DCar *car, const char *name);
/* R3DObject_LoadHeader 0x4426c0: the texture pages ([tpageN.details], [fileN.details]) composed from the
   .fsh files (R3DCar_LoadDetails 0x443e90, TEXTURE 0x5356c0, DTEX). */
void R3DCar_PendingTexturePages(R3DCar *car);

