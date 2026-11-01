/* The 3D car: loading (R3DCar_LoadCrp 0x441940, R3DCar_LoadTpg 0x437780, R3DCar_LoadStyles 0x437a30),
   levels of detail (R3DCarLOD 0x43ad30..0x43e9c0) and the draw (0x4433c0, 0x444d30, 0x43c760). See
   r3dcar.h and docs/re/cars.md. Names the game looks up in its files (section and key names, file name
   formats) and its tables are read from the exe by address. */
#include "render/r3dcar.h"
#include "game/camera.h"
#include "render/rlayers.h"
#include "exe.h"
#include "app.h"
#include "real/mem.h"
#include "real/set3d.h"
#include "real/shape.h"
#include "render/car_pending.h"
#include "pending.h"
#include "render/carhalo.h"
#include "render/carpages.h"
#include "render/render.h"
#include "support/ufile.h"
#include "render/thrash.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_r3d_frontend;
int g_r3d_mirror;
int g_r3d_quality;

/* The item kinds' ids (0x5e5084: 'vt', 'nm', 'uv', 'df', 'Base', 'pr', 'ef'), read from the exe. */
static uint32_t kind_id(int kind) { return exe_u32(0x5e5084 + 4u * (unsigned)kind); }
/* An indexed id of a kind: the kind's two characters with the index in the low half. */
static uint32_t kind_index(int kind, uint32_t index)
{
    uint32_t id = kind_id(kind);
    return index == 0xffffffffu ? id : (id & 0xffff0000u) | index;
}

static void *item_data(const UData *r) { return UData_data(r); }

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- the part's item cache ---- */

/* 0x43c610: an empty cache entry. */
static void cache_init(R3DItemCache *c)
{
    c->rec = NULL;
    c->id = 0;
}

/* The data of the article's item of a kind and index (-1 = the kind's plain id), through the cache;
   NULL if there is none. 0x43c700 */
static void *part_item(R3DPart *p, int kind, uint32_t index)
{
    uint32_t id = kind_index(kind, index);
    if (p->cache[kind].id != id) {
        p->cache[kind].rec = UData_find(p->article, id);
        p->cache[kind].id = id;
    }
    return p->cache[kind].rec == p->end ? NULL : item_data(p->cache[kind].rec);
}

void *R3DPart_Item(R3DPart *p, int kind, uint32_t index) { return part_item(p, kind, index); }

/* The element count of that item (index -1: the cached one's). 0x43c690 */
static uint32_t part_count(R3DPart *p, int kind, uint32_t index)
{
    if (index == 0xffffffffu) return p->cache[kind].rec->count;
    uint32_t id = (kind_id(kind) & 0xffff0000u) | index;
    if (p->cache[kind].id != id) {
        p->cache[kind].rec = UData_find(p->article, id);
        p->cache[kind].id = id;
    }
    return p->cache[kind].rec != p->end ? p->cache[kind].rec->count : 0;
}

/* A one-entry cache of an exact id (0x43c620; 0x43c650 with the index merged in): the record. */
static UData *cache_find(R3DItemCache *c, UData *article, uint32_t id)
{
    if (c->id != id) {
        c->rec = UData_find(article, id);
        c->id = id;
    }
    return c->rec;
}

/* ---- construction ---- */

void R3DCar_Init(R3DCar *car, R3DCarState *state)
{
    memset(car, 0, sizeof *car);
    car->state = state;
    car->dooraxis[1] = 1.0f;                     /* R3DObject's ctor: (0, 1, 0) */
    /* The material set with a bank on the graphics object's texture manager (made at the render system's
       start; without one, textures aren't made). */
    car->materials = R3DMaterialSet_ctor(calloc(1, sizeof *car->materials),
                                         g_texgraphics ? g_texgraphics->textures : NULL, car->texinfo);
    /* The hinge angle table (the original's lives with the car's other animation state): one triple per
       kind, all zero until the simulation fills them (R3DCar_v9 0x438370). */
    car->hinge = calloc(CAR_NHINGES, sizeof *car->hinge);
    for (int i = 0; i < 8; i++) car->anim[i].dir = 1;   /* 0x437850: 8 records {0, 1, 0, 0, 0} */
}

/* ---- the .crp cache (0x43ff80) ---- */

/* The game shares a loaded .crp between the cars that use it (a map by name, a reference count at +0xa4
   of the entry). */
typedef struct R3DCrpEntry {
    char name[128];
    UDataFile file;
    int refs;
    struct R3DCrpEntry *next;
} R3DCrpEntry;
static R3DCrpEntry *crp_cache;

static R3DCrpEntry *crp_acquire(const char *name)
{
    for (R3DCrpEntry *e = crp_cache; e; e = e->next)
        if (strcmp(e->name, name) == 0) {
            e->refs++;
            return e;
        }
    R3DCrpEntry *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    snprintf(e->name, sizeof e->name, "%s", name);
    UDataFile_ctor(&e->file);
    if (!UDataFile_load(&e->file, name, false)) {
        UDataFile_dtor(&e->file);
        free(e);
        return NULL;
    }
    e->refs = 1;
    e->next = crp_cache;
    crp_cache = e;
    return e;
}

/* 0x4400b0 */
static void crp_release(R3DCrpEntry *x)
{
    if (--x->refs > 0) return;
    for (R3DCrpEntry **p = &crp_cache; *p; p = &(*p)->next)
        if (*p == x) {
            *p = x->next;
            break;
        }
    UDataFile_free(&x->file);
    UDataFile_dtor(&x->file);
    free(x);
}

/* ---- the .tpg and styles ---- */

void R3DCar_LoadTpg(R3DCar *car, const char *name)
{
    char path[260];
    if (strstr(name, exe_str(0x5cd2b0)))                       /* ".tpg" */
        snprintf(path, sizeof path, "%s", name);
    else
        snprintf(path, sizeof path, exe_str(0x5cd2a8), name);   /* "%s.tpg" */
    IniFile_Load(&car->tpg, path);
}

static float ease_now(R3DCarEase *e, uint32_t now)   /* 0x437ef0 */
{
    if (e->dur != 0) {
        float t = (float)(now - e->since) / (float)(int32_t)e->dur;
        if (t <= 1.0f) return (e->to - e->from) * t + e->from;
        e->dur = 0;
    }
    return e->to;
}

