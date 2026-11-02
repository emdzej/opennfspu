/* The AI's lane driver (0x401000 .. 0x403000): see ai_lane.c. */
#pragma once
#include "game/car.h"

/* The lanes' scores, the lane chosen, its lateral target and the speed cap. 0x401000, 0x401060, 0x4012b0 */
void AI_LaneDrive(Car *c);
/* Its steps, which the police's chase calls apart: the scores cleared (0x401000), scored (0x401060),
   the lane chosen (0x4012b0); a lane towards lane favoured by 0x5b2400 (0x401380). */
void AI_LaneReset(Car *c);
void AI_LaneScore(Car *c);
void AI_LaneChoose(Car *c);
void AI_LaneBump(Car *c, int lane);
/* How far a is ahead of b along the road. 0x40e8c0 */
float AI_Ahead(Car *a, Car *b);
