/* The cars' damage (0x494470, 0x494d60): nine zones of the body (+0x4e4, floats 0..100: 0 front .. the
   eight around the car, 8 the roof), raised by hard impacts and spreading half to the neighbours, and the
   kick a hit gives the car's spin. */
#pragma once
#include "game/car.h"
#include <stdint.h>

extern int32_t g_race_damage;   /* 0x657a2c: damage on (the front end's option) */

/* The impact +0x430 (over 10) on the zone facing n (the car's frame; the roof when it points down past
   0.5, for race cars), kick 0 none, 1 a hit, 2 a car-on-car hit. 0x494d60 */
void CarSim_ImpactDamage(Car *c, const float n[3], int kick);
/* Damage `impact` to zone z: half the impact (1.5 x in a knockout race), at most 100, kept if higher;
   the neighbours raised to the mean of their sides; with kick, the car's spin knocked about the zone's
   axis (over 80 the car is thrown: up, out of control, randomly turned with kick 2). 0x494470 */
void CarSim_ZoneDamage(Car *c, float impact, int z, int kick);
