/* What the car code calls that isn't ported yet (car_pending.c; docs/internals/porting.md). Each logs
   once ("pending: Name 0x...") and does nothing. */
#pragma once
#include "render/r3dcar.h"

/* LoadCrp's fallback when the .crp can't be read: the stand-in car ("%seden2", 0x5cdc28). */
void R3DCar_PendingFallback(R3DCar *car, const char *name);

