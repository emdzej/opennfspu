/* The era's end (FEEndOfEraCallBackList, vtable 0x5b84fc: made by FEEndOfEra_Run 0x4fcaa0, its update
   _v1 0x4fe730, key _v4 0x4ffd20, draw _v15 0x4fe7a0; the endofera screen, over the victory room, after
   the race that ended an era's last tournament).

   A roll of words flashed one at a time in the middle of the screen, a tenth of a second each (0x691d6c):
   for an era passed, the years from the old era's first to the new one's last, each year in big digits
   (numbers.fsh, at a random place) with the cars of that year after it, the eras' names swelling and
   fading where they begin; with the whole evolution done, the three eras' tournaments with their years.
   Then the closing word letter by letter, swelling and shrinking, flashing out. Behind them the old
   era's pictures (C1 .. M1, C2 .. M2) fade into the new era's, two sets of curves and bars blink in
   gold; near the end the new era's film (er2pc-av .. er4pc-av) plays in the movie oval. The narrator's
   bank (Era-ClassictoGolden.bnk ..) speaks from the start. The film's time over (or Escape), the
   screen goes back. */
#pragma once
#include <stdbool.h>

void FEEndOfEra_Open(void);    /* 0x4fcaa0 */
void FEEndOfEra_Free(void);    /* 0x4fe390 */
bool FEEndOfEra_On(void);
void FEEndOfEra_Update(void);  /* _v1 0x4fe730 (and _v4 0x4ffd20's Escape) */
void FEEndOfEra_Draw(void);    /* _v15 0x4fe7a0 */
