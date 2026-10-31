/* The AI (the [ai] module, 0x403000-0x410000): a controller per race car (0x5e6774[i], made by 0x406580 from
   the car's flags: AIHigh_Human, AIHigh_Opponent, AIHigh_Traffic, AIHigh_Cop, AIHigh_None), each ticked by
   AI_TickAll (0x4067c0) and running a state (AIState_*: Racing, Idle, Chase, Offroad, Purgatory, ...; the
   states live in a pool of 8 at 0x5e6e38). An opponent races in AIState_Racing (0x40ca40): the car's
   place on the road (0x49aee0), its target speed from the road's bends ahead, the surface and its
   driver's skills (0x40b670), its steering at the racing line (the track's <track><dir>.lsp points) with
   the cars ahead to pass (0x40c000), then the drive task (0x49fb20). */
#pragma once
#include "game/car.h"
#include <stdint.h>

extern int32_t g_ai_level;     /* 0x6573f4: the opponents' level (0 .. 2) */
extern int32_t g_ai_drive;     /* the human's car AI-driven (the AIDRIVE key; port: the AI's pedals and steering only) */

/* The race's AI data: the racing line (AI_LoadLineSpeeds 0x4031f0: <track><reversed>.lsp of
   GameData\Track\racer\work\, two floats a node from +4). */
void AI_Load(void);
void AI_Free(void);
/* The controllers for the race's cars (0x406580). */
void AI_MakeControllers(void);
/* Each car's controller. 0x4067c0 */
void AI_TickAll(void);

/* The racing line's point at road node n (x, z). 0x403450 */
void AI_LinePoint(int n, float out[2]);
/* The opponent's target speed (+0xeb8) and its choice at the next junction. 0x40b670 */
void AI_Plan(Car *c);
/* The opponent's pedals and steering (+0xd9c, +0xd9d, +0xda0) towards the racing line ahead. 0x40c000 */
void AI_Steer(Car *c);
