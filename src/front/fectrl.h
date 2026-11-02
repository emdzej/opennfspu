/* The controller setup (the options' controllers panels: NFS5.CONTROLLERSETUP, FEControllerSetupControl
   0x51b830 .., vtable 0x5bac84, over the Controller.* accessors of FEdaControllers 0x51d8b0,
   FEControllerInputAccessor 0x51c440, vtable 0x5bacf4). A control shows its accessor's input (an axis,
   a button, a key, a hat) and, pressed, waits for a new one: a key held alone or a joystick's change;
   an input already another's leaves that one without. The inputs are fe.txt's bindings (C1_GAS ..). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fert.h"

/* Its draw (slot 15 0x51bef0), true when c is one. */
bool FECtrl_Draw(FertControl *c, int32_t ox, int32_t oy);
/* A press on c (slot 9 0x51be70), true when c is one. */
bool FECtrl_Click(FertControl *c);
/* The controls' ticks in the window (slot 1 0x51bd60). */
void FECtrl_Tick(FertControl *w);
/* A control of the window waiting for an input, NULL none; the frame's key presses to it (slot 4). */
FertControl *FECtrl_Waiting(FertControl *w);
void FECtrl_Keys(FertControl *c, const uint8_t *k, int nk);
