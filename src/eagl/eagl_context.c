/* The EAGL context, render-state table, materials and layers (0x5387a0, 0x53eed0..0x53f6f0,
   0x5401e0..0x540920, 0x548f80, 0x549050). */
#include "eagl/eagl.h"
#include "exe.h"
#include "real/set3d.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

EaglContext *g_eagl;

/* ---- context ---- */

/* The device object's constructor allocates the context (0x5387a0). Its memory isn't cleared there
   (operator new): calloc here, so the port starts from zeros where the original had heap contents. */
EaglContext *EAGL_ContextCreate(void)
{
    free(g_eagl);
    g_eagl = calloc(1, sizeof *g_eagl);
    if (!g_eagl) return NULL;
    g_eagl->pad117e0 = 0;
    g_eagl->flags = 0;
    g_eagl->guardband = 1.0f;
    g_eagl->depthbias = 0.0f;
    g_eagl->twouv = 0;
    return g_eagl;
}

void EAGL_ContextFree(void)
{
    free(g_eagl);
    g_eagl = NULL;
}

void EAGL_ContextInit(EaglContext *c, int mode)
{
    int shift = 0;
    float gb[4];
    const ThrashAbout *about = THRASH.fn.about();
    if (THRASH.fn.setstate(THRASH_STATE_GUARDBAND, (uintptr_t)gb) && about && mode > 0 && mode < about->nmodes &&
        (float)about->modes[mode].width * 1.5f < gb[1] && (float)about->modes[mode].height * 1.5f < gb[3]) {
        shift = 1;
        c->flags |= EAGL_CF_GUARDBAND;
    }
    EAGL_SetGuardBand(shift);
    c->guardband = (float)(1 << shift);
    if (about && about->name[0] == 'D' && about->name[1] == 'X' && about->name[2] == '7') c->flags |= EAGL_CF_DX7;
}

void EAGL_ContextResetStages(EaglContext *c)
{
    c->twouv = -1;
    c->blend1 = -1;
    c->blend0 = -1;
    THRASH.fn.setstate(0x29, 8);
    c->blend0 = 8;
    if (c->blend1 != 0) {
        THRASH.fn.setstate(0x10029, 0);
        c->blend1 = 0;
    }
    THRASH.fn.setstate(0x2a, 0);
    c->twouv = 0;
}

void EAGL_ContextSetVertexFormat(EaglContext *c, int twouv)
{
    if (c->twouv == twouv) return;
    if (twouv == 0) {
        if (c->blend0 != 8) {
            THRASH.fn.setstate(0x29, 8);
            c->blend0 = 8;
        }
        if (c->blend1 != 0) {
            THRASH.fn.setstate(0x10029, 0);
            c->blend1 = 0;
        }
        THRASH.fn.setstate(0x2a, 0);
        c->twouv = 0;
    } else if (twouv == 1) {
        THRASH.fn.setstate(0x2a, 1);
        c->twouv = 1;
    }
}

/* The view's near/far and screen mapping into the context for the kernels (0x540580). */
void EAGL_ContextViewport(EaglContext *c)
{
    const EaglView *v = c->view;
    if (v->projection == 1) {
        c->znear = 0.0f;
        c->flags |= EAGL_CF_ORTHO;
        c->zfar = 1.0f;
        c->sx = v->vp_sx;
        c->sy = v->vp_sy;
        c->sz = 1.0f;
        c->ox = v->vp_cx + 0.009999999776482582f;
        c->oy = v->vp_cy + 0.009999999776482582f;
        c->oz = 0.0f;
        return;
    }
    c->flags |= EAGL_CF_PERSPECTIVE;
    c->znear = v->znear;
    c->zfar = v->zfar;
    c->sx = v->vp_sx;
    c->sy = v->vp_sy;
    c->sz = v->zscale;
    c->ox = v->vp_cx + 0.009999999776482582f;
    c->oy = v->vp_cy + 0.009999999776482582f;
    c->oz = v->zoffset;
}

/* Per-primitive codes from the vertex codes (0x540400 and its four cases): strips step one vertex
   (three codes), lines two, triangle lists three, quads four. */
