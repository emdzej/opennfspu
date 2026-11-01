/* The showcase (0x5081e0 .. 0x508630 with FEShowcaseTextAccessor, vtable 0x5b8d64, and the showcase controls):
   a car's pictures (FEData\Showcase\<record +0xe3>.fsh, 993tCo96.fsh without one) and its texts (FEData\Locale\
   <record +0x4d>.loc in the options' language), loaded by the screens' SHOWCASE.<car> callbacks before the
   showcase screen; NFS5.ShowcasePicture draws one of the pictures, NFS5.ANIMSHOWCASEPICTURE the front end's
   show.fsh pictures fading one into the next. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

/* A car's pictures and texts (0x508230, its nfs5.car record); NULL lets them go (0x5081e0). */
void FEShowcase_Load(const uint8_t *rec);
/* Showcase.Text(n) (FEShowcaseTextAccessor_v5 0x508540 -> 0x508370). */
const char *FEShowcase_Text(int n);
/* The SHOWCASE.DEALERCAR, .GENERICCAR, .OWNEDCAR, .USEDCAR, .MERGEDCAR callbacks (0x50d1e0, 0x50d0f0,
   0x50d140, 0x50d190, 0x50d230); false for others. */
bool FEShowcase_Callback(const char *cb);
/* The showcase controls' draws, true when c is one. */
bool FEShowcase_Draw(FertControl *c, int32_t ox, int32_t oy);
