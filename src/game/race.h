/* The race's simulation step (Sim_Step 0x415010): one 64 Hz tick of the race, the start's countdown, the
   cars' task lists (the even list on even ticks), then the tick counter (g_sim_tick, 0x606ac4). Then the
   AI, the race's odd ticks (audio, the wrong way, the scene's triggers); the network game's tick is the
   one call not ported (multiplayer). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern int32_t g_race_started;    /* 0x606ac0: set past tick 0x1bf (the countdown's end) */
extern int32_t g_race_countdown;  /* 0x656858: the countdown's step (its ticks from 0x5cc7c4) */
/* The race from its start again, for the replay (0x414ed0: the counters, the generator from the race's
   seed, the AI's controllers; 0x411100: the cars' lists in their order, each car set up again), at tick
   start (port: the drive view's start; the original's is 0). */
void Race_Restart(int32_t start);
void App_WaitActive(void);   /* 0x4b0d70 */
extern int32_t g_race_seed;       /* 0x657a34: the front end's rand_seed */
extern int32_t g_race_view;       /* 0x657424: 0 racing, 1 .., 2 the replay */

void Sim_Step(void);
