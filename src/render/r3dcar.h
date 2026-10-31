/* The 3D car (R3DCar, base class R3DObject; d:\Nfs\Nfs5\game\Common\Render, 0x437700..0x458000): a car's
   geometry from <car>.crp, its attributes from <car>.tpg, its parts per level of detail (R3DCarLOD), and
   the per-frame draw through EAGL (docs/re/cars.md).

   A car's .crp is a chunk tree (support/udata.h): one 'Arti' article per part (Body, Body1, Roof,
   DoorOut, WheelFront, Interior, ...), each with a 'Base' record, vertex arrays 'vt' / 'nm' / 'uv' per
   LOD, primitive lists 'pr', and 'tr' part transforms per LOD; shared items at the root ('mt'
   materials, 'RMth' render methods, 'sf' shapes, 'PdnB' / 'odnW' point lists).

   Which parts a car draws is decided by its options (the 120 ints of the car state at +0x56c, filled
   from the .tpg's [styleN] section): a part whose Base names an option is kept only if that option has
   the Base's value (Body / Body1 / Body2, cabrio tops, windows, lights, licence plates, decals). Within
   an LOD the parts are drawn in the order of their Base sort key (MSVC's std::sort, so equal keys keep
   the original's order), in two passes (opaque, then the primitives flagged translucent).

   Layouts: the original objects are large C++ classes (R3DCar 0x708 bytes, its car state over 0x1100);
   the structs below hold the fields the ported code reads, each commented with its offset in the
   original, not the whole layout. */
#pragma once
#include "render/carlight.h"
#include "eagl/eagl.h"
#include "real/texture.h"
#include "render/car_ini.h"
#include "render/rmaterial.h"
#include "support/udata.h"
#include <stdbool.h>
#include <stdint.h>

enum { CAR_NOPTIONS = 120, CAR_NLODS = 7, CAR_NHINGES = 0x17 };

/* The car's specification (the car state's +0x558): what the renderer reads of it. */
typedef struct {
    char licence[32];            /* +0x1c0: the licence plate's text (8 characters are drawn) */
    uint32_t colours[4];         /* +0x218: the four chosen colours (0xAARRGGBB; +0x21c is the stripe's) */
    int options[CAR_NOPTIONS];   /* +0x32c: forced option values (-1 = the style's) */
    int racenumber;              /* +0x510: < 0 none */
    uint32_t scheme;             /* +0x514: the colour package (R3DCar_SetColours) */
    int stretch;                 /* +0x518: nonzero = the model is scaled by 0x5b37c8 */
    int customtpage;             /* +0x51c: nonzero = the pages are 4444 and start grey, no shapes drawn */
} R3DCarSpec;

/* The simulation's car as the renderer sees it (R3DObject +0xf4). */
typedef struct {
    float pos[3];                /* +0x330 */
    float rot[9];                /* +0x364: rows; row 1 (+0x370) is the car's up */
    float halfsize[3];           /* +0x3a8: half the bounding box (0x444fc0) */
    float radius;                /* +0x3b4 */
    uint32_t flags;              /* +0x52c: 0x10 = sizes and the wheels' LOD from LOD list index 0 (not 4) */
    R3DCarSpec *spec;            /* +0x558 */
    int style;                   /* +0x568 */
    int options[CAR_NOPTIONS];   /* +0x56c */
    float drivercampos[3];       /* +0x10e8 */
    float drivercamrot[3];       /* +0x10f4 */
} R3DCarState;

/* The cached item lookups of a part (0x43c610 constructs them, 0x43c700 uses them), one per kind in the
   exe's table at 0x5e5084: 0 'vt', 1 'nm', 2 'uv', 3 'df', 4 'Base', 5 'pr', 6 'ef'. */
typedef struct { UData *rec; uint32_t id; } R3DItemCache;
enum { R3D_VT, R3D_NM, R3D_UV, R3D_DF, R3D_BASE, R3D_PR, R3D_EF, R3D_NKINDS };

/* One part of an LOD (0x54 bytes in the original). */
typedef struct R3DPart {
    UData *article;                 /* +0x00 */
    UData *end;                     /* +0x04: the end of the article's items (a lookup's miss) */
    R3DItemCache cache[R3D_NKINDS]; /* +0x08 (the 'pr' cache at +0x30 is the draw's iterator) */
    float (*verts)[4];              /* +0x40: own copy of the vertices (animated or deformable parts) */
    const uint8_t *info;            /* +0x44: Base + 0x44 (docs/re/cars.md) */
    const uint8_t *entry;           /* +0x48: Base + 0x64 + 12 * the LOD's entry */
    const uint8_t *base;            /* +0x4c */
    uint8_t damage;                 /* +0x50 */
} R3DPart;

struct R3DCar;

/* A level of detail (0x24 bytes): the parts of every article that has an entry for this LOD number. */
typedef struct R3DCarLOD {
    struct R3DCar *car;    /* +0x4 */
    uint32_t lod;          /* +0x8: 1 (most detailed) .. 7 */
    R3DPart *parts;        /* +0xc */
    uint32_t nparts;       /* +0x10 */
    UData *slot[4];        /* +0x14: the articles with a wheel slot (Base + 0x48) */
} R3DCarLOD;

/* Materials (R3DMaterial, R3DMaterialSet): render/rmaterial.h. */

/* An animation slot of the car (8 records of 0x14 bytes from +0x568; 0x4380c0 / 0x4380e0). */
typedef struct { int pos; int dir; float t; float frames; uint8_t on; } R3DCarAnim;

struct CarPage;
struct Texture;

