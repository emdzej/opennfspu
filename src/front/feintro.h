/* The front end's movies (FEIntro.cpp): the intro once per run (FE_IntroMovies 0x4dd0b0: EArts-av.mad,
   FE_IntroLogo 0x4dcf50 with inPC-av.mad, the loading screen loadscrn.fsh) and the movie player they use
   (showmad 0x4dc300, its frame queue 0x4dc0c0 and buffers initrender 0x4dbc30).

   The original plays a movie in one call, waiting in loops on the clock; the port runs them as states,
   one app frame at a time, the waits returning to the app. The movie goes to the display through the
   software path (the YUY2 overlay of 0x5683b0 needs DirectDraw: the port has none, as a card without one):
   each frame doubled onto the locked front buffer (0x568530 -> 0x56a6d0, mode 3). */
#pragma once
#include <stdbool.h>

/* The intro from its start. */
void FEIntro_Start(void);
/* One app frame of it: false once it's over. */
bool FEIntro_Frame(void);
/* The front end's loading screen (FE_LoadingScreen 0x4dd4b0), a step of FEApplication_Run's start
   after a race: era0.fsh with "loading" in the options' language and the bar at 12.4 pixels a step on
   the back buffer, flipped; after step 8 its art is freed. */
void FE_LoadingScreen(int step);
