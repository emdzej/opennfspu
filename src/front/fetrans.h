/* The front end's transitions: a screen fading or sliding on and off (FEMenu slots 30 .. 32, 0x4e0540 ..
   0x4e0580) and its controls drawn by its progress (FEControl 0x4c8410's fade, 0x4c8330's offset), by
   their data.transon / data.transoff (0x4c8490). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

void FETrans_On(FertTrans *t);            /* 0x4e0540 */
void FETrans_Off(FertTrans *t);           /* 0x4e0560 */
/* Its tick (0x4e0580); true when it has just come all the way on. */
bool FETrans_Tick(FertTrans *t);
static inline bool FETrans_Gone(const FertTrans *t) { return t->phase == 0; }                    /* 0x4e09b0 */
static inline bool FETrans_Moving(const FertTrans *t) { return t->phase == 1 || t->phase == 3; }  /* 0x4e0980 */
/* The stack's first screen (the application's +0xd8 first entry's current; NULL none), which the
   others follow while they are on (0x4d5e90, 0x4d5dd0). */
void FETrans_SetBase(const FertTrans *base);
/* The larger of t's counter and the first screen's (0x4e0930 with 1). */
int FETrans_Counter(const FertTrans *t);
/* The window whose controls are drawn now (NULL: none, no transition). */
void FETrans_SetWindow(const FertTrans *t);
const FertTrans *FETrans_Window(void);
/* A control's fade 0 .. 0xff (0x4c8410) and its offset added to x, y (0x4c8330) in the window drawn. */
int FETrans_Fade(FertControl *c);
void FETrans_Offset(FertControl *c, int32_t *x, int32_t *y);
