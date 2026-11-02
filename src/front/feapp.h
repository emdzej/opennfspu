/* The front end's application (FEApplication, vtable 0x5b683c; FEApplication_Run 0x4d2e70) as an app
   state: the original runs FERT's event pump (FERT_EventPump_v3 0x529490) until a screen ends it, each
   idle pass a frame (FEApplication_v1 0x4d25d0, drawn by 0x4d2980); the port runs one pass per
   app_frame.

   So far: the screen stack (FEApplication_GotoScreen 0x4d1b50, a screen by its layout's name), the
   mouse as FERT events into the top screen (the pump's poll 0x529530: moves 7, buttons 4 down / 8 up;
   the window's routing 0x5285e0: the control under the mouse gets enter 5 / leave 6), the controls'
   hover state (0x4c7760, 0x4c7820, 0x4c7e70) and the GoTo buttons' destinations (0x4c8f50). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"
#include "game/race_setup.h"

/* The front end's start: its templates, strings, fonts, the 2D queue; then the start screen. */
bool FEApp_Init(const char *start);
/* The sound opened once (GameMain's Audio_Init 0x4a6610, before the intro movies and the front end). */
bool FEApp_Sound(void);
/* A screen by its layout's name ("gamesetup", "quickrace", ...): onto the stack (push), else in place
   of the top one. */
bool FEApp_GotoScreen(const char *name, bool push);
/* Back one screen (the back button's way). */
void FEApp_Back(void);
/* One pass of the pump and a frame. */
void FEApp_Frame(void);
FertControl *FEApp_Top(void);

/* The front end's end before a race (FEApplication_Run's return; its 3D rooms let go, 0x65b928 cleared). */
void FEApp_End(void);
/* The options' language (the front end's strings and the showcase's texts). */
int FEApp_Language(void);

/* The race the front end set up is over: the racers' places (0 not finished) and times, the player
   first, for the career's. */
void FEApp_RaceOver(const int *places, const int *times, const bool *clean, int n, bool quit);
/* The player's result into the track's records (before FEApp_RaceOver). */
void FEApp_RaceRecord(const char *name, const char *car, int32_t best, int32_t time);

/* A message box of a string id up over the screens, closed by its OK (0x512d00). */
void FEApp_Message(int string_id);

/* The race's wear on the player's car (its parts' ids and wear) into the owned car raced, if one was. */
void FEApp_RaceWear(const int32_t *ids, const float *wear, int n);

/* Whether the front end is done: a race to start (PLAYGAME on a screen that can play) or the game to
   end (QUIT); the race it set up. */
typedef enum { FE_RUNNING, FE_PLAY, FE_QUIT } FEResult;
FEResult FEApp_Result(void);
const char *FEApp_Track(void);
const char *FEApp_Car(void);
/* The race car's colours (the description's +0x218 order), NULL for the port's silver. */
const uint32_t *FEApp_Colours(void);
/* The owned car's installed parts for the race (-1 when the car isn't an owned one). */
int FEApp_Parts(int32_t ids[20]);
/* The raced owned car's settings (as FEData_OwnedSetup); false for a car not owned. */
bool FEApp_Setup(float set[17]);
int FEApp_Laps(void);
int FEApp_Opponents(void);
/* The race's traffic count (0x4fb630's fifth) and the career race's own traffic cars (0 slots: the
   track's models). */
int FEApp_Traffic(int16_t slots[7]);
/* The police cars of a Factory Driver mission with police (0x4ec590: its +0x26 gives two), else 0. */
int FEApp_Cops(void);
/* The race's kind (RACE_TYPE, 0x6573ec) for the mode chosen. */
int FEApp_RaceType(void);
/* The knockout's grid (its racers' personalities, the starting places, start[0] the player's); -1 for none. */
int FEApp_Grid(int pers[8], int start[9]);
/* The game type the race was set up for (Game.GameType 0x690aac, the race's mode option 0x30): the test
   drives 6 new, 7 used, 8 the car setup's (0x5dc1b8). */
int FEApp_GameType(void);
/* Factory Driver's opponents one by one (0x4eb830); -1 when the race has none such. */
int FEApp_OpponentCars(RaceOpponent out[8]);
/* A control's callback by name, as a click would run it (port addition: --param fecb=, tests). */
void FEApp_Callback(const char *cb);
/* A click at (x, y) on the top screen: a move there, the button down and up (port addition: tests). */
void FEApp_Click(int32_t x, int32_t y);
