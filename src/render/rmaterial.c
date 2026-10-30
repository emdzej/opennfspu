/* Materials and render methods: see rmaterial.h. */
#include "render/rmaterial.h"
#include "exe.h"
#include "real/shape.h"
#include "render/r3dcar.h"
#include "render/rlayers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t g_rmethod_key;          /* 0x6197f8 */
RTexBank *g_rtex_global;         /* 0x6115e8 */
static R3DMaterialSet *sets;     /* 0x619768 */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* ---- method data (CMethodData, 0x44bc40): one block per (layer type, id), shared by all methods ---- */

typedef struct MethodData { struct MethodData *next; uint32_t type, id; void *data; } MethodData;
static MethodData *method_data;

static void *method_data_for(const EaglLayer *l, int size)
{
    for (MethodData *d = method_data; d; d = d->next)
        if (d->type == l->type && d->id == l->id) return d->data;
    MethodData *d = calloc(1, sizeof *d);
    d->type = l->type;
    d->id = l->id;
    d->data = calloc(1, (size_t)size);   /* "Adding method data [%08x]" in the original's debug output */
    d->next = method_data;
    method_data = d;
    return d->data;
}

/* ---- methods (the map of 0x44ba40: (key, mtkey, name) -> method) ---- */

typedef struct MethodNode { struct MethodNode *next; uint32_t key, mtkey; char name[16]; RMethod m; } MethodNode;
static MethodNode *methods;

RMethod *RMethod_Find(const char *name, uint32_t key, uint32_t mtkey)
{
    for (MethodNode *n = methods; n; n = n->next)
        if (n->key == key && n->mtkey == mtkey && !strncmp(n->name, name, 16)) return &n->m;
    return NULL;
}

int RMethod_Slot(const RMethod *m, uint32_t id)
{
    if (id < 0x10000) {
        /* A small id is the position in the sorted table (the tools resolved it); the original reads past
           the table for a bad one. */
        return id < m->nids ? m->ids[id].slot : -1;
    }
    int lo = 0, hi = (int)m->nids;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (m->ids[mid].id < id) lo = mid + 1;
        else hi = mid;
    }
    return lo < (int)m->nids && m->ids[lo].id == id ? m->ids[lo].slot : -1;
}

void RMethod_Assign(RMethod *m, uint32_t id, void *data, uint32_t info)
{
    int s = RMethod_Slot(m, id);
    if (s == -1) return;
    m->streams[s].info = info;
    m->streams[s].data = data;
}

static int cmp_id(const void *a, const void *b)
{
    uint32_t x = ((const struct RMethodId *)a)->id, y = ((const struct RMethodId *)b)->id;
    return x < y ? -1 : x > y;
}

/* 0x44be40: the method's EAGL material from the record. Groups (16 bytes from +0x1c: first entry, count,
   method-key mask, ...) select layer entries (16 bytes: stream id, layer type, layer id, material-key
   mask); then the constant streams (id, offset, info). */
