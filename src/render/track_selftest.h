/* Port addition: a track viewer. Loads the track (GameData\Track\<name>) through Track_Load and draws it
   each frame from a camera at the start line, through the world's visibility test, the draw objects and
   the render lists as the race frame does; the visibility pass's cell walk (0x47ba50, 0x47f410) isn't
   ported, so every object of the world is tested. Call once per frame after the render system is up
   (the caller presents). False if the track can't be loaded. */
#pragma once
#include <stdbool.h>

bool TRACK_selftest(const char *name);
