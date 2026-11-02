/* \real\pc\ddraw.c: the 2D side of the display. DDRAW_init (0x5757a0) puts its own call in place of the
   driver's THRASH_window: the window chosen there becomes the current 2D window too (WINDOW_cur), as the
   display's window drawn through the driver (no pixels: fills and shapes go as quads), or none. */
#pragma once
#include <stdint.h>

void DDRAW_init(void);
/* 0x5deaa8: the video mode the driver last reported (setstate 0x3a); nothing in the game reads it. */
extern uintptr_t g_ddraw_mode;