static void method_build(RMethod *m, uint32_t key, uint32_t mtkey)
{
    static bool registered;
    if (!registered) {
        /* The game registers its factory at start-up (0x449a60); the port on first use. */
        registered = true;
        RLayers_Register();
    }
    EaglMaterial *mat = calloc(1, sizeof *mat);
    EAGL_MaterialInit(mat);
    m->material = mat;
    m->mtkey = mtkey;
    const uint8_t *d = m->data;
    uint32_t ngroups = rd32(d + 0x10), nent = rd32(d + 0x14), nconst = rd32(d + 0x18);
    m->nentries = nent;
    const uint8_t *groups = d + 0x1c, *ents = groups + 0x10 * ngroups;
    /* The stream ids, sorted and unique. */
    struct RMethodId *ids = calloc(nent ? nent : 1, sizeof *ids);
    for (uint32_t i = 0; i < nent; i++) ids[i] = (struct RMethodId){rd32(ents + 0x10 * i), -1};
    qsort(ids, nent, sizeof *ids, cmp_id);
    uint32_t nids = 0;
    for (uint32_t i = 0; i < nent; i++)
        if (nids == 0 || ids[nids - 1].id != ids[i].id) ids[nids++] = ids[i];
    m->nids = nids;
    m->ids = ids;
    EaglStream *tmp = calloc(2 * nent + 1, sizeof *tmp);
    uint32_t nstreams = 0;
    const uint8_t *g = groups;
    uint32_t group_end = rd32(g + 4);
    for (uint32_t i = 0; i < nent; i++) {
        if (i == group_end && i) {
            g += 0x10;
            group_end = rd32(g) + rd32(g + 4);
        }
        const uint8_t *e = ents + 0x10 * i;
        uint32_t gmask = rd32(g + 8), emask = rd32(e + 0xc);
        uint32_t flags = (uint32_t)(((gmask >> 2) & 1) | 4) << 3;
        if (!((gmask == 0 || (key & gmask)) && (emask == 0 || (mtkey & emask)))) continue;
        uint32_t id = rd32(e);
        struct RMethodId *slot = NULL;
        for (uint32_t k = 0; k < nids; k++)
            if (ids[k].id >= id) {
                slot = &ids[k];
                break;
            }
        EaglLayer *l = EAGL_CreateLayer(rd32(e + 4), rd32(e + 8), flags);
        if (!l) continue;
        if (l->vt->wants_stream0(l)) {
            tmp[nstreams] = (EaglStream){0, NULL};
            if (slot) slot->slot = (int32_t)nstreams;
            nstreams++;
        }
        if (l->vt->wants_stream1(l)) {
            int size = (int)(l->vt->slot8(l) + l->vt->provides(l) * 0xfc);
            tmp[nstreams] = (EaglStream){(uint32_t)size, size >= 1 ? method_data_for(l, size) : NULL};
            nstreams++;
        }
        EAGL_MaterialAddLayer(mat, l);
    }
    EAGL_MaterialBind(mat);
    m->nstreams = nstreams;
    m->streams = calloc(nstreams ? nstreams : 1, sizeof *m->streams);
    memcpy(m->streams, tmp, nstreams * sizeof *tmp);
    free(tmp);
    const uint8_t *consts = ents + 0x10 * nent, *cdata = consts + 0x10 * nconst;
    for (uint32_t i = 0; i < nconst; i++) {
        const uint8_t *c = consts + 0x10 * i;
        RMethod_Assign(m, rd32(c), (void *)(cdata + rd32(c + 4)), rd32(c + 8));
    }
}

void RMethod_Build(const UData *rec, uint32_t key, uint32_t mtkey)
{
    const char *name = UData_data(rec);
    if (RMethod_Find(name, key, mtkey)) return;
    MethodNode *n = calloc(1, sizeof *n);
    n->key = key;
    n->mtkey = mtkey;
    memcpy(n->name, name, 16);
    UData_init(&n->m.rec, NULL, 0, 0, 0x524d7468, 0xffffffffu);   /* 'RMth' */
    UData_set(&n->m.rec, UData_data(rec), UData_size(rec), rec->count);
    n->m.data = UData_data(&n->m.rec);
    method_build(&n->m, key, mtkey);
    n->next = methods;
    methods = n;
}

/* ---- materials ---- */

void R3DMaterial_SetOverride(R3DMaterial *mat, int32_t *override)
{
    mat->override = override;
    if (override)
        for (uint32_t i = 0; i < mat->method->nids; i++) override[i] = 0;
}

void R3DMaterial_OverrideStream(R3DMaterial *mat, uint32_t id, void *data, uint32_t info)
{
    int s = RMethod_Slot(mat->method, id);
    if (s == -1) return;
    mat->override[s] = -1;
    mat->method->streams[s].info = info;
    mat->method->streams[s].data = data;
}

/* The data of a 'pr' stream: the part's item of a kind (below 0x100) or the object's record of a key. */
static uint8_t *stream_base(R3DPart *part, UData *root, uint16_t kind, uint16_t index)
{
    if (kind < 0x100) {
        /* The part's own deformed / animated vertices stand for its 'vt' item (the original swaps them in
           through the override streams of the part's draw). */
        if (kind == 0 && part->verts) return (uint8_t *)part->verts;
        return R3DPart_Item(part, kind, index == 0xffff ? 0xffffffffu : index);
    }
    uint32_t id = (uint32_t)kind << 16 | (index != 0xffff ? index : 0x2020);
    UData *r = root ? UData_find(root, id) : NULL;
    return r ? UData_data(r) : NULL;
}