void EAGL_ContextPrimCodes(EaglContext *c)
{
    const uint32_t *v = c->codes;
    uint32_t (*out)[2] = c->primcodes;
    int n = c->nverts;
    switch (c->prim) {
    case 1:                                    /* 0x540414 */
        for (int k = n - 3; k >= 0; k--, v++, out++) {
            (*out)[1] = (v[0] | v[1] | v[2]) & EAGL_CODE_CLIP;
            (*out)[0] = v[0] & v[2] & v[1] & EAGL_CODE_REJECT;
        }
        break;
    case 2:                                    /* 0x540519 */
        for (int k = n - 2; k >= 0; k -= 2, v += 2, out++) {
            (*out)[1] = (v[0] | v[1]) & EAGL_CODE_CLIP;
            (*out)[0] = v[1] & v[0] & EAGL_CODE_REJECT;
        }
        break;
    case 3:                                    /* 0x54046a */
        for (int k = n - 3; k >= 0; k -= 3, v += 3, out++) {
            (*out)[1] = (v[0] | v[1] | v[2]) & EAGL_CODE_CLIP;
            (*out)[0] = v[0] & v[2] & v[1] & EAGL_CODE_REJECT;
        }
        break;
    case 4:                                    /* 0x5404c0 */
        for (int k = n - 4; k >= 0; k -= 4, v += 4, out++) {
            (*out)[1] = (v[0] | v[2] | v[1] | v[3]) & EAGL_CODE_CLIP;
            (*out)[0] = v[1] & v[3] & v[0] & v[2] & EAGL_CODE_REJECT;
        }
        break;
    }
}

/* Whether every vertex is inside the guard band (OR of the codes) or all outside one plane (AND),
   from codes stored at stride 4 bytes. */
static void summarise(EaglContext *c, const uint32_t *codes, int n)
{
    uint32_t any = 0, all = 0xffffffffu;
    for (; n - 2 >= 0; n -= 2, codes += 2) {
        any |= codes[0] | codes[1];
        all &= codes[1] & codes[0];
    }
    if (n - 1 >= 0) {
        all &= codes[0];
        any |= codes[0];
    }
    if (((any | all << 16) & 0x3c030000u) != 0) c->flags |= EAGL_CF_REJECT;
    if (!(any & EAGL_CODE_CLIP)) c->flags |= EAGL_CF_INSIDE;
}

static void transform_setup(EaglContext *c, uint32_t *codes, int dstride)
{
    EaglView *v = c->view;
    EAGL_ViewUpdate(v);
    EAGL_SetTransformMatrix(&v->combined);
    if (v->projection == 1) {
        c->znear = 0.0f;
        c->zfar = 1.0f;
    } else {
        c->znear = v->znear;
        c->zfar = v->zfar;
    }
    /* The transformer is left pointing at the context's near/far pair, as in the original: later
       transforms (EAGL_ViewProject) clip-code against the last draw's near and far. */
    EAGL_SetNearFar(&c->znear);
    EAGL_SetCodeOutput(codes);
    EAGL_SetSourceStride(16);
    EAGL_SetDestStride(dstride);
    EAGL_SetCodeStride(4);
}

void EAGL_ContextTransform(EaglContext *c, int stride)
{
    transform_setup(c, c->codes, stride);
    EAGL_Transform(c->view->projection == 1 ? 0x613 : 0x20b, c->nverts, c->coords, c->verts);
    uint32_t any = 0, all = 0xffffffffu;
    const uint32_t *k = c->codes;
    int n = c->nverts;
    for (; n - 2 >= 0; n -= 2, k += 2) {
        any |= k[0] | k[1];
        all &= k[1] & k[0];
    }
    if (n - 1 >= 0) {
        all &= k[0];
        any |= k[0];
    }
    if (((any | all << 16) & 0x3c030000u) == 0) {
        c->flags |= EAGL_CF_TRANSFORMED;
        EAGL_ContextViewport(c);
        if (any & EAGL_CODE_CLIP) EAGL_ContextPrimCodes(c);     /* through the jump table 0x540904 */
        else c->flags |= EAGL_CF_INSIDE;
        return;
    }
    c->flags |= EAGL_CF_REJECT;
}

void EAGL_TransformToContext(EaglContext *c, int n, const void *src, void *dst, uint32_t *codes, int dstride)
{
    transform_setup(c, codes, dstride);
    EAGL_Transform(c->view->projection == 1 ? 0x613 : 0x20b, n, src, dst);
    summarise(c, codes, n);
}

/* ---- render states ---- */

typedef void (*RSFn)(uint32_t state, uintptr_t value);
static void rs_probe(uint32_t s, uintptr_t v) { (void)v; EAGL_RSProbe(s); }
static void rs_blend(uint32_t s, uintptr_t v) { EAGL_RSSetBlend(s, (int)v); }
static void rs_cull(uint32_t s, uintptr_t v) { EAGL_RSSetCull(s, (int)v); }
static void rs_relbias(uint32_t s, uintptr_t v) { EAGL_RSSetRelativeBias(s, (int)v); }
static void rs_none(uint32_t s, uintptr_t v) { (void)s; (void)v; }                       /* 0x516210 */
static const RSFn rs_fn[6] = { rs_probe, EAGL_RSSet, rs_blend, rs_cull, rs_relbias, rs_none }; /* 0x5ded60 */
static int32_t rs_state[6], rs_value[6];          /* 0x5ded30: {state, value} pairs, from the exe */
static bool rs_loaded;
static int rs_cull_mode = 1;                    /* the render-state object's cull setting */
static int rs_bias_base;                          /* the render-state object's depth-bias base */

