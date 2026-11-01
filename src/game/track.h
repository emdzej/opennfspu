/* Track loading (0x471000-0x474000, Track_LoadWorld 0x487650): the track's .crp as a world of bundles
   (world.h), its camera animations and visibility walls. The renderer walks BWorld.objects: every object
   is an entry of a bundle, whose article carries the geometry and the Base item (bounding sphere and box);
   docs/re/track.md. */
#pragma once
#include "game/world.h"
#include "support/udata.h"
#include <stdbool.h>
#include <stdint.h>

/* An object of the world (8 bytes): a bundle entry. 0x489760 */
typedef struct {
    W3DObjectBundle *bundle;   /* +0x0 */
    uint8_t flag;              /* +0x4 */
    uint16_t index;            /* +0x6: the entry */
} WObjectRef;

/* A camera animation of the track (0x20 bytes; from the root's 'CmAn' items, 0x4717b0). */
typedef struct {
    uint32_t one;              /* +0x00: 1 */
    uint32_t a, b;             /* +0x04, +0x08: the item's +0x24, +0x28 */
    const void *keys;          /* +0x0c: the item's data from +0x2c */
    uint32_t f10;              /* +0x10 */
    float pos[3];              /* +0x14: the zero vector */
} WCamAnim;

/* The track world (0x3c bytes in the original; ctor 0x487410, the global at 0x628960). The port keeps
   its bundle list (the std::list at +0x14) and object list (built by 0x487940) as arrays. */
typedef struct {
    char *name;                /* +0x00: the path without extension ("GameData\Track\alps") */
    UData *root;               /* +0x04 */
    UDataFile *file;           /* +0x08 */
    void *r3d;                 /* +0x0c: the track renderer (0x44a520), the bundles' world context */
    void *f10;                 /* +0x10: the 0x8c-byte object of 0x47b5d0 */
    W3DObjectBundle *instances;/* +0x18 */
    W3DObjectBundle *scene;    /* +0x1c */
    W3DObjectBundle *anim;     /* +0x20 */
    struct WTree *tree;        /* +0x24: the collision tree (game/wtree.h) */
    uint32_t nvis;             /* +0x28 */
    WVisWall *vis;             /* +0x2c */
    uint8_t *vis_off;          /* +0x30: per wall, set = ignored */
    uint32_t f38;              /* +0x38 */
    /* the bundle list (+0x14) */
    W3DObjectBundle *bundles[8];
    int nbundles;
    /* the object list (0x487940) */
    WObjectRef *objects;
    uint32_t nobjects;
    /* the knockable objects' bodies (+0x34, 0x488820): the scene bundle's, then the instance bundle's */
    struct WSmackBody **bodies;
    uint32_t nbodies;
    /* the camera animations (the vector at +0xc of the global 0x628938) */
    WCamAnim *cams;
    uint32_t ncams;
} BWorld;

extern BWorld *g_world;        /* 0x628960 */
extern char g_track_name[32];  /* 0x657444: the track the race uses ("alps") */

/* Loads g_track_name into g_world; aborts ("ERROR - Couldn't load track") if it can't. Track_Load 0x471ad0 */
void Track_Load(void);
/* Loads <path>.crp and what comes with it. Track_LoadWorld 0x487650 */
bool Track_LoadWorld(BWorld *w, const char *path);
BWorld *BWorld_new(void);      /* 0x487410 */
void BWorld_free(BWorld *w);   /* 0x487500 + operator delete */
void BWorld_DeInit(void);      /* 0x471ba0 */
/* The visibility walls of the root's 'Vis ' item. 0x4889d0 */
void BWorld_LoadVis(BWorld *w);
/* The article bundles and the object list (the first part of 0x487940). */
void BWorld_BuildBundles(BWorld *w);

/* The tracks' directory (the string at 0x65b2b8; "GameData\Track\" in the installed game). */
extern const char *g_track_dir;
/* Calls fn for each *.crp of the tracks' directory; the count. Track_FindCrp 0x471bf0 */
int Track_FindCrp(void (*fn)(const char *name, void *user), void *user);

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(WObjectRef) == 8 && sizeof(WCamAnim) == 0x20, "the object and camera records");
#endif
