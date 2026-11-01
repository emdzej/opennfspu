/* RDrawArticleBundle (vtable 0x5b3cf0) and its base RDrawBundle (0x44fca0); see rdraw.h. */
#include "render/rdraw.h"
#include "game/smack.h"
#include "game/skid.h"
#include "render/sprite.h"
#include "render/particles.h"
#include "render/dynlight.h"
#include "exe.h"
#include "game/track.h"
#include "game/world.h"
#include "pending.h"
#include "render/r3dcar.h"
#include "render/render.h"
#include "render/rmaterial.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RDrawBundle *g_rdraw_list;
uint32_t g_rdraw_key;

static uint32_t kind_id(int kind) { return exe_u32(0x5e5084 + 4u * (unsigned)kind); }

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* The entry's article cache as the part the item lookups take: the bundle's caches are 0x40 bytes with
   the part's first fields (article, end of its items, seven {record, id} slots), which is how the
   original passes them to R3DPart_Item and the material code. The port copies them into a part and back,
   since its part is longer. */
static WArticleCache *entry_cache(RDrawBundle *d, int index)   /* 0x477bc0 */
{
    return &((WArticleBundle *)d->bundle)->cache[index];
}

static void part_from(R3DPart *p, const WArticleCache *c)
{
    memset(p, 0, sizeof *p);
    memcpy(p, c, sizeof *c);
}

static void part_back(WArticleCache *c, const R3DPart *p) { memcpy(c, p, sizeof *c); }

/* x87 fistp in the default mode: round to nearest, ties to even. */
static uint32_t fround(float v) { return (uint32_t)(int64_t)rintf(v); }

/* The current record of a kind's slot (0x451070 with index -1). */
static UData *slot_rec(const R3DPart *p, int kind) { return p->cache[kind].rec; }

/* The first item of a kind with an index in [lo, hi) into the slot; its data, NULL if none. 0x4510d0 */
static const uint32_t *first_item(R3DPart *p, int kind, uint32_t lo, uint32_t hi)
{
    UData *it = UData_finditem(p->article, kind_id(kind), lo, hi);
    p->cache[kind].rec = it;
    p->cache[kind].id = it != p->end ? it->id : 0;
    return it != p->end ? UData_data(it) : NULL;
}

/* The next record while it is the same kind with an index below hi, else the end. 0x450fc0 */
static const uint32_t *next_item(R3DPart *p, int kind, uint32_t hi)
{
    UData *it = p->cache[kind].rec;
    if (it == p->end) return NULL;
    it++;
    uint32_t key = kind_id(kind);
    if (it != p->end && UData_key(it) == key && (it->info & UDATA_INDEXED ? it->id & 0xffff : 0xffffffffu) < hi) {
        p->cache[kind].rec = it;
        p->cache[kind].id = it->id;
        return UData_data(it);
    }
    p->cache[kind].rec = p->end;
    p->cache[kind].id = 0;
    return NULL;
}

/* ---- the base's methods ---- */

static void bundle_dtor(RDrawObject *o, int free_self)   /* 0x450b30 (0x44fe60 for the base) */
{
    RDrawBundle *d = (RDrawBundle *)o, **pp = &g_rdraw_list;
    while (*pp && *pp != d) pp = &(*pp)->next;
    if (*pp) *pp = d->next;
    free(d->visited);
    if (free_self) free(d);
}

static void bundle_s4(RDrawObject *o) {}   /* 0x44fe50 */

/* The world bundle's sphere of the entry (its slot 3). 0x451170 */
static void bundle_sphere(RDrawObject *o, int index, float out[4])
{
    RDrawBundle *d = (RDrawBundle *)o;
    /* Slot 3 of the article and animated bundles is 0x477d40 (the entry's matrix when transformed); the
       instance and scene bundles' 0x478630 and the mid-ground's 0x47cdc0 aren't ported apart from it. */
    WArticleBundle_sphere((WArticleBundle *)d->bundle, index, out);
}

/* The entry's draw flags from its Base flags. 0x4511b0 */
static uint32_t bundle_flags(RDrawObject *o, int index)
{
    RDrawBundle *d = (RDrawBundle *)o;
    R3DPart p;
    part_from(&p, entry_cache(d, index));
    const uint8_t *base = R3DPart_Item(&p, R3D_BASE, 0xffffffffu);
    part_back(entry_cache(d, index), &p);
    uint32_t f = base ? rd32(base) : 0, r = 2;
    if (f & 1) r = 10;
    if (f & 2) r |= 0x40;
    if (f & 0x40) r |= 0x200;
    if (f & 0x10) r |= 4;
    if (f & 8) r |= 0x10;
    if (f & 0x800) r |= 0x80;
    if (f & 0x1000) r |= 0x400;
    return r;
}