static void rs_load(void)
{
    if (rs_loaded || !exe_loaded()) return;
    for (int i = 0; i < 6; i++) {
        rs_state[i] = (int32_t)exe_u32(0x5ded30 + 8 * i);
        rs_value[i] = (int32_t)exe_u32(0x5ded34 + 8 * i);
    }
    rs_loaded = true;
}

void EAGL_RSSet(uint32_t s, uintptr_t v) { THRASH.fn.setstate(s, v); }

/* Depth bias: with the DX7 path the kernels add it to z themselves (2^-15 units), else THRASH does. */
void EAGL_RSSetDepthBias(uint32_t s, int v)
{
    if (g_eagl && (g_eagl->flags & EAGL_CF_DX7)) {
        g_eagl->depthbias = (float)v * 3.0517578125e-05f;
        return;
    }
    THRASH.fn.setstate(s, (uintptr_t)v);
}

void EAGL_RSSetAlias(uint32_t s, uintptr_t v) { THRASH.fn.setstate(s, v); }

void EAGL_RSSetRelativeBias(uint32_t s, int v) { EAGL_RSSetDepthBias(s, rs_bias_base + v); }

void EAGL_RSSetBlend(uint32_t s, int v)
{
    if (v == -1) {
        THRASH.fn.setstate(0x194, 7);
        THRASH.fn.setstate(0x195, 6);
        return;
    }
    THRASH.fn.setstate(s, (uintptr_t)v);
}

void EAGL_RSSetCull(uint32_t s, int v)
{
    (void)s;
    THRASH.fn.setstate(2, v == 1 ? (uintptr_t)rs_cull_mode : 0);
}

void EAGL_RSProbe(uint32_t s)
{
    intptr_t old = THRASH.fn.getstate(s);
    if (!THRASH.fn.setstate(s, s)) {
        THRASH.fn.setstate(s, (uintptr_t)old);
        rs_value[0] = (int32_t)old;
    }
}

void EAGL_RSApply(const int32_t v[6])
{
    rs_load();
    for (int i = 0; i < 6; i++)
        if (v[i] != rs_value[i]) {
            rs_value[i] = v[i];
            rs_fn[i]((uint32_t)rs_state[i], (uintptr_t)v[i]);
        }
}

int32_t EAGL_RSValue(int i)
{
    rs_load();
    return rs_value[i];
}

void EAGL_RSReset(void)
{
    rs_load();
    for (int i = 0; i < 6; i++) rs_fn[i]((uint32_t)rs_state[i], (uintptr_t)rs_value[i]);
}

/* The cull value and depth-bias base come from the render-state object (*0x69ed0c: [1]+0x10, [1]+0x30). */
void EAGL_RSSetDevice(int cull, int biasbase)
{
    rs_cull_mode = cull;
    rs_bias_base = biasbase;
}

/* ---- materials ---- */

static void material_destroy(EaglMaterial *m, int free_it)
{
    for (EaglLayer *l = m->layers, *n; l; l = n) {
        n = l->next;
        l->vt->destroy(l, 1);
    }
    m->layers = NULL;
    if (free_it) free(m);
}

static const EaglMaterialVtbl material_vt = { material_destroy, EAGL_MaterialDraw };   /* 0x5b3c94 */

void EAGL_MaterialInit(EaglMaterial *m)
{
    m->vt = &material_vt;
    m->layers = NULL;
    m->streams = NULL;
}

void EAGL_MaterialDraw(EaglMaterial *m, EaglStream *streams, int prim, int n)
{
    if (n <= 0) return;
    EaglContext *c = g_eagl;
    m->streams = streams;
    c->nverts = n;
    c->prim = prim;
    c->pad117e0 = 0;
    c->flags &= 0xffff0000u;
    c->pad014 = 0;
    c->stamp_uv = 0;
    c->stamp_specular = 0;
    c->stamp_diffuse = 0;
    for (EaglLayer *l = m->layers; l;) {
        EaglStream *s1 = l->slot1 ? m->streams + l->slot1 : NULL;
        l = l->vt->execute(l, m->streams + l->slot0, s1);
    }
}

void EAGL_MaterialAddLayer(EaglMaterial *m, EaglLayer *l)
{
    EaglLayer *t = m->layers;
    if (!t) {
        m->layers = l;
        l->next = NULL;
        m->layers->prev = NULL;
        l->material = m;
        return;
    }
    while (t->next) t = t->next;
    t->next = l;
    l->prev = t;
    l->next = NULL;
    l->material = m;
}

