/* The front end's data accessors (FEAccessor, vtable 0x5b4bbc, and its list kind FEPositionableAccessor):
   named objects in one map (0x65b3a8; the constructor 0x4b7920 registers, a taken name getting a number,
   the destructor 0x4b7c40 lets go; FUN_004b78a0 finds one by name). The controls name them with a field
   and an argument, "Track1.Track(SHORT)": the map holds the name before the argument.

   The port has, so far, the Quick Race screen's lists, made as the data areas make theirs:
   - Game.NumOpponents, an int accessor 0..7 (FEdaGameOptions_v0 0x4e4bc0 -> 0x4b9ef0);
   - Track1.Track, the tracks of nfs5.trk with a model (FE_LoadTrackList 0x4facd0), named by their
     strings;
   - zMergedList.Car, the racing cars of nfs5.car with a model (FE_LoadCarList 0x4f30c0, the list
     0x6919f0), named by their short names; OpponentCar.Car, which of them the opponents race. (The original's lists also follow the
     era and the owned cars, and sort them, 0x4f34c0 / 0x4f05c0: pending.)
   Positions start from fe.txt's race description, what the original's options hold. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct FEAccessor FEAccessor;

void FEData_Init(void);
/* The accessor of a control's path ("Name.Field(ARG)"), NULL if there's none. */
FEAccessor *FEAccess_Find(const char *path);
int FEAccess_Count(const FEAccessor *a);
int FEAccess_Pos(const FEAccessor *a);
void FEAccess_SetPos(FEAccessor *a, int pos);
/* An int accessor's value (its first plus its place) and its range made lo .. hi (0x4ba0d0: a place past
   the new end back to 0). */
int FEAccess_Int(const FEAccessor *a);
void FEAccess_SetRange(FEAccessor *a, int lo, int hi);
/* The text of the current item. */
const char *FEAccess_Text(const FEAccessor *a);

/* Its text for a control's argument (slot 5, 0x4bc0e0): SHORT the item's, PERCENT the position in
   per cent, another argument itself (the kinds' own arguments pend). */
const char *FEAccess_TextArg(const FEAccessor *a, const char *arg);
/* A car accessor's current car file (its .crp's name), NULL for other kinds. */
const char *FEAccess_CarFile(const FEAccessor *a);

/* The race the choices describe: the track's name (its .crp), the car's file, the opponents. */
const char *FEData_Track(void);
/* The track's eight records (nfs5.trk's, 0x4ec200): the race and lap times, drivers, cars. */
bool FEData_TrackRecords(const char *track, int32_t race[8], int32_t lap[8], char names[8][32], char cars[8][32]);
/* A time entered among the track's records, the later ones moved down (0x4fa560: before the first
   slower; none, not entered), the driver and car (17 letters) with it; nfs5.trk saved (0x4fa320). */
void FEData_TrackRecordEnter(const char *track, const char *name, const char *car, int32_t t);
/* zMergedList.Car's car one of the player's (0x4f4420: its place before the owned cars' count). */
bool FEData_MergedOwned(void);
/* GenericCar.Car's nfs5.car record (the merged list's car past the owned ones), NULL on an owned car. */
const uint8_t *FEData_GenericCar(void);
/* The track a game type races a circuit (0x4fb4f0: its nfs5.trk record's +0x6e 1): quick race Track1,
   quick knockout LoopedTrack, knockout the slot's Track<n>; others none. */
bool FEData_TrackLooped(int type);
const char *FEData_Car(void);
/* The quick knockout's track (LoopedTrack.Track) and its opponents (Game.NumKOOpponents). */
const char *FEData_LoopedTrack(void);
int FEData_KOOpponents(void);
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
void FEData_TourResult(int race, const int *places, const int *times, const bool *clean, int n);
void FEData_TourEnd(void);
/* The career race's laps (0x4fb630: its +0x3a2, 1 when 0; its reverse +0x3a4 and mirror +0x3a5 flags pend). */
int FEData_TourLaps(void);
/* The career race's traffic count (0x4fb630); slots[] its own traffic cars' ids (nfs5.car kind 1) when
   the tournament has its own (all 0 otherwise: the track's). */
int FEData_TourTraffic(int16_t slots[7]);
#include "front/fesave.h"
#include "game/race_setup.h"
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
/* Buying the used car shown (UsedCar.Car): 0, or the message to show (0x322, 0x3d). */
int FEData_BuyUsed(void);
/* A bar's share of an accessor (FEGouraudIndicatorBar 0x4ca270): for an argument the accessor's (a car's
   condition in per cent), else its position in per cent of its count. */
int FEAccess_Percent(const FEAccessor *a, const char *arg);
/* The paint shop's update (FEPaintShopCBL_v1 0x510af0): the garage car's colours, stripes and race number
   between it and the accessors. */