int R3DMaterial_Bind(R3DMaterial *mat, uint32_t listinfo, R3DPart *part, UData *root, const uint32_t *pr)
{
    RLayers_SetNoCache(pr[0] >> 16);
    RMethod *m = mat->method;
    const uint8_t *b = (const uint8_t *)pr;
    uint32_t ns = pr[10], nl = pr[11];
    const uint8_t *streams = b + 0x30, *lists = streams + 0x10 * ns, *area = lists + 8 * nl;
    for (uint32_t i = 0; i < mat->nrefs; i++) {
        const R3DMaterialRef *r = &mat->refs[i];
        if (mat->override && mat->override[r->slot] == -1) continue;
        m->streams[r->slot].info = r->info;
        m->streams[r->slot].data = r->data;
    }
    for (uint32_t i = 0; i < ns; i++) {
        const uint8_t *s = streams + 0x10 * i;
        uint32_t id = rd32(s);
        uint16_t kind = rd16(s + 10), index = rd16(s + 12);
        if (!mat->override) {
            uint8_t *base = stream_base(part, root, kind, index);
            RMethod_Assign(m, id, base ? base + rd32(s + 4) : NULL, rd16(s + 8));
            continue;
        }
        int slot = RMethod_Slot(m, id);
        if (slot == -1) continue;
        uint8_t *data;
        if ((uint32_t)mat->override[slot] == 0xffffffffu) {
            if (!m->streams[slot].data) continue;
            data = (uint8_t *)m->streams[slot].data + rd32(s + 4);
        } else {
            uint8_t *base = stream_base(part, root, kind, (uint16_t)(index | mat->override[slot]));
            data = base ? base + rd32(s + 4) : NULL;
        }
        m->streams[slot].info = rd16(s + 8);
        m->streams[slot].data = data;
    }
    for (uint32_t i = 0; i < nl; i++) {
        const uint8_t *l = lists + 8 * i;
        uint32_t id = rd32(l);
        int slot = RMethod_Slot(m, id);
        if (slot == -1) continue;
        void *data = (void *)(area + rd32(l + 4));
        if (mat->override && (uint32_t)mat->override[slot] == 0xffffffffu) {
            if (!m->streams[slot].data) continue;
            data = (uint8_t *)m->streams[slot].data + rd32(l + 4);
        }
        m->streams[slot].info = listinfo;
        m->streams[slot].data = data;
    }
    return (int16_t)pr[0];
}

void R3DMaterial_DrawList(R3DMaterial *mat, R3DPart *part, UData *root, const uint32_t *pr, uint32_t n)
{
    if (!mat || !mat->method) return;
    int prim = R3DMaterial_Bind(mat, n, part, root, pr);
    mat->method->material->vt->draw(mat->method->material, mat->method->streams, prim, (int)n);
}

/* ---- material sets ---- */

R3DMaterialSet *R3DMaterialSet_ctor(R3DMaterialSet *ms, TexManager *m, int texclass)
{
    memset(ms, 0, sizeof *ms);
    RTexBank_ctor(&ms->bank, m, texclass);
    ms->current = -1;
    ms->next = sets;
    sets = ms;
    return ms;
}

void R3DMaterialSet_Clear(R3DMaterialSet *ms)
{
    for (uint32_t i = 0; i < ms->count; i++)
        if (ms->mat[i]) {
            free(ms->mat[i]->refs);
            free(ms->mat[i]);
        }
    free(ms->mat);
    ms->mat = NULL;
    ms->count = 0;
    RTexBank_clear(&ms->bank);
}

void R3DMaterialSet_dtor(R3DMaterialSet *ms)
{
    for (R3DMaterialSet **p = &sets; *p; p = &(*p)->next)
        if (*p == ms) {
            *p = ms->next;
            break;
        }
    R3DMaterialSet_Clear(ms);
    RTexBank_dtor(&ms->bank);
}

/* The object's shapes by (record id, entry name) (a map local to the build in the original). */
typedef struct { uint32_t id, name; const uint8_t *shape; } ShapeKey;

static const uint8_t *shape_find(const ShapeKey *k, int n, uint32_t id, uint32_t name)
{
    for (int i = 0; i < n; i++)
        if (k[i].id == id && k[i].name == name) return k[i].shape;
    return NULL;
}

static UData *items_of(UData *root, uint32_t key, UData **first)
{
    UData *end = UData_itemsend(root);
    *first = UData_finditem(root, key, key == 0x524d7468 ? 0xffffffffu : 0, 0xffffffffu);
    return end;
}

