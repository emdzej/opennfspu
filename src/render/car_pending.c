/* Pending calls of the car code (car_pending.h). */
#include "render/car_pending.h"
#include "pending.h"
#include <math.h>
#include <string.h>

void R3DCar_PendingFallback(R3DCar *car, const char *name) { PENDING_NOTE(0x441940, "R3DCar_LoadCrp (stand-in car)"); }
