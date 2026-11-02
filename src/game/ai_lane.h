/* The AI's lane driver (0x401000 .. 0x403000): see ai_lane.c. */
#pragma once
#include "game/car.h"

/* The lanes' scores, the lane chosen, its lateral target and the speed cap. 0x401000, 0x401060, 0x4012b0 */
void AI_LaneDrive(Car *c);
/* How far a is ahead of b along the road. 0x40e8c0 */
float AI_Ahead(Car *a, Car *b);
