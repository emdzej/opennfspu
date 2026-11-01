/* The race's cars (0x410000-0x418000 race core, 0x494000-0x4a4000 simulation): the car objects
   ("Car_tObj", 0x1254 bytes; Car_Construct 0x411190), their parts (nfs5.prt), their simulation and AI
   specifications (<SPECNAME>.sim, <AISPECNAME>.ais) and the task lists the simulation runs them by.

   The car object is kept as the original's bytes: the simulation reads and writes hundreds of its
   offsets, so the port reaches them through CAR_* accessors at the original's offsets (named here as they
   are understood) rather than a C struct that would have to be complete first. Its pointers are the
   exception: they live in native-sized slots after the bytes (CARP_*, each with its original offset),
   since the native build's pointers don't fit the original's 4-byte fields. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "game/wtree.h"

enum {
    CAR_SIZE = 0x1254,
    CARDESC_SIZE = 0x580,      /* a race car's description (the front end's Car%d.* keys) */
    CARSPECS_SIZE = 0x1c4,     /* "carSpecs" (<SPECNAME>.sim) */
    AISPECS_SIZE = 0x130,      /* "AISpecs" (<AISPECNAME>.ais) */
    CARPART_SIZE = 0x168,      /* "Parts_tCarParts" (an nfs5.prt record) */
    CAR_NPARTS = 20,
    CAR_MAX = 16,
};

/* Offsets in the car object. */
enum {
    CAR_NAME = 0x50c,          /* the class name (0x5d61e8 + 7 * TYPE) */
    CAR_NAME2 = 0x515,         /* (sprintf of 0x5e8e50: "") */
    CAR_INDEX = 0x520,         /* creation order */
    CAR_PLAYER = 0x524,        /* the human player's number, -1 */
    CAR_FLAGS = 0x52c,         /* 1 human, 2 opponent, 4 (human), 0x80 traffic, 0x100 / 0x200 */
    CAR_DESC = 0x558,          /* -> the description */
    CAR_568 = 0x568,           /* the description's BODYSTYLE */
    CAR_R3D = 0x74c,           /* -> the car's renderer (R3DCar) */
    CAR_SPECS = 0x760,         /* -> carSpecs */
    CAR_AISPECS = 0x764,       /* -> AISpecs */
    CAR_PARTS = 0x768,         /* -> 20 part records */
    CAR_WHEELS = 0x7b8,        /* 4 wheel objects, 0xc4 apart (their first word -> a 0x30 object) */
    CAR_TASKS = 0xe0c,         /* ten task functions (Car_Construct registers nine) */
    CAR_10D8 = 0x10d8,         /* the specs' +0xf4 before the engine model, then their ratio */
    CAR_CONTACTS = 0x110c,     /* the per-tick contact accumulator */
};

/* The pointer fields (their offsets in the original's object). */
enum {
    CARP_DESC,                            /* +0x558 */
    CARP_R3D,                             /* +0x74c */
    CARP_SPECS,                           /* +0x760 */
    CARP_AISPECS,                         /* +0x764 */
    CARP_PARTS,                           /* +0x768: 20 */
    CARP_WHEELS = CARP_PARTS + CAR_NPARTS, /* +0x7b8 + 0xc4 i: 4 */
    CARP_TASKS = CARP_WHEELS + 4,         /* +0xe0c: 10 */
    CARP_SWAPPED = CARP_TASKS + 10,       /* +0x550: the car last swapped places with (0x417730) */
    CARP_PREVRIVAL,                       /* +0x10c4: the rival ahead in the order (0x40b3f0) */
    CARP_COUNT,
};

/* The wheels' surface trackers are WSurface objects at +0x7bc + 0xc4 i (in the wheel's bytes from +4):
   the port keeps them as native objects in surf[] (they hold pointers) and copies their surface word
   into the bytes (+0x7e0, the tracker's +0x24) after each update. */
typedef struct {
    uint8_t b[CAR_SIZE];
    void *p[CARP_COUNT];
    WSurface surf[4];
    WSurface body;     /* +0x8: the body's (its surface word copied to +0x2c) */
} Car;

static inline float *CAR_F(Car *c, int off) { return (float *)(void *)(c->b + off); }
static inline int32_t *CAR_I(Car *c, int off) { return (int32_t *)(void *)(c->b + off); }
static inline uint8_t *CAR_DESCB(Car *c) { return c->p[CARP_DESC]; }
static inline uint8_t *CAR_SPECSB(Car *c) { return c->p[CARP_SPECS]; }
static inline float *SPEC_F(Car *c, int off) { return (float *)(void *)(CAR_SPECSB(c) + off); }
static inline int32_t *SPEC_I(Car *c, int off) { return (int32_t *)(void *)(CAR_SPECSB(c) + off); }
static inline int32_t DESC_I(Car *c, int off) { int32_t v; memcpy(&v, CAR_DESCB(c) + off, 4); return v; }

