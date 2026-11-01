/* The track world (0x474000-0x48a000): the loaded track's chunk tree split into bundles of articles, and
   the visibility walls. docs/re/track.md describes the data.

   A track .crp is a chunk tree (support/udata.h) whose root ('Trak') holds the shared items (camera
   animations 'CmAn', descriptions 'Desc', materials 'mt', render methods 'RMth', the simulation data
   'SimD'/'SimT', the visibility walls 'Vis ') and one article ('Arti') per piece of the track: road and
   scenery chunks ("RD1136C (1 PRIMARY)", "CNK0000L (...)"), objects, instances. Each article has a 'Base'
   item (flags, bounding box and sphere, object type), its geometry ('vt', 'nm', 'uv', 'df', 'pr' lists)
   and, for placed objects, a transform 'tr'.

   Track_LoadWorld (track.c) hands every article to the bundles in a fixed order; each bundle takes the
   articles it is for and clears them from the array, so every article ends up in exactly one bundle (the
   scene bundle only copies its prototypes):
     WSceneBundle      'tr' + Base 0x8000: object prototypes the scene places at run time (up to 64)
     WInstanceBundle   'tr' without 0x8000: placed objects (its prototypes are the scene's)
     WAnimBundle       an 'Anqt' item: animated articles
     WMidGroundBundle  Base & 0x6000: mid-ground scenery (0x2000 clear: entry flag 0x400000)
     WStaticBundle     everything left: the static track
     WLineBundle       the line elements (built from the world's line list, not from articles)
   The bundles' drawing (RDrawArticleBundle and friends, 0x44fe30/0x450b10) and collision
   (PCollideArticleBundle 0x474b20) objects are made by other modules. */
#pragma once
#include "support/udata.h"
#include <stdbool.h>
#include <stdint.h>

enum {
    WBASE_SCENE = 0x8000,        /* Base flags: a scene prototype */
    WBASE_MIDGROUND = 0x6000,    /* mid-ground scenery */
    WBASE_MIDSOLID = 0x2000,     /* (mid-ground without it: entry flag WENTRY_NOSOLID) */
    WENTRY_NOSOLID = 0x400000,
};

/* The 'Base' item of an article (the offsets the world code reads). */
typedef struct {
    uint32_t flags;      /* +0x00 */
    uint32_t pad[4];     /* +0x04 */
    float bmin[3];       /* +0x14: bounding box */
    float bmax[3];       /* +0x20 */
    float centre[3];     /* +0x2c: bounding sphere */
    float w;             /* +0x38 */
    float radius;        /* +0x3c */
    uint32_t pad40;      /* +0x40 */
    uint32_t type;       /* +0x44: object type (four characters, e.g. 'TLFD'), scene and instance articles */
} WBase;
_Static_assert(sizeof(WBase) == 0x48, "the Base item's prefix the world reads");

/* One article of a bundle. */
typedef struct {
    uint32_t flags;      /* +0x0: WENTRY_* */
    UData *article;      /* +0x4 */
} WBundleEntry;

/* An article's cached item lookups (0x40 bytes): the article, the end of its items, and seven
   {found record, id} pairs for the ids of the table at 0x5e5084 (vt nm uv df Base pr ef). Lookups of the
   same id reuse the record found last time (0x43c620, 0x43c650). */
enum { WITEM_VT, WITEM_NM, WITEM_UV, WITEM_DF, WITEM_BASE, WITEM_PR, WITEM_EF, WITEM_COUNT };
typedef struct {
    UData *article;      /* +0x00 */
    UData *end;          /* +0x04: UData_itemsend(article) */
    struct { UData *hit; uint32_t id; } slot[WITEM_COUNT]; /* +0x08 */
} WArticleCache;