void EAGL_MaterialBind(EaglMaterial *m)
{
    uint32_t slot = 0;
    for (EaglLayer *l = m->layers; l; l = l->next) {
        l->vt->reset(l);
        l->slot0 = 0;
        l->slot1 = 0;
        if (l->vt->wants_stream0(l)) l->slot0 = slot++;
        if (l->vt->wants_stream1(l)) l->slot1 = slot++;
        if (l->vt->wants_stream0(l) && !l->vt->wants_stream1(l)) l->slot1 = l->slot0;
    }
    for (EaglLayer *l = m->layers; l; l = l->next) EAGL_LayerResolve(l);
}

EaglLayer *EAGL_LayerFindPrev(EaglLayer *self, uint32_t type, uint32_t id)
{
    for (EaglLayer *l = self->prev; l; l = l->prev)
        if (l->type == type && (id == 0 || id == l->id)) return l;
    return NULL;
}

EaglLayer *EAGL_LayerFindNext(EaglLayer *self, uint32_t type, uint32_t id)
{
    for (EaglLayer *l = self->next; l; l = l->next)
        if (l->type == type && (id == 0 || id == l->id)) return l;
    return NULL;
}

/* ---- layers ---- */

void EAGL_LayerInit(EaglLayer *l, const EaglLayerVtbl *vt, uint32_t type, uint32_t id)
{
    memset(l, 0, sizeof *l);
    l->type = type;
    l->vt = vt;
    l->id = id;
    l->dep_pos = NULL;
    l->dep = NULL;
}

void EAGL_LayerFree(EaglLayer *l)
{
    free(l->dep_pos);
    free(l->dep);
    l->dep_pos = NULL;
    l->dep = NULL;
}

/* Finds each dependency among the layers before this one (newest first) and its position in the
   material (the stamp the kernels compare). */
void EAGL_LayerResolve(EaglLayer *l)
{
    const uint32_t *d = l->vt->deps(l, &l->ndeps);
    if (!d) return;
    l->dep_pos = malloc((l->ndeps ? l->ndeps : 1) * sizeof *l->dep_pos);
    l->dep = malloc((l->ndeps ? l->ndeps : 1) * sizeof *l->dep);
    for (uint32_t i = 0; i < l->ndeps; i++, d += 2) {
        EaglLayer *f = l->prev;
        while (f && !(f->type == d[0] && (d[1] == 0 || d[1] == f->id))) f = f->prev;
        l->dep[i] = f;
        int16_t pos = 0;
        EaglLayer *k = l->material->layers;
        while (k && k != f) {
            k = k->next;
            pos++;
        }
        l->dep_pos[i] = k ? pos : -1;
    }
}

EaglLayer *EAGL_LayerNext(EaglLayer *l, EaglStream *s0, EaglStream *s1) { return l->next; }
void EAGL_LayerSetParam(EaglLayer *l, uint32_t p) { l->param = p; }

void *EAGL_LayerData(const EaglLayer *l)
{
    if (!l) return NULL;
    return l->material->streams[l->slot1].data;
}

static void layer_destroy(EaglLayer *l, int free_it)          /* 0x53fb40 */
{
    EAGL_LayerFree(l);
    if (free_it) free(l);
}
static const uint32_t *no_deps(EaglLayer *l, uint32_t *n) { *n = 0; return NULL; }   /* 0x449ef0 */
static void no_reset(EaglLayer *l) {}                                                /* 0x514260 */
static bool yes(EaglLayer *l) { return true; }                                       /* 0x449f20 */
static bool no(EaglLayer *l) { return false; }                                       /* 0x449f70 */
static uint32_t gives_none(EaglLayer *l) { return 0; }                               /* 0x403e10 */
static uint32_t gives_colour(EaglLayer *l) { return 4; }                             /* 0x44a010 */
static uint32_t gives_uv(EaglLayer *l) { return 8; }                                 /* 0x449fe0 */
static uint32_t gives_coords(EaglLayer *l) { return 0x10; }                          /* 0x449fd0 */
static uint32_t gives_one(EaglLayer *l) { return 1; }                                /* 0x53fb00 */
static uint32_t gives_coords2(EaglLayer *l) { return 0x28; }                         /* 0x53fb30 */

/* The draw layers' dependencies are exe data: {5 colours, 0x10 specular, 2 uv, 3 texture} (0x5bbe38) and
   with two stages {5, 0x10, 2/0xc, 2/0xd, 3/0xe, 3/0xf} (0x5bbe58). */
static uint32_t deps_draw[8], deps_draw2[12];
static bool deps_loaded;
static void load_deps(void)
{
    if (deps_loaded || !exe_loaded()) return;
    for (int i = 0; i < 8; i++) deps_draw[i] = exe_u32(0x5bbe38 + 4 * i);
    for (int i = 0; i < 12; i++) deps_draw2[i] = exe_u32(0x5bbe58 + 4 * i);
    deps_loaded = true;
}
static const uint32_t *draw_deps(EaglLayer *l, uint32_t *n) { load_deps(); *n = 4; return deps_draw; }   /* 0x53fab0 */
/* The indexed uv layer's one dependency: {2 uv, any id} (0x5c08c0). */
static uint32_t deps_indexuv[2];
static const uint32_t *indexuv_deps(EaglLayer *l, uint32_t *n)   /* 0x574900 */
{
    if (exe_loaded()) deps_indexuv[0] = exe_u32(0x5c08c0), deps_indexuv[1] = exe_u32(0x5c08c4);
    *n = 1;
    return deps_indexuv;
}
static const uint32_t *draw2_deps(EaglLayer *l, uint32_t *n) { load_deps(); *n = 6; return deps_draw2; } /* 0x53fb10 */

