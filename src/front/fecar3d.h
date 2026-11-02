/* The screens' 3D car control (NFS5.3DCAR, FECar3DControl, vtable 0x5b5878; 0x4bf820 .. 0x4c0d10), its
   interactive part over the room's car (position type 0): a click on the car opens or closes what is
   under the mouse, by the car's turn (the room's yaw) and where across the control it falls (0x4c08f0):
   a convertible's roof along its top 5/16, the doors in the middle of a side, the hood and the trunk at
   the ends. Each part opens 0 .. 255 over its own time with its sounds (0x4c0100): the doors on hinges 6
   and 7, the roof on animation slot 1, the hood and the trunk through the car's lids. A second click on
   a door opened that way takes the room's camera into the car (the zoom over half a second, the control
   holding the mouse), the next brings it out. With "damage" the owned car's damage zones show. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

/* A click (the left button down) at (x, y) on a control (FECar3DControl_v9 0x4c0a90): true when c is
   the room car's 3D car control and took it. */
bool FECar3D_Click(FertControl *c, int32_t x, int32_t y);
/* The mouse over it (v8 0x4c0d10: the part under it kept). */
void FECar3D_Move(FertControl *c, int32_t x, int32_t y);
/* Each frame for the screen's room car control (v1 0x4c0100), NULL for none; the car's damage zones
   when it shows damage (NULL none). */
void FECar3D_Tick(FertControl *c, const float *damage);
/* The window the control is in (whose mouse it takes while the camera is in the car). */
void FECar3D_SetWindow(FertControl *w);
/* Whether the control holds the mouse (the camera in the car: its flag 0x40). */
bool FECar3D_Captured(void);
