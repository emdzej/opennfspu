/* The front end's data accessors (FEAccessor, vtable 0x5b4bbc, and its list kind FEPositionableAccessor):
   named objects in one map (0x65b3a8; the constructor 0x4b7920 registers, a taken name getting a number,
   the destructor 0x4b7c40 lets go; FUN_004b78a0 finds one by name). The controls name them with a field
   and an argument, "Track1.Track(SHORT)": the map holds the name before the argument.

   The port has, so far, the Quick Race screen's lists, made as the data areas make theirs:
   - Game.NumOpponents, an int accessor 0..7 (FEdaGameOptions_v0 0x4e4bc0 -> 0x4b9ef0);
   - Track1.Track, the tracks of nfs5.trk with a model (FE_LoadTrackList 0x4facd0), named by their
     strings;
   - zMergedList.Car and OpponentCar.Car, the racing cars of nfs5.car with a model (FE_LoadCarList
     0x4f30c0, the list 0x6919f0), named by their short names. (The original's lists also follow the
     era and the owned cars, and sort them, 0x4f34c0 / 0x4f05c0: pending.)
   Positions start from fe.txt's race description, what the original's options hold. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct FEAccessor FEAccessor;

void FEData_Init(void);
/* The accessor of a control's path ("Name.Field(ARG)"), NULL if there's none. */
FEAccessor *FEAccess_Find(const char *path);
int FEAccess_Count(const FEAccessor *a);
int FEAccess_Pos(const FEAccessor *a);
void FEAccess_SetPos(FEAccessor *a, int pos);
/* The text of the current item. */
const char *FEAccess_Text(const FEAccessor *a);

/* Its text for a control's argument (slot 5, 0x4bc0e0): SHORT the item's, PERCENT the position in
   per cent, another argument itself (the kinds' own arguments pend). */
const char *FEAccess_TextArg(const FEAccessor *a, const char *arg);
/* A car accessor's current car file (its .crp's name), NULL for other kinds. */
const char *FEAccess_CarFile(const FEAccessor *a);

/* The race the choices describe: the track's name (its .crp), the car's file, the opponents. */
const char *FEData_Track(void);
const char *FEData_Car(void);
int FEData_Opponents(void);

/* The dealer's car bought into the player's save (0x5012a0): 0 done, else the message's string id (too
   little money 0x322, too many cars 0x3d), -1 without a car or a save. */
int FEData_BuyCar(void);
/* A part's value (an nfs5.prt record's +0x164). */
int32_t FEData_PartValue(int32_t id);
/* A car accessor's current car's nfs5.car record (an owned car's copy), NULL for other kinds. */
const uint8_t *FEAccess_CarRecord(const FEAccessor *a);
/* The chosen tournament's record (0xd40 bytes in the save's Tournament section), NULL without one. */
const uint8_t *FEData_Tournament(void);
/* The chosen remaining race of the tournament (its index in the record, -1 none) and its track's file. */
int FEData_TourRace(void);
const char *FEData_TourTrack(void);
/* That race run: the racers' places (0 not finished) and times, the player first. */
void FEData_TourResult(int race, const int *places, const int *times, int n);
/* The career race's laps (0x4fb630: its +0x3a2, 1 when 0; its reverse +0x3a4 and mirror +0x3a5 flags pend). */
int FEData_TourLaps(void);
#include "front/fesave.h"
/* An owned car's value in a year (0x4f83f0). */
int32_t FEData_CarValue(const SaveCar *c, int year);
/* A car's damage in per cent (0x4eefd0). */
float FEData_CarDamage(const SaveCar *c);
/* A car accessor's current car's colours in the description's order (+0x218: body, the slots 0x7c, 0x80,
   interior): an owned car's own, else its record's (the dealer's chosen colour and interior). */
bool FEAccess_CarColours(const FEAccessor *a, uint32_t out[4]);
/* The repair screen's quick fix of the owned car (0x50e9f0): 0 done, else the message's string id. */
int FEData_QuickFix(void);
/* The owned car an accessor's position is (its index in the save), -1 when it isn't one. */
int FEData_OwnedIndex(const char *accessor);
/* A race's wear (the race car's parts by id) added to an owned car's installed parts (the race's
   hand-off of PARTDAMAGE, the description's +0x78, to the front end: the port's). */
void FEData_AddWear(int car, const int32_t *ids, const float *wear, int n);
/* An owned car's installed parts' ids (at most 20, the rest 0); their count. */
int FEData_OwnedParts(int car, int32_t ids[20]);
/* An owned car's seventeen settings (+0x6d0 of its object, the setup screen's order); false without one. */
bool FEData_OwnedSetup(int car, float set[17]);
/* The garage car's settings to a new car's (DEFAULTSETTINGS). */
void FEData_DefaultSetup(void);
/* The garage car's settings kept (SAVESETTINGS) or the kept ones back (LOADSETTINGS, load). */
void FEData_KeepSetup(bool load);
/* The career's year (0x502870). */
int FEData_CareerYear(void);
/* A used car of a record made for the market (0x4f7860): random colours of the record's, stripes and
   race number, mileage, parts and wear for the year, its value as its price. */
bool FEData_UsedCar(const uint8_t *r, int year, SaveCar *out);
/* The records the market's new cars come from (0x6919f0: the racers with a model, in order). */
int FEData_UsedPool(const uint8_t **out, int cap);
