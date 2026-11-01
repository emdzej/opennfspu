/* The front end's movies in its screens (FEMovie.cpp: FEMovie, vtable 0x5b6be4, 0x4e1690 .. 0x4e2150) and
   the controls that show them: NFS5.MOVIEBOX (FEMovieBox, vtable 0x5b6198, 0x4cc9b0 .. 0x4cd080) and
   NFS5.MOVIECIRCLE (FEMovieCircle, vtable 0x5b6208, 0x4cd1f0 .. 0x4cd600: the box's movie cut into the
   menus' oval by its stencil shapes).

   A movie is decoded frame by frame (real/mad.c) into a 256 x 256 window of the front end's depth, the
   window's shape is the texture "MOVIE", and the front end draws that texture's top-left w x h stretched
   over the box. The frames follow the clock (TIMER_ticks), one at most per update without sound. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

typedef struct FEMovie FEMovie;

extern int g_fe_movies;   /* 0x6907a0: the movies open */

/* A movie (its path) shown at (x, y) over w x h, with its sound or not; notex without its window and
   texture. A missing file gives a movie that does nothing. FEMovie_Load 0x4e1690 */
FEMovie *FEMovie_Load(const char *path, int x, int y, int w, int h, bool audio, bool notex);
void FEMovie_Free(FEMovie *m);                          /* 0x4e1a30 */
/* Its frame at the box offset by (ox, oy), in colour (the alpha its translucency). 0x4e2000 */
void FEMovie_Draw(FEMovie *m, int32_t ox, int32_t oy, uint32_t colour);
bool FEMovie_Ended(const FEMovie *m);                   /* 0x4e2140: it came to its end once */
bool FEMovie_Playing(const FEMovie *m);                 /* 0x4e1b50 */
void FEMovie_Pause(FEMovie *m);                         /* 0x4e1b40 */

/* The movie controls of the current screens: their draw (FEMovieBox_v15 0x4cd080, FEMovieCircle_v15
   0x4cd520; the first makes the box, its v20 and v21), true when c is one; their update in the front
   end's update pass (FEMovieBox_v1 0x4cce50: it may change the screen). */
bool FEMovieBox_Draw(FertControl *c, int32_t ox, int32_t oy);
void FEMovieBox_Update(void);
/* The boxes no longer on screen freed (FEMovieBox_v22 0x4cce20), keep(c) telling which stay. */
void FEMovieBox_Drop(bool (*keep)(const FertControl *c));
