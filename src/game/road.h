/* The track's road (the object at 0x628bc0, 0x24 bytes; ctor 0x47fbd0): the nodes along the road from the
   track's 'SimD' item (0x50 bytes each: +0x04 x, +0x08 y, +0x0c z, then the road's vectors), its segments
   from 'SimT' (the lengths of the runs of nodes; a track without 'SimT' is one run) and the junctions
   where runs meet. Built by World_LoadSim (0x47ff20 -> 0x47ffe0). The simulation, the AI, the sky and the
   race logic ask it where a point is along the road (Road_NearestNode 0x4812a0). The std::vectors of the
   original are arrays here. */
#pragma once
#include "support/udata.h"
#include <stdbool.h>
#include <stdint.h>

enum { ROAD_NODE_SIZE = 0x50 };

/* A segment's end (0x10 bytes): its node, the segment's index (after the split, 0x4807a0), the branch
   weight a junction gives it (1 / the ends on its side as an integer, then the .jnc file's), and which
   end it is (0 the first node, 1 the last). */
typedef struct {
    int32_t node;           /* +0x0 */
    int32_t seg;            /* +0x4 */
    int32_t weight;         /* +0x8 */
    int32_t dir;            /* +0xc */
} RoadEnd;

struct RoadJunction;

/* A segment (0x28 bytes): a run of nodes, its two ends and their junctions. */
typedef struct {
    union {
        struct {
            int32_t first, f04, f08, f0c;   /* +0x00: the first end */
            int32_t last, f14, f18, f1c;    /* +0x10: the last end */
        };
        RoadEnd end[2];
    };
    struct RoadJunction *junction0;   /* +0x20: the junction at its first node */
    struct RoadJunction *junction1;   /* +0x24: at its last */
} RoadSegment;

/* A junction (0x38 bytes, 0x4809e0): up to eight segment ends within 16 of each other, its distance in
   nodes from the first junction along the race (0x481880) and that as the race's progress there. */
typedef struct RoadJunction {
    RoadEnd *end[8];        /* +0x00 */
    int32_t count;          /* +0x20 */
    int32_t dist;           /* +0x24 */
    float progress;         /* +0x28 */
    float centre[3];        /* +0x2c: the ends' nodes' mean */
} RoadJunction;

/* A node near a junction and its squared distance (ground plane) from the junction's centre. */
typedef struct { int32_t node; float d2; } RoadNear;
typedef struct { RoadNear *e; int32_t n; } RoadNearList;

typedef struct {
    int32_t nnodes;         /* +0x00 */
    int32_t length;         /* +0x04: the race's length in nodes (0x4817b0) */
    int32_t lastnode;       /* +0x08: the last node of the segments ending at the race's end */
    uint8_t *nodes;         /* +0x0c: nnodes x 0x50 */
    int8_t *node_near;      /* +0x10: per node, the junction it is nearest within reach of (-1 none) */
    int8_t *node_end;       /* +0x14: per node, the junction one of whose ends it is (-1 none) */
    RoadSegment *seg;       /* +0x18: the segments (a vector) */
    int32_t nseg;
    RoadJunction *junctions;   /* +0x1c */
    int32_t njunctions;
    RoadNearList *near;     /* +0x20: per junction, its near nodes by distance */
} Road;

extern Road *g_road;   /* 0x628bc0 */

Road *Road_new(void);                          /* 0x47fbd0 */
/* The road from the track root's 'SimD' and 'SimT' items; false (with the original's message) without
   'SimD'. 0x47ff20 */
bool World_LoadSim(UData *root);
/* The node nearest to p along the road, searched from the node near (the last answer): a scan of every
   eighth node when near is more than sqrt(0x5b47d0) away, the junction's nodes at a junction, then
   stepping to the neighbour that is nearer in the ground plane (at most 1000 steps). 0x4812a0 */
int Road_NearestNode(const Road *r, const float p[3], int near);
/* The segment holding node n, -1 if none. 0x481b20 */
int Road_SegmentOf(const Road *r, int n);
/* The node `by` nodes on from n (back if negative) in n's segment, clamped to the road; past the
   segment's end on through the junction there (branch: its road, -1 the one going the same way), else
   the segment's end. 0x481670 */
int Road_Step(const Road *r, int n, int by, int branch);
/* Node n's segment as the race's progress runs along it: the progress at its first and last nodes (the
   last's 1 when it is 0), its length and first node; 0, 0, 1 for a node in no segment. 0x481a10 */
void Road_SegmentRange(const Road *r, int n, float *start, float *end, int *count, int *first);
/* The branch weights of the junctions from <track>.jnc (0x24 bytes per junction: a word, then one per
   end), when it has one record per junction. 0x480f20 */
void World_LoadJunctions(const char *path);
static inline const float *Road_NodePos(const Road *r, int n) { return (const float *)(const void *)(r->nodes + n * ROAD_NODE_SIZE + 4); }