static void style_extent(R3DCarEase *e, float extent, uint32_t now)
{
    if (ease_now(e, now) == 0) {
        e->from = ease_now(e, now);
        e->to = 0;
    } else if (e->dur == 0) {
        e->from = e->to;
        e->to = extent;
    } else {
        float t = (float)(now - e->since) / (float)(int32_t)e->dur;
        float old = e->to;
        e->to = extent;
        if (t <= 1.0f) {
            e->from = (old - e->from) * t + e->from;
        } else {
            e->dur = 0;
            e->from = old;
        }
    }
    e->since = now;
    e->dur = 0;
}

void R3DCar_LoadStyles(R3DCar *car, int style)
{
    R3DCarState *st = car->state;
    char key[32], sec[32];
    st->style = style;
    memset(st->options, 0, sizeof st->options);
    if (!car->tpg.ini) return;
    snprintf(sec, sizeof sec, exe_str(0x5cd660), style + 1);    /* "style%d" */
    if (IniData_SelectSection(car->tpg.ini, sec) >= 0) {
        int count = IniFile_GetInt(&car->tpg, exe_str(0x5cd658), 0);   /* "count" */
        for (int i = 1; i <= count; i++) {
            snprintf(key, sizeof key, exe_str(0x5cd64c), i);           /* "geometry%d" */
            int opt = IniFile_GetInt(&car->tpg, key, -1);
            if (opt < 0) {
                snprintf(key, sizeof key, exe_str(0x5cd640), i);       /* "texture%d" */
                opt = IniFile_GetInt(&car->tpg, key, 0) + 0x3c;
            }
            snprintf(key, sizeof key, exe_str(0x5cd638), i);           /* "type%d" */
            int v = IniFile_GetInt(&car->tpg, key, 0);
            /* The original writes options[opt] unchecked; the data stays within the 120. */
            if (opt >= 0 && opt < CAR_NOPTIONS) st->options[opt] = v;
        }
    }
    if (IniFile_GetBool(&car->tpg, exe_str(0x5cd630), false))   /* "stripe" */
        car->colours[6] = st->spec->colours[1];
    /* The hood, the trunk and the pop-up headlights: closed (from where they are) if they are, else to the
       car's extents, at once. */
    uint32_t now = app_ticks_ms();   /* GetTickCount */
    style_extent(&car->hoodease, car->hoodextent, now);
    style_extent(&car->trunkease, car->trunkextent, now);
    style_extent(&car->headease, car->headlightextent, now);
}

/* R3DCar_v19 0x43ad80 (before the LODs are built): the style's options, then the car spec's forced ones.
   The race's rest of it (light and gear state, the timed extents) pends with the simulation. */
static void car_prelod(R3DCar *car)
{
    R3DCarState *st = car->state;
    R3DCar_LoadStyles(car, st->style);
    for (int i = 0; i < 8; i++) {
        car->anim[i].pos = 0;
        car->anim[i].dir = 0;
        car->anim[i].t = 0;
        car->anim[i].frames = 0;
        car->anim[i].on = 1;
    }
    if (st->spec)
        for (int i = 0; i < CAR_NOPTIONS; i++)
            if (st->spec->options[i] >= 0) st->options[i] = st->spec->options[i];
}

/* ---- the part sort: MSVC 6 std::sort over 12-byte records by the signed key byte at +8 ---- */

typedef struct { UData *article; uint32_t entry; int8_t key; } PartRef;

static void adjust_heap(PartRef *f, int hole, int len, PartRef v)      /* 0x43eb10 */
{
    int top = hole, i = 2 * hole + 2;
    for (; i < len; i = 2 * i + 2) {
        if (f[i].key < f[i - 1].key) i--;
        f[hole] = f[i];
        hole = i;
    }
    if (i == len) {
        f[hole] = f[len - 1];
        hole = len - 1;
    }
    for (i = (hole - 1) / 2; top < hole && f[i].key < v.key; i = (hole - 1) / 2) {
        f[hole] = f[i];
        hole = i;
    }
    f[hole] = v;
}

static void make_heap(PartRef *f, PartRef *l)                         /* 0x43e9c0 */
{
    int len = (int)(l - f);
    for (int h = len / 2; h > 0;) {
        h--;
        adjust_heap(f, h, len, f[h]);
    }
}

static void sort_loop(PartRef *f, PartRef *l, int depth)              /* 0x43e640 */
{
    while (l - f >= 17) {
        if (depth == 0) {
            make_heap(f, l);
            while (l - f > 1) {
                PartRef v = l[-1];
                l[-1] = f[0];
                l--;
                adjust_heap(f, 0, (int)(l - f), v);
            }
            return;
        }
        depth--;
        PartRef *m = f + (l - f) / 2;
        int8_t a = f->key, b = m->key, c = l[-1].key;
        PartRef *pv = f;
        if (a < b) {
            if (b < c) pv = m;
            else if (a < c) pv = l - 1;
        } else if (c <= a) {
            pv = l - 1;
            if (c <= b) pv = m;
        }
        int8_t pivot = pv->key;
        PartRef *i = f, *j = l;
        for (;;) {
            while (i->key < pivot) i++;
            do j--; while (pivot < j->key);
            if (j <= i) break;
            PartRef t = *i;
            *i = *j;
            *j = t;
            i++;
        }
        sort_loop(i, l, depth);
        l = i;
    }
}

static void insertion_sort(PartRef *f, PartRef *l)                    /* 0x43e840 */
{
    if (f == l) return;
    for (PartRef *p = f + 1; p != l; p++) {
        PartRef v = *p;
        if (v.key < f->key) {
            memmove(f + 1, f, (size_t)(p - f) * sizeof *f);
            *f = v;
        } else {
            PartRef *q = p;
            while (v.key < q[-1].key) {
                *q = q[-1];
                q--;
            }
            *q = v;
        }
    }
}

static void unguarded_insertion(PartRef *f, PartRef *l)               /* 0x43e950 */
{
    for (PartRef *p = f; p != l; p++) {
        PartRef v = *p, *q = p;
        while (v.key < q[-1].key) {
            *q = q[-1];
            q--;
        }
        *q = v;
    }
}

