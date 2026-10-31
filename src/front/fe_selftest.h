/* Port addition: draws a front-end screen (a layout of FEData\Layouts, e.g. "main.lay") through the
   ported FERT and the front end's 2D queue, without its behaviour yet; call once per frame (the caller
   presents). False if the layout can't be read. */
#pragma once
#include <stdbool.h>

bool FE_selftest(const char *layout);
