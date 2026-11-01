/* Factory Driver's missions (the front end's mission store, 0x51a170 .. 0x51ac00, and its data area
   FEdaFactoryDriver 0x518f50). The missions are nfs5.fac's 0xf0-byte records (FE_LoadFactory 0x51a420),
   or the player save's "Missions" section (its loader 0x519fe0: a size, then the records), which the
   store works in place so that the save keeps them. A record:
     +0x00 the brief's string (formatted with the player's name)   +0x08 the level, 1 .. 3
     +0x0c the end of level's string (-1 once shown)               +0x10 the prize car's id
     +0x1e the narrator (NARRNAME 0x9c4 + n, FactoryPeople.fsh)    +0x20 the track's id
     +0x24 the skill test (its <track>_ST<n>.scn)                  +0x26 cops, +0x2a the top down
     +0x3f the map's shape file ("1m01")                           +0x58 .. +0x6c pass and fail strings
     +0x70 the car's id, +0x72 its parts' count, +0x74 their ids   +0xa0, +0xb0 its colours (swapped)
     +0xbc the stripes' colour, +0xbe the race number              +0xc1/+0xc2 the time limit
     +0xc4/+0xc6 the damage limit                                  +0xc8 cones: +0xca 0 a limit +0xcc, 1 a penalty +0xce
     +0xd0/+0xd2 the place to beat                                 +0xd4 five opponents (personality, car)
     +0xe8 done (the race's pass flag)                             +0xea .. +0xee the results */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "front/fesave.h"

enum { MISSION_SIZE = 0xf0 };

void FEFactory_Load(void);                 /* FE_LoadFactory 0x51a420 / 0x519fe0, then 0x51a170 */
int FEFactory_Count(void);                 /* 0x69c8d0 */
uint8_t *FEFactory_Mission(void);          /* 0x51a700: the first not done (the first when all are) */
int FEFactory_MissionIndex(void);          /* 0x51a740 */
int FEFactory_Rank(void);                  /* 0x51a680: -1 before the first, the level - 1, 3 when all done */
bool FEFactory_AllDone(void);              /* 0x51a8a0 */
bool FEFactory_LevelDone(void);            /* 0x51a810: the last mission ended a level not yet told */
/* The race's result into the current mission (0x51a770): done = pass (0x657834), the results. */
void FEFactory_Result(int place, int time, int value, int pass);
/* The race cheat's skip (0x51a8e0): missions marked by pass up to a level's end. */
void FEFactory_Skip(int pass);
/* The first n - 1 missions done, the rest not (0x51aa20); once: only the first such call. */
void FEFactory_Cheat(int n, bool once);
SaveCar *FEFactory_Car(void);              /* 0x51aab0: the mission's car object (0x69c8c8) */
const uint8_t *FEFactory_Last(void);       /* 0x51a7f0: the mission last raced (0x69c8f0) */
void FEFactory_ClearEndOfLevel(void);      /* 0x51a800 */
/* Factory.Data's text for an argument (FEFactoryDriverDataAccessor_v5 0x519620); profile: Factory.ProfileNum. */
const char *FEFactory_Data(const char *arg, int profile);
/* The mission's opponents (+0xd4 pairs that are set): their personalities and car ids; their count. */
int FEFactory_Opponents(int pers[5], int cars[5]);
/* The mission's race options (0x4eb6d0, 0x4ec480's skill test) into the race's mission globals. */
void FEFactory_RaceOptions(void);
/* The mission's track's name (0x4fb630's type 9) and its laps; NULL without a mission. */
const char *FEFactory_Track(int *laps);