static void sort_parts(PartRef *f, PartRef *l)
{
    if (f == l) return;
    int depth = 0;
    for (int n = (int)(l - f); n != 1; n >>= 1) depth++;
    sort_loop(f, l, depth * 2);
    if (l - f < 17) insertion_sort(f, l);
    else {
        insertion_sort(f, f + 16);
        unguarded_insertion(f + 16, l);
    }
}

/* ---- LODs ---- */

static const uint32_t ARTI = 0x41727469;   /* 'Arti' (an operand of 0x43c0e0) */

void R3DCarLOD_Build(R3DCarLOD *lod, UData *root)
{
    R3DCarLOD_Free(lod);
    R3DCarState *st = lod->car->state;
    int n = UData_countarticles(root, ARTI);
    PartRef *refs = calloc((size_t)(n > 0 ? n : 1), sizeof *refs);
    lod->nparts = 0;
    for (UData *a = UData_findarticle(root, ARTI, 0xffffffffu, 0xffffffffu), *end = UData_items(root); a != end; a++) {
        if (UData_key(a) != ARTI) continue;
        UData *b = UData_find(a, kind_id(R3D_BASE)), *bend = UData_itemsend(a);
        if (b == bend) continue;
        const uint8_t *bd = item_data(b);
        /* The option is read unchecked by the original (an index past the 120 would read on into the
           car state); the data's go up to 0x28. */
        if (bd[0x44] >= CAR_NOPTIONS || (uint32_t)st->options[bd[0x44]] != bd[0x45]) continue;
        uint32_t first = (uint32_t)le32(bd + 8), count = (uint32_t)le32(bd + 12);
        /* The entry looked at runs from the table's start while the index recorded runs from first:
           the same when first is 0, which it is in every car. */
        const uint8_t *e = bd + 0x64;
        for (uint32_t i = first; i < count; i++, e += 12)
            if ((uint32_t)(e[8] | e[9] << 8) == lod->lod) {
                refs[lod->nparts].article = a;
                refs[lod->nparts].entry = i;
                refs[lod->nparts].key = (int8_t)bd[0x46];
                lod->nparts++;
                break;
            }
    }
    sort_parts(refs, refs + lod->nparts);
    lod->parts = calloc(lod->nparts ? lod->nparts : 1, sizeof *lod->parts);
    for (uint32_t k = 0; k < lod->nparts; k++) {
        R3DPart *p = &lod->parts[k];
        p->article = refs[k].article;
        p->end = UData_itemsend(p->article);
        for (int c = 0; c < R3D_NKINDS; c++) cache_init(&p->cache[c]);
        p->verts = NULL;
        p->damage = 0;
        p->base = part_item(p, R3D_BASE, 0xffffffffu);
        p->info = p->base + 0x44;
        p->entry = p->base + 0x64 + refs[k].entry * 12;
        uint32_t idx = (uint32_t)(p->entry[8] | p->entry[9] << 8);
        if (p->info[9] == 0) {
            /* Parts that deform get their own vertices (damage detail on). */
            if ((p->info[10] & 8) && g_r3d_quality > 0) {
                UData *r = cache_find(&p->cache[R3D_VT], p->article, kind_index(R3D_VT, idx));
                const void *src = r == p->end ? NULL : item_data(r);
                UData *c = cache_find(&p->cache[R3D_VT], p->article, kind_index(R3D_VT, idx));
                uint32_t nv = c == p->end ? 0 : c->count;
                /* The damaged shape is looked up too (its result is unused here). */
                cache_find(&p->cache[R3D_VT], p->article, kind_index(R3D_VT, idx + 0x8000));
                p->verts = calloc(nv ? nv : 1, 16);
                if (src) memcpy(p->verts, src, (size_t)nv * 16);
            }
        } else {
            /* Animated parts (the cabrio tops): a copy of frame 0, the frames counted for the slot. */
            uint32_t nv = part_count(p, R3D_VT, idx);
            p->verts = calloc(nv ? nv : 1, 16);
            UData *r = cache_find(&p->cache[R3D_VT], p->article, kind_index(R3D_VT, idx));
            if (r != p->end) memcpy(p->verts, item_data(r), (size_t)r->count * 16);
            uint32_t frames = 0, fi = idx;
            for (;;) {
                fi += 0x10;
                frames++;
                UData *f = cache_find(&p->cache[R3D_VT], p->article, kind_index(R3D_VT, fi));
                if (f == p->end || !item_data(f)) break;
            }
            R3DCarAnim *an = &lod->car->anim[p->info[9] & 7];
            if (an->frames < (float)frames) {
                an->frames = (float)frames;   /* 0x4380e0 */
                an->pos = 0;                  /* 0x438000 (slot, 0) */
                an->t = 0;
            }
        }
        int8_t slot = (int8_t)p->info[4];
        if (slot >= 0 && slot < 4) lod->slot[slot] = p->article;
    }
    free(refs);
}

void R3DCar_AnimReset(R3DCar *car, int slot, float v)
{
    R3DCarAnim *an = &car->anim[slot];
    if (v < 0) v = 0;
    if (an->frames < v) v = an->frames;
    if ((float)(uint32_t)an->pos + an->t != v) {
        an->pos = (int)lrintf(v - 0.5f);   /* (0x5b2778) */
        an->t = v - (float)(uint32_t)an->pos;
        an->dir = an->pos + 1;
        an->on = 1;
    }
}

/* A LOD's animated parts whose slot moved (0x43e050): the vertices between the slot's two frames (the
   LOD's 'vt' record plus 0x10 per frame), the first frame's when the second is missing; as many as the
   LOD's own record has. */
