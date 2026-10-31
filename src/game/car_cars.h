/* The cars against each other (0x498040 and its parts 0x495570-0x497a80, 0x4967d0): two cars' boxes
   (centre +0x330, axes +0x364, half sizes +0x3a8) tested face centre and corner against each other
   (0x496f40), pushed apart over up to 32 steps by their masses or their velocities (0x497a80), then one
   impulse at the contact (0x4967d0) for their velocities and spins, the hits' strengths (+0x430) and
   kinds (+0x438), the contact (+0x440) and both cars' damage; repeated up to four times while they stay
   locked, the edges and objects tested again each time, the last time with a heavy knock. */
#pragma once
#include "game/car.h"
#include <stdbool.h>

/* a and b against each other: whether they touched. 0x498040 */
bool CarSim_Cars(Car *a, Car *b);
