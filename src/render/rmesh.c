/* The render system's mesh helper (see rmesh.h). */
#include "render/rmesh.h"
#include "render/rtex.h"
#include <stdlib.h>
#include <string.h>

static EaglStream *slot(RMesh *m, int stage, int k) { return &m->streams[m->stride * stage + k]; }

static void common(RMesh *m, int type, int prim)
{
    m->prim = prim;
    m->mat = NULL;
    m->nstages = 0;
    m->b0c = 0;
    m->slot_pos = 0;
    m->type = type;
    m->mat = malloc(sizeof *m->mat);
    if (m->mat) EAGL_MaterialInit(m->mat);
}

static void alloc_streams(RMesh *m, int nstages)
{
    m->streams = calloc((size_t)(m->stride * nstages + m->nbase), sizeof *m->streams);
}

RMesh *RMesh_ctor(RMesh *m, int type, int prim, int nstages, void *positions, int nverts)
{
    common(m, type, prim);
    m->slot_colour = 4;
    m->stride = 4;
    m->slot_pos2 = -1;
    m->slot_uv = 1;
    m->slot_tex = 2;
    m->slot_blend = 3;
    m->slot_iuv = -1;
    m->slot_icolour = -1;
    m->nbase = 1;
    alloc_streams(m, nstages);
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(0x800e, 0, 0x20));
    m->streams[m->slot_pos] = (EaglStream){(uint32_t)nverts << 4, positions};
    m->nstages = nstages;
    m->nverts = nverts;
    return m;
}

RMesh *RMesh_ctorIndexed(RMesh *m, int type, int prim, int nstages, void *positions, void *indexpos, int nverts,
                         uint32_t idxinfo)
{
    common(m, type, prim);
    m->slot_uv = 2;
    m->nbase = 2;
    m->slot_pos2 = 1;
    m->slot_tex = 3;
    m->slot_blend = 4;
    m->slot_colour = 5;
    m->slot_iuv = 6;
    m->slot_icolour = 7;
    m->stride = 6;
    alloc_streams(m, nstages);
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(0x8008, 0, 0x20));
    m->streams[m->slot_pos] = (EaglStream){(uint32_t)nverts << 4, positions};
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(0x8009, 0, 0x20));
    m->streams[m->slot_pos2] = (EaglStream){idxinfo, indexpos};
    m->nstages = nstages;
    m->nverts = nverts;
    return m;
}

void RMesh_dtor(RMesh *m)
{
    free(m->streams);
    if (m->mat) m->mat->vt->destroy(m->mat, 1);
}

/* 0x468380: the layers of a stage. The texture's states for it: clamped unless noclamp (0x5377e0 with
   noclamp == 0), state 1 on (0x5377b0), state 0 when the quality row of class 1 starts with 0x10
   (0x5377a0 with RTex_Quality 0x44deb0), set on the texture (0x5361e0). */
static void tex_states(Texture *tex, int noclamp)
{
    TextureStates st = tex->states;
    TextureStates_settile(&st, noclamp == 0);
    TextureStates_set(&st, 1, 1);
    const uint8_t *q = RTex_quality(1);
    TextureStates_set(&st, 0, q && *q == 0x10);
    Texture_setstates(tex, &st);
}

static void stage_layers(RMesh *m, int s, Texture *tex, void *blend, void *uvs, void *colours, int noclamp)
{
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(3, 0, 0x20));
    *slot(m, s, m->slot_uv) = (EaglStream){(uint32_t)m->nverts << 3, uvs};
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(4, 0, 0x20));
    *slot(m, s, m->slot_tex) = (EaglStream){0x54, tex};
    if (tex) tex_states(tex, noclamp);
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(8, 0, 0x20));
    *slot(m, s, m->slot_blend) = (EaglStream){0x18, blend};
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(6, 0, 0x20));
    *slot(m, s, m->slot_colour) = (EaglStream){(uint32_t)m->nverts << 2, colours};
}

void RMesh_stage(RMesh *m, int stage, Texture *tex, void *blend, void *uvs, void *colours, int noclamp)
{
    stage_layers(m, stage, tex, blend, uvs, colours, noclamp);
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(0, 0, 0x20));
    EAGL_MaterialBind(m->mat);
}

void RMesh_stageIndexed(RMesh *m, int stage, Texture *tex, void *blend, void *uvs, uint32_t iuvinfo, void *iuvs,
                        void *colours, uint32_t icolinfo, void *icolours, int noclamp)
{
    stage_layers(m, stage, tex, blend, uvs, colours, noclamp);
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(0x800d, 0, 0x20));
    *slot(m, stage, m->slot_iuv) = (EaglStream){iuvinfo, iuvs};
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(0x800c, 0, 0x20));
    *slot(m, stage, m->slot_icolour) = (EaglStream){icolinfo, icolours};
    EAGL_MaterialAddLayer(m->mat, EAGL_CreateLayer(0, 0, 0x20));
    EAGL_MaterialBind(m->mat);
}

void RMesh_setPositions(RMesh *m, void *positions, int nverts)
{
    m->streams[m->slot_pos] = (EaglStream){(uint32_t)nverts << 4, positions};
    m->nverts = nverts;
}

void RMesh_setUVs(RMesh *m, int stage, void *uvs) { *slot(m, stage, m->slot_uv) = (EaglStream){(uint32_t)m->nverts << 3, uvs}; }

void RMesh_setColours(RMesh *m, int stage, void *colours)
{
    *slot(m, stage, m->slot_colour) = (EaglStream){(uint32_t)m->nverts << 2, colours};
}

void RMesh_setBlend(RMesh *m, int stage, void *blend) { *slot(m, stage, m->slot_blend) = (EaglStream){0x18, blend}; }

void RMesh_setTexture(RMesh *m, int stage, Texture *tex, int noclamp)
{
    *slot(m, stage, m->slot_tex) = (EaglStream){0x54, tex};
    if (tex) tex_states(tex, noclamp);
}

void RMesh_setIndexPositions(RMesh *m, void *idx, uint32_t info) { m->streams[m->slot_pos2] = (EaglStream){info, idx}; }
void RMesh_setIndexUVs(RMesh *m, int stage, void *idx, uint32_t info) { *slot(m, stage, m->slot_iuv) = (EaglStream){info, idx}; }
void RMesh_setIndexColours(RMesh *m, int stage, void *idx, uint32_t info) { *slot(m, stage, m->slot_icolour) = (EaglStream){info, idx}; }

void RMesh_draw(RMesh *m, int n)
{
    if (n == -1) n = m->nverts;
    if (n && m->mat) m->mat->vt->draw(m->mat, m->streams, m->prim, n);
}
