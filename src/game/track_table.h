/* The track's AI tuning: Track_Table (0x4a2ba0) and the track's factor for a car (0x4a2730). The original
   keeps the tuning as code (per track and direction, immediates stored into the zones, then a factor by
   the car's class, 0x4a29a0); the port runs those three functions from the user's Porsche.exe in a small
   interpreter of the instructions they use, so no table of them is in the source. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TRACK_NZONES 10

/* The road's width the AI's speeds scale by (0x5e6e34) and ten speed zones: node ranges (0x5e74ac, the
   pairs) with their factors (0x5e74fc). Track_Table sets them for the race's track. */
extern float g_track_road_width;
extern int32_t g_track_zone[TRACK_NZONES][2];
extern float g_track_zone_f[TRACK_NZONES];

/* The track's factor for the AI's speeds of a car of the AI spec name aispec (the description's +0x2a8),
   setting the zones too. Track_Table 0x4a2ba0 */
float Track_Table(const char *aispec);
/* The track's factor for the car's +0x140 spec. 0x4a2730 */
float Track_CarFactor(const char *aispec);
/* Whether the last run finished (false: the code wasn't the expected build's, the results are defaults). */
bool Track_TableOk(void);
