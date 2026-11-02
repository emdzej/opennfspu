/* Port addition: drive a car on a track. Loads the track as the track viewer does and one human car
   (game/race_setup.c), puts it on the start line and runs the simulation's task lists at 64 Hz from the
   frame time, with the keyboard (arrows or WASD, space the handbrake) or the pad (left/right steer, A
   throttle, B brake, X handbrake) as the driver; a chase camera follows it. The race's input packets
   (0x412e90), the grid placement and the race cameras aren't ported, so this stands in for them. False if
   the track or car can't be loaded. */
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
/* The local player's car's parts (their ids) and the wear the race gave them (the results' +0xc48);
   their count (20), 0 without a race. */
int DRIVE_PartsWear(int32_t ids[20], float wear[20]);
/* The local player's place when it finished, else 0. */
int DRIVE_PlayerPlace(void);
/* The race's end, for the front end to take over again. */
void DRIVE_Shutdown(void);
