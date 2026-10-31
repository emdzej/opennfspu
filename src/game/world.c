/* The track world's bundles and visibility walls (see world.h). */
#include "game/world.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "game/track_pending.h"
#include "game/wtree.h"
#include "render/rdraw.h"
#include <stdlib.h>
#include <string.h>

/* Constants the world code reads from the exe's data: the zero vector 0x5bb2b0, the identity rotation
   0x5bb2d0, 0.0 at 0x5b23f8 and 1.0 at 0x5b24a8. */
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
static void W3DObjectBundle_ctor(W3DObjectBundle *b, uint32_t kind, void *world)
{
    b->transformed = 0;
    b->b5 = 0;
    b->draw = NULL;
    b->collide = NULL;
    b->f10 = b->f14 = 0;
    b->count = 0;
    b->entries = NULL;
    b->f20 = 0;
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

/* 0x477d40. The transformed case (entries with their own matrix) moves the centre by the entry's matrix
   and grows the radius by the matrix's scale; no track bundle sets `transformed`. */
void WArticleBundle_sphere(WArticleBundle *a, int i, float out[4])
{
    const WBase *bs = WArticleBundle_base(a, i);
    out[0] = bs->centre[0];
    out[1] = bs->centre[1];
    out[2] = bs->centre[2];
    out[3] = bs->radius;
    if (a->base.transformed) PENDING_NOTE(0x477d40, "WArticleBundle_sphere (transformed entries)");
}

/* 0x477ed0 */
void WArticleBundle_box(WArticleBundle *a, int i, float bmin[3], float bmax[3])
{
    const WBase *bs = WArticleBundle_base(a, i);
    memcpy(bmin, bs->bmin, sizeof bs->bmin);
    memcpy(bmax, bs->bmax, sizeof bs->bmax);
}

/* The slots the world code doesn't call yet (2-8: the drawing and query methods). */
static void slot_pending(void) { PENDING_NOTE(0x4892d0, "W3DObjectBundle slots 2-8"); }

#define VT(load_fn) { WArticleBundle_dtor, load_fn, slot_pending, slot_pending, slot_pending, slot_pending, \
                      slot_pending, slot_pending, slot_pending, WArticleBundle_buildcache, WArticleBundle_fillcache }

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
    b->f14 = 0;
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
        if (anim && anim[0]) placed_init(&in->placed[k]);
        k++;
        arts[i] = NULL;
    }
    b->vt->buildcache(b);
    b->draw = RDrawArticleBundle_new(b);          /* 0x450b10 */
    b->collide = PCollideArticleBundle_new(b);    /* 0x474b20 */
    return (int)b->count;
}

static const W3DObjectBundleVtbl vt_scene = VT(WSceneBundle_load);            /* 0x5b47e0 */
static const W3DObjectBundleVtbl vt_instance = VT(WInstanceBundle_load);      /* 0x5b4718 */
static const W3DObjectBundleVtbl vt_anim = VT(WAnimBundle_load);              /* 0x5b46b8 */
static const W3DObjectBundleVtbl vt_midground = VT(WMidGroundBundle_load);    /* 0x5b477c */
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

W3DObjectBundle *WSceneBundle_new(void *world) { return article_bundle(sizeof(WSceneBundle), &vt_scene, world); }
W3DObjectBundle *WInstanceBundle_new(void *world) { return article_bundle(sizeof(WInstanceBundle), &vt_instance, world); }
W3DObjectBundle *WAnimBundle_new(void *world) { return article_bundle(sizeof(WAnimBundle), &vt_anim, world); }
W3DObjectBundle *WMidGroundBundle_new(void *world) { return article_bundle(sizeof(WMidGroundBundle), &vt_midground, world); }
W3DObjectBundle *WStaticBundle_new(void *world) { return article_bundle(sizeof(WStaticBundle), &vt_static, world); }
W3DObjectBundle *WArticleBundle_new(void *world) { return article_bundle(sizeof(WArticleBundle), &vt_article, world); }

void W3DObjectBundle_delete(W3DObjectBundle *b)
{
    if (!b) return;
    if (b->vt == &vt_scene) {
        WSceneBundle *s = (WSceneBundle *)b;
        free(s->protos);
        free(s->proto);
        free(s->placed);
    } else if (b->vt == &vt_instance) {
        WInstanceBundle *in = (WInstanceBundle *)b;
        free(in->protos);
        free(in->proto);
        free(in->placed);
    }
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

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

void World_VisitObject(W3DObjectBundle *b, int index, EaglView *view, const float eye[3], float viewdist2,
                       const WVisWall *walls, const uint8_t *disabled, uint32_t nwalls)
{
    RDrawBundle *d = b->draw;
    if (!d || d->visited[index]) return;
    d->visited[index] = 1;
    float s[4];
    d->base.vt->sphere(&d->base, index, s);
    if (!EAGL_ViewSphereVisible(view, s, s[3])) return;
    uint32_t flags = b->entries[index].flags;
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
