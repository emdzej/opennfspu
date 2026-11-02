/* The track world's bundles and visibility walls (see world.h). */
#include "game/world.h"
#include "game/smack.h"
#include "game/skid.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "game/anim.h"
#include "game/track_pending.h"
#include "game/wtree.h"
#include "game/track.h"
#include "render/rdraw.h"
#include <stdlib.h>
#include <string.h>

/* Constants the world code reads from the exe's data: the zero vector 0x5bb2b0, the identity rotation
   0x5bb2d0, 0.0 at 0x5b23f8 and 1.0 at 0x5b24a8. */
static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float vzero[3], rot_identity[9], f_zero, f_one;
static bool consts_loaded;

static void load_consts(void)
{
    if (consts_loaded) return;
    const uint8_t *z = exe_data(0x5bb2b0, sizeof vzero), *r = exe_data(0x5bb2d0, sizeof rot_identity);
    const uint8_t *a = exe_data(0x5b23f8, 4), *b = exe_data(0x5b24a8, 4);
    if (z) memcpy(vzero, z, sizeof vzero);
    if (r) memcpy(rot_identity, r, sizeof rot_identity);
    if (a) memcpy(&f_zero, a, 4);
    if (b) memcpy(&f_one, b, 4);
    consts_loaded = z && r && a && b;
}

/* The item ids of the cache slots (the table at 0x5e5084, read from the exe). */
static uint32_t cache_ids[WITEM_COUNT];

static uint32_t cache_id(int slot)
{
    if (!cache_ids[WITEM_BASE]) {
        for (int i = 0; i < WITEM_COUNT; i++) {
            const uint8_t *p = exe_data(0x5e5084 + 4 * (uint32_t)i, 4);
            cache_ids[i] = p ? (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) : 0;
        }
    }
    return cache_ids[slot];
}

/* The item `id` of an article through a {hit, id} cache pair (0x43c620): the end of the items on a miss. */
static UData *cached_find(UData *article, UData **hit, uint32_t *hid, uint32_t id)
{
    if (*hid != id) {
        *hit = UData_find(article, id);
        *hid = id;
    }
    return *hit;
}

/* An item's data, NULL for the end of the items (the pattern of 0x43c600 after a find). */
static void *item_data(UData *article, UData *rec) { return rec == UData_itemsend(article) ? NULL : UData_data(rec); }

/* An uncached Base lookup, as the loads do with their stack caches. */
static const WBase *article_base(UData *article)
{
    UData *hit = NULL;
    uint32_t hid = 0;
    return item_data(article, cached_find(article, &hit, &hid, cache_id(WITEM_BASE)));
}

/* The article's 'tr' item (index 0: 'tr' + 0; -1: 'tr  '), NULL without one. 0x59efd0 */
static void *article_tr(UData *article, uint32_t index)
{
    uint32_t id = index == 0xffffffffu ? 0x74722020u : (index | 0x74720000u);
    return item_data(article, UData_find(article, id));
}

/* ------------------------------------------------------------------ W3DObjectBundle, WArticleBundle */

static int bundle_serial;   /* 0x5d0608 */

/* W3DObjectBundle ctor 0x489090 (kind, world). */
static void W3DObjectBundle_ctor(W3DObjectBundle *b, uint32_t kind, void *world);
void W3DObjectBundle_init(W3DObjectBundle *b, uint32_t kind, void *world) { W3DObjectBundle_ctor(b, kind, world); }
static void W3DObjectBundle_ctor(W3DObjectBundle *b, uint32_t kind, void *world)
{
    b->transformed = 0;
    b->b5 = 0;
    b->draw = NULL;
    b->collide = NULL;
    b->f10 = 0;
    b->smack = NULL;
    b->count = 0;
    b->entries = NULL;
    b->place = NULL;
    b->kind = kind;
    b->serial = bundle_serial++;
    b->world = world;
}

/* WArticleBundle_v0 0x477b60 / 0x477ba0: frees what the bundle allocated (the articles are the track's). */
static void WArticleBundle_dtor(W3DObjectBundle *b, int free_self)
{
    WArticleBundle *a = (WArticleBundle *)b;
    free(a->cache);
    free(b->entries);
    if (free_self) free(b);
}

static WBundleEntry *entries_new(uint32_t n) { return calloc(n ? n : 1, sizeof(WBundleEntry)); }

/* 0x477ca0: the per-entry caches (64 bytes each, page memory in the original), then slot 10. */
static void WArticleBundle_buildcache(W3DObjectBundle *b)
{
    WArticleBundle *a = (WArticleBundle *)b;
    a->cache = calloc(b->count ? b->count : 1, sizeof(WArticleCache));
    b->vt->fillcache(b);
}

