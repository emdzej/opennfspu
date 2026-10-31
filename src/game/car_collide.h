/* The cars' collisions (0x493d40-0x499200): against the road's edges (the walls), the world's objects
   and each other, and the collision task (0x498f60) that runs them. */
#pragma once
#include "game/car.h"

/* The car pushed out of a wall at the point p by push, its velocity off the normal n: the impact's size
   (0 when moving away). The wall's flag picks the sound (+0x438). 0x493f70 */
float CarSim_HitWall(Car *c, const float n[3], const float p[3], const float push[3], bool turn);
/* The car's box against the road edges near it. 0x494f90 */
void CarSim_Edges(Car *c);
/* The collision task (even ticks, priority 0x28). 0x498f60 */
void CarSim_Collide(void *car);