static void lod_morph(R3DCarLOD *lod)
{
    R3DCar *car = lod->car;
    for (uint32_t k = 0; k < lod->nparts; k++) {
        R3DPart *p = &lod->parts[k];
        if (!p->info[9] || !car->anim[p->info[9]].on || !p->verts) continue;
        const R3DCarAnim *an = &car->anim[p->info[9]];
        UData *r = cache_find(&p->cache[R3D_VT], p->article, kind_index(R3D_VT, lod->lod));
        uint32_t n = r == p->end ? 0 : r->count;
        UData *ra = cache_find(&p->cache[R3D_VT], p->article, kind_index(R3D_VT, (uint32_t)an->pos * 0x10 + lod->lod));
        const float (*a)[4] = ra == p->end ? NULL : item_data(ra);
        UData *rb = cache_find(&p->cache[R3D_VT], p->article, kind_index(R3D_VT, (uint32_t)an->dir * 0x10 + lod->lod));
        const float (*b)[4] = rb == p->end ? NULL : item_data(rb);
        if (!a) continue;
        if (!b) {
            memcpy(p->verts, a, (size_t)n * 16);
            continue;
        }
        for (uint32_t i = 0; i < n; i++) {   /* 0x53c2a0, 0x53c340, 0x53c200 */
            for (int j = 0; j < 4; j++) p->verts[i][j] = (b[i][j] - a[i][j]) * an->t + a[i][j];
            p->verts[i][3] = 1.0f;
        }
    }
}

void R3DCar_ApplyMorphs(R3DCar *car)
{
    for (int i = 0; i < 8; i++)
        if (car->anim[i].on) {
            for (uint32_t l = 0; l < car->nlods; l++) lod_morph(car->lods[l]);
            for (int j = 0; j < 8; j++) car->anim[j].on = 0;
            return;
        }
}

/* R3DCar_v20 0x43b040: slot 1 at its last frame when the car's description says so (+0x50c), then the
   moved slots' parts. */
static void car_postlod(R3DCar *car)
{
    PENDING_NOTE(0x43b040, "R3DCar_v20's slots kept over a reload (+0x608)");
    const R3DCarSpec *sp = car->state ? car->state->spec : NULL;
    R3DCar_AnimReset(car, 1, sp && sp->slot1end ? car->anim[1].frames : 0);
    R3DCar_ApplyMorphs(car);
}

void R3DCarLOD_Free(R3DCarLOD *lod)
{
    if (lod->parts) {
        for (uint32_t i = 0; i < lod->nparts; i++) free(lod->parts[i].verts);
        free(lod->parts);
        lod->parts = NULL;
        lod->nparts = 0;
    }
    memset(lod->slot, 0, sizeof lod->slot);
}

/* A part's 'tr' matrix (index -1 = 'tr  '), NULL if it has none. 0x59efd0 */
static const EaglMatrix *part_transform(R3DPart *p, uint32_t idx)
{
    uint32_t id = idx == 0xffffffffu ? 0x74722020u : (idx | 0x74720000u);   /* 'tr  ' */
    UData *r = UData_find(p->article, id);
    return r == p->end ? NULL : item_data(r);
}

static void transform_point(const EaglMatrix *m, const float in[4], float out[4])   /* 0x53a0a0 */
{
    for (int j = 0; j < 4; j++)
        out[j] = in[0] * m->m[j] + in[1] * m->m[4 + j] + in[2] * m->m[8 + j] + in[3] * m->m[12 + j];
}

void R3DCarLOD_Bounds(R3DCarLOD *lod, EaglMatStack *model, float mn[3], float mx[3])
{
    (void)model;
    float v[4] = {0, 0, 0, 0};
    mn[0] = mn[1] = mn[2] = 0;
    mx[0] = mx[1] = mx[2] = 0;
    for (uint32_t k = 0; k < lod->nparts; k++) {
        R3DPart *p = &lod->parts[k];
        uint32_t idx = (uint32_t)(p->entry[8] | p->entry[9] << 8);
        const EaglMatrix *tr = part_transform(p, idx);
        const float *src = part_item(p, R3D_VT, idx);
        if (!src) continue;
        for (uint32_t i = 0;; i++, src += 4) {
            UData *r = cache_find(&p->cache[R3D_VT], p->article, kind_index(R3D_VT, idx));
            if (r == p->end || i >= r->count) break;
            if (tr) transform_point(tr, src, v);
            else memcpy(v, src, sizeof v);
            for (int j = 0; j < 3; j++) {
                if (v[j] < mn[j]) mn[j] = v[j];
                if (mx[j] < v[j]) mx[j] = v[j];
            }
        }
    }
}

/* The object's size from LOD list index 4 (or 0 for the low-detail model). 0x444fc0 */
static void car_size(R3DCar *car)
{
    R3DCarState *st = car->state;
    R3DCarLOD *l = (st->flags & 0x10) == 0 ? car->lods[4] : car->lods[0];
    float mn[3], mx[3];
    R3DCarLOD_Bounds(l, NULL, mn, mx);
    for (int j = 0; j < 3; j++) st->halfsize[j] = (mx[j] - mn[j]) * 0.5f;
    if (st->spec && st->spec->stretch) {
        union { uint32_t u; float f; } c = {exe_u32(0x5b37c8)};
        for (int j = 0; j < 3; j++) st->halfsize[j] = c.f * st->halfsize[j];
    }
    float r = sqrtf(st->halfsize[0] * st->halfsize[0] + st->halfsize[1] * st->halfsize[1] +
                    st->halfsize[2] * st->halfsize[2]);
    /* As the original: the length, then again with the depth added a second time. */
    st->radius = sqrtf(r * r + st->halfsize[2] * st->halfsize[2]);
}

/* 0x4424e0: the LODs (R3DCar_v16: 7; R3DCar_v17 makes one), built between the pre / post hooks. */
static void car_build_lods(R3DCar *car)
{
    for (uint32_t i = 0; i < car->nlods; i++) {
        R3DCarLOD_Free(car->lods[i]);
        free(car->lods[i]);
        car->lods[i] = NULL;
    }
    car->nlods = 0;
    car_prelod(car);                                   /* slot 19 */
    for (uint32_t i = 1; i <= CAR_NLODS; i++) {
        R3DCarLOD *l = calloc(1, sizeof *l);
        l->car = car;
        l->lod = i;
        R3DCarLOD_Build(l, car->root);
        car->lods[car->nlods++] = l;
    }
    car_postlod(car);                                  /* slot 20 */
    car_size(car);
}

/* ---- materials (0x44a6b0: render/rmaterial.c) ---- */

static void car_free_materials(R3DCar *car)
{
    if (car->materials) R3DMaterialSet_Clear(car->materials);
}

/* ---- loading ---- */

