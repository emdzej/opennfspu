/* The race's progress (0x4169d0, 0x416330, 0x417460, Race_TickEven 0x417820, Race_TickOdd 0x417030): each
   car's distance along the race (+0xaf8: laps plus the road's progress), its laps (+0x530, the wrong-way
   ones +0x534), lap times and top speeds, its finish (+0xb50: 2 finished, 3 out), the cars' order, the
   ranking of distances, and the race's end once every human has finished. */
#pragma once
#include "game/car.h"
#include <stdint.h>

extern int32_t g_race_laps;       /* 0x6573f0 */
extern int32_t g_race_over;       /* 0x606a8c */
extern int32_t g_race_quit;       /* 0x657e34: the race left from its menus (no records kept) */
extern int32_t g_race_local;      /* 0x65741c: the local player's index */
extern int32_t g_race_inputs;     /* 0x658084: the players giving input */
extern int32_t g_race_end_wait[4];  /* 0x656ae4, 0x657300, 0x6573a8, 0x6573ac (the replay's marks: 0, 0, -1, -1) */

/* A row of the distance ranking (0x608b38, 16 bytes): the car's index (+0x520), its distance, the tick it
   got there, a human's flag. */
typedef struct { int32_t car; float dist; int32_t tick; int32_t human; } RaceRank;
extern RaceRank g_race_rank[CAR_MAX];

/* The leaders the race tracks (0x5e6df0 ..): the first human racer still racing, the first human, the
   first and last rival in the order. 0x40b3f0 */
extern Car *g_lead_human, *g_first_human, *g_first_rival, *g_last_rival;

/* The car task (every tick, priority 0x19; on even ticks): a racer's odometer (+0xd00) and progress, the
   others' distance from the road. 0x4169d0 */
void CarSim_RaceTask(void *car);
/* A racer's progress, laps, finish and its row in the ranking. 0x416330 */
void Race_CarProgress(Car *c);
/* Laps counted from the distance crossing a whole number (a circuit), or the finish's progress (a
   point-to-point race). 0x417460 */
void Race_CountLap(Car *c);
void Race_TickEven(void);   /* 0x417820 */
void Race_TickOdd(void);    /* 0x417030 */
/* The human players' wrong-way count (+0xd30, ticks going against the race; 0 when right, finished, AI
   driven, respawning, or in the pursuit), the audio's player tick's part (0x4a7430). */
void Race_WrongWay(void);
/* The racers' places (+0xb48, from the ranking: 1 the first, 0 when not ranked); the first human's row.
   In the two-player split race (0x657420, one human) the catch-up factors +0x53c by the gap to it.
   0x415fe0 */
void Race_UpdatePositions(void);
/* The ranking emptied (its first eight rows: car -1, distance -100), before the cars' tasks rank them
   again (0x416160, named Sim_ResetCollisionSlots in the Ghidra project). */
void Race_ResetRanking(void);
/* The race's results into the cars (+0xcc4 .. +0xd00, what the front end reads back): the humans' (kind
   1, quit: not finished) and the opponents' still racing given a time from their progress. 0x416ae0 */
void Race_End(int kind);

/* The car's half extent across the road: its box's half sizes along the road's right. 0x4176a0 */
float Race_CarAcross(Car *c);
/* The lane at offset x from node's centre line: 7 and up to the right by the lane width, 6 and down to
   the left; 2 .. 11. 0x40eb80 */
int Race_LaneAt(int node, float x);
/* The lanes the car covers (+0xea8, a bit per lane) and the one its centre is in (+0x1020). 0x40ec10 */
void Race_CarLanes(Car *c);
/* Off the ground or out of control (0x408870): flag 0x800, no wheel down, or a police car with one. */
bool Car_OutOfControl(Car *c);
