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

/* The front end's start: its templates, strings, fonts, the 2D queue; then the start screen. */
bool FEApp_Init(const char *start);
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

/* The race the front end set up is over: the racers' places (0 not finished) and times, the player
   first, for the career's. */
void FEApp_RaceOver(const int *places, const int *times, int n);

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
int FEApp_Laps(void);
int FEApp_Opponents(void);
