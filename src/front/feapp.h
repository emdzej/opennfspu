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

/* The race the front end set up is over: the player's place (0 not finished), for the career's. */
void FEApp_RaceOver(int place);

/* Whether the front end is done: a race to start (PLAYGAME on a screen that can play) or the game to
   end (QUIT); the race it set up. */
typedef enum { FE_RUNNING, FE_PLAY, FE_QUIT } FEResult;
FEResult FEApp_Result(void);
const char *FEApp_Track(void);
const char *FEApp_Car(void);
int FEApp_Laps(void);
int FEApp_Opponents(void);
