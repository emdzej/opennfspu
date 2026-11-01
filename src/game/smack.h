/* The knockable objects (WAnimatableBundle 0x475e10 .. 0x477650, WAnimatableArticleBundle 0x475d80): the
   instance bundle's animatable state (its +0x14), an entry per placed object with its object definition
   and its pieces. The objects of collision kind 3 (smackable: signs, cones, gas pumps, tables, parked vans)
   get a body (0x483030, 0x70 bytes): the points it rests and tumbles on, its mass and sound kind, and the
   simulated position and rotation it follows, those of the traffic car the race makes for it (game/
   traffic.h: Car_RaceInit 0x4a12f0 points the body at the car's). The entry's piece and its current
   record each have a SmackAnim (0x4710e0, vtable 0x5b45e4): the body's motion since its placing as a
   matrix (slot 1, 0x471140). The canned pieces of the other animated objects (0x476310: by the hit's
   speed, QuatAnims from the prototypes' 'Anim' items) are pending. */
#pragma once
#include "eagl/eagl.h"
#include "support/udata.h"
#include <stdbool.h>
#include <stdint.h>

struct W3DObjectBundle;

/* A smackable's body (0x483030 / 0x483070). */
typedef struct WSmackBody {
    float offset[3];        /* +0x00: minus the points' box centre */
    float (*pts)[3];        /* +0x0c: the points, about the centre */
    int32_t npts;           /* +0x10 */
    float mass;             /* +0x14 */
    int32_t audio;          /* +0x18: the definition's sound kind */
    uint8_t b1c;            /* +0x1c */
    const float *pos;       /* +0x20: the simulated position (the zero vector 0x5bb2b0 until a car's) */
    const float *rot;       /* +0x24: the simulated rotation (the identity 0x5bb2d0 until a car's) */
    float pos0[3];          /* +0x28: the placing's position and the centre */
    float rot0[9];          /* +0x34: the placing's rotation without its scale */
    float scale[3];         /* +0x58: the placing's scale */
} WSmackBody;

/* A SmackAnim (200 bytes): an Anim (+4 a flag byte, +8 the start tick, +0xc the state: 0 still, 1 moved
   off its place, 2 done) following a body. */
typedef struct {
    uint8_t b4;             /* +0x04 */
    int32_t start;          /* +0x08 */
    int32_t state;          /* +0x0c */
    WSmackBody *body;       /* +0x10 */
    EaglMatrix base;        /* +0x14: the inverse of the body's placing */
    int32_t moved;          /* +0x54: the tick the body last moved */
    float lastpos[3];       /* +0x58 */
    float lastrot[9];       /* +0x64 */
    EaglMatrix last;        /* +0x88 */
} WSmackAnim;

/* A piece's record (0x18 bytes). */
typedef struct {
    WSmackAnim *anim;       /* +0x00 */
    UData *article;         /* +0x04 */
    UData *art2;            /* +0x08 */
    UData *art3;            /* +0x0c: the vertex animation's */
    WSmackBody *body;       /* +0x10 */
    uint32_t type;          /* +0x14 */
} WSmackPiece;

/* An entry (0x38 bytes). */
typedef struct {
    const void *def;        /* +0x00: the WObjectDef */
    uint8_t marked;         /* +0x04 */
    int32_t sounded;        /* +0x08 */
    uint32_t colour;        /* +0x0c: the light at it (packed; -1 to take again) */
    uint8_t dynlight;       /* +0x10 */
    int32_t live;           /* +0x14: the pieces playing */
    int32_t npieces;        /* +0x18 */
    WSmackPiece cur;        /* +0x1c */
    WSmackPiece *pieces;    /* +0x34 */
} WSmackEntry;

typedef struct WAnimatable {
    struct W3DObjectBundle *bundle;   /* +0x04, +0x08 */
    WSmackEntry *e;                   /* +0x0c */
    WSmackBody **bodies;              /* +0x10: the bodies made (a vector) */
    uint32_t nbodies, cap;
} WAnimatable;

/* 0x475d80: the instance bundle's state, each entry set up (0x476210, 0x477280). */
WAnimatable *WAnimatable_new(struct W3DObjectBundle *instances);
void WAnimatable_free(WAnimatable *a);   /* 0x475ff0 */

/* SmackAnim slot 1 (0x471140): the matrix of the body's motion since its placing (cached while the body
   keeps still); its state. */
int SmackAnim_Matrix(WSmackAnim *s, EaglMatrix *out);

/* The body follows a rotation and a position (a traffic object's +0x364, +0x330). 0x4832a0 */
void WSmackBody_Follow(WSmackBody *b, const float *rot, const float *pos);
/* An entry's matrix with its piece's motion (-1: the current record's): the motion times the placing; false
   when the piece has no SmackAnim. 0x476730, 0x476970 */
bool WAnimatable_Matrix(WAnimatable *a, int i, int piece, EaglMatrix *out);
/* The hit kind of entry i struck at vel: 0 for an object with a body, else 2 - whether its canned
   pieces start (1, 2). 0x476760 */
int WAnimatable_HitKind(WAnimatable *a, int i, const float vel[3]);