/* R3DObject slot 2 (0x442320): what a previous load left. */
static void car_unload(R3DCar *car)
{
    car->loaded = 0;
    if (car->shape_d) { MEM_purge(car->shape_d); car->shape_d = NULL; }
    if (car->shape_l) { MEM_purge(car->shape_l); car->shape_l = NULL; }
    for (uint32_t i = 0; i < car->nlods; i++) {
        R3DCarLOD_Free(car->lods[i]);
        free(car->lods[i]);
        car->lods[i] = NULL;
    }
    car->nlods = 0;
    car_free_materials(car);
    free(car->sf);
    car->sf = NULL;
    car->nsf = 0;
    if (car->crp) crp_release(car->crp);
    car->crp = NULL;
}

/* A point list item of the root ('PdnB' / 'odnW'): records {option, value, then count points of four
   floats}; the points of records whose option has that value are taken (and scaled for stretched
   models). */
static uint32_t car_points(R3DCar *car, uint32_t id, float (*out)[3], uint32_t cap, bool lift)
{
    R3DCarState *st = car->state;
    UData *end = UData_itemsend(car->root);
    uint32_t n = 0;
    for (UData *r = UData_finditem(car->root, id, 0xffffffffu, 0xffffffffu); r != end && UData_key(r) == id; r++) {
        const uint8_t *d = item_data(r);
        if (le32(d) >= CAR_NOPTIONS || (uint32_t)st->options[le32(d)] != le32(d + 4) || r->count == 0) continue;
        const uint8_t *pt = d + 8;
        for (uint32_t i = 0; i < r->count && n < cap; i++, pt += 16, n++) {
            memcpy(out[n], pt, 12);
            if (st->spec && st->spec->stretch) {
                union { uint32_t u; float f; } c = {exe_u32(0x5b37c8)};
                for (int j = 0; j < 3; j++) out[n][j] *= c.f;
            }
            if (lift) out[n][1] = out[n][1] - st->halfsize[1];
        }
    }
    return n;
}

bool R3DCar_LoadCrp(R3DCar *car, const char *name)
{
    R3DCarState *st = car->state;
    char path[260];
    car_unload(car);
    snprintf(car->name, sizeof car->name, "%s", name);
    car->textures = 1;
    if (!g_r3d_frontend && !R3DCar_PendingTexturesWanted(car)) car->textures = 0;
    if (strstr(name, exe_str(0x5cdcec)))                         /* ".crp" */
        snprintf(path, sizeof path, "%s", name);
    else
        snprintf(path, sizeof path, exe_str(0x5cdce4), name);    /* "%s.crp" */
    /* The global textures the car's materials share (race: 'SPC1'..'SPC3', front end 'FEDU', 'FE  ',
       'FESH'): pending with the texture system. */
    R3DCar_GlobalTextures(car);
    car->crp = crp_acquire(path);
    if (!car->crp) {
        /* The original loads a stand-in car ("%seden2" under its car directory) unless this was it. */
        R3DCar_PendingFallback(car, name);
        return true;
    }
    R3DCar_LoadTpg(car, name);
    if (car->tpg.ini) IniData_SelectSection(car->tpg.ini, exe_str(0x5cdad8));     /* "header" */
    static const float campos_def[3] = {-0.36f, 0.4f, -0.25f}, zero[3] = {0, 0, 0}, up[3] = {0, 1.0f, 0};
    float campos[3], camrot[3];
    if (car->tpg.ini) {
        IniFile_GetVec3(&car->tpg, campos, exe_str(0x5cdcd4), campos_def);       /* drivercampos */
        IniFile_GetVec3(&car->tpg, camrot, exe_str(0x5cdcc4), zero);             /* drivercamrot */
        IniFile_GetVec3(&car->tpg, car->dooraxis, exe_str(0x5cdcb8), up);        /* dooraxis */
        car->noroof = IniFile_GetBool(&car->tpg, exe_str(0x5cdcb0), false);      /* noroof */
        car->hoodextent = IniFile_GetFloat(&car->tpg, exe_str(0x5cdca4), 0.15f);
        car->trunkextent = IniFile_GetFloat(&car->tpg, exe_str(0x5cdc98), -0.15f);
        car->headlightextent = IniFile_GetFloat(&car->tpg, exe_str(0x5cdc88), 0);
        car->spoilerextent = IniFile_GetFloat(&car->tpg, exe_str(0x5cdc74), 0);
        char key[32];
        snprintf(key, sizeof key, exe_str(0x5cdc60), st->style);                 /* drivercamposstyle%d */
        IniFile_GetVec3(&car->tpg, st->drivercampos, key, campos);
        snprintf(key, sizeof key, exe_str(0x5cdc4c), st->style);                 /* drivercamrotstyle%d */
        IniFile_GetVec3(&car->tpg, st->drivercamrot, key, camrot);
    } else {
        memcpy(st->drivercampos, campos_def, sizeof campos_def);
        memset(st->drivercamrot, 0, sizeof st->drivercamrot);
    }
    if (!g_r3d_frontend && g_r3d_mirror > 0) {
        st->drivercampos[0] = -st->drivercampos[0];
        st->drivercamrot[0] = -st->drivercamrot[0];
    }
    car->root = UDataFile_root(&car->crp->file);
    /* The root's 'sf' items (shapes), by their index, into a vector sized for "numfiles". */
    uint32_t numfiles = car->tpg.ini ? (uint32_t)IniFile_GetInt(&car->tpg, exe_str(0x5cdc40), 0) : 0;
    car->nsf = numfiles;
    car->sf = calloc(numfiles ? numfiles : 1, sizeof *car->sf);
    for (UData *r = UData_items(car->root), *end = UData_itemsend(car->root); r != end; r++) {
        if (UData_key(r) != 0x73662020u) continue;   /* 'sf  ' */
        uint32_t i = r->info & UDATA_INDEXED ? r->id & 0xffff : 0xffffffffu;
        if (i == 0xffffffffu) continue;
        if (i >= car->nsf) {
            car->sf = realloc(car->sf, (i + 1) * sizeof *car->sf);
            memset(car->sf + car->nsf, 0, (i + 1 - car->nsf) * sizeof *car->sf);
            car->nsf = i + 1;
        }
        car->sf[i] = item_data(r);
    }
    if (car->textures) {
        char fsh[260];
        if (!g_r3d_frontend && car->hsv[2] > 0x50) {
            snprintf(fsh, sizeof fsh, exe_str(0x5cdc30), name);   /* "%sl.fsh" */
            car->shape_l = UTIL_loadshape(fsh, 0);
        } else {
            snprintf(fsh, sizeof fsh, exe_str(0x5cdc38), name);   /* "%sd.fsh" */
            car->shape_d = UTIL_loadshape(fsh, 0);
            if (g_r3d_frontend || car->hsv[2] > 0x50) {
                snprintf(fsh, sizeof fsh, exe_str(0x5cdc30), name);
                car->shape_l = UTIL_loadshape(fsh, 0);
            }
        }
        /* The decals and liveries of the chosen options, from those shapes (0x447b80): pending. */
        R3DCar_PendingDecals(car);
    }
    car_build_lods(car);                     /* slot 18 (nothing for a car), then 0x4424e0 */
    /* R3DObject_LoadHeader 0x4426c0: the texture pages, then the material set (0x44a6b0). */
    R3DObject_LoadHeader(car);   /* the pages, then the material set */
    car->nbound = car_points(car, 0x426e6450u, car->bound, 48, true);     /* 'PdnB' */
    car->nwindow = car_points(car, 0x576e646fu, car->window, 8, false);   /* 'odnW' */
    car->loaded = 1;
    return true;
}

