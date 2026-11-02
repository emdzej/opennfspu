/* The colour boxes' custom colour picker (FEColorSelectBox, vtable 0x5b5ba8: NFS5.ColorBox is that combo
   box): chosen from the box's "custom" row (0x4c35c0, 0x65b710 on), it opens under the box with a hue
   and saturation field (weeicons.fsh's COLB), a lightness strip, the colour, the player's eight custom
   colours and OK and cancel. The colour made goes into the slot chosen (the save's) and the box's
   accessor is set to it as it changes; cancel puts the slots and the accessor's place back. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

/* Opened on box c (its focus taken), and whether it is open (0x4c35d0) on c. */
void FEColour_Open(FertControl *c);
bool FEColour_Open_On(const FertControl *c);
FertControl *FEColour_Box(void);
/* Its draw over the box (_v17 0x4c3800), true while open on c. */
bool FEColour_Draw(FertControl *c, int32_t ox, int32_t oy);
/* The mouse while open: kind 0 a press (_v5 0x4c42e0), 1 a move (_v8 0x4c4400, down: the button
   held), 2 a release (_v9 0x4c4530); true when it took it. */
bool FEColour_Mouse(int kind, int32_t mx, int32_t my, bool down);
/* HSL (each 0 .. 1) to an opaque colour and back (0x4d83b0, 0x4d8680). */
uint32_t FEColour_Hsl(float h, float s, float l);
void FEColour_ToHsl(uint32_t c, float *h, float *s, float *l);
