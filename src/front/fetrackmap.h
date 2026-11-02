/* The tournament's track map (NFS5.TrackSmall2dSpline, FETrackSmall2DSpline 0xd78 bytes, vtable 0x5b8128;
   0x4fbf40 .. 0x4fc5b0): the chosen tournament's races' maps in turn, two seconds each (256 ticks), the
   track's map (FEData\TrackArt\<+0x58>.fsh, shape 0: "AlpsSpSm") fading in and out in gold, its start
   and direction marks (shape 1, or 2 for a reversed race) over it in white, both flipped across for a
   mirrored one; beside it the races' list, a dash and the track's name each, the one shown in gold. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

/* Its draw (slot 15 0x4fc330, after its update, slot 1 0x4fc0f0), true when c is one. */
bool FETrackMap_Draw(FertControl *c, int32_t ox, int32_t oy);
/* The maps no longer on screen let go (slot 22 0x4fc040), keep(c) telling which stay. */
void FETrackMap_Drop(bool (*keep)(const FertControl *c));