void R3DCar_Free(R3DCar *car)
{
    car_unload(car);
    IniFile_Free(&car->tpg);
    R3DMaterialSet_dtor(car->materials);
    free(car->materials);
    car->materials = NULL;
    free(car->hinge);
    car->hinge = NULL;
}

/* ---- rotations (EAGL_MatAxisAngle 0x539940, 0x532430) ---- */

static float f32(uint32_t va) { union { uint32_t u; float f; } c = {exe_u32(va)}; return c.f; }

static double f64(uint32_t va)
{
    union { uint64_t u; double d; } c = {(uint64_t)exe_u32(va) | (uint64_t)exe_u32(va + 4) << 32};
    return c.d;
}

void Mat3_AxisRotation(const float axis[3], float turns, float out[9])
{
    if (turns == 0.0f) {
        for (int i = 0; i < 9; i++) out[i] = f32(0x5bb2d0 + 4u * (unsigned)i);   /* the identity */
        return;
    }
    /* sin and cos of turns * 0x5bb2c0 (2 pi), x87 FSIN / FCOS, stored as floats. */
    float s = (float)sin((double)turns * f64(0x5bb2c0)), c = (float)cos((double)turns * f64(0x5bb2c0));
    float len = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);   /* 0x532890 */
    float inv = (float)(1.0 / (double)len);
    float x = (float)((double)axis[0] * inv), y = (float)((double)axis[1] * inv), z = (float)((double)axis[2] * inv);
    float k = 1.0f - c, kx = x * k, ky = y * k, kz = k * z;
    out[0] = x * kx + c;
    out[1] = y * kx + z * s;
    out[2] = z * kx - y * s;
    out[3] = ky * x - z * s;
    out[4] = y * ky + c;
    out[5] = z * ky + x * s;
    out[6] = kz * x + y * s;
    out[7] = kz * y - x * s;
    out[8] = z * kz + c;
}

void EAGL_MatAxisAngle(EaglMatrix *m, float degrees, float ax, float ay, float az)
{
    /* Degrees to turns (0x5b3e84 = 1/360) in extended precision; the whole turns dropped (__ftol). */
    double t = (double)degrees * (double)f32(0x5b3e84);
    float frac = (float)(t - (double)(long long)t);
    float axis[3] = {ax, ay, az}, r[9];
    Mat3_AxisRotation(axis, frac, r);
    memset(m, 0, sizeof *m);
    m->m[0] = r[0]; m->m[1] = r[1]; m->m[2] = r[2];
    m->m[4] = r[3]; m->m[5] = r[4]; m->m[6] = r[5];
    m->m[8] = r[6]; m->m[9] = r[7]; m->m[10] = r[8];
    m->m[15] = 1.0f;
}

/* ---- the draw ---- */

void R3DCarLOD_Draw(R3DCarLOD *lod, EaglMatStack *model, uint8_t mask, bool cockpit)
{
    R3DCar *car = lod->car;
    R3DMaterialSet *ms = car->materials;
    THRASH.fn.setstate(0x24, 10);
    for (uint32_t pass = 0; pass < 2; pass++) {
        for (uint32_t k = 0; k < lod->nparts; k++) {
            R3DPart *p = &lod->parts[k];
            /* The second pass only for parts with translucent primitives (Base byte 0 bit 0). */
            if (pass == 1 && !(p->base[0] & 1)) continue;
            const uint8_t *info = p->info, *e = p->entry;
            if (cockpit ? info[0xd] == 0 : info[0xe] != 0) continue;
            if (!(info[10] & mask & 0xf0)) continue;
            uint32_t idx = (uint32_t)(e[8] | e[9] << 8);
            const EaglMatrix *tr = part_transform(p, idx);
            if (tr) {
                EAGL_StackPush(model);
                EaglMatrix m;
                EAGL_MatCopy(&m, tr);
                EAGL_MatPreMul(EAGL_StackTopForWrite(model), &m);
                int8_t slot = (int8_t)info[4];
                if (slot >= 0) {
                    const float *w = car->wheelpos[slot & 3];
                    EAGL_MatTranslation(&m, w[0], w[1], w[2]);
                    EAGL_MatPreMul(EAGL_StackTopForWrite(model), &m);
                }
                uint8_t kind = info[7];
                if (kind == 6 || kind == 7) {
                    float axis[3] = {car->dooraxis[0], car->dooraxis[1], car->dooraxis[2]}, r[9];
                    if (kind == 7) axis[0] = -axis[0];
                    static const float zero[3] = {0, 0, 0};
                    Mat3_AxisRotation(axis, car->hinge[kind][1], r);
                    EAGL_MatFromRotTrans(&m, r, zero);
                    EAGL_MatPreMul(EAGL_StackTopForWrite(model), &m);
                } else if (kind != 0 && kind < CAR_NHINGES) {
                    const float *a = car->hinge[kind];
                    float deg = f32(0x5b25cc);   /* 360 */
                    EaglMatrix rx, ry, rz;
                    EAGL_MatAxisAngle(&rx, a[0] * deg, 1.0f, 0, 0);
                    EAGL_MatAxisAngle(&ry, a[1] * deg, 0, 1.0f, 0);
                    EAGL_MatAxisAngle(&rz, a[2] * deg, 0, 0, 1.0f);
                    EAGL_MatPostMul(&rx, &ry);
                    EAGL_MatPostMul(&rx, &rz);
                    EAGL_MatPreMul(EAGL_StackTopForWrite(model), &rx);
                }
            }
            if (e[0]) {
                /* The primitive lists of the entry's range, through the part's 'pr' cache. */
                uint32_t prid = kind_id(R3D_PR);
                uint16_t lo = (uint16_t)(e[4] | e[5] << 8), hi = (uint16_t)(e[6] | e[7] << 8);
                UData *it = UData_finditem(p->article, prid, lo, hi);
                p->cache[R3D_PR].rec = it;
                p->cache[R3D_PR].id = it != p->end ? it->id : 0;
                while (it != p->end) {
                    const uint32_t *pr = item_data(it);
                    if (!pr) break;
                    if (pass == (pr[0] >> 16 & 1)) {
                        uint32_t mi = pr[1] & 0xffff;
                        R3DMaterial *mat = ms && mi < ms->count ? ms->mat[mi] : NULL;
                        R3DMaterial_DrawList(mat, p, car->root, pr, it->count);
                    }
                    /* The next record while it is still a 'pr' below the range's end. */
                    it++;
                    if (it == p->end || UData_key(it) != prid ||
                        (it->info & UDATA_INDEXED ? it->id & 0xffff : 0xffffffffu) >= hi) {
                        it = p->end;
                    }
                    p->cache[R3D_PR].rec = it;
                    p->cache[R3D_PR].id = it != p->end ? it->id : 0;
                }
            }
            if (tr) EAGL_StackPop(model);
        }
    }
}