typedef struct R3DCar {
    IniFile tpg;                    /* +0x004: the .tpg */
    uint32_t colours[18];           /* +0x01c: the recolouring slots of the pages (+0x34: the stripe) */
    float hoodextent;               /* +0x068 */
    float trunkextent;              /* +0x06c */
    float headlightextent;          /* +0x070 */
    float spoilerextent;            /* +0x074 */
    struct CarPage *pages;          /* +0x078: the texture pages (a vector) */
    uint32_t npages;
    const void **sf;                /* +0x084: the 'sf' shapes of the .crp by index (a vector) */
    uint32_t nsf;
    void *shape_d, *shape_l;        /* +0x090, +0x094: <car>d.fsh, <car>l.fsh */
    uint8_t hsv[3];                 /* +0x0a8: hue, saturation, value of colour 0 (0x443bd0); a value above
                                       0x50 takes the light decals (<car>l.fsh) */
    R3DMaterialSet *materials;      /* +0x0ac */
    R3DCarLOD *lods[CAR_NLODS];     /* +0x0b0: the LOD list (a vector) */
    uint32_t nlods;
    struct Texture *globaltex[3];   /* +0x0dc: the shared textures of the materials (0x440870) */
    UData *root;                    /* +0x0ec */
    struct R3DCrpEntry *crp;        /* +0x0f0 */
    R3DCarState *state;             /* +0x0f4 */
    uint32_t nbound;                /* +0x0f8: 'PdnB' points */
    float bound[48][3];             /* +0x0fc */
    uint32_t nwindow;               /* +0x27c: 'odnW' points */
    float window[8][3];             /* +0x280 */
    float dooraxis[3];              /* +0x2b0 */
    uint8_t noroof;                 /* +0x2bc */
    int curlod;                     /* +0x2c0: the LOD list index drawn */
    int cockpitlod;                 /* +0x2c8 */
    float (*hinge)[3];              /* +0x2d0: hinge angles (turns) per kind (R3DCar_v9 fills them) */
    uint8_t loaded;                 /* +0x470 */
    uint8_t interior;               /* +0x471: draw the cockpit LOD */
    uint8_t textures;               /* +0x47c: textures are loaded with the geometry */
    int texinfo;                    /* +0x480 */
    R3DCarAnim anim[8];             /* +0x568 */
    float wheelpos[4][3];           /* +0x6d0: wheel slot offsets (the simulation's) */
    char name[64];                  /* port: the name given to R3DCar_LoadCrp */
    /* port: the pages' texture states (the original's live on R3DObject_LoadHeader's stack, and the
       pages keep a pointer to them). */
    TextureStates pagestates;
    /* port: the car's light block (the original's +0x2dc: render/carlight.h), and whether it was set */
    CarLight light;
    bool lit;
} R3DCar;

/* Game state the renderer reads (globals of the original). */
extern int16_t g_r3d_night;     /* 0x628a28: the track's night flag */
extern int g_r3d_frontend;      /* 0x65b928: the front end's showroom (vs the race) */
extern int g_r3d_mirror;        /* 0x65742c: > 0 mirrors the driver's camera */
extern int g_r3d_quality;       /* 0x6197c4: the detail level (0 = parts never deform; indexes the LOD
                                   tables 0x5cdb44 and 0x5cdb1c) */

/* R3DObject + R3DCar constructors (0x4413f0, 0x437850): state is the car state the car reads. */
void R3DCar_Init(R3DCar *car, R3DCarState *state);
void R3DCar_Free(R3DCar *car);                          /* R3DCar_v0 0x437980 + R3DObject 0x441730 */
/* Loads <name>.crp and <name>.tpg (name as the game gives it, e.g. "GameData\\CarModel\\356a"). Returns
   true as the original does even when the .crp is missing (then car->loaded stays false). 0x441940 */
bool R3DCar_LoadCrp(R3DCar *car, const char *name);
void R3DCar_LoadTpg(R3DCar *car, const char *name);    /* 0x437780 */
void R3DCar_LoadStyles(R3DCar *car, int style);         /* 0x437a30 (the options; the timed extents pend) */

/* The data of a part's article item of a kind (R3D_VT ...) and index (-1: the kind's plain id) through
   the part's cache; NULL if none. 0x43c700 */
void *R3DPart_Item(R3DPart *p, int kind, uint32_t index);

void R3DCarLOD_Build(R3DCarLOD *lod, UData *root);      /* R3DCarLOD_v1 0x43c0e0 */
void R3DCarLOD_Free(R3DCarLOD *lod);                    /* R3DCarLOD_v5 0x43dff0 */
/* Draws the parts whose view mask (Base + 0x4e) shares a bit of mask & 0xf0; cockpit = the driver's view
   (parts flagged for it, Base + 0x51; else those not flagged exterior-only, + 0x52). R3DCarLOD_v2 0x43c760 */
void R3DCarLOD_Draw(R3DCarLOD *lod, EaglMatStack *model, uint8_t mask, bool cockpit);
void R3DCarLOD_Bounds(R3DCarLOD *lod, EaglMatStack *model, float mn[3], float mx[3]); /* R3DCarLOD_v4 0x43ddc0 */

/* The object's draw: model matrix from the car state (0x4433c0), then the LOD dispatch (R3DObject_v14
   0x444d30 via R3DCar_v14 0x43a3c0). model is the current view's object stack. */
void R3DCar_Draw(R3DCar *car, EaglMatStack *model);

/* EAGL_MatAxisAngle 0x539940: a rotation of degrees about an axis. Not in src/eagl yet (the car's
   hinges are its first user): move it there when EAGL's matrix code grows. */
void EAGL_MatAxisAngle(EaglMatrix *m, float degrees, float ax, float ay, float az);
/* The 3x3 rotation by a fraction of a turn about an axis (the identity at 0). 0x532430 */
void Mat3_AxisRotation(const float axis[3], float turns, float out[9]);
