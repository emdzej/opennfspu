/* The Porsche Chronicle (0x506450 .. 0x508200): its callback list (FEChronicleCallBackList, vtable 0x5b8b24,
   the screens with callbacklist ID_CHRONICLE), its data area (FEdaChronicle 0x5080b0: Chronicle.Year, an
   int accessor of 1948 ..), and its controls: the year box (NFS5.ChronicleMainBoxControl, vtable 0x5b8c04),
   the picture (NFS5.ChroniclePicture, 0x5b8c74), its icons (NFS5.ChroniclePictureIcon, 0x5b8ce4) and the
   year slider (NFS5.CHRONICLESLIDER, FEHorizontalSlider 0x5b5fc4 over FESlider_MainFunctionality 0x5b6764).

   The five eras (0x5dba3c: the racing cars, the 911, the 356, the 924, the 914; the 356 at start) each
   have a bank of icons in FEData\Chron (icons<era>.fsh), two shapes an icon (plain, hovered), named by
   their year's two digits and a kind: p pictures, m the media, v the movies. The box shows five columns
   from the slider's year, a year one column per icon set it has; a click on a picture or a media icon
   loads its files (<kind><era letter of "CBADE"><yy><n><set>.fsh, n 1 .. 4) and goes to the picture
   screen, which shows one of them big, dragged about, the others' icons beside it. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

/* The callback list made for a screen (0x506450): the icon banks loaded once (0x5066a0), the year's range
   the era's, the year its first. */
void FEChronicle_Open(void);
/* The banks and the media let go (0x506920, at the front end's end 0x4d5d40). */
void FEChronicle_Free(void);
/* An era's button (RACEMODEL, 911, 356, 924, 914: 0x506d50 .. 0x506e50); false for other callbacks. */
bool FEChronicle_Callback(const char *cb);
int FEChronicle_Era(void);                 /* 0x5dba3c */
/* The era's movie's name (0x506a50: ch5, Ch3, Ch1, Ch2, Ch4, else Ch0), and the movie chosen for the
   movie screen (0x699c98: only media of kind 2 set it, which nothing loads: ""). */
const char *FEChronicle_EraMovie(void);
const char *FEChronicle_Movie(void);
/* The media file the picture shows (0x506b40 of 0x699cfc), NULL without one. */
const char *FEChronicle_Shown(void);
/* The controls' draws (their slot 15), true when c is one of them. */
bool FEChronicle_Draw(FertControl *c, int32_t ox, int32_t oy);
/* The mouse: down on c (slot 5), moved with c under it (slot 8; drags follow it anywhere), left c (slot 7),
   up (slot 9 on c; any drag ends), true when it was c's. x, y in the window's coordinates. */
bool FEChronicle_Down(FertControl *c, int32_t x, int32_t y);
void FEChronicle_Move(FertControl *c, int32_t x, int32_t y);
void FEChronicle_Leave(FertControl *c);
bool FEChronicle_Up(FertControl *c);
