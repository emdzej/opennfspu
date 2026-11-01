/* The skid marks (0x478880 .. 0x479ff0, 0x479e20): the world's line bundle (WLineBundle 0x479890, the global
   0x628b70) has a cell for each node of the world's grid (game/wgrid.h) down to the level whose cells are
   still 100 wide; a cell keeps up to 20 strips (0x14 bytes each, a ring) of quads in pairs of vertices on
   the ground, each with its surface's texture and colour. Each wheel has a trail (0x30 bytes, car +0x7b8 +
   0xc4 w) that, while the wheel slips past its threshold, adds a pair across the wheel's way every step to
   its strip in the cell under it, a new strip when it fills, the kind of ground changes or it crosses into
   another cell. Strips of the fading kinds lose 4 of alpha a tick (World_Tick) and are freed when clear;
   a cell short of free strips recycles its oldest. The line bundle's entries go through the visibility
   pass like the other objects (far ones: 600 m), and a queued cell draws its strips (RDrawLineBundle). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct W3DObjectBundle;
struct WGrid;

/* A wheel's trail (0x30 bytes). */
typedef struct SkidTrail {
    float width;        /* +0x00: half the mark's width */
    int32_t cell;       /* +0x04: the line bundle's cell (-1 none) */
    int32_t strip;      /* +0x08: the cell's strip (-1 none) */
    int32_t kind;       /* +0x0c: the ground's kind */
    int32_t state;      /* +0x10: the strips' state (4 fading for kind 6, else 8) */
    int32_t steps;      /* +0x14 */
    uint32_t tex;       /* +0x18: the texture's name (0x5d00e8 by kind) */
    int32_t kind15;     /* +0x1c: the kind & 15 */
    int32_t mode;       /* +0x20: 0 marking, 1 a new strip, 2 not marking, 3 the kind changed */
    float f24, f28, f2c;
} SkidTrail;

/* 0x478e30: the half width from the tyre's (car specs +0x84) by 0.0005 (0x5b4744) and the quality's 1.5 or
   0.9 (0x5b246c, 0x5b25e8). */
SkidTrail *SkidTrail_new(float tyre);
void SkidTrail_Reset(SkidTrail *t);   /* 0x478e90 */
/* 0x478ec0: one step of the wheel at pos (its contact point) going dir (the car's +0x348) slipping by slip
   on ground of kind: nothing at or under the threshold, else marks of (slip - threshold) * scale. */
void SkidTrail_Step(SkidTrail *t, const float pos[3], float slip, int kind, const float dir[3], float threshold, float scale);

/* The line bundle over the grid (0x479890) and its parts the draw object and the visibility pass use. */
struct W3DObjectBundle *WLineBundle_new(void *world, struct WGrid *g);
void WLineBundle_Sphere(struct W3DObjectBundle *b, int cell, float out[4]);   /* slot 3, 0x479f50 */
void WLineBundle_DrawCell(struct W3DObjectBundle *b, int cell);               /* 0x479ff0 */
/* The fading strips' step (World_Tick 0x479e20, the odd ticks) and every strip cleared (0x479d60). */
void World_Tick(void);
void WLineBundle_Clear(void);
