/* The display options' HUD preview (NFS5.HUD, FEHudDisplayControl 0x220 bytes, vtable 0x5b603c; 0x4caea0
   .. 0x4cbcc0): a 240 x 180 picture of the race's screen (FEData\Art\hud.fsh, 15 shapes: the screen,
   then a shape for each of the HUD's visuals as each choice shows it), each visual at its place in the
   player's HUD layout, shown as the Display.* choices say. On screen the choices are taken from the
   layout; when the screen goes they are written into it and the layout files saved. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

/* Its draw (slot 15 0x4cb440), true when c is one. */
bool FEHudPreview_Draw(FertControl *c, int32_t ox, int32_t oy);
/* The preview no longer on screen let go (slot 22 0x4cb2e0), keep(c) telling whether it stays. */
void FEHudPreview_Drop(bool (*keep)(const FertControl *c));
