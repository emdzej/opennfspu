/* The cars' simulation (0x494000-0x4a4000): the task functions (registered by Car_Construct) as they are
   ported. The car's state (offsets in the car object): +0x330 position, +0x33c velocity, +0x348.. , +0x35c
   (the wheels' speed), +0x364 orientation (3 x 3, rows), +0x388 angular velocity (turns per second about
   x, y, z), +0x7d frozen, +0x7f active, +0x80 the countdown to the next orthonormalisation; the points of
   the car's body from +0x8c (count +0x32c) and their world positions from +0x1dc; per wheel (0xc4 apart
   from +0x7b8) the contact point +0x40, the spin +0x88 (turns), the surface +0x8c. */
#pragma once
#include "game/car.h"

void CarSim_Move(void *car);       /* 0x49b710: every tick, the position by the velocity */
void CarSim_Turn(void *car);       /* 0x49b850: every tick, the orientation by the angular velocity */
void CarSim_BodyPoints(Car *c);    /* 0x49ada0: the body points in the world */
void CarSim_WheelSpin(Car *c);     /* 0x4a2570 */
