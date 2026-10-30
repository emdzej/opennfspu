/* The render system's mesh helper (0x468180..0x4688a0, 0x28 bytes): an EAGL material built from a list of
   layers and the stream array it draws from, for the sky's pieces (the horizon, the sky levels). The
   layout of the streams: a few base slots, then a fixed number per texture stage; the byte fields say
   which slot holds what. */
#pragma once
#include "eagl/eagl.h"
#include "real/texture.h"
#include <stdint.h>

typedef struct {
    EaglMaterial *mat;      /* +0x00 */
    EaglStream *streams;    /* +0x04 */
    int32_t nstages;        /* +0x08 */
    uint8_t b0c;            /* +0x0c */
    int32_t nverts;         /* +0x10 */
    int32_t prim;           /* +0x14 */
    int32_t type;           /* +0x18 */
    int8_t slot_pos;        /* +0x1c: the positions (base slot) */
    int8_t slot_uv;         /* +0x1d: per stage: the texture coordinates */
    int8_t slot_tex;        /* +0x1e: the texture */
    int8_t slot_blend;      /* +0x1f: the blend parameters (a 0x18-byte object) */
    int8_t slot_colour;     /* +0x20: the vertex colours */
    int8_t slot_pos2;       /* +0x21: the second positions (indexed meshes; base slot) */
    int8_t slot_iuv;        /* +0x22: per stage: indexed texture coordinates */
    int8_t slot_icolour;    /* +0x23: per stage: indexed colours */
    int8_t stride;          /* +0x24: slots per stage */
    int8_t nbase;           /* +0x25: base slots */
} RMesh;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(RMesh) == 0x28, "the original's layout");
#endif

/* Plain mesh: transform layer 0x800e over nverts positions (16 bytes each). 0x468180 */
RMesh *RMesh_ctor(RMesh *m, int type, int prim, int nstages, void *positions, int nverts);
/* Indexed mesh: layers 0x8008 (positions) and 0x8009 (the index positions, info idxinfo). 0x468250 */
RMesh *RMesh_ctorIndexed(RMesh *m, int type, int prim, int nstages, void *positions, void *indexpos, int nverts,
                         uint32_t idxinfo);
void RMesh_dtor(RMesh *m);                                                        /* 0x4688a0 */
/* A stage's layers: uv (3), texture (4, its states set for the sky), blend (8), colour (6); then the draw
   layer and the bind. 0x468520 (via 0x468380) */
void RMesh_stage(RMesh *m, int stage, Texture *tex, void *blend, void *uvs, void *colours, int noclamp);
/* An indexed mesh's stage: the above, then indexed uv (0x800d) and indexed colour (0x800c). 0x468570 */
void RMesh_stageIndexed(RMesh *m, int stage, Texture *tex, void *blend, void *uvs, uint32_t iuvinfo, void *iuvs,
                        void *colours, uint32_t icolinfo, void *icolours, int noclamp);
void RMesh_setPositions(RMesh *m, void *positions, int nverts);                   /* 0x468650 */
void RMesh_setUVs(RMesh *m, int stage, void *uvs);                                /* 0x468680 */
void RMesh_setColours(RMesh *m, int stage, void *colours);                        /* 0x4686c0 */
/* Draws n vertices (-1: all). 0x468350 */
void RMesh_draw(RMesh *m, int n);
