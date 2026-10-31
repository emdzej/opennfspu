/* The world's collision tree (0x4835e0-0x487000) and the queries on it (0x4731a0-0x474600): a quadtree
   over the ground plane (x, z) whose leaves list the elements touching them. The elements are the track
   articles' polygons (triangles and quads made by PCollideArticleBundle, 0x475040) and the road edges of
   <track>.edg; the cars' wheels find the polygon under them through a surface tracker (WSurface).

   Cells (0x18 bytes in the original): an id of x (14 bits), z (14 bits, from bit 14) and level (4 bits,
   from bit 28), four children and the leaf's element list. Level l's cells are 16384 / 2^l wide (for the
   world's tree: 8192 the half size, 15 levels; the sizes are globals, 0x628be8, set by each tree made),
   from -8192. Children: [0] low x / high z, [1] high x / high z, [2] low x / low z, [3] high x / low z
   (the quadrant codes of 0x4839b0). A leaf splits past 48 elements unless it is at level 12. */
#pragma once
#include "support/udata.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct WTreeElem WTreeElem;

/* The elements' virtual methods, slots in the original's order (vtables 0x5b4868 base, 0x5b4880 Quad,
   0x5b48b0 Tri, 0x5b484c Edge). The edge's table ends at slot 5, which there is its point i. */
typedef struct {
    void (*dtor)(WTreeElem *e);                                            /* 0 */
    void (*centre)(const WTreeElem *e, float out[3]);                      /* 1 */
    void (*bounds)(const WTreeElem *e, float mn[3], float mx[3]);          /* 2 */
    bool (*contains)(const WTreeElem *e, const float p[3]);                /* 3: in the ground plane */
    bool (*crosses)(const WTreeElem *e, const float seg[6]);               /* 4 */
    void (*normal)(const WTreeElem *e, float out[3]);                      /* 5 (polygons) */
    const float *(*vertex)(const WTreeElem *e, int i);                     /* 6 */
    const float *const *(*vertices)(const WTreeElem *e);                   /* 7: 0x485d20 */
    int (*nvertices)(const WTreeElem *e);                                  /* 8 */
    int (*crossed_side)(const WTreeElem *e, const float seg[6]);           /* 9: -1 none */
    uint16_t (*colour)(const WTreeElem *e, int i);                         /* 10 */
} WTreeElemVtbl;

enum { WTREE_OBJECT = 0, WTREE_POLY = 1, WTREE_EDGE = 2 };

/* WTreeElem (8 bytes + the kind's fields). The polygons point at their article's vertices (16 bytes
   each, x y z and a pad) and keep the vertices' colours as 5:5:5. */
struct WTreeElem {
    const WTreeElemVtbl *vt;   /* +0x00 */
    uint8_t kind;              /* +0x04 */
    uint16_t w8;               /* +0x08: polygons: clockwise (byte; the centre on no edge's positive side,
                                  as the float's bits); edges: the .edg record's flag */
    uint16_t flags;            /* +0x0a: polygons: the surface (the material's 'mt' first word) */
    union {
        struct { const float *v[4]; uint16_t colour[4]; } poly;   /* +0x0c, colours +0x1c (Tri: +0x18) */
        struct { float p[2][4]; } edge;                           /* +0x0c, +0x1c */
    } u;
};

/* The objects' own slots (vtables 0x5b490c cylinder, 0x5b4938 box): their slots 0-4 are the element's,
   from 5 on these. */
typedef struct WTreeObject WTreeObject;
enum { WOBJ_CYLINDER = 1, WOBJ_BOX = 2 };
typedef struct {
    float (*radius)(const WTreeObject *o);                                  /* 5 */
    /* 6: against a segment (seg[0..2] from, [3..5] to, in the ground plane): the sides it crosses (the
       box), a normal and a point out (the original's first argument, the car's position, is unused). */
    int (*collide)(const WTreeObject *o, const float seg[6], float normal[3], float at[3]);
    int (*hitkind)(const WTreeObject *o, const float vel[3]);               /* 8: 1 animated, 2 solid, 3 drive-through */
} WTreeObjectVtbl;

/* WTreeElem_Object (0x34 bytes; base ctor 0x4862b0, vtable 0x5b48e0): the bounding box of its bundle
   entry, the definition's drive-through flag, its bundle and entry. The cylinder (0x38, 0x486600) adds
   its radius, the box (0x68, 0x486bf0) its four corners in the ground plane and the radius of its
   bounding box. */
