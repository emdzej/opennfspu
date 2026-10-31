/* The application as a per-frame state machine: the original's WinMain loop (front end, race, replays)
   becomes states stepped once per gasm frame. A step never blocks and never polls input itself. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define APP_FRAME_HZ 60.0
/* The sound library's device rate at the game's high sound quality (0x5b4ae4: 32000, 22050, 16000). */
#define APP_AUDIO_RATE 32000

bool app_init(void);        /* data mounted: check the exe, set up the driver; false = fatal (logged) */
bool app_frame(void);       /* one frame; false = the game quit */
void app_audio(float *stereo, unsigned frames);
/* GetTickCount for the game's renderer: the frames stepped so far at APP_FRAME_HZ, as milliseconds (so
   runs repeat; app_clock.c, in the core for the tests). */
extern uint32_t g_app_frames;
uint32_t app_ticks_ms(void);
void app_exit(void);
