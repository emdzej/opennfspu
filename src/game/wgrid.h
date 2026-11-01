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

/* A node (0x20 bytes in the original, 0x10 for the leaves 0x47c160 makes, without the children). */
typedef struct WGridNode {
    uint16_t flags;                 /* +0x00: bit 0 = has children (the 0x20-byte kind) */
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
