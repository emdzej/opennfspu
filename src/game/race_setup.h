/* Port addition until the front end is ported: the race's car descriptions as the front end's hand-off
   (0x4ee430, the Car%d.* keys) would write them, for one human player's car taken from the car database
   FEData\Data\nfs5.car (FE_LoadCarList 0x4f30c0: 0x670-byte records). docs/re/cars.md has the fields. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The record whose FILENAME (+0x7f, the model: "964") or SPECNAME (+0xb1: "964coupe36") is name, as race
   car 0, human (CLASS 1); one race car, no traffic. False if the database has no such car. */
bool RaceSetup_SingleCar(const char *name);
/* n opponents (CLASS 2) in car 0's car, as the front end's race set-up writes them (0x4eb430): opponent i's
   driver the i-th record of FEData\Data\nfs5.per (0x2c bytes: +0x0 the id, +0x4 the name's string, then
   four skills and their ranges, slow, medium, fast and default) scaled by the event's difficulty (1 here),
   its personality i; the player at the back of the grid. */
bool RaceSetup_Opponents(int n);
/* The human car's colours the front end chose (the description's +0x218 order), when set. */
extern uint32_t g_race_colours[4];
extern bool g_race_colours_set;
