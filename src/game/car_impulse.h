/* The car body's collision impulses (0x495ea0-0x496600): the body's points that went through a plane
   (the ground when the car is over on its side or roof, a box's side) push it back out and kick its
   velocity and spin. Used by the force task (0x49bcb0) and the box response (0x498600). */
#pragma once
#include "game/car.h"

/* The body's points behind the plane through `at` with normal n: their mean takes an impulse (0x495ea0,
   with friction fr along the plane), the car moves out by the deepest point's depth, its spin is held
   (0x496440) and the impact recorded (+0x430 4 x the closing speed). 0x496580 */
void CarSim_BodyImpulse(Car *c, const float n[3], const float at[3], float fr);
/* An impulse at `at` (point velocity v) against normal n: the normal part (0.3 of the full, 0.4 or less
   for large traffic), the friction part limited to the sliding speed; tiny ones damp the motion. The
   car's spin (+0x388) is in radians a second here. 0x495ea0 */
void CarSim_ContactImpulse(Car *c, const float at[3], const float v[3], const float n[3], float fr);
/* The spin held to 0.75 turns a second (2.25 for the tiny cars, 6.75 for light traffic). 0x496440 */
void CarSim_ClampSpin(Car *c);
