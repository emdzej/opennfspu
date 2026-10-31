/* The track's animations (the scene's animation module, 0x470df0 .. 0x471660): QuatAnim (vtable 0x5b460c,
   base Anim 0x5b45bc), a run of keys (0x34 bytes: a rotation quaternion, a position, a scale) played on
   the clock of 0x628938 (the simulation's ticks, 1/64 s), and the animated bundle's entries (the
   articles with an 'Anqt' item: WAnimBundle_load 0x477780), each with its animation, the sounds of its
   object type (animdefs.txt's section:audio) and its type. */
#pragma once
#include "eagl/eagl.h"
#include "game/scene.h"
#include <stdbool.h>
#include <stdint.h>

/* The keys and how they play (0x20 bytes): +0 flags (1 loop, 8 turned by +0x10, 0x10 spun down over the
   run), +4 the seconds a key (0.1), +8 the keys, +0xc their data, +0x10 the turn (turns), +0x14 a drift
   per second. */
typedef struct {
    uint32_t flags;
    float rate;
    uint32_t nkeys;
    const uint8_t *keys;
    float turn;
    float drift[3];
} QuatAnimData;

/* A QuatAnim (0x14 bytes): +4 a flag byte, +8 the tick it started, +0xc its state (0 not started, 1
   playing, 2 played out), +0x10 its keys. */
typedef struct {
    uint8_t b4;
    int32_t start;
    int32_t state;
    QuatAnimData *d;
} QuatAnim;

QuatAnim *QuatAnim_new(uint32_t nkeys, const uint8_t *keys, uint8_t b4, uint32_t flags, float rate, float turn);   /* 0x4713f0 */
void QuatAnim_free(QuatAnim *a);                                                       /* QuatAnim_v0 0x471490 */
void QuatAnim_Start(QuatAnim *a);                                                      /* slot 5, 0x471640 */
/* The matrix now (slot 1, 0x4714d0): the first key before it starts, else at the ticks since its
   start; its state (1 playing, 2 past its end). */
int QuatAnim_Matrix(QuatAnim *a, EaglMatrix *out);
/* The matrix at the tick (slot 3, 0x471570 -> 0x470df0); false past the last key. */
bool QuatAnim_At(const QuatAnim *a, int32_t tick, EaglMatrix *out);

/* An animated bundle's entry (0x10 bytes at the bundle's +0x34). */
typedef struct WAnimEntry {
    QuatAnim *anim;           /* +0x00 */
    uint32_t nsounds;         /* +0x04 */
    SceneElem *sounds;        /* +0x08: CAudioElem-shaped records (the 0x28 bytes at their +0x7c) */
    uint32_t type;            /* +0x0c: the Base type */
} WAnimEntry;

struct W3DObjectBundle;
/* The entries of the bundle's articles, each animation started (0x477780's part). */
WAnimEntry *WAnim_create(struct W3DObjectBundle *b);
void WAnim_free(WAnimEntry *e, uint32_t n);