void FEData_PaintShop(void);
/* A colour accessor's colour (FEColourAccessor +0x2c; the dealer car's its palette entry). */
uint32_t FEAccess_Colour(const FEAccessor *a);
/* The parts shop's lists' rows (FEPartsAllPartsAccessor_v15 0x514ef0, FEPartsShoppingBasket_v15 0x515800). */
typedef struct {
    bool header, open;        /* a group's header and whether it's open */
    char text[160];
    bool has_price, dim;      /* dim: a part in stock already in the basket */
    int32_t price;
    bool label;               /* its category's label above it (13 high) */
    int category;             /* the label's string */
    int height;               /* 14, and 13 more with the label */
    char status[40];          /* the car setup's INSTALLED / UNINSTALLED, the used car's wear */
    bool status_dim;
    const char *status_pos;   /* the control whose x places it (+3) */
    int status_align;         /* 0 from there, 2 to there */
} FEPartsRow;
bool FEData_PartsRow(const FEAccessor *a, int row, FEPartsRow *out);
/* The parts screen made (0x518ba0). */
void FEData_PartsOpen(void);
/* A parts screen callback (BUYPARTCALLBACK, ...): 0, or a message (0x32a: too little money). */
int FEData_PartsAction(const char *cb);
/* What the buttons may do (FEPurchasePartsCBL_v1 0x518cf0); true with a part chosen. */
bool FEData_PartsState(bool *buy, bool *sell, bool *basket, bool *selected);
/* The car setup's INSTALLPART / UNINSTALLPART (0x4f1300, 0x4f12b0). */
int FEData_InstallAction(const char *cb);
/* The install list's row chosen: a fitted part (true) or one in stock; false for none. */
bool FEData_InstallState(bool *fitted);
/* The garage car has its required parts (0x4ef890); else msg says which are missing. */
bool FEData_CarComplete(char *msg, size_t cap);
/* The career race allowed (0, the entry fee paid at a first race) or the message why not (0x32d, 0x359),
   or 0x35a: another tournament is unfinished, the question first. */
int FEData_TourCheck(void);
bool FEData_TourOtherUnfinished(void);   /* 0x504110 */
/* The question answered ok: not asked again, the other unfinished tournaments reset (0x504180). */
void FEData_TourProceed(void);
/* The front end's Game.GameType (the track lists depend on it). */
void FEData_SetGameType(int type);
/* A knockout between its races (0x4dd9b0). */
bool FEData_KnockoutOn(void);
/* The knockout run (0x4dd9e0): start it; the race next (its slot's track and laps, the opponents still
   in, their personalities and the grid: start[0] the player's, 0 the front); its results (0x4ddb10). */
void FEData_KnockoutStart(void);
int FEData_KnockoutRace(char *track, int cap, int *laps, int pers[8], int start[8]);
/* Knockout racer i's (pers[] above) personality and car (0x4ddc00: OpponentCar.Car's car i - 1). */
void FEData_KnockoutOpponent(int racer, RaceOpponent *o);
/* The knockout race's traffic count (its Track<n>.Traffic, 0x4fb630). */
int FEData_KnockoutTraffic(void);
/* The other race kinds' n opponents (0x4eb230): OpponentCar.Car's cars picked again (0x4f45c0), their
   personalities picked of Game.Difficutly's difficulty (0x4e2940); their count. */
int FEData_RaceOpponents(int n, RaceOpponent out[8]);
/* The career race's opponents from its tournament (0x4eb430), their places on the grid by the
   standings (0x502c10, 0-based) and the race's skill factor (+0x3cc); -1 outside a tournament. */
int FEData_TourOpponents(RaceOpponent out[8], int start[9], float *skill);
void FEData_KnockoutResult(const int *places, int np);
/* The data areas' lookups other modules share: an nfs5.prt record by its id (0x49c3e0), an nfs5.car
   record by its id (0x4f39e0: the first record when none has it), an nfs5.trk record by its id (0x4fb4b0). */
const uint8_t *FEData_PartRecord(int32_t id);
const uint8_t *FEData_CarById(int id);
const uint8_t *FEData_TrackById(int id);
/* A new car object of an nfs5.car record (0x4eda40): its colours in the object's order (+0x674 ..),
   its stripe package and race number, its stock parts fitted. */
void FEData_NewCar(SaveCar *c, const uint8_t *r, const int32_t col[4], int32_t stripe, int32_t racenum);
/* A car of a record by id with those colours into the player's owned cars (0x501820): paid nothing
   (+0x6a4), the list kept in order (0x501080). False without a save or room. */
bool FEData_AddOwned(int id, const int32_t col[4], int32_t stripe, int32_t racenum);
int FEData_PersonalityIndex(int id);   /* nfs5.per's record (0-based) of a personality id (0x4e2900) */
/* A Factory Driver opponent's car by id (0x4eb830): its SPECNAME, colours (the race description's
   order), stripes package and race number, picked at random as the original does. */
bool FEData_OpponentCar(int id, char spec[0x40], uint32_t col[4], int32_t *stripe, int32_t *racenum);
/* UserList.NameInput's string (the create player screen's name) and the string accessors' typing
   (FEStringAccessor slots 12 and 14: a character added, 8 the last taken off); false when it's full. */
char *FEData_NameInput(void);
bool FEAccess_TypeChar(FEAccessor *a, int ch);
/* The knockout given up (0x4ddaf0: a player chosen or made). */
void FEData_KnockoutStop(void);
/* The cheats typed as a player's name (0x65b940) and whether one is on (0x4d8240). */
extern uint32_t g_fe_cheats;
bool FE_Cheat(int bit);
/* A name that is a cheat's code (0x4d8130): the cheat taken, true. */
bool FEData_CheatCode(const char *name);

/* The career race's tournament numbers for the race (0x4ebb70). */
void FEData_TourRaceInfo(RaceTournament *t);
/* The race's era option (0x4ecc60's 0xbb from 0x503db0): 0 .. 2. */
int FEData_RaceEra(void);
/* The career's era (0x502880: 0x209 .. 0x20b), an era with no tournament left never completed
   (0x503480), the k-th tournament record of an era's list (0x5028f0), the last tournament's end having
   ended its era (0x504db0), the whole car list's i-th record (0x6919e8). */
int FEData_Era(void);
bool FEData_EraDone(int era);
const uint8_t *FEData_EraTournament(int era, int k);
bool FEData_EraEnded(void);
/* The player's four race cameras (Camera.1 .. Camera.4: 0 .. 3, or the save's). */
void FEData_Cameras(int32_t out[4]);
const uint8_t *FEData_CarAt(int i);
