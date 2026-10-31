/* Port addition: drive a car on a track. Loads the track as the track viewer does and one human car
   (game/race_setup.c), puts it on the start line and runs the simulation's task lists at 64 Hz from the
   frame time, with the keyboard (arrows or WASD, space the handbrake) or the pad (left/right steer, A
   throttle, B brake, X handbrake) as the driver; a chase camera follows it. The race's input packets
   (0x412e90), the grid placement and the race cameras aren't ported, so this stands in for them. False if
   the track or car can't be loaded. */
#pragma once
#include <stdbool.h>

bool DRIVE_selftest(const char *track, const char *car);