/* A hinge's turn about x, y and z (0x443e40: the car's hinge angles, turns) as one matrix; without x
   for the second (the beam's). */
static void hinge_rot(const R3DCar *car, int kind, EaglMatrix *all, EaglMatrix *yz)
{
    static const float none[3] = {0, 0, 0};
    const float *a = car->hinge && kind < CAR_NHINGES ? car->hinge[kind] : none;
    float deg = f32(0x5b25cc);   /* 360 */
    EaglMatrix rx, ry, rz;
    EAGL_MatAxisAngle(&rx, a[0] * deg, 1.0f, 0, 0);
    EAGL_MatAxisAngle(&ry, a[1] * deg, 0, 1.0f, 0);
    EAGL_MatAxisAngle(&rz, a[2] * deg, 0, 0, 1.0f);
    *all = rx;
    EAGL_MatPostMul(all, &ry);
    EAGL_MatPostMul(all, &rz);
    EAGL_MatIdentity(yz);
    EAGL_MatPostMul(yz, &ry);
    EAGL_MatPostMul(yz, &rz);
}

static void transpose4(const EaglMatrix *a, EaglMatrix *out)   /* 0x53a3c0 */
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) out->m[r * 4 + c] = a->m[c * 4 + r];
}

/* For every part whose LOD entry has effects, its 'ef' records of type 5: when the light of the part's
   id (info +0xc) is on, through the part's transform (the one the LOD's number names, not the entry's
   as the draw takes: the original's) and its hinge (info +7); when the record's flag 8 is set and the
   light of its own id (+0x56) is on, its mirror image: x, the first axis and the second's x negated
   (and +0x30's), through the transform's rotation transposed and its translation's x negated, with
   the record's hinge (+0x55). R3DCarLOD_v3 0x43d730 */
void R3DCarLOD_DrawLights(R3DCarLOD *lod, const EaglMatrix *m)
{
    R3DCar *car = lod->car;
    RenderState *rs = g_render && g_render->device ? g_render->device->rs : NULL;
    if (rs) {
        rs->state39 = 0;   /* 0x5375e0(0) */
        EAGL_RSSet(0x39, 0);
    }
    THRASH.fn.setstate(0x24, 10);
    uint32_t efid = kind_id(R3D_EF);
    for (uint32_t k = 0; k < lod->nparts; k++) {
        R3DPart *p = &lod->parts[k];
        const uint8_t *info = p->info;
        if (!p->entry[1]) continue;
        UData *it = UData_finditem(p->article, efid, 0, 0xffff);   /* 0x43cc80 */
        while (it != p->end) {
            const uint8_t *ef = item_data(it);
            if (!ef) break;
            if (le32(ef) == 5) {
                const float *light = (const float *)(ef + 8);
                if (car->lighton[info[0xc] & 15]) {   /* 0x443590 */
                    EaglMatrix m1 = *m, m2 = *m;
                    const EaglMatrix *tr = part_transform(p, lod->lod);   /* 0x59efd0 */
                    if (tr) {
                        EaglMatrix t;
                        EAGL_MatCopy(&t, tr);
                        EAGL_MatPreMul(&m1, &t);
                        m2 = m1;
                        if (info[7]) {
                            EaglMatrix r, ryz;
                            hinge_rot(car, info[7], &r, &ryz);
                            EAGL_MatPreMul(&m1, &r);
                            EAGL_MatPreMul(&m2, &ryz);
                        }
                    }
                    R3DCar_LightHalo(light, car->road, &m1, car->lightlevel[info[0xc] & 15]);   /* 0x4435c0 */
                    if (!g_r3d_frontend && ef[0x57] && 0 < g_r3d_night) R3DCar_LightBeam(light, &m1, &m2);   /* 0x43d420 */
                }
                if ((ef[0x50] & 8) && car->lighton[ef[0x56] & 15]) {
                    float twin[0x14];
                    memcpy(twin, light, sizeof twin);
                    twin[0] = -twin[0];
                    twin[4] = -twin[4];
                    twin[5] = -twin[5];
                    twin[6] = -twin[6];
                    twin[8] = -twin[8];
                    twin[12] = -twin[12];
                    EaglMatrix m1 = *m, m2 = *m;
                    const EaglMatrix *tr = part_transform(p, lod->lod);
                    if (tr) {
                        EaglMatrix t, rot, mv;
                        EAGL_MatCopy(&t, tr);
                        float tx = -t.m[12], ty = t.m[13], tz = t.m[14];
                        t.m[12] = t.m[13] = t.m[14] = 0;
                        transpose4(&t, &rot);
                        EAGL_MatTranslation(&mv, tx, ty, tz);
                        EAGL_MatPreMul(&rot, &mv);
                        EAGL_MatPreMul(&m1, &rot);
                        m2 = m1;
                        if (info[7]) {
                            EaglMatrix r, ryz;
                            hinge_rot(car, ((const uint8_t *)twin)[0x4d], &r, &ryz);
                            EAGL_MatPreMul(&m1, &r);
                            EAGL_MatPreMul(&m2, &ryz);
                        }
                    }
                    R3DCar_LightHalo(twin, car->road, &m1, car->lightlevel[ef[0x56] & 15]);
                    if (!g_r3d_frontend && ef[0x57] && 0 < g_r3d_night) R3DCar_LightBeam(twin, &m1, &m2);   /* 0x43d420 */
                }
            }
            /* The next record while it is still an 'ef'. */
            it++;
            if (it == p->end || UData_key(it) != efid) it = p->end;
        }
    }
    if (rs) {
        rs->state39 = 1;   /* 0x5375e0(1) */
        EAGL_RSSet(0x39, 1);
    }
}

