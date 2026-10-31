/* The player's save (SaveData\<player>.sav: Save_Load 0x4de060, its reader 0x4de460, Save_Write
   0x4de850). The file is a run of sections, each a name and what that section's loader reads (no
   lengths between them; the loaders are the list 0x65b634 by name, the writers 0x65b638):
     Missions       0x519fe0  int bytes, then bytes (0xf0 a mission)
     PartsMarket    0x5170f0  int n, n parts of 0x168 bytes
     Tournament     0x501fa0  seven ints, int bytes, then bytes (0xd40 a tournament)
     PlayerInfo     0x500bc0  int n, n cars; int money (0x691d90), int (0x5db740), the name and a
                              second string, three bytes, 32 bytes (0x691d98)
     TrackRecords   0x4fa280  nothing (the records are their own file)
     UsedCarMarket  0x4f6d70  int n, int (0x691b58), n cars
     Personalities  0x4e2480  int n, n shorts
     Knockout       0x4dd810  0x84 bytes
     CDPlayer       0x4bdd50  int n, n ints
     accessor       0x4b7dc0  int bytes: the accessors' values, each a name and its own data
   A car (the car object 0x760 bytes, 0x4edd30): 0x758 bytes of it, then int n1, n2 and n1 + n2 parts as
   (part id, value) int pairs (the installed ones, then the others). The port keeps the sections it
   doesn't read yet as their bytes, so a written save keeps them. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SAVE_CAR = 0x758 };

typedef struct {
    uint8_t rec[SAVE_CAR];   /* the car object's first 0x758 bytes (an nfs5.car record and its state) */
    int32_t ninstalled, nother;
    int32_t (*parts)[2];     /* (part id, value) */
} SaveCar;

typedef struct {
    char name[32];           /* the section's name */
    uint8_t *data;           /* its bytes as read (the ones the port doesn't take apart) */
    size_t size;
} SaveSection;

typedef struct {
    SaveSection *sec;
    int nsec;
    /* PlayerInfo */
    SaveCar *cars;
    int ncars;
    int32_t money;           /* 0x691d90 */
    int32_t info;            /* 0x5db740 */
    char player[64], player2[64];
    uint8_t flags[3];        /* 0x691dd5, 0x691d94, 0x691df5 */
    uint8_t extra[32];       /* 0x691d98 */
    /* UsedCarMarket */
    SaveCar *used;
    int nused;
    int32_t usedinfo;        /* 0x691b58 */
} Save;

/* The sections of a save's bytes; false on a section the port can't read or a short file. */
bool Save_Parse(Save *s, const uint8_t *d, size_t n);
/* The bytes again, sections in their order (PlayerInfo and UsedCarMarket from their fields). */
uint8_t *Save_Serialize(const Save *s, size_t *n);
void Save_Free(Save *s);

/* The current player's save (the front end's): the user's own if it has one, else the game's default
   player's (SaveData\XXDefXX.sav, 0x5d9a44). */
Save *FESave_Current(void);
/* Written back to the user's files. */
bool FESave_Write(void);
/* Money as the front end writes it (0x4dba30: the strings 0x6a4 .. 0x6a7 group the thousands). */
const char *FE_MoneyText(int32_t v);