/* A placed object of the scene or instance bundle (0x38 bytes). */
typedef struct {
    float pos[3];        /* +0x00 */
    float rot[9];        /* +0x0c */
    uint32_t type;       /* +0x30: the Base type */
    uint8_t flag;        /* +0x34: the type's animation definition byte +0x15 (WMidGround object defs) */
} WPlaced;

typedef struct W3DObjectBundle W3DObjectBundle;

/* The bundles' virtual methods, slots in the original's order (re/vtables.tsv). */
typedef struct {
    void (*dtor)(W3DObjectBundle *b, int free_self);                     /* 0 */
    int (*load)(W3DObjectBundle *b, UData **articles, int count);        /* 1: takes its articles; the count */
    void (*s2)(void), (*s3)(void), (*s4)(void);
    /* 5: the entry's own matrix (transformed bundles; NULL for the others in the port) */
    void (*matrix)(W3DObjectBundle *b, int entry, float out[16]);
    void (*s6)(void), (*s7)(void), (*s8)(void);
    void (*buildcache)(W3DObjectBundle *b);                              /* 9: 0x477ca0 */
    void (*fillcache)(W3DObjectBundle *b);                               /* 10: 0x477cd0 */
} W3DObjectBundleVtbl;

/* R3DTypes::W3DObjectBundle (0x30 bytes; ctor 0x489090). */
struct W3DObjectBundle {
    const W3DObjectBundleVtbl *vt; /* +0x00 */
    uint8_t transformed;           /* +0x04: entries carry their own transform */
    uint8_t b5;                    /* +0x05 */
    void *draw;                    /* +0x08: the drawing object (render module) */
    void *collide;                 /* +0x0c: the collision object */
    uint32_t f10;                  /* +0x10 */
    uint32_t f14;                  /* +0x14 */
    uint32_t count;                /* +0x18 */
    WBundleEntry *entries;         /* +0x1c */
    uint32_t f20;                  /* +0x20 */
    uint32_t kind;                 /* +0x24: 'Arti' for article bundles */
    int serial;                    /* +0x28: from the counter at 0x5d0608 */
    void *world;                   /* +0x2c: the world's context (BWorld +0xc) */
};

/* WArticleBundle (0x34) and the bundles derived from it. */
typedef struct {
    W3DObjectBundle base;
    WArticleCache *cache;          /* +0x30: one per entry */
} WArticleBundle;

typedef struct {                   /* WSceneBundle (0x4c; ctor 0x482550) */
    WArticleBundle a;
    int nproto;                    /* +0x34 */
    WBundleEntry *protos;          /* +0x38 */
    WPlaced *proto;                /* +0x3c: one per prototype */
    WPlaced *placed;               /* +0x40: one per entry (count = the scene's instance capacity) */
    uint8_t unlimited;             /* +0x44: set when the scene has no instance limit (0x46d910 == 0) */
    uint32_t used;                 /* +0x48: instances placed */
} WSceneBundle;

typedef struct {                   /* WInstanceBundle (0x44; ctor 0x477fd0) */
    WArticleBundle a;
    int nproto;                    /* +0x34: the prototypes ('tr' + Base 0x8000), kept but not taken */
    WBundleEntry *protos;          /* +0x38 */
    WPlaced *proto;                /* +0x3c */
    WPlaced *placed;               /* +0x40: one per entry */
} WInstanceBundle;

typedef WArticleBundle WStaticBundle;      /* 0x34; ctor 0x483350 */
typedef WArticleBundle WMidGroundBundle;   /* 0x34; ctor 0x47cb40 */
typedef struct {                   /* WAnimBundle (0x38; ctor 0x4776a0) */
    WArticleBundle a;
    struct WAnimEntry *anims;      /* +0x34: the entries' animations, sounds, types (game/anim.h) */
} WAnimBundle;

/* A visibility wall (0x1c bytes; from the 'Vis ' item's 0x18-byte records, two points each): the
   segment in the ground plane (x, z). */
