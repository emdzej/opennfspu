/* The headlights at night (0x452460 .. 0x452fe0): see headlights.c. */
#pragma once
#include "game/car.h"
#include <stdbool.h>
#include <stdint.h>

extern uint32_t g_headlight_colour;      /* 0x61bd4c */
extern float g_headlight_light[256];     /* 0x61bd60: the points' light of the last Headlights_Light */
extern bool g_headlight_lit;             /* 0x61c16c: the vertex light layer lit a vertex of this draw */

void Headlights_Load(void);              /* 0x452460: night.psh's beam on a night track */
void Headlights_Free(void);              /* 0x452530 */
void Headlights_SetCar(Car *car);        /* 0x452550: the car whose lights they are */
void Headlights_Update(void);            /* 0x452590: its headlight space this frame */
void Headlights_Light(int n, const float *pts, const float *model);   /* 0x4527e0 */
/* The beam's texture (common.fsh 'HEAD', 0x61e1d4) and n points into its tilted space (0x61bd0c). */
struct Texture *Headlights_BeamTexture(void);
void Headlights_BeamPoints(int n, const float *pts, float *out);
/* The police roof light (0x452920): whether one shines this frame, its light on n points, and a vertex
   colour with it added (point i; flip: the pass that takes the other side's colour, as the original's
   first loop does). */
bool Headlights_CopOn(void);
void Headlights_CopLight(int n, const float *pts);
uint32_t Headlights_CopAdd(int i, uint32_t cv, bool flip);