static int table(uint32_t va, int i) { return (int)exe_u32(va + 4u * (unsigned)i); }

/* R3DObject_v14 0x444d30 (R3DCar_v14 adds the race's window reflections, pending): which LOD draws
   which parts. The front end's showroom: the current LOD's body (0x50), then LOD list index 0's
   wheels and glass (0x20). */
static void car_draw_lods(R3DCar *car, EaglMatStack *model)
{
    RLayers_ResetCache();   /* 0x44a090 */
    if (car->nlods == 0) return;
    /* The camera's car (0x465c40) in a view inside it (the camera's flag 8, 0x627f43): LOD list index 1's
       interior (0x10) and 0's parts seen from inside (0x40), and unless a look key is held (0x5e99ac,
       0x5e99b0, 0x5e99b4) at a detail level above 0, the detail's LOD (0x5cdb1c) shadow and decals (0x80). */
    if (!g_r3d_frontend && car->state->index == g_camera_car && (g_racecam[0].flags2 >> 3 & 1)) {
        if (car->nlods > 1) R3DCarLOD_Draw(car->lods[1], model, 0x10, true);
        R3DCarLOD_Draw(car->lods[0], model, 0x40, true);
        if (g_look[3] == 0 && g_look[1] == 0 && g_look[2] == 0 && 0 < g_r3d_quality)
            R3DCarLOD_Draw(car->lods[table(0x5cdb1c, g_r3d_quality)], model, 0x80, true);
        return;
    }
    if (car->interior) {
        R3DCarLOD_Draw(car->lods[car->cockpitlod], model, 0x30, false);
        return;
    }
    int cur = car->curlod;
    if (!g_r3d_frontend && cur < 4) R3DCarLOD_Draw(car->lods[table(0x5cdb28, cur)], model, 0x80, false);
    R3DCarLOD_Draw(car->lods[cur], model, 0x50, false);
    if (g_r3d_frontend) {
        R3DCarLOD_Draw(car->lods[0], model, 0x20, false);
        return;
    }
    if (car->state->flags & 0x10) {
        R3DCarLOD_Draw(car->lods[table(0x5cdb98, cur)], model, 0x20, false);
        return;
    }
    R3DCarLOD_Draw(car->lods[table(0x5cdb44, g_r3d_quality * 7 + cur)], model, 0x20, false);
}

void R3DCar_Draw(R3DCar *car, EaglMatStack *model)
{
    if (!car->loaded) return;
    R3DCarState *st = car->state;
    Render_SetCull(g_render);   /* 0x467ed0: the car's mirrored axes flip the winding */
    EAGL_StackPush(model);
    /* The centre: the car's up (rotation row 1) times its half height (R3DCar_v5 0x43a340). */
    float c[3] = {st->rot[3] * st->halfsize[1], st->rot[4] * st->halfsize[1], st->rot[5] * st->halfsize[1]};
    EaglMatrix m;
    static const float zero[3] = {0, 0, 0};
    /* a TINYCAR's model at 0x5b37c8's scale (the race's bundle draw 0x4431e0) */
    float k = 1.0f;
    if (st->spec && st->spec->stretch) {
        union { uint32_t u; float f; } c = {exe_u32(0x5b37c8)};
        k = c.f;
    }
    EAGL_MatScale(&m, -k, k, -k, 1.0f);
    EAGL_MatPostMul(EAGL_StackTopForWrite(model), &m);
    EAGL_MatFromRotTrans(&m, st->rot, zero);
    EAGL_MatPostMul(EAGL_StackTopForWrite(model), &m);
    EAGL_MatScale(&m, 1.0f, -1.0f, 1.0f, 1.0f);
    EAGL_MatPostMul(EAGL_StackTopForWrite(model), &m);
    EAGL_MatTranslation(&m, st->pos[0] - c[0], st->pos[1] - c[1], st->pos[2] - c[2]);
    EAGL_MatPostMul(EAGL_StackTopForWrite(model), &m);
    /* its light block as the layers' environment, a copy for the draw (0x4433c0: 0x65 words) */
    float *saved = g_rlayer_lights;
    static float env[CARLIGHT_SIZE / 4];
    if (car->lit) {
        memcpy(env, car->light.f, sizeof env);
        g_rlayer_lights = env;
        g_rlayer_envtex = car->light.tex;
        g_rlayer_lighttex = &car->light.lighttex;
    }
    g_rlayer_spectex = car->globaltex;   /* (the light block's +0xdc: 0x440870) */
    car_draw_lods(car, model);
    EaglMatrix world = *EAGL_StackTop(model);
    g_rlayer_lights = saved;
    g_rlayer_envtex = NULL;
    g_rlayer_spectex = NULL;
    g_rlayer_lighttex = NULL;
    EAGL_StackPop(model);
    /* the race's draw (0x4431e0) then, the stack popped, LOD list index 0's lights under the car's
       matrix (their corners are the world's) */
    if (!g_r3d_frontend && car->nlods) R3DCarLOD_DrawLights(car->lods[0], &world);
    Render_SetCull(g_render);
}
