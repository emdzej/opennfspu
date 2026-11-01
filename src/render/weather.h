/* The weather (0x627e08, 0x458a50 .. 0x4590d0): snow and rain as drops in a box about the eye, kept in the
   camera's own space (turned and moved back by the camera's motion each step, wrapped round the box),
   blown by the wind and its gusts, as additive triangles of sno0..sno3 / rai0..rai3 (by the special
   effects; snow, rain otherwise) from particle.fsh, their counts by the sky's snow and rain levels at
   the eye (0x45ce90, 0x45cec0); and the drops on the hood camera's lens (SPLT, pending). */
#pragma once

/* Made with the race's render (0x458a50 from Render_RaceInit) and gone with it (0x458a80). */
void Weather_Init(void);
void Weather_Free(void);
/* Each frame at the world pass's end (0x458ef0): the steps since the last (one while a race screen is up),
   then the drops drawn about the eye with z writes off. */
void Weather_Frame(void);
/* The ticks and the steps the weather ran (0x627e10, 0x627e14; the lens drops' clock). */
extern float g_weather_time, g_weather_steps;
