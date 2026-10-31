/* A race car's set-up at the race's start (0x410790): its simulation state, its grid slot along the road
   from the start line (0x4105e0), its placement on the road there (0x49b000), its progress along the
   race (0x4162b0), its controls and race state cleared. */
#pragma once
#include "game/car.h"
#include <stdint.h>

extern int32_t g_race_reverse;    /* 0x657430: the track driven backwards */
extern int32_t g_race_linear;     /* 0x657494: a point-to-point race (no laps) */
extern int32_t g_race_type;       /* 0x6573ec: the game type (4: the skill test) */
extern int32_t g_race_knockout;   /* 0x657400 (with g_race_mode 3: the grid spread over the road) */
extern int32_t g_race_side;       /* 0x5e753c: the grid's side flip with the car's direction (1 by 0x40e6d0) */
extern int32_t g_hud_metric;      /* 0x628a28: the units (km/h); the car's +0xd87 */

/* The race's progress at road node n, 0..1 along the race (from its segment's ends; backwards on a
   reversed track, where a circuit's 1 is 0). 0x4162b0 */
float Race_Progress(int n);
/* The car's grid slot: the road node (*node) behind `start` by its grid position (three nodes a slot
   with more than two cars), and its offset across the road (out, in the road's axes: across, up 0.5, along
   0): left or right of the centre by the slot's parity. 0x4105e0 */
void Race_GridSlot(Car *c, int start, int *node, float out[3]);
/* The car on the road at node n (at least 1), offset by off in the road's axes (or at off itself for a
   respawning opponent past the start): its frame from the road's polygon and direction (dir -1 turns it
   round), the body's and the wheels' trackers, the road tracking, the wheels on the ground. 0x49b000 */
void CarSim_Place(Car *c, int n, const float off[3], int dir);
/* The car's state for the race: the simulation's (0x49b480), the part wear from the description, the grid
   placement from the start line, the race progress, the controls and the race's counters cleared.
   0x410790 */
void Car_RaceInit(Car *c, uint32_t id);