/* ---- RDrawArticleBundle ---- */

/* The level of detail for the distance and the queued lists. 0x450cb0 */
static void article_queue(RDrawObject *o, int index, float dist2)
{
    RDrawBundle *d = (RDrawBundle *)o;
    WArticleCache *c = entry_cache(d, index);
    R3DPart p;
    part_from(&p, c);
    const uint8_t *base = R3DPart_Item(&p, R3D_BASE, 0xffffffffu);
    if (!base) goto done;
    uint32_t nlod = rd32(base + 0xc), lod = 0;
    if (g_render_skiplod0 && nlod > 1 && !(rd32(base) & 0x10000)) lod = 1;
    if (lod >= nlod) goto done;
    /* The first level whose distance (u16 at +0xa of each 12-byte entry from +0x64) squared exceeds the
       squared distance, as an integer (__ftol) compared in floats. */
    float d2 = (float)(int32_t)dist2;
    while ((float)rd16(base + 0x64 + 12 * lod + 0xa) * (float)rd16(base + 0x64 + 12 * lod + 0xa) <= d2)
        if (++lod >= nlod) goto done;
    const uint8_t *e = base + 0x64 + 12 * lod;
    RListEntry q = {NULL, o, 0, (uint16_t)index, fround(dist2)};
    const RCamera *cam = g_render ? g_render->camera : NULL;
    /* The camera's point at +0x34 (kept by the camera behaviours, not ported: the eye until then). */
    const float *cp = cam ? cam->frame.eye : NULL;
    if (e[0]) {
        uint32_t lo = rd16(e + 4), hi = rd16(e + 6);
        for (const uint32_t *pr = first_item(&p, R3D_PR, lo, hi); pr; pr = next_item(&p, R3D_PR, hi)) {
            q.item = slot_rec(&p, R3D_PR);
            float dx = 0, dy = 0, dz = 0;
            if (cp) {
                float cx, cy, cz;
                memcpy(&cx, pr + 2, 4);
                memcpy(&cy, pr + 3, 4);
                memcpy(&cz, pr + 4, 4);
                dx = cx - cp[0];
                dy = cy - cp[1];
                dz = cz - cp[2];
            }
            q.key = fround(dx * dx + dy * dy + dz * dz);
            uint32_t f = pr[0];
            if (f & 0x20000) {
                q.key += pr[1] & 0xffff0000u;
                RLists_Insert(RLIST_PRIORITY, &q);
            } else if (f & 0x10000)
                RLists_Insert(RLIST_OPAQUE, &q);
            else if (f & 0x100000)
                RLists_Insert(RLIST_TRANSLUCENT, &q);
            else if ((f & 0x40000) && g_render_extras)
                RLists_Insert(RLIST_OPAQUE, &q);
        }
    }
    if (e[1]) {
        for (const uint32_t *ef = first_item(&p, R3D_EF, 0, 0xffff); ef; ef = next_item(&p, R3D_EF, 0xffff)) {
            q.item = slot_rec(&p, R3D_EF);
            q.key = fround(dist2);
            RLists_Insert(RLIST_EFFECTS, &q);   /* 0x466a20 */
        }
    }
done:
    part_back(c, &p);
}

/* A queued primitive list through its material. 0x450b70 */
static void article_draw(RDrawObject *o, int index, const RListEntry *e)
{
    RDrawBundle *d = (RDrawBundle *)o;
    WArticleCache *c = entry_cache(d, index);
    UData *rec = e->item;
    const uint32_t *pr = UData_data(rec);
    /* The world context (+0x2c of the bundle) is the track renderer, whose first field is the material
       array. */
    R3DMaterialSet *ms = d->bundle->world;
    uint32_t mi = pr[1] & 0xffff;
    R3DMaterial *mat = ms && mi < ms->count ? ms->mat[mi] : NULL;
    g_rdraw_key = e->key;
    R3DPart p;
    part_from(&p, c);
    R3DMaterial_DrawList(mat, &p, g_world ? g_world->root : NULL, pr, rec->count);
    part_back(c, &p);
    if (mat) mat->override = NULL;   /* +0x0c */
}

