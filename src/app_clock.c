/* The port's GetTickCount (app.h): app_frame counts the frames. */
#include "app.h"

uint32_t g_app_frames;

uint32_t app_ticks_ms(void) { return (uint32_t)((double)g_app_frames * 1000.0 / APP_FRAME_HZ); }
