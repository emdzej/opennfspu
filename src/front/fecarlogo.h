/* The car logo (NFS5.CarLogo, FECarLogoControl 0x21c bytes, vtable 0x5b5900; 0x4c0da0 .. 0x4c19b0): a car's
   logo and its name (two shapes of FEData\Art\finallogos1/2.fsh, the car record's +0x147 file and its +0x1f6
   logo, twice that the first shape) beside each other in the control's box, aligned by data.align, and every
   so often a glint (WeeIcons GLNT) sweeping across it. The car is data.accessor's, or by data.othersource:
   FactoryDriver the mission's, FactoryDriverPrize its prize, EndOfEra the era's prize car, CarCompare1
   the comparison's; Chron the chronicle picture's (its media file's second shape's name: two digits, the
   logo, then the logo file). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

/* Its draw (slot 15 0x4c1750, after its update, slot 1 0x4c1240), true when c is one. */
bool FECarLogo_Draw(FertControl *c, int32_t ox, int32_t oy);
/* The logos no longer on screen let go (slot 22 0x4c1010), keep(c) telling which stay. */
void FECarLogo_Drop(bool (*keep)(const FertControl *c));
