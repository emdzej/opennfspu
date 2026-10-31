/* A race car's hits and its return to the road (the car task 0x4102e0, priority 0x32 on even ticks): the
   last hit remembered (+0x454 .. +0x45c), then for a race car (0x40f7a0) the reset it asks for (+0xd8b 5 from
   the AI, +0xd89 the player's key) and the crash timer (+0x1100: started when it lies on its back or off
   the road, 3 seconds, faster off the road), which put it back on the road (0x40f5a0) at the first free
   node from its own, on the racing line for an opponent, still. */
#pragma once
#include "game/car.h"

/* 0x4102e0 */
void CarSim_HitTask(void *car);
/* Back on the road; kind 0 a crash (not when it is upright and fine), 1 stuck, 2 asked. 0x40f5a0 */
void CarSim_Respawn(Car *c, int kind);