/* The race cars (0x5e8b34, count 0x5e8b58), all cars in creation order, traffic (0x5e8c24, 0x5e8b5c). */
extern Car *g_cars[CAR_MAX];   /* 0x5e8b34: the race's cars (not traffic), count 0x5e8b58 */
extern int g_ncars;
extern Car *g_traffic[CAR_MAX];
extern int g_ntraffic;

/* The race's car descriptions (0x65808c, 0x580 bytes each) and counts (race cars 0x658080, traffic
   0x65807c): what the front end hands the race. */
extern uint8_t g_cardesc[CAR_MAX][CARDESC_SIZE];
extern int g_nracecars, g_ntrafficcars;

/* The race's car lists (0x40f1c0), by the description's CLASS bits (+0x08). A car of several bits is in
   several lists, an opponent of two kinds twice in the opponents'. */
typedef struct { Car *c[CAR_MAX * 3]; int n; } CarList;
extern CarList g_racers;      /* 0x5e8e2c, 0x5e8ae8: the humans and the opponents (flags 1) */
extern CarList g_humans;      /* 0x5e8c00, 0x5e8bfc: class 1 (flags 4); their index is the car's +0x524 */
extern CarList g_opponents;   /* 0x5e8aec, 0x5e8e28: class 2, 4, 8 or 0x10 (flags 2) */
extern CarList g_viewed;      /* 0x5e8b10, 0x5e8ab8: the humans (and the class-2 opponents with 0x657414) (flags 0x100) */
extern CarList g_rivals;      /* 0x5e8bd4, 0x5e8bf8: class 2 (flags 8) */
extern CarList g_cops;        /* 0x5e8b60, 0x5e8e24: class 4 (flags 0x10) */
extern CarList g_chasers;     /* 0x5e8ac4, 0x5e8abc: class 8 or 0x10 (flags 0x20; 0x10 also 0x40) */
extern CarList g_ranked;      /* 0x5e8b88: the race's cars by their race distance less laps (0x417730), rank +0x528 */
extern CarList g_ordered;     /* 0x5e8bac: by race distance (+0x550 the car swapped with, +0x554 when) */
extern int32_t g_race_view_opponents;   /* 0x657414 (class-2 opponents viewed too) */

/* The race's state the simulation reads: the tick counter (0x606ac4, 64 Hz), the RALLY option (0x6573f8: the Dakar cheat's road grip), a
   network game (0x657408). */
extern int32_t g_sim_tick;
extern int32_t g_race_rally;
extern int32_t g_race_network;
extern int32_t g_race_rollover_events;   /* 0x657a2c */
extern int32_t g_race_heavy;             /* 0x6573fc: the human cars ten times heavier */

/* ---- task lists (0x414c50 / 0x414cd0 / 0x414d50) ---- */

typedef void (*TaskFn)(void *arg);
typedef struct {
    int32_t f0;
    int32_t count;                                                           /* +0x04 */
    struct { int32_t prio; TaskFn fn; void *arg; int32_t pad; } e[64];       /* +0x08 */
} TaskList;
extern TaskList g_tasks_tick;   /* 0x606acc: every 64 Hz tick */
extern TaskList g_tasks_even;   /* 0x606ad0: even ticks */
/* Inserted before the first entry of a higher priority. 0x414c50 */
void TaskList_Add(TaskList *l, TaskFn fn, void *arg, int prio);
void TaskList_Remove(TaskList *l, TaskFn fn, void *arg);   /* 0x414cd0 */
void TaskList_Run(TaskList *l);                            /* 0x414d50 */

/* ---- parts (nfs5.prt) ---- */

int Parts_Load(void);            /* 0x49c310: the records (0x628f84, count 0x628f80) */
void Parts_Find(Car *c);         /* 0x49c430: the description's PARTINDEX into the car's part records */

/* ---- specifications ---- */

void Sim_LoadCarSim(Car *c);     /* 0x49c630 (then Sim_BuildEngine) */
void Sim_LoadCarAis(Car *c);     /* 0x49c8b0 */
void Sim_BuildEngine(Car *c);    /* 0x49c9d0 */

/* ---- the cars ---- */

/* The car's ten task functions by its flags (in 0x410dc0) and their registration (Car_Construct). */
void Car_SetTasks(Car *c);
void Car_RegisterTasks(Car *c);

/* The track's factor for a car's AI speeds (Track_Table 0x4a2ba0) and for its +0x140 (0x4a2730): AI
   tuning; pending (1.0) until the AI is ported. */
float Track_TableFactor(const char *aispec);
float Track_CarFactor(const char *aispec);

/* Every car of the race: parts, objects, lists, specs, renderers, tasks. 0x411190 */
void Car_Construct(void);
