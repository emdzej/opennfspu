/* Port addition: draws a car (the base name in GameData\CarModel, e.g. "356a") through the ported car
   code from a fixed view, then flips; call once per frame after the driver's mode is set (the caller
   presents). False if the car can't be loaded. */
#pragma once
#include <stdbool.h>

bool CAR_selftest(const char *name);