/* LayerCoords (type 1): the positions, transformed here (0x53f220; stride 0x28 for the two-texture
   vertex format: 0x53f260). A wholly rejected draw stops the chain. */
static EaglLayer *coords_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    g_eagl->coords = s0->data;
    EAGL_ContextTransform(g_eagl, 0x20);
    return (g_eagl->flags & EAGL_CF_REJECT) ? NULL : l->next;
}
static EaglLayer *coords2_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    g_eagl->coords = s0->data;
    EAGL_ContextTransform(g_eagl, 0x28);
    return (g_eagl->flags & EAGL_CF_REJECT) ? NULL : l->next;
}

/* Index coords (type 0xe, factory 0x10): already transformed positions and codes picked by a byte index
   list: {float4 positions, codes, u8 indices[]} (0x53f2a0). */
static EaglLayer *indexcoords_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    const uint8_t *rec = s0->data;
    const float *pos;
    const uint32_t *codes;
    memcpy(&pos, rec, sizeof pos);
    memcpy(&codes, rec + sizeof pos, sizeof codes);
    const uint8_t *idx = rec + 2 * sizeof pos;
    for (int i = 0; i < c->nverts; i++) {
        float *v = (float *)(c->verts + 0x20 * i);
        const float *p = pos + 4 * idx[i];
        v[0] = p[0];
        v[1] = p[1];
        v[2] = p[2];
        v[3] = p[3];
        c->codes[i] = codes[idx[i]];
    }
    c->flags |= EAGL_CF_TRANSFORMED;
    EAGL_ContextViewport(c);
    EAGL_ContextPrimCodes(c);
    EAGL_ContextViewport(c);
    return l->next;
}

/* Sort (type 8, factory 9; 0x810 bytes): this draw's triangles are drawn back to front (0x53f4d0). */
static EaglLayer *sort_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    g_eagl->sort = 1;
    return l->next;
}

/* LayerState (type 7): the render-state table (0x53f4f0). */
static EaglLayer *state_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EAGL_RSApply(s0->data);
    return l->next;
}

/* The draw layer (type 0): stage-0 texture, colours, specular and uv from its dependencies, then the
   kernel of the primitive type (0x53f390). */
static EaglLayer *draw_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    const float *uv = NULL;
    int stamp_uv = 0;
    EaglLayer *tex = l->dep[3];
    if (!tex) {
        c->texture = NULL;
    } else {
        uv = EAGL_LayerData(l->dep[2]);
        stamp_uv = l->dep_pos[2];
        c->texture = EAGL_LayerData(tex);
    }
    const uint32_t *diffuse = EAGL_LayerData(l->dep[0]);
    const uint32_t *specular = EAGL_LayerData(l->dep[1]);
    int sd = l->dep_pos[0], ss = l->dep_pos[1];
    switch (c->prim) {
    case 1: EAGL_DrawStrip(c, diffuse, specular, uv, sd, ss, stamp_uv); break;
    case 2: EAGL_DrawLines(c, diffuse, specular, uv, sd, ss, stamp_uv); break;
    case 3: EAGL_DrawList(c, diffuse, specular, uv, sd, ss, stamp_uv); break;
    case 4: EAGL_DrawQuads(c, diffuse, specular, uv, sd, ss, stamp_uv); break;
    }
    return l->next;
}

/* The two-stage draw layer (type 0, factory 0x12; 0x53f510): the strip, list and quads kernels with the
   second stage (0x542090, 0x544780, 0x546bb0: EAGL_DrawTwoStage). Without a second texture it draws as
   one stage would. */
static EaglLayer *draw2_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    void *t0 = EAGL_LayerData(l->dep[4]), *t1 = EAGL_LayerData(l->dep[5]);
    const int32_t *modes = s0->data;     /* the blend mode of each stage; 2 = stage off */
    int m0 = modes[0], m1 = modes[1];
    if (!t1) m1 = 2;
    if (m0 == 2 && m1 == 2) {
        c->flags = 0;          /* the original clears the whole flag word here (+0x117e4 = 0) */
        return l->next;
    }
    c->stage_tex[2] = t0;                          /* +0x1181c, +0x11820: the textures */
    c->stage_tex[3] = t1;
    c->stage_tex[0] = EAGL_LayerData(l->dep[2]);   /* +0x11814, +0x11818: their uv sets */
    c->stage_tex[1] = EAGL_LayerData(l->dep[3]);
    EAGL_DrawTwoStage(c, EAGL_LayerData(l->dep[0]), EAGL_LayerData(l->dep[1]), m0, m1, l->dep_pos);
    return l->next;
}

