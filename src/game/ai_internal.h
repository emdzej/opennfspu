/* The AI's controllers and states as the [ai] module's files share them (ai.c, ai_police.c). The
   original's objects are kept as words at their offsets where its fields overlap or hold other objects
   (those as car indices, exe records as their addresses). */
#pragma once
#include "game/car.h"
#include <stdbool.h>
#include <stdint.h>

typedef union { int32_t i; float f; } AiWord;

typedef struct AiState AiState;
typedef struct {
    void (*tick)(AiState *s);
    void (*done)(AiState *s);
    bool (*ready)(AiState *s);
} AiStateVtbl;
/* A state of the pool (0x40b590: eight at 0x5e6e38, up to 0x94 bytes). */
struct AiState {
    const AiStateVtbl *vt;
    Car *car;      /* +4 */
    float target;  /* +8: the idle state's lateral offset */
    int32_t hold;  /* +0xc: the idle state keeps the car's offset */
    AiWord w[0x21];   /* +0x10 .. +0x94 */
};
#define SW(s, off) ((s)->w[((off) - 0x10) / 4])

/* A controller (AIHigh_*): its car, its state, the state's phase, ticked always (+0x10) or by the task
   rate, then the kind's words from +0x14 (the traffic's: +0x14 the node when made, +0x18 past the
   police's first check, +0x1c sent back; the pursued's and the police's in ai_police.c). */
typedef struct {
    int kind;          /* 0 none, 1 human, 2 opponent, 3 traffic, 4 police */
    Car *car;          /* +0x04 */
    AiState state;     /* +0x08 (the pool's) */
    int32_t phase;     /* +0x0c */
    int32_t always;    /* +0x10 */
    AiWord w[0x2b];    /* +0x14 .. +0xc0 */
} AiHigh;
#define HW(h, off) ((h)->w[((off) - 0x14) / 4])

/* The car's controller (0x5e6774[i]), NULL if none. */
AiHigh *AIi_Ctl(int i);
void AIi_SetState(AiHigh *h, AiState s);
void AIi_StateTick(AiState *s);   /* 0x40b530 */
AiState AIi_Normal(Car *c);       /* 0x40b630 */
AiState AIi_Idle(Car *c);         /* 0x40cad0's state, holding the car's offset */
AiState AIi_Purgatory(Car *c);    /* 0x40de30 */
Car *AIi_CarByIndex(int i);
void AIi_SpeedTarget(Car *c);    /* 0x40afd0 */
void AIi_Place(Car *c);          /* 0x4076f0 (its second argument 0) */
/* The end of a placement at node n: the drive's fields reset (0x406d30), its offset across the road
   (0x4173e0), extent (0x4176a0) and lanes (0x40ec10). */
void AIi_Settle(Car *c, int n);