/* 0x477cd0: each cache gets its article and the end of its items; the pairs start empty. */
static void WArticleBundle_fillcache(W3DObjectBundle *b)
{
    WArticleBundle *a = (WArticleBundle *)b;
    for (uint32_t i = 0; i < b->count && a->cache; i++) {
        WArticleCache *c = &a->cache[i];
        c->article = b->entries[i].article;
        c->end = UData_itemsend(c->article);
        memset(c->slot, 0, sizeof c->slot);
    }
}

/* WArticleBundle_v1 0x477bd0: takes every article as given (none are cleared). */
static int WArticleBundle_load(W3DObjectBundle *b, UData **arts, int n)
{
    b->count = n > 0 ? (uint32_t)n : 0;
    b->entries = entries_new(b->count);
    for (int i = 0; i < n; i++) {
        b->entries[i].article = arts[i];
        arts[i] = NULL;
    }
    b->vt->buildcache(b);
    return (int)b->count;
}

const WBase *WArticleBundle_base(WArticleBundle *a, int i)
{
    WArticleCache *c = &a->cache[i];
    UData *r = cached_find(c->article, &c->slot[WITEM_BASE].hit, &c->slot[WITEM_BASE].id, cache_id(WITEM_BASE));
    return item_data(c->article, r);
}

/* 0x477d40. The transformed case (entries with their own matrix: the scene, instance and animated
   bundles) moves the centre by the entry's matrix and scales the radius by it. */
/* The sphere through entry i's matrix (slot 5): the centre moved; the radius the largest of (r, r, r)'s
   components through it less its translation (0x5591d0). */
static void sphere_through(W3DObjectBundle *b, int i, float out[4])
{
    EaglMatrix m;
    b->vt->matrix(b, i, m.m);
    float c[4] = {out[0], out[1], out[2], 1.0f};
    EAGL_MatTransform4(&m, 1, c, 0x10, c, 0x10);
    float r = out[3], v[3];
    for (int k = 0; k < 3; k++) v[k] = r * m.m[k] + r * m.m[4 + k] + r * m.m[8 + k] + m.m[12 + k];
    float rr = v[0] - m.m[12];
    if (rr < v[1] - m.m[13]) rr = v[1] - m.m[13];
    if (rr < v[2] - m.m[14]) rr = v[2] - m.m[14];
    out[0] = c[0];
    out[1] = c[1];
    out[2] = c[2];
    out[3] = rr;
}

void WArticleBundle_sphere(WArticleBundle *a, int i, float out[4])
{
    const WBase *bs = WArticleBundle_base(a, i);
    out[0] = bs->centre[0];
    out[1] = bs->centre[1];
    out[2] = bs->centre[2];
    out[3] = bs->radius;
    if (a->base.transformed && a->base.vt->matrix) sphere_through(&a->base, i, out);
}

/* WInstanceBundle's and WSceneBundle's slot 3 (0x478630): the Base of the entry's article, or of its
   first piece's when the entry has knockable pieces; once its sound has played the radius grows by how
   far the body has moved (the current record's slot 8). Always through the entry's matrix. */
static void instance_sphere(W3DObjectBundle *b, int i, float out[4])
{
    UData *art = b->entries[i].article;
    float extra = 0;
    WAnimatable *a = b->smack;
    if (a && WAnimatable_HasAnim(a, i, 0)) {   /* 0x489820, 0x476730 */
        const WSmackEntry *e = &a->e[i];
        art = e->pieces[0].article;
        if (e->sounded) {
            float v = WAnimatable_Drift(a, i);
            if (v != 0) extra = v;   /* (0x5b23f8) */
        }
    }
    const WBase *bs = article_base(art);
    out[3] = extra + bs->radius;
    out[0] = bs->centre[0];
    out[1] = bs->centre[1];
    out[2] = bs->centre[2];
    sphere_through(b, i, out);
}

/* WMidGroundBundle_v3 0x47cdc0: the Base's sphere, its radius times 2.25 (0x5b4774) and at least 200
   (0x5b2484). */
static void midground_sphere(W3DObjectBundle *b, int i, float out[4])
{
    const WBase *bs = article_base(b->entries[i].article);
    float r = bs->radius * exe_f32(0x5b4774);
    if (r <= exe_f32(0x5b2484)) r = exe_f32(0x5b2484);
    out[0] = bs->centre[0];
    out[1] = bs->centre[1];
    out[2] = bs->centre[2];
    out[3] = r;
}

/* 0x477ed0 */
void WArticleBundle_box(WArticleBundle *a, int i, float bmin[3], float bmax[3])
{
    const WBase *bs = WArticleBundle_base(a, i);
    memcpy(bmin, bs->bmin, sizeof bs->bmin);
    memcpy(bmax, bs->bmax, sizeof bs->bmax);
}