/* The vertex list at +0x10c0c of layers 0xc, 0xd and 0xf: the reader starts at the list and takes the
   first entry before it moves, so the first vertex comes twice; a negative entry ends it. Nothing in this
   build fills the list (no data makes these layers); the port stops at the end of its area. */
static int32_t vlist_first(EaglContext *c)
{
    c->vcursor = c->vlist;
    return c->vlist[0];
}

static int32_t vlist_next(EaglContext *c)
{
    if (c->vcursor >= c->vlist + sizeof c->vlist / sizeof *c->vlist) return -1;
    return *c->vcursor++;
}

static void *find_data(EaglLayer *l, uint32_t type, uint32_t id)
{
    return EAGL_LayerData(EAGL_LayerFindPrev(l, type, id));
}

/* fistp: to nearest, the integer indefinite for what doesn't fit */
static int32_t fist(float v)
{
    if (!(v > -2147483648.0f && v < 2147483648.0f)) return INT32_MIN;
    return (int32_t)lrintf(v);
}

static float len3d(const float *v) { return (float)sqrt((double)v[2] * v[2] + (double)v[1] * v[1] + (double)v[0] * v[0]); }   /* 0x573830 (0x532890) */

/* Environment colour (type 5, factory 0xc; 0x5741f0): the listed vertices lit by a light set (layer
   type 10): {count, positions[8] (float4, +4), kinds[8] (+0x84), colours[8] (float4, +0xa8)} with the ids
   of its colour layers at +0x124 (type 9: the diffuse colours, float4 per vertex, rgb in 1..3), +0x128
   (type 6: the shininess per vertex, and type 9: the specular colours, else the diffuse ones) and +0x130
   (type 6: the ambient factor per vertex). Each light adds its colour * the diffuse colour * n.l when
   n.l > 0 (the vertex through the object stack, the light through the camera's, the normal as it is);
   kind 2 also the light's colour * the ambient factor; kind 3 the specular colour * (r.e)^shininess, r
   the light's reflection and e the camera's -z in model space. Out: the colours (type 5) scaled by the
   sums over 255, each at most 1 (no lower bound), as ARGB. */
static EaglLayer *envcolour_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    const uint8_t *set = find_data(l, 10, 0);
    const float *normals = find_data(l, 1, 6), *pos = find_data(l, 1, 7);
    const uint32_t *colours = find_data(l, 5, 0);
    uint32_t id_diffuse, id_spec, id_amb;
    memcpy(&id_diffuse, set + 0x124, 4);
    memcpy(&id_spec, set + 0x128, 4);
    memcpy(&id_amb, set + 0x130, 4);
    const float *diffuse = find_data(l, 9, id_diffuse), *spec = NULL;
    const float *shine = NULL, *amb = NULL;
    EaglLayer *f = EAGL_LayerFindPrev(l, 6, id_spec);
    if (f) {
        shine = EAGL_LayerData(f);
        f = EAGL_LayerFindPrev(l, 9, id_spec);
        spec = f ? EAGL_LayerData(f) : diffuse;
    }
    f = EAGL_LayerFindPrev(l, 6, id_amb);
    if (f) amb = EAGL_LayerData(f);
    uint32_t *out = s1->data;
    EaglView *view = c->view;
    EaglMatrix inv;
    EAGL_MatInvert(EAGL_StackTop(&view->view), &inv);   /* 0x53a1c0 */
    float eye[4], back[4] = {0, 0, -1.0f, 0};
    EAGL_MatTransform4(&inv, 1, back, 16, eye, 16);     /* 0x53a0a0 */
    int32_t count;
    memcpy(&count, set, 4);
    for (int32_t v = vlist_first(c); v >= 0; v = vlist_next(c)) {
        float p[4], acc[3] = {0, 0, 0};
        EAGL_MatTransform4(EAGL_StackTop(&view->model), 1, pos + 4 * v, 16, p, 16);
        const float *n = normals + 4 * v;
        for (int32_t i = 0; i < count; i++) {
            float lp[4], d[3], lc[4];
            int32_t kind;
            EAGL_MatTransform4(EAGL_StackTop(&view->view), 1, (const float *)(set + 4 + 16 * i), 16, lp, 16);
            memcpy(&kind, set + 0x84 + 4 * i, 4);
            memcpy(lc, set + 0xa8 + 16 * i, 16);
            d[0] = lp[0] - p[0], d[1] = lp[1] - p[1], d[2] = lp[2] - p[2];
            double len = len3d(d);
            double ndl = ((double)d[2] * n[2] + (double)d[1] * n[1] + (double)d[0] * n[0]) / len;
            if (ndl > 0.0)   /* (0x5b23f8) */
                for (int k = 0; k < 3; k++) acc[k] = (float)(diffuse[4 * v + 1 + k] * (double)lc[k] * ndl + acc[k]);
            if (kind == 2 && amb)
                for (int k = 0; k < 3; k++) acc[k] = (float)((double)amb[v] * lc[k] + acc[k]);
            if (kind == 3 && shine) {
                float r[3];
                for (int k = 0; k < 3; k++) r[k] = (float)(ndl * n[k] * 2.0 - d[k] / len);
                double el = len3d(eye);
                double s = (-(double)eye[0] * r[0] - (double)eye[1] * r[1] - (double)eye[2] * r[2]) / (float)el;
                double e = pow(s, shine[v]);   /* (0x5a3730) */
                for (int k = 0; k < 3; k++) acc[k] = (float)(e * spec[4 * v + 1 + k] + acc[k]);
            }
        }
        uint32_t col = colours[v];
        float a = (float)(col >> 24) * (1.0f / 255.0f);   /* 0x5b3888 */
        float rgb[3] = {(float)(col >> 16 & 0xff) * acc[0] * (1.0f / 255.0f), (float)(col >> 8 & 0xff) * acc[1] * (1.0f / 255.0f),
                        (float)(col & 0xff) * acc[2] * (1.0f / 255.0f)};
        if (a >= 1.0f) a = 1.0f;   /* (0x5b24a8; NaN kept, as the x87 compare leaves it) */
        for (int k = 0; k < 3; k++)
            if (rgb[k] >= 1.0f) rgb[k] = 1.0f;
        uint32_t ia = (uint32_t)fist(a * 255.0f), ir = (uint32_t)fist(rgb[0] * 255.0f), ig = (uint32_t)fist(rgb[1] * 255.0f),
                 ib = (uint32_t)fist(rgb[2] * 255.0f);   /* 0x5b3710 */
        out[v] = ((ia << 8 | ir) << 8 | ig) << 8 | ib;
    }
    return l->next;
}