void R3DMaterialSet_Build(R3DMaterialSet *ms, UData *root, const char *dir)
{
    /* The method templates: the root's 'RMth' records by name (first one wins). */
    UData *r, *end = items_of(root, 0x524d7468, &r);
    const UData *templates[64];
    int ntemplates = 0;
    for (UData *t = r; t != end && UData_key(t) == UData_key(r); t++)
        if (ntemplates < 64) templates[ntemplates++] = t;
    /* The shapes: inline 'sf' files, else 'sn' names of files under dir. */
    UData *s;
    end = items_of(root, 0x73662020, &s);   /* 'sf  ' */
    if (s == end) end = items_of(root, 0x736e2020, &s);   /* 'sn  ' */
    ShapeKey *keys = NULL;
    int nkeys = 0, capkeys = 0;
    UDataFile files[16];
    int nfiles = 0;
    for (UData *t = s; t != end && UData_key(t) == UData_key(s); t++) {
        const uint8_t *shp;
        if (UData_key(t) == 0x73662020) shp = UData_data(t);
        else {
            const char *name = UData_data(t);
            if (*name == '\\') name++;
            char path[300];
            snprintf(path, sizeof path, "%s%s", dir ? dir : "", name);   /* 0x5cc588 "%s%s" */
            if (nfiles >= 16) continue;
            UDataFile_ctor(&files[nfiles]);
            UDataFile_load(&files[nfiles], path, false);
            shp = (const uint8_t *)UDataFile_root(&files[nfiles]);
            nfiles++;
            if (!shp) continue;
        }
        uint32_t n = SHAPE_count(shp);
        for (uint32_t i = 0; i < n; i++) {
            if (nkeys == capkeys) {
                capkeys = capkeys ? capkeys * 2 : 64;
                keys = realloc(keys, (size_t)capkeys * sizeof *keys);
            }
            keys[nkeys++] = (ShapeKey){t->id, SHAPE_entryname(shp, (int)i), SHAPE_entry(shp, (int)i)};
        }
    }
    /* The materials. */
    UData *mt;
    end = items_of(root, 0x6d742020, &mt);   /* 'mt  ' */
    uint32_t maxi = 0;
    for (UData *t = mt; t != end && UData_key(t) == 0x6d742020; t++) {
        uint32_t i = t->info & UDATA_INDEXED ? t->id & 0xffff : 0xffffffffu;
        if (maxi < i) maxi = i;
    }
    ms->count = maxi + 1;
    ms->mat = calloc(ms->count, sizeof *ms->mat);
    for (UData *t = mt; t != end && UData_key(t) == 0x6d742020; t++) {
        const uint8_t *d = UData_data(t);
        uint32_t idx = t->info & UDATA_INDEXED ? t->id & 0xffff : 0xffffffffu;
        uint32_t nrefs = rd32(d + 8) & 0xffff, nconst = rd32(d + 8) >> 16, mtkey = rd32(d + 0xc);
        const char *mname = (const char *)(d + 0x10);
        RMethod *m = RMethod_Find(mname, g_rmethod_key, mtkey);
        if (!m)
            for (int k = 0; k < ntemplates; k++)
                if (!strncmp(UData_data(templates[k]), mname, 16)) {
                    RMethod_Build(templates[k], g_rmethod_key, mtkey);
                    m = RMethod_Find(mname, g_rmethod_key, mtkey);
                    break;
                }
        if (idx >= ms->count) continue;
        R3DMaterial *mat = calloc(1, sizeof *mat);
        ms->mat[idx] = mat;
        mat->index = idx;
        mat->texture = nrefs == 0 ? 0x4e544558u : rd32(d + 0x28);   /* 'XETN' */
        mat->mt = d;
        mat->method = m;
        if (!m) continue;
        mat->refs = calloc(m->nstreams ? m->nstreams : 1, sizeof *mat->refs);
        uint32_t cap = m->nstreams;
        for (uint32_t i = 0; i < nrefs; i++) {
            const uint8_t *ref = d + 0x20 + 0x2c * i;
            uint32_t sid = rd32(ref), id = rd32(ref + 4), name = rd32(ref + 8);
            const TextureStates *st = (const TextureStates *)(const void *)(ref + 0x10);
            Texture *tex = g_rtex_global ? RTexBank_find(g_rtex_global, id, name) : NULL;
            if (!tex || memcmp(&tex->states, st, sizeof *st)) {
                const uint8_t *shape = shape_find(keys, nkeys, id, name);
                if (shape) tex = RTexBank_get(&ms->bank, shape, id, name, st, 1, true, 0);
            }
            int slot = RMethod_Slot(m, sid);
            if (slot != -1 && mat->nrefs < cap) mat->refs[mat->nrefs++] = (R3DMaterialRef){tex, 0x54, slot, sid};
        }
        const uint8_t *consts = d + 0x20 + 0x2c * nrefs;
        const uint8_t *cdata = consts + 0x10 * nconst;
        for (uint32_t i = 0; i < nconst; i++) {
            const uint8_t *c = consts + 0x10 * i;
            int slot = RMethod_Slot(m, rd32(c));
            if (slot != -1 && mat->nrefs < cap)
                mat->refs[mat->nrefs++] = (R3DMaterialRef){(void *)(cdata + rd32(c + 4)), rd32(c + 8), slot, rd32(c)};
        }
    }
    free(keys);
    /* The shape files stay loaded with the original's local map until here: the textures copied them. */
    for (int i = 0; i < nfiles; i++) UDataFile_dtor(&files[i]);
}
