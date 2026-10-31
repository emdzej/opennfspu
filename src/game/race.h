/* The race's simulation step (Sim_Step 0x415010): one 64 Hz tick of the race, the start's countdown, the
   cars' task lists (the even list on even ticks), then the tick counter (g_sim_tick, 0x606ac4). The AI,
   the race's own ticks, audio, the world's animations, the network and the replay are pending. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern int32_t g_race_started;    /* 0x606ac0: set past tick 0x1bf (the countdown's end) */
extern int32_t g_race_countdown;  /* 0x656858: the countdown's step (its ticks from 0x5cc7c4) */

void Sim_Step(void);
