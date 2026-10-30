/* The application as a per-frame state machine: the original's WinMain loop (front end, race, replays)
   becomes states stepped once per gasm frame. A step never blocks and never polls input itself. */
#pragma once
#include <stdbool.h>

#define APP_FRAME_HZ 60.0
#define APP_AUDIO_RATE 44100

bool app_init(void);        /* data mounted: check the exe, set up the driver; false = fatal (logged) */
bool app_frame(void);       /* one frame; false = the game quit */
void app_audio(float *stereo, unsigned frames);
void app_exit(void);
