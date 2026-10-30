/* Pending calls of the car code (car_pending.h). */
#include "render/car_pending.h"
#include "pending.h"
#include <math.h>
#include <string.h>

void R3DCar_PendingStyleExtents(R3DCar *car) { PENDING_NOTE(0x437a30, "R3DCar_LoadStyles (stripe, timed extents)"); }
void R3DCar_PendingPostLOD(R3DCar *car) { PENDING_NOTE(0x43b040, "R3DCar_v20"); }
bool R3DCar_PendingTexturesWanted(R3DCar *car)
{
    PENDING_NOTE(0x44deb0, "RTex_Info");
    return true;
}
void R3DCar_PendingGlobalTextures(R3DCar *car) { PENDING_NOTE(0x44d1f0, "RTex_Find (SPC1..SPC3, FEDU, FE, FESH)"); }
void R3DCar_PendingFallback(R3DCar *car, const char *name) { PENDING_NOTE(0x441940, "R3DCar_LoadCrp (stand-in car)"); }
void R3DCar_PendingDecals(R3DCar *car) { PENDING_NOTE(0x447b80, "R3DCar_Decals"); }
void R3DCar_PendingTexturePages(R3DCar *car) { PENDING_NOTE(0x4426c0, "R3DObject_LoadHeader (texture pages)"); }
void R3DObject_PendingRenderState(R3DCar *car) { PENDING_NOTE(0x467ed0, "Render_SetCull"); }