struct WTreeObject {
    WTreeElem e;                     /* +0x00: vt, kind 0 */
    float bmin[3], bmax[3];          /* +0x08, +0x14 */
    int32_t f20;                     /* +0x20: the cylinder's definition radius as an integer, 0 */
    int32_t drivethru;               /* +0x24 */
    struct W3DObjectBundle *bundle;  /* +0x28 */
    uint8_t f2c;                     /* +0x2c: 1 */
    uint16_t entry;                  /* +0x2e */
    const WTreeObjectVtbl *ovt;      /* port: the slots from 5 on */
    uint8_t shape;                   /* port: WOBJ_CYLINDER or WOBJ_BOX (the original's RTTI) */
    float radius;                    /* cylinder +0x34 (0.25 at least); box +0x64 */
    float corner[4][3];              /* box +0x34 .. +0x60 */
};

/* The objects of the bundle's entries whose definitions make them cylinders or boxes (0x475040's object
   branch). */
WTreeElem *WTreeCylinder_new(struct W3DObjectBundle *b, int entry, int32_t drivethru, int32_t defradius, float radius);
WTreeElem *WTreeBox_new(struct W3DObjectBundle *b, int entry, const float corners[4][3], int32_t drivethru);
/* The plan-view rectangle of an article's collision vertices: their hull (0x5a0230), the turn (in 0.1
   degree steps up to 90) whose bounds have the least area; the four corners (x, 0, z, 1). 0x474c40 */
void WTree_ArticleRect(UData *article, int *nhull, float corners[16]);

/* A leaf's entry: the element's kind, the element. */
typedef struct { uint8_t kind; WTreeElem *elem; } WTreeEntry;
typedef struct { WTreeEntry *e; uint32_t n, cap; } WTreeList;

typedef struct WTreeCell {
    uint32_t id;                    /* +0x00 */
    struct WTreeCell *child[4];     /* +0x04 */
    WTreeList *list;                /* +0x14: leaves */
} WTreeCell;

typedef struct { WTreeElem **e; uint32_t n, cap; } WTreeElems;

/* WTree (8 bytes; 0x4835e0): the root and every element added (the tree owns them). */
typedef struct WTree {
    WTreeCell *root;           /* +0x0 */
    WTreeElems *all;           /* +0x4 */
} WTree;

/* half: the root's half size; levels: the number of sizes (cells' widths). 0x4835e0 */
WTree *WTree_new(float half, int levels);
void WTree_delete(WTree *t);                                       /* 0x483780 + 0x483580 */
/* Adds the elements (kept by the tree) and inserts each. 0x483870 */
void WTree_Add(WTree *t, WTreeElems *v);
/* The leaf the point's (x, z) falls in: the root when the descent reaches a missing child. 0x4842e0 */
WTreeCell *WTree_LeafAt(WTreeCell *c, const float p[3]);
bool WTree_CellContains(const WTreeCell *c, const float p[3]);     /* 0x484230 */
/* The leaves within r of the point (squares overlapping the square of side 2r). 0x484410 */
typedef struct { WTreeCell **c; uint32_t n, cap; } WTreeCells;
void WTree_LeavesNear(WTreeCell *c, const float p[3], float r, WTreeCells *out);
void WTree_CellBounds(const WTreeCell *c, float *x0, float *z0, float *size);

/* The objects whose bounding boxes come within r of p (the leaves within 3 of it); NULL for none. The
   caller frees it (WTreeElems_free). 0x4737d0 */
WTreeElems *WTree_ObjectsNear(const float p[3], float r);
void WTreeElems_push(WTreeElems *v, WTreeElem *e);
void WTreeElems_free(WTreeElems *v);    /* the vector and its array, not the elements */

/* The elements. pts: vertex pointers; colours: 5:5:5. 0x485330, 0x485ba0, 0x485070 */
WTreeElem *WTreeQuad_new(const float *const pts[4], const uint16_t colours[4], uint16_t flags);
WTreeElem *WTreeTri_new(const float *const pts[3], const uint16_t colours[3], uint16_t flags);
/* The edge from {p0, pad, p1, pad}; one longer than 100 in x or z becomes a unit edge at p1. */
WTreeElem *WTreeEdge_new(const float pts[8], uint16_t flags);
/* The edge's point i (its slot 5). 0x485320 */
const float *WTreeEdge_point(const WTreeElem *e, int i);

/* ---- PCollideArticleBundle (0xc bytes, vtable 0x5b4674; the base PCollidableBundle 0x5b465c) ---- */

struct W3DObjectBundle;
typedef struct {
    struct W3DObjectBundle *bundle;    /* +0x4 (PCollidableBundle's) */
    struct W3DObjectBundle *bundle2;   /* +0x8 */
} PCollideArticleBundle;