/* Normals (type 1, factory 0xd; 0x574770): the listed vertices' normals (type 1, id 6) through the
   object stack's rotation, all four columns, into stream 1. */
static EaglLayer *normals_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    const float *src = find_data(l, 1, 6);
    float *out = s1->data;
    const float *m = EAGL_StackTop(&c->view->model)->m;
    for (int32_t v = vlist_first(c); v >= 0; v = vlist_next(c)) {
        const float *n = src + 4 * v;
        for (int k = 0; k < 4; k++) out[4 * v + k] = (float)((double)n[1] * m[4 + k] + (double)m[k] * n[0] + (double)n[2] * m[8 + k]);
    }
    return l->next;
}

/* Indexed uv (type 2, factory 0xf; 0x574880): each listed vertex's uv pair from the first dependency's
   by its byte in stream 0, into stream 1. */
static EaglLayer *indexuv_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    const uint32_t *src = EAGL_LayerData(l->dep[0]);
    const uint8_t *idx = s0->data;
    uint32_t *out = s1->data;
    for (int32_t v = vlist_first(c); v >= 0; v = vlist_next(c)) {
        out[2 * v] = src[2 * idx[v]];
        out[2 * v + 1] = src[2 * idx[v] + 1];
    }
    return l->next;
}

#define VT(exec, deps, s0, s1, gives) { exec, layer_destroy, EAGL_LayerSetParam, deps, no_reset, s0, s1, gives, gives_none }
static const EaglLayerVtbl vt_draw = VT(draw_execute, draw_deps, no, no, gives_none);              /* 0x5bbc60 */
static const EaglLayerVtbl vt_sort = VT(sort_execute, no_deps, no, no, gives_none);                /* 0x5bbc84 */
static const EaglLayerVtbl vt_draw2 = VT(draw2_execute, draw2_deps, yes, no, gives_none);          /* 0x5bbca8 */
static const EaglLayerVtbl vt_specular = VT(EAGL_LayerNext, no_deps, yes, no, gives_colour);       /* 0x5bbccc */
static const EaglLayerVtbl vt_indexcoords = {indexcoords_execute, layer_destroy, EAGL_LayerSetParam, no_deps, no_reset, yes, no,
                                              gives_one, gives_uv};   /* 0x5bbcf0 (slot 8: 0x449fe0) */
