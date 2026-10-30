/* Materials and render methods of 3D objects (cars and tracks; render module 0x44a090-0x44c330).

   A render method ('RMth' records of a .crp: a 16-byte name, groups, layer entries, constant streams) is
   an EAGL material built from the record for a method key (0x6197f8, the quality's layer groups) and a
   material key (the 'mt' record's +0xc, which layers apply): the layers come from EAGL's factories (the
   game's: rlayers.c), and the method keeps one stream per layer input, found by stream id.

   A material set (one per object) holds the object's materials, built from its 'mt' records: each refers
   to a method by name and binds textures (by (id, name) from the global bank or the object's 'sf' shapes)
   and constant streams to stream ids. Drawing a primitive list binds its streams (R3DMaterial_Bind) and
   runs the method's material. */
#pragma once
#include "eagl/eagl.h"
#include "render/rtex.h"
#include "support/udata.h"
#include <stdbool.h>
#include <stdint.h>

/* A render method (the value of the method map's node at +0x28). */
typedef struct {
    UData rec;                  /* +0x00: a copy of the 'RMth' record (owns its data) */
    const uint8_t *data;        /* +0x10 */
    EaglMaterial *material;     /* +0x14 */
    uint32_t nids;              /* +0x18 */
    uint32_t nstreams;          /* +0x1c */
    uint32_t nentries;          /* +0x20: layer entries of the record */
    uint32_t mtkey;             /* +0x24 */
    struct RMethodId { uint32_t id; int32_t slot; } *ids;   /* +0x28: sorted by id */
    EaglStream *streams;        /* +0x2c */
} RMethod;

/* A material's bound stream (0x10 bytes; CRect-shaped in the original). */
typedef struct { void *data; uint32_t info; int32_t slot; uint32_t id; } R3DMaterialRef;

/* A material (0x1c bytes). */
typedef struct {
    uint32_t texture;           /* +0x00: the first texture reference's name ('XETN' none) */
    uint32_t index;             /* +0x04 */
    uint32_t nrefs;             /* +0x08: bound references */
    int32_t *override;          /* +0x0c: during a draw, the streams the part replaces (-1) */
    R3DMaterialRef *refs;       /* +0x10 */
    RMethod *method;            /* +0x14 */
    const uint8_t *mt;          /* +0x18: the 'mt' item's data */
} R3DMaterial;

/* A material set (0x28 bytes). */
typedef struct R3DMaterialSet {
    R3DMaterial **mat;          /* +0x00 */
    void *shapes;               /* +0x04: (cleared; the original's tree node) */
    TexManager *manager;        /* +0x08: the constructor's manager */
    RTexBank bank;              /* +0x0c: the object's textures (the original's bank is 12 bytes; the port's
                                   is longer, so the fields below are past their original offsets) */
    uint32_t count;             /* +0x18 */
    int32_t current;            /* +0x1c: -1 */
    uint32_t pad20;
    struct R3DMaterialSet *next;/* +0x24: the list of sets (0x619768) */
} R3DMaterialSet;

extern uint32_t g_rmethod_key;     /* 0x6197f8: the layer groups of the quality (0: the base group only) */
extern RTexBank *g_rtex_global;    /* 0x6115e8: the global textures ('SPC1'.., the texture pages) */

R3DMaterialSet *R3DMaterialSet_ctor(R3DMaterialSet *ms, TexManager *m, int texclass);   /* 0x44a520 */
void R3DMaterialSet_dtor(R3DMaterialSet *ms);                                             /* 0x44a5d0 */
/* The materials and the bank's textures (what an object's reload drops; part of 0x44a5d0). */
void R3DMaterialSet_Clear(R3DMaterialSet *ms);
/* The object's methods, shapes and materials from its root record; dir is where 'sn' shape files are
   (the track directory, 0x65b2b8). 0x44a6b0 */
void R3DMaterialSet_Build(R3DMaterialSet *ms, UData *root, const char *dir);

/* The method of a name for the keys, NULL if not built yet. RMethod_Find 0x44ba40 */
RMethod *RMethod_Find(const char *name, uint32_t key, uint32_t mtkey);
/* Builds the method of an 'RMth' record for the keys (once). 0x44bab0 (0x44be40 builds it) */
void RMethod_Build(const UData *rec, uint32_t key, uint32_t mtkey);
/* A stream id's slot, -1 if the method has none. 0x44a160 */
int RMethod_Slot(const RMethod *m, uint32_t id);
/* Sets a stream by id. 0x44a4c0 */
void RMethod_Assign(RMethod *m, uint32_t id, void *data, uint32_t info);

void R3DMaterial_SetOverride(R3DMaterial *mat, int32_t *override);                        /* 0x44a0e0 */
void R3DMaterial_OverrideStream(R3DMaterial *mat, uint32_t id, void *data, uint32_t info); /* 0x44a110 */
struct R3DPart;
/* Binds the material's streams and a primitive list's ('pr' item data) into its method: the part's items
   for kinds below 0x100, the object's records (root) for the others, the index lists with listinfo.
   Returns the primitive type. 0x44a190 */
int R3DMaterial_Bind(R3DMaterial *mat, uint32_t listinfo, struct R3DPart *part, UData *root, const uint32_t *pr);
/* Bind, then the method's material draws n elements (the material's slot 1). */
void R3DMaterial_DrawList(R3DMaterial *mat, struct R3DPart *part, UData *root, const uint32_t *pr, uint32_t n);
