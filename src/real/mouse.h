/* The real library's mouse (MOUSE_getstate 0x537010 with 6: the position in the display mode's pixels and
   the buttons). The port's from gasm's pointer, the present's letterbox undone. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* False when there is no pointer. buttons: bit 0 the left. */
bool MOUSE_state(int32_t *x, int32_t *y, uint32_t *buttons);
/* Port addition for the headless tests: the state given instead of the pointer's (on false: the
   pointer's again). */
void MOUSE_Override(bool on, int32_t x, int32_t y, uint32_t buttons);
