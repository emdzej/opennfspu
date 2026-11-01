/* The cars' drive (0x493810-0x4a2000): the driver's controls into the pedals and steering the model
   reads, the engine and gearbox, the brakes, the tyres' forces, and the drive task (0x49fb20) that sums
   them into the car's accelerations.

   The car's controls (bytes of the car object): +0xd7c the throttle as driven (0..255), +0xd7d the brake,
   +0xd7e the gear asked for, +0xd7f / +0xd80 the throttle and brake the model uses, +0xd81 the previous
   gear, +0xd82 the gear (0 reverse, 1 neutral, 2 first..), +0xd83 a downshift, +0xd84 the shift's
   countdown; +0xd90 the steering as driven (-127..127), +0xd94 as the model uses it. */
#pragma once
#include "game/car.h"

extern float g_drive_throttle, g_drive_brake, g_drive_steer;
extern int32_t g_drive_front_down, g_drive_rear_down;
extern int32_t g_race_demo;
extern int32_t g_race_players;
extern int32_t g_race_mode;      /* 0x6573e8 */
extern int32_t g_race_alt_grip;  /* 0x606ae0 */

/* The controls into the model's pedals, gear and steering. 0x49db10 */
void CarSim_Controls(Car *c);

/* The parts' condition (their wear by the damage of the zones they cover): the engine's mean (with the
   car's +0x4e8), the brakes' and the gearbox's sums. 0x493810, 0x493980, 0x493a10 */
float CarSim_EngineWear(Car *c);
float CarSim_BrakeWear(Car *c);
float CarSim_GearboxWear(Car *c);
/* The suspension's mean (types 4, 5, 8) and the body's (its nine zones, scaled). 0x493b30, 0x493c00 */
float CarSim_SuspensionWear(Car *c);
float CarSim_BodyWear(Car *c);
/* Gravity's pull along slopes into the drive's forces (+0xd54 forward, +0xd4c sideways). 0x49eeb0 */
void CarSim_SlopePull(Car *c);

/* The drag factor of drafting behind the race's cars (1 = none). 0x49d7a0 */
float CarSim_DraftFactor(Car *c);
/* The automatic gearbox's shifts. 0x49d8e0 */
void CarSim_AutoShift(Car *c);
/* The engine: its speed (+0xdb4) by the throttle, the clutch and the wheels, and the drive torque less
   the air's drag. 0x49e230 */
float CarSim_Engine(Car *c);

/* The aero parts' wear (type 6). 0x493aa0 */
float CarSim_AeroWear(Car *c);
/* The wheels' loads (+0xa8 of each tracker: +0x864, +0x928, +0x9ec, +0xab0) from the weight, the grip of
   their surface and tyre, the transfer by braking and cornering, and the downforce; the gravity scale
   (+0x84). 0x4a16e0 */
void CarSim_TyreLoads(Car *c);

/* The tyres' wear (type 9). 0x4938f0 */
float CarSim_TyreWear(Car *c);
/* Wheel w's forces from its torque, load, slip and the friction circle (the wheel objects at
   +0x7b8 + 0xc4 w). 0x4a1eb0 */
void CarSim_Tyre(Car *c, int w);

/* The drive task (even ticks, priority 0x1e): the controls, steering, engine, loads and tyres into the
   car's accelerations; on its side or roof no grip. 0x49fb20 (0x49ef50) */
void CarSim_Drive(void *car);

/* The drive's state back to the start: neutral (first gear for an automatic), the wheels' trackers the
   body's, the front wheels marked. 0x49e010 */
void CarSim_ResetDrive(Car *c);