static void article_sphere(W3DObjectBundle *b, int i, float out[4]) { WArticleBundle_sphere((WArticleBundle *)b, i, out); }
static void article_box(W3DObjectBundle *b, int i, float mn[3], float mx[3]) { WArticleBundle_box((WArticleBundle *)b, i, mn, mx); }
static void instance_sphere(W3DObjectBundle *b, int i, float out[4]);
static void midground_sphere(W3DObjectBundle *b, int i, float out[4]);
static void midground_corners(W3DObjectBundle *b, int i, float c[8][4]);

#define VT6(load_fn, corners_fn, sphere_fn, matrix_fn) { WArticleBundle_dtor, load_fn, corners_fn, sphere_fn, article_box, \
                      matrix_fn, W3DObjectBundle_nop, W3DObjectBundle_zero, W3DObjectBundle_sphere2, \
                      WArticleBundle_buildcache, WArticleBundle_fillcache }
#define VT5(load_fn, sphere_fn, matrix_fn) VT6(load_fn, W3DObjectBundle_corners, sphere_fn, matrix_fn)
#define VT(load_fn) VT5(load_fn, article_sphere, NULL)

/* ------------------------------------------------------------------ the article bundles' loads */

/* WStaticBundle_v1 0x4833a0: every article still in the array. */
static int WStaticBundle_load(W3DObjectBundle *b, UData **arts, int n)
{
    b->count = 0;
    for (int i = 0; i < n; i++)
        if (arts[i]) b->count++;
    b->entries = entries_new(b->count);
    for (int i = 0, k = 0; i < n; i++)
        if (arts[i]) {
            b->entries[k++].article = arts[i];
            arts[i] = NULL;
        }
    b->vt->buildcache(b);
    b->draw = RDrawArticleBundle_new(b);          /* 0x450b10 */
    b->collide = PCollideArticleBundle_new(b);    /* 0x474b20 */
    return (int)b->count;
}

/* WMidGroundBundle_v1 0x47cba0: articles whose Base has 0x6000 set; without 0x2000 the entry is marked
   WENTRY_NOSOLID. */
static int WMidGroundBundle_load(W3DObjectBundle *b, UData **arts, int n)
{
    b->count = 0;
    for (int i = 0; i < n; i++)
        if (arts[i]) {
            const WBase *bs = article_base(arts[i]);
            if (bs->flags & WBASE_MIDGROUND) b->count++;
        }
    b->entries = entries_new(b->count);
    for (int i = 0, k = 0; i < n; i++) {
        if (!arts[i]) continue;
        const WBase *bs = article_base(arts[i]);
        if (!(bs->flags & WBASE_MIDGROUND)) continue;
        b->entries[k].article = arts[i];
        if (!(bs->flags & WBASE_MIDSOLID)) b->entries[k].flags |= WENTRY_NOSOLID;
        k++;
        arts[i] = NULL;
    }
    b->vt->buildcache(b);
    b->draw = RDrawArticleBundle_new(b);          /* 0x450b10 */
    b->collide = NULL;
    return (int)b->count;
}

/* WAnimBundle_v1 0x477780: articles with an 'Anqt' (quaternion animation) item; each gets its animation
   (0x4713f0, the scene's animation module). */
static int WAnimBundle_load(W3DObjectBundle *b, UData **arts, int n)
{
    const uint32_t ANQT = 0x416e7174;
    b->count = 0;
    for (int i = 0; i < n; i++)
        if (arts[i] && UData_find(arts[i], ANQT) != UData_itemsend(arts[i])) b->count++;
    b->entries = entries_new(b->count);
    for (int i = 0, k = 0; i < n; i++)
        if (arts[i] && UData_find(arts[i], ANQT) != UData_itemsend(arts[i])) {
            b->entries[k++].article = arts[i];
            arts[i] = NULL;
        }
    ((WAnimBundle *)b)->anims = WAnim_create(b);  /* 0x4713f0 per entry */
    b->vt->buildcache(b);
    b->draw = RDrawAnimArticleBundle_new(b);      /* 0x44fe30 */
    b->collide = NULL;
    return (int)b->count;
}

static void placed_init(WPlaced *p)
{
    memcpy(p->pos, vzero, sizeof p->pos);
    memcpy(p->rot, rot_identity, sizeof p->rot);
    p->type = 0;
    p->flag = 0;
}

/* The placement record of a prototype or an instance: the Base type and its object definition's byte. */
static void placed_type(WPlaced *p, const WBase *bs)
{
    p->type = bs->type;
    const WObjectDef *def = WObjectDefs_find(p->type);   /* 0x47ec50 */
    if (def) p->flag = def->triggerable;   /* +0x15 */
}

/* WSceneBundle_v1 0x482680: the prototypes ('tr' + Base 0x8000; copied, not taken), then room for the
   scene's instances: 64, or as many as the scene has geometry elements when it has a limit (+0x44
   clear). The instances start off the map (placeholders of type 'DFLT', 0x544c4644 as the bytes are read, at y = 9999), and the
   real ones are placed by the scene (0x482bb0). */
