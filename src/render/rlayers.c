/* The game's EAGL layers: see rlayers.h. */
#include "render/rlayers.h"
#include "exe.h"
#include "pending.h"
#include "platform.h"
#include "real/real.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The source file and messages of the asserts and aborts (exe strings). */
#define RCUSTOMLAYERS exe_str(0x5cdf04)
static void dep_assert(void) { REAL_abort(exe_str(0x5cded8), 0x8f, "%s", exe_str(0x5cde54)); }

float g_rlayer_eye[3];
float *g_rlayer_lights;
struct Texture **g_rlayer_envtex;
bool g_rlayer_fullambient_fill;

static float exef(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* ---- the class table ---- */

typedef EaglLayer *(*ExecFn)(EaglLayer *, EaglStream *, EaglStream *);

typedef struct {
    uint32_t factory;       /* the layer type the method asks for */
    int32_t basetype;       /* the layer's own type (EAGL_LayerInit's) */
    uint32_t deps;          /* address of its (type, id) table, 0 = none */
    uint32_t ndeps;
    bool stream0, stream1;  /* slots 5 and 6 */
    uint32_t provides, datasize;  /* slots 7 and 8 */
    uint32_t param;         /* +0x14 the constructor sets (0 = leave) */
    ExecFn exec;
    uint32_t exec_addr;
    const char *name;
} RLayerClass;

static EaglLayer *exec_untransformed(EaglLayer *, EaglStream *, EaglStream *);
static EaglLayer *exec_transform(EaglLayer *, EaglStream *, EaglStream *);
static EaglLayer *exec_transformindex(EaglLayer *, EaglStream *, EaglStream *);
static EaglLayer *exec_indexeduv(EaglLayer *, EaglStream *, EaglStream *);
static EaglLayer *exec_indexedcolour(EaglLayer *, EaglStream *, EaglStream *);
static EaglLayer *exec_ambient(EaglLayer *, EaglStream *, EaglStream *);
static EaglLayer *exec_fullambient(EaglLayer *, EaglStream *, EaglStream *);
static EaglLayer *exec_light(EaglLayer *, EaglStream *, EaglStream *);

static const RLayerClass classes[] = {
    {0x8001, -1, 0x5b3c18, 1, true, false, 0, 0, 0x20, NULL, 0x448bf0, "RCustomLayers::LayerUVAnimate"},
    {0x8002, -1, 0, 0, false, false, 4, 0, 0, NULL, 0x452fe0, "Headlights::LayerVertexLight"},
    {0x8003, -1, 0, 0, false, false, 4, 0, 0, NULL, 0x452fe0, "Headlights::LayerVertexLight_Opaque"},
    {0x8004, -1, 0, 0, false, true, 0, 4, 0, NULL, 0x453b70, "Headlights::LayerUVLight"},
    {0x8005, 5, 0, 0, true, true, 4, 0, 0x20, exec_fullambient, 0x448b60, "RCustomLayers::LayerFullAmbient"},
    {0x8006, 5, 0x5b3c20, 1, false, true, 4, 0, 0x20, NULL, 0x448e30, "RCustomLayers::LayerSetAlpha"},
    {0x8007, 5, 0, 0, true, false, 4, 0, 0x20, exec_ambient, 0x448ba0, "RCustomLayers::LayerAmbient"},
    {0x8008, 1, 0, 0, true, false, 0x10, 0, 0x20, exec_untransformed, 0x449fc0, "RCustomLayers::LayerUntransformedCoords"},
    {0x8009, 0x8009, 0x5b3928, 2, true, false, 0x10, 0, 0x20, exec_transformindex, 0x4492b0, "RCustomLayers::LayerTransformIndexCoords"},
    {0x800c, -1, 0x5b3c38, 1, true, false, 4, 0, 0x20, exec_indexedcolour, 0x449900, "RCustomLayers::LayerIndexedColour"},
    {0x800d, -1, 0x5b3c30, 1, true, false, 8, 0, 0x20, exec_indexeduv, 0x449830, "RCustomLayers::LayerIndexedUV"},
    {0x800e, 1, 0x5b3c28, 1, true, false, 0x10, 0, 0x20, exec_transform, 0x448eb0, "RCustomLayers::LayerTransformCoords"},
    {0x800f, 0x8000, 0, 0, false, false, 0, 0, 0x20, NULL, 0x4499c0, "RCustomLayers::LayerFalseSort1"},
    {0x8010, 0x8000, 0, 0, false, false, 0, 0, 0x20, NULL, 0x449a10, "RCustomLayers::LayerFalseSort2"},
    {0x8011, -1, 0, 0, true, false, 4, 0, 0, NULL, 0x453490, "Headlights::LayerVertexLight_Ambient"},
    {0x8100, 3, 0x5b38c0, 3, true, true, 0, 0, 0, NULL, 0x445450, "R3DSpecularTextureLayer"},
    {0x8101, 3, 0x5b38c0, 3, true, true, 0, 0, 0, NULL, 0x445a40, "R3DSkyMapTextureLayer"},
    {0x8102, 3, 0x5b38c0, 3, false, true, 0, 0, 0, NULL, 0x446060, "R3DShadowMapTextureLayer"},
    {0x8104, -1, 0x5b38c0, 3, true, false, 0, 0xb8, 0, exec_light, 0x446460, "R3DLightLayer"},
    {0x8105, -1, 0x5b38c0, 3, true, false, 0, 4, 0, NULL, 0x446db0, "R3DSpecularLayer"},
};
enum { NCLASSES = sizeof classes / sizeof *classes };

/* A game layer: the EAGL layer and its class. */
typedef struct {
    EaglLayer base;
    const RLayerClass *cls;
    uint32_t deptable[6];   /* the class's (type, id) pairs, copied from the exe */
} RLayer;

static const RLayerClass *cls_of(const EaglLayer *l) { return ((const RLayer *)l)->cls; }

static EaglLayer *v_execute(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    const RLayerClass *c = cls_of(l);
    if (c->exec) return c->exec(l, s0, s1);
    /* Not ported yet: the layer passes, as one that changes nothing. */
    static bool noted[NCLASSES];
    int i = (int)(c - classes);
    if (!noted[i]) {
        noted[i] = true;
        PENDING_NOTE(0x449a90, "RLayers (a game layer class; see the next line)");
        plat_log(c->name);
    }
    return l->next;
}

static void v_destroy(EaglLayer *l, int free_it)
{
    EAGL_LayerFree(l);
    if (free_it) free(l);
}

static const uint32_t *v_deps(EaglLayer *l, uint32_t *count)
{
    RLayer *r = (RLayer *)l;
    *count = r->cls->ndeps;
    return r->cls->ndeps ? r->deptable : NULL;
}

static void v_reset(EaglLayer *l) {}   /* 0x514260 */
static bool v_stream0(EaglLayer *l) { return cls_of(l)->stream0; }
static bool v_stream1(EaglLayer *l) { return cls_of(l)->stream1; }
static uint32_t v_provides(EaglLayer *l) { return cls_of(l)->provides; }
static uint32_t v_datasize(EaglLayer *l) { return cls_of(l)->datasize; }

static const EaglLayerVtbl rlayer_vtbl = {v_execute, v_destroy, EAGL_LayerSetParam, v_deps, v_reset,
                                          v_stream0, v_stream1, v_provides, v_datasize};

EaglLayer *RLayers_Create(uint32_t type, uint32_t id)
{
    for (int i = 0; i < NCLASSES; i++) {
        const RLayerClass *c = &classes[i];
        if (c->factory != type) continue;
        RLayer *r = calloc(1, sizeof *r);
        if (!r) return NULL;
        EAGL_LayerInit(&r->base, &rlayer_vtbl, (uint32_t)c->basetype, id);
        r->cls = c;
        for (uint32_t k = 0; k < 2 * c->ndeps && k < 6; k++) r->deptable[k] = exe_u32(c->deps + 4 * k);
        if (c->param) r->base.param = c->param;
        return &r->base;
    }
    return NULL;
}

void RLayers_Register(void)
{
    EAGL_RegisterLayerFactory(RLayers_Create);
}

/* ---- shared pieces ---- */

/* The data of dependency i (its material's stream at its slot 1), and that stream's size word. */
static const void *dep_data(const EaglLayer *l, uint32_t i)
{
    if (!l->dep || l->ndeps <= i) dep_assert();
    const EaglLayer *d = l->dep[i];
    return d ? EAGL_LayerData(d) : NULL;
}

static uint32_t dep_info(const EaglLayer *l, uint32_t i)
{
    const EaglLayer *d = l->dep[i];
    return d->material->streams[d->slot1].info;
}

static bool dep_present(const EaglLayer *l, uint32_t i) { return l->dep && l->ndeps > i && l->dep[i]; }

/* The model matrix: the top of the current view's object stack. */
static const float *model_matrix(void) { return EAGL_StackTop(&g_eagl->view->model)->m; }

/* RenderControl::BackFaceCullPrimitive: per primitive plane (x, y, z, d) of planes, primitives whose
   plane the eye is behind get the "culled" code. The eye goes to model space (only the translation when the
   model matrix has unit scale on the diagonal). Lists of triangles and quads only. */
static void backface_cull(EaglContext *c, const float *planes)
{
    const float *m = model_matrix();
    const float *eye = g_rlayer_eye;
    float lx, ly, lz;
    if (m[0] == 1.0f && m[5] == 1.0f && m[10] == 1.0f) {
        lx = eye[0] - m[12];
        ly = eye[1] - m[13];
        lz = eye[2] - m[14];
    } else {
        float dx = eye[0] - m[12], dy = eye[1] - m[13], dz = eye[2] - m[14];
        lx = dz * m[2] + dx * m[0] + dy * m[1];
        ly = dz * m[6] + dx * m[4] + dy * m[5];
        lz = dz * m[10] + dx * m[8] + dy * m[9];
    }
    int per;
    switch (c->prim) {
    case 1: case 2: return;
    case 3: per = 3; break;
    case 4: per = 4; break;
    default:
        REAL_abort(RCUSTOMLAYERS, 0x183, "%s", exe_str(0x5cdf38));
        return;
    }
    bool culled = false;
    uint32_t (*pc)[2] = c->primcodes;
    for (int left = c->nverts; left > 0; left -= per, planes += 4, pc++) {
        float s = planes[0] * lx + planes[1] * ly + planes[2] * lz - planes[3];
        if (signbit(s)) {
            (*pc)[0] = 0xffffffffu;
            culled = true;
        }
    }
    if (culled) c->flags &= ~(uint32_t)EAGL_CF_INSIDE;
}

/* ---- the layers ---- */

/* 0x449fc0: the coordinates as given (a source for the others). */
static EaglLayer *exec_untransformed(EaglLayer *l, EaglStream *s0, EaglStream *s1) { return l->next; }

/* 0x448eb0: positions transformed into the work vertices, then back faces culled with the planes of the
   '$n' layer (dependency 0). */
static EaglLayer *exec_transform(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    EAGL_TransformToContext(c, c->nverts, s0->data, c->verts, c->codes, (int)l->param);
    if (c->flags & EAGL_CF_REJECT) return NULL;
    c->flags |= EAGL_CF_TRANSFORMED;
    const float *planes = dep_data(l, 0);
    if (!(c->flags & EAGL_CF_INSIDE) || l->dep[0]) {
        EAGL_ContextViewport(c);
        EAGL_ContextPrimCodes(c);
        if (l->dep[0] && planes) backface_cull(c, planes);
    }
    return l->next;
}

/* The vertex cache (0x613e10). */
typedef struct {
    uint32_t stamp, count;
    const void *src;
    float (*verts)[4];       /* 0x1000 bytes (0x613e70 + 0x1000 i) */
    uint32_t *codes;         /* 0x400 bytes (0x617e70 + 0x400 i) */
    uint32_t flags;
} VCache;
static VCache vcache[4];
static float vcache_verts[4][256][4];
static uint32_t vcache_codes[4][256];
static uint32_t vcache_stamp;      /* 0x619764 */
static bool vcache_off;            /* 0x619761 */

void RLayers_ResetCache(void)
{
    vcache_stamp = 0;
    for (int i = 0; i < 4; i++)
        vcache[i] = (VCache){0, 0, NULL, vcache_verts[i], vcache_codes[i], 0};
}

void RLayers_SetNoCache(uint32_t prflags) { vcache_off = (prflags >> 9) & 1; }

/* 0x4492b0: the whole source array (dependency 0's data, its size word / 16 vertices) transformed once
   (through the cache), then gathered into the work vertices by the byte index list of stream 0; back
   faces culled with dependency 1's planes. */
static EaglLayer *exec_transformindex(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    const float *src = dep_data(l, 0);
    uint32_t count = dep_info(l, 0) >> 4;
    static float own_verts[256][4];
    static uint32_t own_codes[256];
    const float (*tv)[4];
    const uint32_t *tc;
    if (!vcache_off) {
        VCache *hit = NULL, *target = &vcache[0];
        for (int i = 0; i < 4; i++) {
            VCache *v = &vcache[i];
            if (v->src == src && v->count == count) {
                hit = v;
                break;
            }
            if (!v->src) {
                target = v;
                break;
            }
            if (v->stamp < target->stamp) target = v;
        }
        if (!hit) {
            target->src = src;
            target->count = count;
            EAGL_TransformToContext(c, (int)count, src, target->verts, target->codes, 0x10);
            target->flags = c->flags | EAGL_CF_TRANSFORMED;
            hit = target;
        }
        tv = (const float (*)[4])hit->verts;
        tc = hit->codes;
        hit->stamp = vcache_stamp++;
        c->flags = hit->flags;
    } else {
        EAGL_TransformToContext(c, (int)count, src, own_verts, own_codes, 0x10);
        c->flags |= EAGL_CF_TRANSFORMED;
        tv = (const float (*)[4])own_verts;
        tc = own_codes;
    }
    if (c->flags & EAGL_CF_REJECT) return NULL;
    const uint8_t *idx = s0->data;
    uint8_t *dst = c->verts;
    for (int k = 0; k < c->nverts; k++, dst += l->param) {
        c->codes[k] = tc[idx[k]];
        memcpy(dst, tv[idx[k]], 16);
    }
    bool planes_dep = dep_present(l, 1);
    if ((c->flags & EAGL_CF_INSIDE) && !planes_dep) return l->next;
    EAGL_ContextViewport(c);
    EAGL_ContextPrimCodes(c);
    const float *planes = planes_dep ? dep_data(l, 1) : NULL;
    if (planes) backface_cull(c, planes);
    return l->next;
}

/* 0x449830: texture coordinates gathered by the byte index list of stream 0 from the uv layer's data. */
static EaglLayer *exec_indexeduv(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    if (!l->dep || !l->ndeps) dep_assert();
    if (!l->dep[0]) REAL_abort(RCUSTOMLAYERS, 0x32a, "%s", exe_str(0x5cdfd0));
    const float (*uv)[2] = (const float (*)[2])EAGL_LayerData(l->dep[0]);
    const uint8_t *idx = s0->data;
    uint8_t *dst = c->verts + 0x18;
    for (int k = 0; k < c->nverts; k++, dst += l->param) memcpy(dst, uv[idx[k]], 8);
    c->flags |= EAGL_CF_UV;
    return l->next;
}

/* 0x449900: diffuse colours gathered by the byte index list of stream 0 from the colour layer's data. */
static EaglLayer *exec_indexedcolour(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    if (!l->dep || !l->ndeps) dep_assert();
    if (!l->dep[0]) REAL_abort(RCUSTOMLAYERS, 0x344, "%s", exe_str(0x5ce008));
    const uint32_t *col = EAGL_LayerData(l->dep[0]);
    const uint8_t *idx = s0->data;
    uint8_t *dst = c->verts + 0x10;
    for (int k = 0; k < c->nverts; k++, dst += l->param) memcpy(dst, &col[idx[k]], 4);
    c->flags |= EAGL_CF_DIFFUSE;
    return l->next;
}

/* 0x448ba0: one colour (stream 0's first word) for every vertex. */
static EaglLayer *exec_ambient(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    uint32_t colour;
    memcpy(&colour, s0->data, 4);
    uint8_t *dst = c->verts + 0x10;
    for (int k = 0; k < c->nverts; k++, dst += l->param) memcpy(dst, &colour, 4);
    c->flags |= EAGL_CF_DIFFUSE;
    return l->next;
}

/* 0x448b60: once (0x619760), the method's data (0xfc words) filled with white. */
static EaglLayer *exec_fullambient(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    if (g_rlayer_fullambient_fill && s1 && s1->data) {
        g_rlayer_fullambient_fill = false;
        memset(s1->data, 0xff, 0xfc * 4);
    }
    return l->next;
}

/* A colour channel as the original makes it: x + 1.5 * 2^23 (the float's low mantissa bits are then the
   rounded integer), masked to 22 bits and clamped at 255 (negative values come out as 255). */
static uint32_t channel(float x, float magic)
{
    float t = x + magic;
    uint32_t u;
    memcpy(&u, &t, 4);
    u &= 0x3fffff;
    return u > 0xff ? 0xff : u;
}

/* 0x446460: vertex lighting. Stream 0 holds (ambient scale, diffuse scale, light index); the light comes
   from the environment (g_rlayer_lights). Each vertex's normal (dependency 0, 16 bytes each) against the
   light direction in model space gives ambient + diffuse * (n . l), or the ambient alone facing away;
   through the indexed coordinates (dependency 2) each source vertex is lit once per draw. */
static EaglLayer *exec_light(EaglLayer *l, EaglStream *s0, EaglStream *s1)
{
    EaglContext *c = g_eagl;
    if (c->flags & EAGL_CF_REJECT) return l->next;
    const float *normals = dep_data(l, 0);
    (void)dep_data(l, 1);
    const float *p = s0->data;
    const float *env = g_rlayer_lights;
    static float defenv[64];
    if (!env) env = defenv;
    int li = (int)p[2];
    const float *amb = (const float *)((const uint8_t *)env + 0x18 + 16 * li);
    float scale = p[0] * exef(0x5b3710);
    float a = amb[0] * scale, r = amb[1] * scale, g = amb[2] * scale, b = amb[3] * scale;
    float m98 = exef(0x5cde98), m94 = exef(0x5cde94);
    uint32_t ambient = channel(a, m98) << 24 | channel(r, m98) << 16 | channel(g, m98) << 8 | channel(b, m98);
    int per = c->prim;
    int stride = (int)l->param;
    if (!l->dep || l->ndeps < 3) dep_assert();
    const EaglLayer *indexed = l->dep[2];
    uint8_t *dst = c->verts + 0x10;
    if (!(exef(0x5b23f8) < p[1])) {
        for (int k = 0; k < c->nverts; k++, dst += stride) memcpy(dst, &ambient, 4);
        c->flags |= EAGL_CF_DIFFUSE;
        return l->next;
    }
    /* The light direction in model space (the model matrix's rows), scaled (0x5b38e8). */
    const float *m = model_matrix();
    const float *dir = (const float *)((const uint8_t *)env + 0x48);
    const float *mul = (const float *)((const uint8_t *)env + 0x90);
    float ds = exef(0x5b38e8);
    float lx = (dir[0] * m[0] + m[1] * dir[1] + m[2] * dir[2]) * ds;
    float ly = (m[4] * dir[0] + m[5] * dir[1] + m[6] * dir[2]) * ds;
    float lz = (m[8] * dir[0] + m[9] * dir[1] + m[10] * dir[2]) * ds;
    const float *dif = (const float *)((const uint8_t *)env + 0x18 + 16 * (li + 4));
    float dr = dif[1] * mul[0] * p[1], dg = dif[2] * mul[1] * p[1], db = dif[3] * mul[2] * p[1];
#define LIT(nv) ({ \
        float d_ = lx * (nv)[0] + ly * (nv)[1] + lz * (nv)[2]; \
        signbit(d_) ? ambient : (0xff000000u | channel(dr * d_ + r, m94) << 16 | channel(dg * d_ + g, m94) << 8 | \
                                 channel(db * d_ + b, m94)); })
    static uint32_t lit[256];
    static uint32_t litstamp[256], stamp;      /* 0x611618 (stamp, colour) pairs, 0x613de8 */
    if (!(c->flags & EAGL_CF_INSIDE) && per != 1) {
        /* Primitives culled or clipped: only those drawn are lit. */
        uint32_t (*pc)[2] = c->primcodes;
        if (!indexed) {
            const float *n = normals;
            for (int k = 0; k < c->nverts; pc++) {
                if ((*pc)[0] == 0)
                    for (int v = 0; v < per; v++, k++, n += 4, dst += stride) {
                        uint32_t col = LIT(n);
                        memcpy(dst, &col, 4);
                    }
                else {
                    k += per;
                    n += 4 * per;
                    dst += stride * per;
                }
            }
        } else {
            const uint8_t *idx = EAGL_LayerData(indexed);
            stamp++;
            for (int k = 0; k < c->nverts; pc++) {
                if ((*pc)[0] == 0)
                    for (int v = 0; v < per; v++, k++, dst += stride) {
                        uint8_t i = idx[k];
                        if (litstamp[i] != stamp) {
                            litstamp[i] = stamp;
                            lit[i] = LIT(normals + 4 * i);
                        }
                        memcpy(dst, &lit[i], 4);
                    }
                else {
                    k += per;
                    dst += stride * per;
                }
            }
        }
    } else if (!indexed) {
        const float *n = normals;
        for (int k = 0; k < c->nverts; k++, n += 4, dst += stride) {
            uint32_t col = LIT(n);
            memcpy(dst, &col, 4);
        }
    } else {
        uint32_t nsrc = dep_info(l, 1) >> 4;   /* the coordinates' count */
        for (uint32_t i = 0; i < nsrc && i < 256; i++) lit[i] = LIT(normals + 4 * i);
        const uint8_t *idx = EAGL_LayerData(indexed);
        for (int k = 0; k < c->nverts; k++, dst += stride) memcpy(dst, &lit[idx[k]], 4);
    }
#undef LIT
    c->flags |= EAGL_CF_DIFFUSE;
    return l->next;
}