void *PCollideArticleBundle_new(struct W3DObjectBundle *b);   /* operator new(0xc) + 0x474b20 */
/* The bundle's collision elements into v: its entries' polygons (only_flagged: only those whose surface
   has bits outside 0xf0) and the animdefs objects (cylinders, boxes). 0x475040 */
void PCollideArticleBundle_Build(PCollideArticleBundle *c, WTreeElems *v, bool only_flagged);

/* The world tree's elements: the edges (<track>.edg) and the flagged polygons. Track_LoadEdges 0x488000 */
void Track_LoadEdges(void *world);

/* ---- the surface under a point (the wheels') ---- */

/* WSurface (0x30 bytes): what a wheel stands on. */
typedef struct {
    int16_t node;              /* +0x00: the road node (0x4743a0) */
    uint8_t normal_ok;         /* +0x02: normal and slope computed for the polygon */
    int32_t f4;                /* +0x04 */
    uint8_t node_changed;      /* +0x08 */
    uint8_t changed;           /* +0x09: the polygon (or node) changed */
    uint8_t none;              /* +0x0a: no polygon under the point */
    float normal[3];           /* +0x0c */
    float slope[3];            /* +0x18: the polygon's first edge turned into its plane, unit */
    uint16_t surface;          /* +0x24: the polygon's flags (0 without one) */
    WTreeElem *elem;           /* +0x28 */
    WTreeCell *cell;           /* +0x2c: the leaf searched last */
} WSurface;

/* A tracker with no polygon: normal up, slope forward (as 0x4744a0 makes one). */
void WSurface_Init(WSurface *s);
/* Back to no polygon at road node `node` (the normal and slope stay). 0x473170 */
void WSurface_Reset(WSurface *s, int16_t node);
/* The polygon under p (in x, z) whose centre is nearest p in height, among those with surface bits
   (& 0xf); NULL if none. 0x4731a0 */
WTreeElem *WSurface_FindBelow(WSurface *s, const float p[3]);
/* The nearest whose centre is above p by more than 0x5b38ec; its flags (0 if none). 0x473290 */
uint16_t WSurface_FindAbove(WSurface *s, const float p[3], WTreeElem **out);
/* Updates the polygon for p: kept while p is still on it if keep. 0x473fc0 */
void WSurface_Update(WSurface *s, const float p[3], bool keep);
void WSurface_ComputeNormal(WSurface *s);                          /* 0x474280 */
const float *WSurface_Normal(WSurface *s);                         /* 0x474380 */
const float *WSurface_Slope(WSurface *s);                          /* 0x474390 */
/* The polygon's vertex i, the zero vector without one. 0x474550 */
const float *WSurface_Vertex(const WSurface *s, int i);
/* The light (1, r, g, b) at p from the vertex colours of the polygon under it. 0x474600 */
void WSurface_Light(const WSurface *s, const float p[3], float out[4]);
/* The ground height under p (a fresh tracker). 0x4744a0 */
float WSurface_HeightAt(const float p[3]);

/* The road edge the segment seg (start, end) crosses whose midpoint is nearest the segment's in height,
   among the edges within |dx| + |dz| of its start; its points into out, its flag into *flag. Only with
   the tracker on a polygon. 0x474030 */
bool WSurface_FindEdge(WSurface *s, const float seg[6], float out[6], uint16_t *flag);

/* The cameras' queries of the world's tree (each returns a vector the caller frees, WTreeElems_free).
   The leaves the segment's box overlaps (0x484690); the polygons holding p in the leaves within r:
   ceilings (flag 0x8000 and no surface bits, 0x473380) or ground (surface bits, 0x4735b0); the objects
   the segment hits (NULL for none; from: the original's unused first argument, 0x473940); the road
   edges within r of p (0x473b00) or crossing the segment (0x473d60). */
void WTree_LeavesAlong(WTreeCell *c, const float seg[6], WTreeCells *out);
WTreeElems *WTree_CeilingsAt(const float p[3], float r);
WTreeElems *WTree_GroundAt(const float p[3], float r);
WTreeElems *WTree_ObjectsAlong(const float from[3], const float seg[6]);
WTreeElems *WTree_EdgesNear(const float p[3], float r);
WTreeElems *WTree_EdgesAlong(const float seg[6]);
/* The height of the plane through v with normal n at p's (x, z). 0x489f90 */
double WTree_PlaneHeight(const float n[3], const float v[3], const float p[3]);

extern WTree *g_wtree;   /* the world's (BWorld +0x24) */