static int WSceneBundle_load(W3DObjectBundle *b, UData **arts, int n)
{
    WSceneBundle *s = (WSceneBundle *)b;
    load_consts();
    s->nproto = 0;
    b->count = 0;
    for (int i = 0; i < n; i++)
        if (arts[i] && article_tr(arts[i], 0) && (article_base(arts[i])->flags & WBASE_SCENE)) s->nproto++;
    s->protos = entries_new((uint32_t)s->nproto);
    s->proto = calloc(s->nproto ? (size_t)s->nproto : 1, sizeof(WPlaced));
    for (int i = 0; i < s->nproto; i++) placed_init(&s->proto[i]);
    for (int i = 0, k = 0; i < n; i++) {
        if (!arts[i] || !article_tr(arts[i], 0)) continue;
        const WBase *bs = article_base(arts[i]);
        if (!(bs->flags & WBASE_SCENE)) continue;
        s->protos[k].article = arts[i];
        placed_init(&s->proto[k]);
        placed_type(&s->proto[k], bs);
        k++;
    }
    b->count = 0x40;
    if (!s->unlimited) {
        uint32_t lim = Scene_CountGeom();         /* 0x46d910 */
        b->count = lim < 0x40 ? lim : 0x40;
    }
    b->entries = entries_new(b->count);
    s->placed = calloc(b->count ? b->count : 1, sizeof(WPlaced));
    for (uint32_t i = 0; i < b->count; i++) placed_init(&s->placed[i]);
    if (s->nproto < 1) b->count = 0;
    else {
        s->used = 0;
        float off[3] = {vzero[0], 9999.0f, vzero[2]};
        for (uint32_t i = 0; i < b->count; i++) WSceneBundle_place(s, 0x544c4644, off, rot_identity);   /* "DFLT" */
        s->used = 0;
    }
    b->vt->buildcache(b);
    b->draw = RDrawAnimArticleBundle_new(b);      /* 0x44fe30 */
    b->smack = NULL;
    b->collide = NULL;
    return (int)b->count;
}

/* 0x482bb0: the next instance slot gets the prototype of `type` (the first one if there is no such type)
   at pos with rot; its index, or -1 when the scene is full or has no prototypes. */
int WSceneBundle_place(WSceneBundle *s, uint32_t type, const float pos[3], const float rot[9])
{
    uint32_t i = s->used;
    if (!s->nproto) return -1;
    int p = WSceneBundle_findproto(s, type);
    if (p < 0) p = 0;
    if (i >= 0x40 || p >= s->nproto) return -1;
    s->a.base.entries[i] = s->protos[p];
    s->placed[i].type = s->proto[p].type;
    s->placed[i].flag = s->proto[p].flag;
    memcpy(s->placed[i].rot, rot, sizeof s->placed[i].rot);
    memcpy(s->placed[i].pos, pos, sizeof s->placed[i].pos);
    s->used++;
    return (int)i;
}

/* The world's scene bundle as the scene's geometry fills it (Scene_PlaceGeometry 0x46d120). */
static WSceneBundle *world_scene(void) { return g_world && g_world->scene ? (WSceneBundle *)g_world->scene : NULL; }

/* 0x482af0: the slots used back to DFLT placeholders far above (0, 9999, 0), none used. */
void WSceneBundle_Reset(void)
{
    WSceneBundle *s = world_scene();
    if (!s) return;
    uint32_t n = s->used;
    s->used = 0;
    if (0 < s->nproto) {
        float off[3] = {vzero[0], 9999.0f, vzero[2]};   /* 0x461c3c00 */
        for (uint32_t i = 0; i < n; i++) WSceneBundle_place(s, 0x544c4644, off, rot_identity);   /* 0x5bb2d0 */
    }
    s->used = 0;
}

void WSceneBundle_Add(uint32_t type, const float pos[3], const float mat[9])   /* 0x482f70 */
{
    WSceneBundle *s = world_scene();
    if (s) WSceneBundle_place(s, type, pos, mat);
}

/* 0x482fc0: the article caches refilled (slot 10); unless kept, its collision object (0x474b20) and the
   triggerable objects (+0x34) put 10000 above where they are, out of the way (0x482d80): a type 7
   trigger brings one back (WSceneBundle_Trigger); its knockable objects' state at +0x14 (0x475e10) made
   anew; the entries' flags from the draw and collision objects again (0x489220). */
/* 0x489220: each entry's flags ORed with bit 0 for a transformed bundle, its draw object's (slot 6) and
   its collision object's (slot 2: an article bundle's 0x474ad0, 0x100), and its own 0x400000 again. */