typedef struct {
    uint8_t high;        /* +0x00: the first point's y isn't 0 */
    float x, z;          /* +0x04 */
    float dx, dz;        /* +0x0c */
    float len2;          /* +0x14 */
    float inv_len2;      /* +0x18 */
} WVisWall;

/* Bundle constructors (the world context is the BWorld's +0xc object). */
W3DObjectBundle *WSceneBundle_new(void *world);      /* 0x482550 */
W3DObjectBundle *WInstanceBundle_new(void *world);   /* 0x477fd0 */
W3DObjectBundle *WAnimBundle_new(void *world);       /* 0x4776a0 */
W3DObjectBundle *WMidGroundBundle_new(void *world);  /* 0x47cb40 */
W3DObjectBundle *WStaticBundle_new(void *world);     /* 0x483350 */
void W3DObjectBundle_delete(W3DObjectBundle *b);     /* vt->dtor(b, 1) */
void W3DObjectBundle_init(W3DObjectBundle *b, uint32_t kind, void *world);   /* 0x489090 */

/* An article's Base item (through its cache slot), NULL without one. */
const WBase *WArticleBundle_base(WArticleBundle *b, int entry);
/* The entry's bounding sphere (x, y, z, radius); transformed bundles move it by the entry's
   transform. 0x477d40 */
void WArticleBundle_sphere(WArticleBundle *b, int entry, float out[4]);
/* The entry's bounding box. 0x477ed0 */
void WArticleBundle_box(WArticleBundle *b, int entry, float bmin[3], float bmax[3]);

/* The visibility walls from the 'Vis ' item of the track root (0x4889d0); a wall (0x4872a0). */
void WVisWall_set(WVisWall *w, const float p0[3], const float p1[3]);
/* Whether the sight line from a to b (x, z) crosses an enabled wall; `high` counts only walls whose first
   point is off the ground. 0x488ac0 */
bool WVis_blocked(const WVisWall *walls, const uint8_t *disabled, uint32_t n, const float a[3], const float b[3], bool high);
/* Segment intersection in the ground plane: p and q are {x0, y0, z0, x1, y1, z1}; at (if not NULL) gets
   the point on p. 0x489bf0 */
bool World_SegmentsCross(const float p[6], const float q[6], float at[3]);

/* The scene's instances: the next slot gets the prototype of `type` (0x482bb0); a type's prototype
   (0x4785d0). */
int WSceneBundle_place(WSceneBundle *s, uint32_t type, const float pos[3], const float rot[9]);
int WSceneBundle_findproto(const WSceneBundle *s, uint32_t type);
W3DObjectBundle *WArticleBundle_new(void *world);   /* 0x477b30 */

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(W3DObjectBundle) == 0x30 && sizeof(WArticleBundle) == 0x34, "the bundles' layouts");
_Static_assert(sizeof(WSceneBundle) == 0x4c && sizeof(WInstanceBundle) == 0x44 && sizeof(WAnimBundle) == 0x38, "the bundles' layouts");
_Static_assert(sizeof(WArticleCache) == 0x40 && sizeof(WBundleEntry) == 8, "the caches and entries");
#endif
_Static_assert(sizeof(WPlaced) == 0x38 && sizeof(WVisWall) == 0x1c, "the placement and wall records");

/* One object of the visibility pass (the per-object part of 0x47f8a0): skipped if already queued this
   frame, outside the view's sphere test, behind a visibility wall, or beyond the distance (far objects,
   the entry flags 0x400020: 700 m, or 600 m with 0x400000; others the camera's view distance, against
   their nearest point by the pass's square-root estimate); else handed to its draw object (slot 1) with
   the squared distance. viewdist2 is the camera's (+0x128). */
struct EaglView;
void World_VisitObject(W3DObjectBundle *b, int index, struct EaglView *view, const float eye[3], float viewdist2,
                       const WVisWall *walls, const uint8_t *disabled, uint32_t nwalls);
