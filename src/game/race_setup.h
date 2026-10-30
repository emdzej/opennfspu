/* Port addition until the front end is ported: the race's car descriptions as the front end's hand-off
   (0x4ee430, the Car%d.* keys) would write them, for one human player's car taken from the car database
   FEData\Data\nfs5.car (FE_LoadCarList 0x4f30c0: 0x670-byte records). docs/re/cars.md has the fields. */
#pragma once
#include <stdbool.h>

/* The record whose FILENAME (+0x7f, the model: "964") or SPECNAME (+0xb1: "964coupe36") is name, as race
   car 0, human (CLASS 1); one race car, no traffic. False if the database has no such car. */
bool RaceSetup_SingleCar(const char *name);