void W3DObjectBundle_EntryFlags(W3DObjectBundle *b)
{
    for (uint32_t i = 0; i < b->count; i++) {
        uint32_t *f = &b->entries[i].flags;
        if (b->transformed) *f |= 1;
        RDrawObject *d = b->draw;
        if (d) *f |= d->vt->flags(d, (int)i);
        if (b->collide) *f |= 0x100;
    }
}

void WSceneBundle_Update(bool keep)
{
    WSceneBundle *s = world_scene();
    if (!s) return;
    W3DObjectBundle *b = &s->a.base;
    b->vt->fillcache(b);
    W3DObjectBundle_EntryFlags(b);   /* 0x489220 */
    if (keep) return;
    WAnimatable_free(b->smack);
    b->smack = WAnimatable_new(b);   /* 0x482d80: 0x475e10 */
    if (!b->collide) b->collide = PCollideArticleBundle_new(b);   /* 0x474b20 */
    for (uint32_t i = 0; i < b->count; i++) {
        WPlaced *p = &s->placed[i];
        if (!p->flag) continue;
        if (8000.0f < p->pos[1] && 8000.0f < p->pos[1]) p->pos[1] -= 10000.0f;   /* 0x5b47d8, 0x5b2764 */
        if (p->pos[1] <= 8000.0f) p->pos[1] += 10000.0f;
    }
}

/* A type 7 trigger's geometry (0x482fa0 -> 0x482c80): every triggerable object put back above, then the
   one of this type at the element's x and z brought down. */
void WSceneBundle_Trigger(uint32_t type, const float pos[3])
{
    WSceneBundle *s = world_scene();
    if (!s) return;
    W3DObjectBundle *b = &s->a.base;
    for (uint32_t i = 0; i < b->count; i++) {
        WPlaced *p = &s->placed[i];
        if (!p->flag) continue;
        if (8000.0f < p->pos[1] && 8000.0f < p->pos[1]) p->pos[1] -= 10000.0f;
        if (p->pos[1] <= 8000.0f) p->pos[1] += 10000.0f;
    }
    for (uint32_t i = 0; i < b->count; i++) {
        WPlaced *p = &s->placed[i];
        if (p->pos[0] == pos[0] && p->pos[2] == pos[2] && p->type == type && p->flag && 8000.0f < p->pos[1])
            p->pos[1] -= 10000.0f;
    }
}

/* 0x4785d0: the prototype with this type, -1 if none. */
int WSceneBundle_findproto(const WSceneBundle *s, uint32_t type)
{
    for (int i = 0; i < s->nproto; i++)
        if (s->proto[i].type == type) return i;
    return -1;
}

/* WInstanceBundle_v1 0x478130: the placed objects ('tr' without Base 0x8000), which it takes; the
   prototypes ('tr' + 0x8000) it keeps a copy of, like the scene bundle. An instance's record has its Base
   type and the rotation and position of its 'tr' matrix; an instance with a running 'Anim' item (first
   word not 0) is placed by its animation and keeps the zero position and identity rotation. */
static int WInstanceBundle_load(W3DObjectBundle *b, UData **arts, int n)
{
    const uint32_t ANIM = 0x416e696d;
    WInstanceBundle *in = (WInstanceBundle *)b;
    load_consts();
    in->nproto = 0;
    b->count = 0;
    for (int i = 0; i < n; i++)
        if (arts[i] && article_tr(arts[i], 0)) {
            if (article_base(arts[i])->flags & WBASE_SCENE) in->nproto++;
            else b->count++;
        }
    in->protos = entries_new((uint32_t)in->nproto);
    in->proto = calloc(in->nproto ? (size_t)in->nproto : 1, sizeof(WPlaced));
    b->entries = entries_new(b->count);
    in->placed = calloc(b->count ? b->count : 1, sizeof(WPlaced));
    for (int i = 0; i < in->nproto; i++) placed_init(&in->proto[i]);
    for (uint32_t i = 0; i < b->count; i++) placed_init(&in->placed[i]);
    for (int i = 0, k = 0, kp = 0; i < n; i++) {
        if (!arts[i]) continue;
        const float *tr = article_tr(arts[i], 0);
        if (!tr) continue;
        const WBase *bs = article_base(arts[i]);
        if (bs->flags & WBASE_SCENE) {
            in->protos[kp].article = arts[i];
            placed_type(&in->proto[kp], bs);
            kp++;
            continue;
        }
        b->entries[k].article = arts[i];
        placed_type(&in->placed[k], bs);
        EaglMatrix m;
        memcpy(m.m, tr, sizeof m.m);
        EAGL_MatToRotTrans(&m, in->placed[k].rot, in->placed[k].pos);   /* 0x539ab0 */
        const uint32_t *anim = item_data(arts[i], UData_find(arts[i], ANIM));
        if (anim && anim[0]) {   /* (0x478130: the placing zeroed, the type kept) */
            memcpy(in->placed[k].pos, vzero, sizeof in->placed[k].pos);
            memcpy(in->placed[k].rot, rot_identity, sizeof in->placed[k].rot);
        }
        k++;
        arts[i] = NULL;
    }
    b->vt->buildcache(b);
    b->draw = RDrawAnimArticleBundle_new(b);      /* 0x44fe30 */
    b->smack = WAnimatable_new(b);                /* 0x475d80 */
    b->collide = PCollideArticleBundle_new(b);    /* 0x474b20 */
    return (int)b->count;
}

