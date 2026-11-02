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
/* Controller.AllControllers (pads false: the devices but the mice) and Controller.AllPads: count, names. */
int FECtrl_ListCount(bool pads);
const char *FECtrl_ListName(bool pads, int i);
/* The defaults for device dev (FEControllers_Map 0x51ca50; the DEFAULTCONTROLS button's with the
   AllControllers' device, 0x511820). */
void FECtrl_Map(int dev);
/* AllControllers' i-th device (0x51eab0). */
int FECtrl_ListDevice(int i);
/* The dead zone panel's NFS5.DEADZONE (FEControllerDeadZoneControl, 0x51ae70 ..): a press at mouse x
   on c (true when c is one), and the mouse's moves over hover (or the release) for its handles. */
bool FECtrl_DeadZonePress(FertControl *c, int mx);
void FECtrl_DeadZoneMouse(FertControl *hover, int mx, bool up);
/* The race's RAMPS, RAMPG, RAMPB from the steering's, gas's and brake's inputs (0x51c480's 0x222 .. 0x224:
   1 unless an axis). */
void FECtrl_Ramps(int32_t out[3]);
