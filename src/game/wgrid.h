/* The world's grid (the 0x8c-byte object at BWorld +0x10; 0x47b5d0, built by 0x47b5f0 from World_BuildBundles):
   a quadtree over the ground plane (x, z) fitted to the world's objects, its cells split where the articles'
   vertices are dense. The line bundle's cells (and with them the skid marks) are its nodes down to the
   level whose cells are still 100 wide; the original's visibility pass walks it too (0x47ba50).

   Nodes: an id of x (14 bits), z (14 bits, from bit 14) and level (4 bits, from bit 28); level l's cells
   are size[l] wide from the origin. Children: [0] low x / low z, [1] high x / low z, [2] low x / high z,
   [3] high x / high z (the quadrant's bit 0 is x, bit 1 z). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct W3DObjectBundle;

/* An object filed in a node (12 bytes): the bundle's entry and its flags (the entry's, & 0x4187fe for
   the fixed bundles), ORed when the entry is filed there again. */
typedef struct {
    uint32_t flags;                 /* +0x00 */
    struct W3DObjectBundle *bundle; /* +0x04 */
    uint16_t index;                 /* +0x0a */
} WGridRec;

/* A node (0x20 bytes in the original, 0x10 for the leaves 0x47c160 makes, without the children). */
typedef struct WGridNode {
    uint16_t flags;                 /* +0x00: bit 0 = has children (the 0x20-byte kind); the original keeps
                                       the records' room in the bits above it (grown by 4, 0x47c7a0) */
    uint16_t count;                 /* +0x02: the records */
    WGridRec *recs;                 /* +0x04 */
    uint16_t room;                  /* port: the records' room */
    uint32_t id;                    /* +0x08 */
    struct WGridNode *parent;       /* +0x0c */
    struct WGridNode *child[4];     /* +0x10 */
    uint32_t seq;                   /* port: the order made in (the original's pool addresses; see wgrid.c) */
} WGridNode;

typedef struct WGrid {
    float origin[3];                /* +0x04 */
    float size[14];                 /* +0x10: the cells' widths by level */
    float inv[14];                  /* +0x48 */
    float height;                   /* +0x80: the objects' height and 100 */
    WGridNode *root;                /* +0x84 */
    int32_t calls;                  /* +0x88: the build's node visits */
    uint32_t nseq;                  /* port */
} WGrid;

struct BWorld;
/* 0x47b5f0: fitted to the objects' boxes (the bundles' slot 2), split by the articles' vertices. */
WGrid *WGrid_Build(const void *world);
void WGrid_Free(WGrid *g);   /* 0x47b9e0 */
/* The deepest node down to maxlevel holding the point (x, z), NULL outside the grid. 0x47ba60 */
WGridNode *WGrid_Find(const WGrid *g, const float p[3], int maxlevel);
/* A node's centre (y the origin's) and its box (y from the origin up by the height). 0x47c2c0, 0x47c1e0 */
void WGrid_Centre(const WGrid *g, const WGridNode *n, float out[3]);
void WGrid_Box(const WGrid *g, const WGridNode *n, float mn[3], float mx[3]);
static inline int WGrid_Level(const WGridNode *n) { return (int)(n->id >> 28); }

/* The deepest node holding the sphere (x, y, z, r) whole: down while the sphere keeps off the node's
   centre lines; the root for a sphere not wholly in the grid. 0x47bbb0 */
WGridNode *WGrid_FindSphere(const WGrid *g, const float s[4]);

/* Whether the convex polygon of n points (x, z) and the box (x, z) meet. 0x5a0540 */
bool WGrid_PolyBox(int n, const float (*p)[4], const float mn[3], const float mx[3]);

/* A bundle's entries filed in the nodes (0x4892a0 -> 0x489460): the fixed bundles' entries in the nodes
   their box's hull covers (0x489860), the moving ones' (+5) in the node holding their sphere, again when
   it moved (World_BuildBundles, then every 8th world pass 0x488c30). */
void WGrid_FileBundle(WGrid *g, struct W3DObjectBundle *b);