/* An article's effect ('ef'): road markers (2), particle emitters (3), lights (4). 0x450be0 */
static void article_special(RDrawObject *o, int index, const RListEntry *e)
{
    const uint32_t *ef = UData_data((UData *)e->item);
    switch (ef ? ef[0] : 0) {
    case 2: Sprite_DrawGlow((const float *)(ef + 2)); break;   /* 0x451810 */
    case 3: Particles_Ask((const PartEmitter *)(ef + 2), (float)e->key); break;   /* 0x455020 */
    case 4:
        if (g_render_extras) {
            R3DMaterialSet *ms = ((RDrawBundle *)o)->bundle->world;
            Texture *t = ms ? RTexBank_find(&ms->bank, ef[6], ef[7]) : NULL;   /* 0x44af40 */
            DynLights_Add((const DynLightRec *)(ef + 2), t);                   /* 0x455f60 */
        }
        break;
    }
}

static const RDrawVtbl article_vtbl = {bundle_dtor, article_queue, article_draw, article_special,
                                       bundle_s4, bundle_sphere, bundle_flags};

/* ---- RDrawAnimArticleBundle ---- */

/* Visible (0x44fe90): the entry whole into the opaque list by its distance (no item: the draw takes its
   lists itself). */
static void anim_queue(RDrawObject *o, int index, float dist2)
{
    RListEntry q = {NULL, o, 1, (uint16_t)index, fround(dist2)};
    RLists_Insert(RLIST_OPAQUE, &q);   /* 0x466950 */
}

/* The level of detail of the article at the squared distance (as article_queue), NULL past them all. */
static const uint8_t *lod_entry(const uint8_t *base, uint32_t key)
{
    uint32_t nlod = rd32(base + 0xc);
    float d2 = (float)key;
    for (uint32_t lod = 0; lod < nlod; lod++) {
        float r = (float)rd16(base + 0x64 + 12 * lod + 0xa);
        if (d2 < r * r) return base + 0x64 + 12 * lod;
    }
    return NULL;
}

/* An article under a matrix (0x450250): its level's primitive lists, each through its material with the
   colour stream 'Am' overridden to the colour (white but for a lit knockable object: the light at it;
   the vertex stream 'vt' too: with no vertex animation, the article's own); its markers ('ef' type 2)
   pending. */
static void draw_article(RDrawBundle *d, WArticleCache *c, const RListEntry *e, const EaglMatrix *m, uint32_t colour)
{
    R3DPart p;
    part_from(&p, c);
    const uint8_t *base = R3DPart_Item(&p, R3D_BASE, 0xffffffffu);
    const uint8_t *lod = base ? lod_entry(base, e->key) : NULL;
    if (lod && g_eagl && g_eagl->view) {
        EaglMatStack *st = &g_eagl->view->model;
        EAGL_StackPush(st);
        EAGL_MatPreMul(EAGL_StackTopForWrite(st), m);
        R3DMaterialSet *ms = d->bundle->world;
        if (lod[0]) {
            uint32_t lo = rd16(lod + 4), hi = rd16(lod + 6);
            for (const uint32_t *pr = first_item(&p, R3D_PR, lo, hi); pr; pr = next_item(&p, R3D_PR, hi)) {
                UData *rec = slot_rec(&p, R3D_PR);
                uint32_t mi = pr[1] & 0xffff;
                R3DMaterial *mat = ms && mi < ms->count ? ms->mat[mi] : NULL;
                if (!mat || !mat->method) continue;
                static int32_t ov[256];
                static uint32_t am;
                am = colour;
                R3DMaterial_SetOverride(mat, mat->method->nids <= 256 ? ov : NULL);
                if (mat->override) R3DMaterial_OverrideStream(mat, 0x416d0001u, &am, 4);   /* 'Am' */
                R3DMaterial_DrawList(mat, &p, g_world ? g_world->root : NULL, pr, rec->count);
                mat->override = NULL;   /* +0x0c */
            }
        }
        if (lod[1]) PENDING_NOTE(0x451810, "the animated articles' markers ('ef' type 2)");
        EAGL_StackPop(st);
    }
    part_back(c, &p);
}

