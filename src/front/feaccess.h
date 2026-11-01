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