static const EaglLayerVtbl vt_type9 = VT(EAGL_LayerNext, no_deps, yes, no, gives_coords);          /* 0x5bbd14 */
static const EaglLayerVtbl vt_type10 = VT(EAGL_LayerNext, no_deps, no, yes, gives_none);           /* 0x5bbd38 */
static const EaglLayerVtbl vt_coords2 = VT(coords2_execute, no_deps, yes, no, gives_coords2);       /* 0x5bbe10 */
static const EaglLayerVtbl vt_coords = VT(coords_execute, no_deps, yes, no, gives_coords);         /* 0x5b6a34 LayerCoords */
static const EaglLayerVtbl vt_coordsdata = VT(EAGL_LayerNext, no_deps, yes, no, gives_coords);     /* 0x5bbdec */
static const EaglLayerVtbl vt_uv = VT(EAGL_LayerNext, no_deps, yes, no, gives_uv);                 /* 0x5b69e4 LayerUV */
static const EaglLayerVtbl vt_texture = VT(EAGL_LayerNext, no_deps, yes, no, gives_none);          /* 0x5b6a0c LayerTexture */
static const EaglLayerVtbl vt_colours = VT(EAGL_LayerNext, no_deps, yes, no, gives_colour);        /* 0x5b3bf0 LayerColours */
static const EaglLayerVtbl vt_type6 = VT(EAGL_LayerNext, no_deps, yes, no, gives_colour);          /* 0x5bbdc8 */
static const EaglLayerVtbl vt_state = VT(state_execute, no_deps, yes, no, gives_none);             /* 0x5b69bc LayerState */
static const EaglLayerVtbl vt_typec = VT(EAGL_LayerNext, no_deps, yes, no, gives_coords);          /* 0x5bbda4 */
static const EaglLayerVtbl vt_envcolour = VT(envcolour_execute, no_deps, no, yes, gives_colour); /* 0x5bbd5c */
static const EaglLayerVtbl vt_normals = VT(normals_execute, no_deps, no, yes, gives_coords);    /* 0x5bbd80 */
static const EaglLayerVtbl vt_indexuv = VT(indexuv_execute, indexuv_deps, yes, yes, gives_uv);        /* 0x5c0898 */

/* size is the original's (0x30 for the base layer): the port allocates its own layer struct plus what
   the original's object had beyond the base (the sort layer's 0x7e0 bytes). */
static EaglLayer *new_layer(size_t size, const EaglLayerVtbl *vt, uint32_t type, uint32_t id)
{
    EaglLayer *l = calloc(1, sizeof(EaglLayer) + (size - 0x30));
    if (l) EAGL_LayerInit(l, vt, type, id);
    return l;
}

EaglLayer *EAGL_DefaultLayerFactory(uint32_t type, uint32_t id)
{
    switch (type) {
    case 0: return new_layer(0x30, &vt_draw, 0, id);
    case 1: return new_layer(0x30, &vt_coords, 1, id);
    case 2: return new_layer(0x30, &vt_coords2, 1, id);
    case 3: return new_layer(0x30, &vt_uv, 2, id);
    case 4: return new_layer(0x30, &vt_texture, 3, id);
    case 5: return new_layer(0x30, &vt_coordsdata, 1, id);
    case 6: return new_layer(0x30, &vt_colours, 5, id);
    case 7: return new_layer(0x30, &vt_type6, 6, id);
    case 8: return new_layer(0x30, &vt_state, 7, id);
    case 9: return new_layer(0x810, &vt_sort, 8, id);
    case 10: return new_layer(0x30, &vt_type9, 9, id);
    case 0xb: return new_layer(0x30, &vt_type10, 10, id);
    case 0xc: return new_layer(0x30, &vt_envcolour, 5, id);
    case 0xd: return new_layer(0x30, &vt_normals, 1, id);
    case 0xe: return new_layer(0x30, &vt_typec, 0xc, id);
    case 0xf: return new_layer(0x30, &vt_indexuv, 2, id);
    case 0x10: return new_layer(0x30, &vt_indexcoords, 0xe, id);
    case 0x12: return new_layer(0x30, &vt_draw2, 0, id);
    case 0x13: return new_layer(0x30, &vt_specular, 0x10, id);
    default: return NULL;
    }
}

/* The factory chain (0x69ebb0): registered factories first, newest first; EAGL's own last. */
typedef struct FactoryLink { struct FactoryLink *next; EaglLayerFactory f; } FactoryLink;
static FactoryLink *factories;

static void ensure_default(void)
{
    if (factories) return;
    factories = calloc(1, sizeof *factories);
    if (factories) factories->f = EAGL_DefaultLayerFactory;
}

void EAGL_RegisterLayerFactory(EaglLayerFactory f)
{
    FactoryLink *k = calloc(1, sizeof *k);
    if (!k) return;
    k->f = f;
    ensure_default();
    k->next = factories;
    factories = k;
}

EaglLayer *EAGL_CreateLayer(uint32_t type, uint32_t id, uint32_t param)
{
    ensure_default();
    for (FactoryLink *k = factories; k; k = k->next) {
        EaglLayer *l = k->f(type, id);
        if (l) {
            l->vt->setparam(l, param);
            return l;
        }
    }
    return NULL;
}