/* Its draw (0x44fee0): nothing without a level of the entry's article at the queued distance; then the
   entry's matrix (the bundle's slot 5) and its article, or with the bundle's knockable state (+0x14) each
   live piece (one at least) under its own animation's matrix (else slot 5), its article (a canned
   piece's prototype; a local item cache, as 0x450250 makes) lit by the entry's colour. */
static void anim_draw(RDrawObject *o, int index, const RListEntry *e)
{
    RDrawBundle *d = (RDrawBundle *)o;
    g_rdraw_key = e->key;
    if (!d->bundle->vt->matrix) return;
    WAnimatable *a = d->bundle->smack;
    EaglMatrix m;
    if (!a) {
        d->bundle->vt->matrix(d->bundle, index, m.m);
        draw_article(d, entry_cache(d, index), e, &m, 0xffffffffu);
        return;
    }
    const WSmackEntry *se = &a->e[index];
    int n = WAnimatable_Pieces(a, index);   /* 0x450220 */
    for (int k = 0; k < n && se->pieces && k < se->npieces; k++) {
        const WSmackPiece *pc = &se->pieces[k];
        if (WAnimatable_HasAnim(a, index, k)) WAnimatable_Matrix(a, index, k, false, &m);   /* 0x476970 */
        else d->bundle->vt->matrix(d->bundle, index, m.m);
        if (!pc->article) continue;
        if (pc->art3) {
            /* the vertex animation of art3's 'Anim' frames (its second word, the frames' count) */
            UData *r = UData_find(pc->art3, 0x416e696du);
            if (r != UData_itemsend(pc->art3) && ((const uint32_t *)UData_data(r))[1])
                PENDING_NOTE(0x450250, "the canned pieces' vertex animation (their art3's frames)");
        }
        WArticleCache c = {pc->article, UData_itemsend(pc->article), {{0}}};
        draw_article(d, &c, e, &m, se->colour);
    }
    if (se->sounded) PENDING_NOTE(0x450850, "the knocked objects' markers");
}

static void anim_special(RDrawObject *o, int index, const RListEntry *e) { (void)o, (void)index, (void)e; }   /* 0x44fe50 */

static const RDrawVtbl anim_vtbl = {bundle_dtor, anim_queue, anim_draw, anim_special, bundle_s4, bundle_sphere, bundle_flags};

/* The base (0x44fca0): the bundle, the visited marks (one per entry, 100 when the count is 10000 or
   more), the link into the list of draw objects. */
static RDrawBundle *bundle_ctor(RDrawBundle *d, W3DObjectBundle *b)
{
    d->bundle = b;
    uint32_t n = b->count > 9999 ? 100 : b->count;
    d->visited = calloc(n ? n : 1, 1);
    d->next = g_rdraw_list;
    g_rdraw_list = d;
    return d;
}

RDrawBundle *RDrawArticleBundle_ctor(RDrawBundle *d, W3DObjectBundle *b)
{
    bundle_ctor(d, b);
    d->bundle2 = b;
    d->base.vt = &article_vtbl;
    return d;
}

static void line_draw(RDrawObject *o, int index, const RListEntry *e)
{
    RDrawBundle *d = (RDrawBundle *)o;
    g_rdraw_key = e->key;
    WLineBundle_DrawCell(d->bundle, index);
}

static void line_sphere(RDrawObject *o, int index, float out[4]) { WLineBundle_Sphere(((RDrawBundle *)o)->bundle, index, out); }
static uint32_t line_flags(RDrawObject *o, int index) { (void)o, (void)index; return 0xe; }

static const RDrawVtbl line_vtbl = {bundle_dtor, anim_queue, line_draw, anim_special, bundle_s4, line_sphere, line_flags};   /* 0x5b3d28 */

RDrawBundle *RDrawLineBundle_ctor(RDrawBundle *d, W3DObjectBundle *b)   /* 0x4512a0 */
{
    bundle_ctor(d, b);
    d->bundle2 = b;
    d->base.vt = &line_vtbl;
    return d;
}

RDrawBundle *RDrawAnimArticleBundle_ctor(RDrawBundle *d, W3DObjectBundle *b)   /* 0x44fe30 */
{
    bundle_ctor(d, b);
    d->base.vt = &anim_vtbl;
    return d;
}

void RDraw_ClearVisited(void)
{
    for (RDrawBundle *d = g_rdraw_list; d; d = d->next) {
        uint32_t n = d->bundle->count > 9999 ? 100 : d->bundle->count;
        memset(d->visited, 0, n);
    }
}