/* The entry's matrix: the animated bundle's from its animation (WAnimBundle_v5 0x477740, the identity
   without one), the scene bundle's from its knockable object's motion over its placing, or the placing
   (WSceneBundle_v5 0x4825b0). */
static void anim_matrix(W3DObjectBundle *b, int entry, float out[16])
{
    WAnimEntry *e = ((WAnimBundle *)b)->anims;
    EaglMatrix m;
    if (e && e[entry].anim) QuatAnim_Matrix(e[entry].anim, &m);
    else EAGL_MatIdentity(&m);
    memcpy(out, m.m, sizeof m.m);
}

static void scene_matrix(W3DObjectBundle *b, int entry, float out[16])
{
    EaglMatrix m;
    if (b->smack && WAnimatable_HasAnim(b->smack, entry, 0)) {   /* 0x476730 (the first piece's) */
        WAnimatable_Matrix(b->smack, entry, -1, false, &m);       /* 0x476970 (the current record's) */
        memcpy(out, m.m, sizeof m.m);
        return;
    }
    const WPlaced *p = &((WSceneBundle *)b)->placed[entry];
    EAGL_MatFromRotTrans(&m, p->rot, p->pos);
    memcpy(out, m.m, sizeof m.m);
}

/* WInstanceBundle_v5 0x478050: an object with a body's matrix (its first piece's motion over its placing,
   0x476970), else its placing's. */
static void instance_matrix(W3DObjectBundle *b, int entry, float out[16])
{
    EaglMatrix m;
    if (b->smack && WAnimatable_HasAnim(b->smack, entry, 0)) {   /* 0x476730 (the first piece's) */
        WAnimatable_Matrix(b->smack, entry, -1, false, &m);       /* 0x476970 (the current record's) */
        memcpy(out, m.m, sizeof m.m);
        return;
    }
    const WPlaced *p = &((WInstanceBundle *)b)->placed[entry];
    EAGL_MatFromRotTrans(&m, p->rot, p->pos);   /* 0x539a40 */
    memcpy(out, m.m, sizeof m.m);
}

static const W3DObjectBundleVtbl vt_scene = VT5(WSceneBundle_load, instance_sphere, scene_matrix);         /* 0x5b47e0 */
static const W3DObjectBundleVtbl vt_instance = VT5(WInstanceBundle_load, instance_sphere, instance_matrix); /* 0x5b4718 */
static const W3DObjectBundleVtbl vt_anim = VT5(WAnimBundle_load, article_sphere, anim_matrix);              /* 0x5b46b8 */
static const W3DObjectBundleVtbl vt_midground = VT6(WMidGroundBundle_load, midground_corners, midground_sphere, NULL);   /* 0x5b477c */
static const W3DObjectBundleVtbl vt_static = VT(WStaticBundle_load);          /* 0x5b4814 */
static const W3DObjectBundleVtbl vt_article = VT(WArticleBundle_load);        /* 0x5b46e8 */

static W3DObjectBundle *article_bundle(size_t size, const W3DObjectBundleVtbl *vt, void *world)
{
    W3DObjectBundle *b = calloc(1, size);
    if (!b) return NULL;
    W3DObjectBundle_ctor(b, 0x41727469, world);   /* 'Arti'; WArticleBundle ctor 0x477b30 */
    ((WArticleBundle *)b)->cache = NULL;
    b->vt = vt;
    return b;
}

W3DObjectBundle *WSceneBundle_new(void *world)   /* 0x482550: transformed */
{
    W3DObjectBundle *b = article_bundle(sizeof(WSceneBundle), &vt_scene, world);
    if (b) b->transformed = b->b5 = 1;
    return b;
}
W3DObjectBundle *WInstanceBundle_new(void *world)
{
    /* 0x477fd0: transformed and moving; its load makes the knockable objects' state (+0x14) */
    W3DObjectBundle *b = article_bundle(sizeof(WInstanceBundle), &vt_instance, world);
    if (b) b->transformed = b->b5 = 1;
    return b;
}
W3DObjectBundle *WAnimBundle_new(void *world)   /* 0x4776a0: transformed */
{
    W3DObjectBundle *b = article_bundle(sizeof(WAnimBundle), &vt_anim, world);
    if (b) b->transformed = b->b5 = 1;
    return b;
}
W3DObjectBundle *WMidGroundBundle_new(void *world) { return article_bundle(sizeof(WMidGroundBundle), &vt_midground, world); }
W3DObjectBundle *WStaticBundle_new(void *world) { return article_bundle(sizeof(WStaticBundle), &vt_static, world); }
W3DObjectBundle *WArticleBundle_new(void *world) { return article_bundle(sizeof(WArticleBundle), &vt_article, world); }

