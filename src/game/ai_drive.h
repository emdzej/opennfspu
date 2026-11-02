/* The AI's drive (the [ai] module's car task at +0xe1c of every AI car, 0x4083c0): for the cars that
   don't race (the road traffic and the police; a racer returns at once), at the AI's task rate, their
   speed and lane targets (+0xeb8, +0xeb4) turned into motion. On the ground it moves them along the road
   (0x4088a0: the speed eased to its target, the lateral offset to its lane, the orientation the road's);
   off it, or out of control, into forces from the traffic configuration's two sets (0x409cd0, 0x409780)
   integrated into the car's velocities (0x40a230). */
#pragma once
#include "game/car.h"

void AI_DriveTask(void *car);
/* The AI's speed factor for a car at speed v (0x408210): 0 while its engine is cut (+0xedc), the
   traffic's falling with speed, the personality's and the catch-up's. */
float AI_SpeedFactor(Car *c, float v);
/* The drivers' moods (+0xe3c): the events others give them (0 hit by, 1 passed, 2/3 alongside ahead or
   behind, 4/5 swapped places, 6 hooted at) through their personality's script into moods for a time.
   The mood with any of the bits is its car's index, -1 none. */
int AI_Mood(Car *c, uint32_t bits);
int AI_MoodTime(Car *c);
void AI_MoodsReset(Car *c);
void AI_MoodsInit(Car *c, int script);
void AI_MoodNote(Car *c, int car, int event, int tick);
void AI_MoodsTick(Car *c, int dt);
