/* The race (the port's frame around RaceLoop 0x415300's parts): the track and the racers loaded
   (game/race_setup.c) from the front end's race description, or from the launch parameters for the
   drive viewer (--param drive=<track>), put on the grid, then the race's frames: the input, the
   simulation's task lists at 64 Hz, the cameras, the HUD and its menus, the replay. False if the track or
   the car can't be loaded. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool DRIVE_selftest(const char *track, const char *car);

/* The race the front end set up (its race description: fe.txt's numopponentracecars, num_laps), used
   unless the launch parameters say otherwise. */
extern int g_drive_opponents, g_drive_laps, g_drive_traffic, g_drive_cops;
/* The race\'s kind (0x6573ec) the front end chose: 0 a race, 2 a series race, 3 the lap knockout. */
extern int g_drive_race_type;
/* What the race loop's menus chose (RaceLoop 0x415300's actions on HUD_HandleInput's): 0 go on, 1 the
   race's end (exit to the game menu, or Escape in the race-over), 2 the race again from its start, 3 the
   game's end (exit to windows). */
extern int g_drive_result;
/* Whether the race is paused (0x606a90). */
extern bool g_drive_paused;
/* The racers' places (0 not finished) and times as the results keep them, the local player first; their
   count (at most 8). */
int DRIVE_Results(int places[8], int times[8], bool clean[8]);
/* The player's (racer 0's) driver, car, best lap and race time as the race left them. */
bool DRIVE_PlayerResult(char name[32], char car[32], int32_t *best, int32_t *time);
/* The local racer's stats a Factory Driver mission keeps (0x4ed590's type 9 from the results' copy of
   the car's +0xad4): the race time (+0xcd8), the damage (the ten zones the race's end kept, +0xc98,
   summed with each step truncated, a tenth) and the cones knocked down (+0xb60). */
bool DRIVE_MissionStats(int32_t *time, int32_t *damage, int32_t *cones);
/* The local player's car's parts (their ids) and the wear the race gave them (the results' +0xc48);
   their count (20), 0 without a race. */
int DRIVE_PartsWear(int32_t ids[20], float wear[20]);
/* The local player's place when it finished, else 0. */
int DRIVE_PlayerPlace(void);
/* The race's end, for the front end to take over again. */
void DRIVE_Shutdown(void);