/* The box's eight corners, x, y, z, 1 from the least corner, x up, then y, back in x, then the same at
   the greatest z. */
static void box_corners(const float mn[3], const float mx[3], float c[8][4])
{
    static const uint8_t hi[8] = {0, 1, 3, 2, 4, 5, 7, 6};   /* bit 0 x, bit 1 y, bit 2 z */
    for (int k = 0; k < 8; k++) {
        c[k][0] = hi[k] & 1 ? mx[0] : mn[0];
        c[k][1] = hi[k] & 2 ? mx[1] : mn[1];
        c[k][2] = hi[k] & 4 ? mx[2] : mn[2];
        c[k][3] = 1.0f;
    }
}

/* Slot 2 (0x4892d0; the line bundle's too, over its cell box): the box (slot 4) as corners, moved by the
   entry's matrix (slot 5) for the transformed bundles. */
void W3DObjectBundle_corners(W3DObjectBundle *b, int i, float c[8][4])
{
    float mn[3], mx[3];
    b->vt->box(b, i, mn, mx);
    box_corners(mn, mx, c);
    if (b->transformed && b->vt->matrix) {
        EaglMatrix m;
        b->vt->matrix(b, i, m.m);
        EAGL_MatTransform4(&m, 8, &c[0][0], 16, &c[0][0], 16);   /* 0x539cc0 */
    }
}

/* WMidGroundBundle_v2 0x47ced0: the box spread from its centre by 2.25 (0x5b4774). */
static void midground_corners(W3DObjectBundle *b, int i, float c[8][4])
{
    float mn[3], mx[3];
    b->vt->box(b, i, mn, mx);
    float k = exe_f32(0x5b4774);
    for (int j = 0; j < 3; j++) {
        float m = (mn[j] + mx[j]) * 0.5f;   /* (0x5b2778, a double half) */
        float lo = (mn[j] - m) * k + m, hi = (mx[j] - m) * k + m;
        mn[j] = lo < hi ? lo : hi;
        mx[j] = lo < hi ? hi : lo;
    }
    box_corners(mn, mx, c);
}

void W3DObjectBundle_nop(W3DObjectBundle *b) { (void)b; }
int W3DObjectBundle_zero(W3DObjectBundle *b) { (void)b; return 0; }
void W3DObjectBundle_sphere2(W3DObjectBundle *b, int i, float out[4])
{
    float s[4];
    b->vt->sphere(b, i, s);
    memcpy(out, s, sizeof s);
}

void W3DObjectBundle_delete(W3DObjectBundle *b)
{
    if (!b) return;
    if (b->vt == &vt_scene) {
        WSceneBundle *s = (WSceneBundle *)b;
        WAnimatable_free(b->smack);
        free(s->protos);
        free(s->proto);
        free(s->placed);
    } else if (b->vt == &vt_instance) {
        WInstanceBundle *in = (WInstanceBundle *)b;
        free(in->protos);
        free(in->proto);
        WAnimatable_free(b->smack);
        free(in->placed);
    } else if (b->vt == &vt_anim) {
        WAnim_free(((WAnimBundle *)b)->anims, b->count);   /* (WAnimBundle's dtor 0x4776d0) */
    }
    free(b->place);
    b->vt->dtor(b, 1);
}

/* ------------------------------------------------------------------ visibility walls */

/* 0x4872a0 */
void WVisWall_set(WVisWall *w, const float p0[3], const float p1[3])
{
    load_consts();
    w->high = p0[1] != f_zero;
    w->x = p0[0];
    w->z = p0[2];
    w->dx = p1[0] - p0[0];
    float dz = p1[2] - p0[2];
    w->dz = dz;
    float dx2 = w->dx * w->dx;
    w->len2 = dz * dz + dx2;
    w->inv_len2 = f_one / (w->dz * w->dz + dx2);
}

/* 0x489bf0: the two parameters of the crossing must both be in [0, 1]. */
bool World_SegmentsCross(const float p[6], const float q[6], float at[3])
{
    load_consts();
    float den = (p[3] - p[0]) * (q[5] - q[2]) - (q[3] - q[0]) * (p[5] - p[2]);
    if (den == f_zero) return false;
    float t = ((p[2] - q[2]) * (q[3] - q[0]) - (p[0] - q[0]) * (q[5] - q[2])) / den;
    if (!(f_zero <= t && t <= f_one)) return false;
    float u = ((p[2] - q[2]) * (p[3] - p[0]) - (p[0] - q[0]) * (p[5] - p[2])) / den;
    if (!(f_zero <= u && u <= f_one)) return false;
    if (at) {
        float d[3] = {p[3] - p[0], p[4] - p[1], p[5] - p[2]};   /* 0x5320a0: end - start */
        at[0] = d[0] * t + p[0];
        at[1] = d[1] * t + p[1];
        at[2] = d[2] * t + p[2];
    }
    return true;
}

/* 0x488ac0. The sight line is a to a + (b - a) in the ground plane; a wall is {x, z} to {x + dx, z + dz}.
   Without `high` the wall is the first segment and any crossing blocks; with it the sight line is first
   and only walls with `high` set block. */
/* 0x488a70: each wall off (0x487320) when it keeps outside the circle of r2 squared about (x, z): its
   nearest point's distance squared, along the wall past an end by the wall's length, more than r2. */
void WVis_Cull(const WVisWall *walls, uint8_t *disabled, uint32_t n, float x, float z, float r2)
{
    float one = exe_f32(0x5b24a8), zero = exe_f32(0x5b23f8);
    for (uint32_t i = 0; i < n; i++) {
        const WVisWall *w = &walls[i];
        float px = x - w->x, pz = z - w->z;
        float t = (px * w->dx + pz * w->dz) * w->inv_len2;
        px -= t * w->dx;
        pz -= t * w->dz;
        float along;
        if (t < one) along = t < zero ? t * t * w->len2 : zero;
        else along = (t - one) * (t - one) * w->len2;
        disabled[i] = !(px * px + pz * pz + along <= r2);
    }
}

bool WVis_blocked(const WVisWall *walls, const uint8_t *disabled, uint32_t n, const float a[3], const float b[3], bool high)
{
    float ex = (b[0] - a[0]) + a[0], ez = (b[2] - a[2]) + a[2];
    for (uint32_t i = 0; i < n; i++) {
        if (disabled[i]) continue;
        const WVisWall *w = &walls[i];
        float wall[6] = {w->x, 0, w->z, w->dx + w->x, 0, w->dz + w->z};
        float sight[6] = {a[0], 0, a[2], ex, 0, ez};
        if (!high) {
            if (World_SegmentsCross(wall, sight, NULL)) return true;
        } else if (World_SegmentsCross(sight, wall, NULL) && w->high) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ the visibility pass */


void World_VisitObject(W3DObjectBundle *b, int index, uint32_t flags, EaglView *view, const float eye[3], float viewdist2,
                       const WVisWall *walls, const uint8_t *disabled, uint32_t nwalls)
{
    RDrawBundle *d = b->draw;
    if (!d || d->visited[index]) return;
    d->visited[index] = 1;
    float s[4];
    d->base.vt->sphere(&d->base, index, s);
    if (!EAGL_ViewSphereVisible(view, s, s[3])) return;
    bool far = (flags & 0x400020) != 0;
    if (walls && WVis_blocked(walls, disabled, nwalls, s, eye, far)) return;
    float dx = s[0] - eye[0], dy = s[1] - eye[1], dz = s[2] - eye[2];
    float d2 = dx * dx + dy * dy + dz * dz;
    if (!far) {
        /* The square root by halving the float's bits and rebiasing ((i >> 1) + 0x1fc00000), less the
           radius, at least 0x5b23f8 (0): the distance to the nearest point, compared squared. */
        int32_t i;
        memcpy(&i, &d2, 4);
        i = (i >> 1) + 0x1fc00000;
        float r;
        memcpy(&r, &i, 4);
        r -= s[3];
        float lo = exe_f32(0x5b23f8);
        if (r < lo) r = lo;
        if (viewdist2 <= r * r) return;
        d->base.vt->queue(&d->base, index, r * r);
    } else if (flags & 0x400000) {
        if (exe_f32(0x5b47a8) <= d2) return;
        d->base.vt->queue(&d->base, index, d2);
    } else if (d2 < exe_f32(0x5b47ac))
        d->base.vt->queue(&d->base, index, d2);
}

/* The world's restart (0x471ac0 -> 0x487d90): each bundle's knockable objects reset (0x476100), the
   scene's triggerable objects put back above (0x482b60). */
void World_Restart(void)
{
    if (!g_world) return;
    for (int i = 0; i < g_world->nbundles; i++)
        if (g_world->bundles[i]->smack) WAnimatable_Restart(g_world->bundles[i]->smack);
    WSceneBundle *s = world_scene();
    if (!s) return;
    for (uint32_t i = 0; i < s->a.base.count; i++) {
        WPlaced *p = &s->placed[i];
        if (p->flag && p->pos[1] <= 8000.0f) p->pos[1] += 10000.0f;   /* 0x5b47d8, 0x5b2764 */
    }
}
