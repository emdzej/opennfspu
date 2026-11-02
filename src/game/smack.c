/* The knockable objects: see smack.h. */
#include "game/smack.h"
#include "exe.h"
#include "game/objdefs.h"
#include "game/world.h"
#include "game/wtree.h"
#include "game/audio.h"
#include "game/anim.h"
#include "pending.h"
#include "render/r3dcar.h"   /* EAGL_MatAxisAngle */
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern int32_t g_sim_tick;   /* (the clock of 0x628938) */

static float f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static const float zero3[3] = {0, 0, 0};                     /* 0x5bb2b0 */
static const float ident9[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   /* 0x5bb2d0 */

/* The article's Base and the vertices of its collision level of detail (Base +4's record, its vertex
   list's index at +8; 16 bytes a vertex). */
static const uint8_t *article_base(UData *a)
{
    UData *end = UData_itemsend(a), *rb = UData_find(a, exe_u32(0x5e5084 + 16));
    return rb == end ? NULL : UData_data(rb);
}

static const float *collision_verts(UData *a, const uint8_t *base, uint32_t *n)
{
    *n = 0;
    const uint8_t *e = base + 100 + rd32(base + 4) * 12;
    UData *end = UData_itemsend(a), *rv = UData_find(a, (exe_u32(0x5e5084) & 0xffff0000u) | rd16(e + 8));
    if (rv == end) return NULL;
    *n = rv->count;
    return (const float *)UData_data(rv);
}

/* ---- the contact points ---- */

/* 0x476e60: the collision vertices above the box's middle height and those at or below it, the hull of
   each in the ground plane; the upper hull at the box's top (one point at its middle when it is under
   0.1 across both ways, 0x5b2604), then the lower at its bottom; at most 20 of them. */
static float (*points_general(UData *a, int *count))[3]
{
    *count = 0;
    const uint8_t *base = article_base(a);
    if (!base) return NULL;
    uint32_t n;
    const float *v = collision_verts(a, base, &n);
    const WBase *bs = (const WBase *)(const void *)base;
    float mid = (bs->bmax[1] + bs->bmin[1]) * f32(0x5b23f0);
    float *up = malloc((n + 2) * 16), *low = malloc((n + 2) * 16), *hu = malloc((n + 2) * 16), *hl = malloc((n + 2) * 16);
    float (*out)[3] = NULL;
    if (!up || !low || !hu || !hl) goto done;
    int nu = 0, nl = 0;
    for (uint32_t i = 0; i < n; i++) {
        const float *p = v + 4 * i;
        if (p[1] <= mid) memcpy(low + 4 * nl++, p, 16);
        else memcpy(up + 4 * nu++, p, 16);
    }
    int hn = nu ? WTree_Hull(nu, up, hu) : 0, ln = nl ? WTree_Hull(nl, low, hl) : 0;
    float mn[2] = {1e9f, 1e9f}, mx[2] = {-1e9f, -1e9f};   /* 0x489b20 */
    for (int i = 0; i < hn; i++) {
        if (hu[4 * i] < mn[0]) mn[0] = hu[4 * i];
        if (mx[0] < hu[4 * i]) mx[0] = hu[4 * i];
        if (hu[4 * i + 2] < mn[1]) mn[1] = hu[4 * i + 2];
        if (mx[1] < hu[4 * i + 2]) mx[1] = hu[4 * i + 2];
    }
    if (mx[0] - mn[0] < f32(0x5b2604) && mx[1] - mn[1] < f32(0x5b2604)) {
        hn = 1;
        hu[0] = (mn[0] + mx[0]) * f32(0x5b23f0);
        hu[2] = (mn[1] + mx[1]) * f32(0x5b23f0);
    }
    out = malloc((size_t)(hn + ln ? hn + ln : 1) * sizeof *out);
    if (!out) goto done;
    for (int i = 0; i < hn; i++) {
        out[i][0] = hu[4 * i];
        out[i][1] = bs->bmax[1];
        out[i][2] = hu[4 * i + 2];
    }
    for (int i = 0; i < ln; i++) {
        out[hn + i][0] = hl[4 * i];
        out[hn + i][1] = bs->bmin[1];
        out[hn + i][2] = hl[4 * i + 2];
    }
    *count = hn + ln < 0x14 ? hn + ln : 0x14;
done:
    free(up);
    free(low);
    free(hu);
    free(hl);
    return out;
}

/* 0x476a10: the matrix without its scale (the lengths of its axes, from m times its transpose) and the
   scale. */
static void unscale(const EaglMatrix *m, EaglMatrix *out, float scale[3])
{
    EaglMatrix t = *m, p;
    EAGL_MatInvertOrtho(&t);   /* 0x539b20 */
    EAGL_MatMul(m, &t, &p);    /* 0x558f90 */
    scale[0] = sqrtf(p.m[0]);   /* 0x532540 */
    scale[1] = sqrtf(p.m[5]);
    scale[2] = sqrtf(p.m[10]);
    EaglMatrix s, r;
    EAGL_MatScale(&s, f32(0x5b24a8) / scale[0], f32(0x5b24a8) / scale[1], (float)((double)f32(0x5b24a8) / (double)scale[2]), 1.0f);
    EAGL_MatMul(m, &s, &r);
    *out = r;
}

/* 0x476ac0: a sign's, from its silhouette: the collision vertices scaled by the placing's scale and turned
   a quarter about x, their hull made a slab 2 x 0x5b2638 thick and turned back; every point above the
   lowest by more than 0.1, and of those at the bottom the first either side of z 0x5b27b4. */
static float (*points_sign(UData *a, const EaglMatrix *place, int *count))[3]
{
    *count = 0;
    const uint8_t *base = article_base(a);
    if (!base) return NULL;
    uint32_t n;
    const float *v = collision_verts(a, base, &n);
    if (!v || !n) return NULL;
    EaglMatrix m, s, q, qi;
    float scale[3];
    unscale(place, &m, scale);
    EAGL_MatScale(&s, scale[0], scale[1], scale[2], 1.0f);   /* 0x5398a0 */
    float *buf = malloc((size_t)n * 16), *hull = malloc(((size_t)n + 2) * 16);
    float (*out)[3] = NULL, *slab = NULL;
    if (!buf || !hull) goto done;
    EAGL_MatTransform4(&s, (int)n, v, 16, buf, 16);
    EAGL_MatAxisAngle(&q, 90.0f, 1.0f, 0, 0);
    EAGL_MatTransform4(&q, (int)n, buf, 16, buf, 16);
    int hn = WTree_Hull((int)n, buf, hull);
    slab = malloc((size_t)(hn ? hn : 1) * 32);
    out = malloc((size_t)(hn ? hn : 1) * 2 * sizeof *out);
    if (!slab || !out) goto done;
    float k = f32(0x5b2638);
    for (int i = 0; i < hn; i++) {
        float *p = slab + 4 * i, *r = slab + 4 * (hn + i);
        p[0] = hull[4 * i], p[1] = hull[4 * i + 1] - k, p[2] = hull[4 * i + 2], p[3] = 1.0f;
        r[0] = hull[4 * i], r[1] = hull[4 * i + 1] + k, r[2] = hull[4 * i + 2], r[3] = 1.0f;
    }
    int total = 2 * hn;
    qi = q;
    EAGL_MatInvertOrtho(&qi);   /* 0x539b20 */
    EAGL_MatTransform4(&qi, total, slab, 16, slab, 16);
    float low = f32(0x5b46b0);
    for (int i = 0; i < total; i++)
        if (!(low <= slab[4 * i + 1])) low = slab[4 * i + 1];
    low += f32(0x5b2604);
    bool far_side = false, near_side = false;   /* (the bottom points kept: z above 0x5b27b4, at or below it) */
    int kept = 0;
    for (int i = 0; i < total; i++) {
        const float *p = slab + 4 * i;
        bool above = f32(0x5b27b4) < p[2];
        bool bottom = !(low < p[1]);
        if (bottom && (above ? far_side : near_side)) continue;
        memcpy(out[kept++], p, 12);
        if (bottom) {
            if (above) far_side = true;
            else near_side = true;
        }
    }
    *count = kept;
done:
    free(buf);
    free(hull);
    free(slab);
    return out;
}

/* ---- the bodies ---- */

/* 0x483030 + 0x483070: the points about their box's centre (the offset), the placing's rotation without
   its scale and its position moved to the centre (the centre isn't turned, as the original), the mass
   and sound kind; the simulated position and rotation the zero vector and identity until a car takes
   the body. */
static WSmackBody *body_new(const float (*pts)[3], int n, const float rot[9], const float pos[3], float mass, int32_t audio)
{
    WSmackBody *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->pts = calloc(n ? (size_t)n : 1, sizeof *b->pts);
    b->npts = n;
    b->mass = mass;
    b->audio = audio;
    EaglMatrix m, u;
    EAGL_MatFromRotTrans(&m, rot, zero3);
    unscale(&m, &u, b->scale);
    float t[3];
    EAGL_MatToRotTrans(&u, b->rot0, t);
    b->rot = ident9;
    b->pos = zero3;
    float mn[3] = {1e8f, 1e8f, 1e8f}, mx[3] = {-1e8f, -1e8f, f32(0x5b480c)};
    for (int i = 0; i < n; i++)
        for (int j = 0; j < 3; j++) {
            if (pts[i][j] < mn[j]) mn[j] = pts[i][j];
            if (mx[j] < pts[i][j]) mx[j] = pts[i][j];
        }
    for (int j = 0; j < 3; j++) {
        b->offset[j] = -((mx[j] + mn[j]) * f32(0x5b23f0));
        b->pos0[j] = pos[j] - b->offset[j];
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < 3; j++) b->pts[i][j] = pts[i][j] + b->offset[j];
    return b;
}

static void body_free(WSmackBody *b)   /* 0x483290 */
{
    if (!b) return;
    free(b->pts);
    free(b);
}

/* ---- SmackAnim ---- */

/* 0x4710e0: following the body; its base the inverse of the body's placing (0x53a2b0's inverse by
   elimination; the port's by cofactors, 0x53a1c0, the same matrix but for its rounding). */
static WSmackAnim *smackanim_new(WSmackBody *body)
{
    WSmackAnim *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->body = body;
    EaglMatrix m;
    EAGL_MatFromRotTrans(&m, body->rot0, body->pos0);
    if (!EAGL_MatInvert(&m, &s->base)) s->base = m;
    return s;
}

int SmackAnim_Matrix(WSmackAnim *s, EaglMatrix *out)   /* 0x471140 */
{
    const WSmackBody *b = s->body;
    const float *r = b->rot, *p = b->pos;
    bool same = s->lastpos[0] == p[0] && s->lastpos[1] == p[1] && s->lastpos[2] == p[2];
    for (int i = 0; i < 9 && same; i++) same = s->lastrot[i] == r[i];
    if (same) {
        *out = s->last;
        return s->state;
    }
    /* T(offset) . the body now . the inverse of its placing . T(-offset) */
    EaglMatrix m, a, t, o;
    EAGL_MatFromRotTrans(&m, r, p);
    EAGL_MatMul(&m, &s->base, &a);
    EAGL_MatTranslation(&t, b->offset[0], b->offset[1], b->offset[2]);
    EAGL_MatMul(&t, &a, &o);
    EAGL_MatTranslation(&t, -b->offset[0], -b->offset[1], -b->offset[2]);
    EAGL_MatMul(&o, &t, out);
    memcpy(s->lastrot, r, sizeof s->lastrot);
    memcpy(s->lastpos, p, sizeof s->lastpos);
    s->moved = g_sim_tick;
    s->last = *out;
    /* 0x4832c0: off its place by more than 0.01 in x or z */
    if (s->state != 1) {
        double lim;   /* the double 0.01 at 0x5b2bc0 */
        const uint8_t *q = exe_data(0x5b2bc0, 8);
        if (q) memcpy(&lim, q, 8);
        else lim = 0.01;
        if (lim < fabs((double)b->pos0[0] - p[0]) || lim < fabs((double)b->pos0[2] - p[2])) s->state = 1;
    }
    return s->state;
}

/* ---- a piece's animation (the base Anim's virtual slots) ---- */

static int anim_state(const WSmackPiece *p) { return p->canned ? ((const QuatAnim *)p->anim)->state : ((const WSmackAnim *)p->anim)->state; }
static uint8_t anim_b4(const WSmackPiece *p) { return p->canned ? ((const QuatAnim *)p->anim)->b4 : ((const WSmackAnim *)p->anim)->b4; }
static int anim_matrix(WSmackPiece *p, EaglMatrix *out)   /* slot 1 */
{
    return p->canned ? QuatAnim_Matrix(p->anim, out) : SmackAnim_Matrix(p->anim, out);
}
/* slot 4: a SmackAnim moved within the last 64 ticks (0x471330), a QuatAnim playing (0x471480) */
static bool anim_recent(const WSmackPiece *p)
{
    if (p->canned) return ((const QuatAnim *)p->anim)->state == 1;
    return g_sim_tick - ((const WSmackAnim *)p->anim)->moved <= 0x40;
}
/* slot 8: how far a SmackAnim's body has moved in x and z from its placing (0x471370; the square root
   the bit trick's), 0 before its first move; a QuatAnim's 0 (0x4710b0). */
static float anim_drift(const WSmackPiece *p)
{
    if (p->canned) return 0;
    const WSmackAnim *s = p->anim;
    if (s->lastpos[0] == 0 && s->lastpos[2] == 0) return 0;   /* (0x5b23f8) */
    float dx = s->body->pos0[0] - s->lastpos[0], dz = s->body->pos0[2] - s->lastpos[2];
    union { float f; int32_t i; } u = {dz * dz + dx * dx};
    u.i = (u.i >> 1) + 0x1fc00000;
    return u.f;
}
float WAnimatable_Drift(const WAnimatable *a, int i) { return a->e[i].cur.anim ? anim_drift(&a->e[i].cur) : 0; }

static void anim_reset(WSmackPiece *p)   /* slot 7 (0x471360): still */
{
    if (p->canned) ((QuatAnim *)p->anim)->state = 0;
    else ((WSmackAnim *)p->anim)->state = 0;
}
static void anim_free(WSmackPiece *p)   /* slot 0 */
{
    if (!p->anim) return;
    if (p->canned) QuatAnim_free(p->anim);
    else free(p->anim);
    p->anim = NULL;
}

/* ---- the entries ---- */

/* (the scene and instance bundles share their layout to +0x40, the placings: the original reads both
   the same way) */
static void entry_init(WAnimatable *a, int i, UData *article)   /* 0x476210 */
{
    WInstanceBundle *in = (WInstanceBundle *)a->bundle;
    WSmackEntry *e = &a->e[i];
    uint32_t type = in->placed[i].type;
    const WObjectDef *d = WObjectDefs_find(type);
    e->def = d;
    e->dynlight = d ? d->dynamiclight : 0;   /* (the original reads it before the test) */
    e->npieces = d ? (d->npieces ? d->npieces : 1) : 0;
    e->pieces = calloc(e->npieces ? (size_t)e->npieces : 1, sizeof *e->pieces);
    if (!e->pieces) return;
    e->pieces[0] = (WSmackPiece){NULL, article, NULL, NULL, NULL, type, 0};
    e->cur = e->pieces[0];
}

static UData *proto_article(WAnimatable *a, uint32_t type)   /* 0x4785d0, 0x478600 */
{
    WInstanceBundle *in = (WInstanceBundle *)a->bundle;
    int p = WSceneBundle_findproto((const WSceneBundle *)(const void *)in, type);
    return 0 <= p ? in->protos[p].article : NULL;
}

static const uint32_t *item_words(UData *a, uint32_t id)
{
    UData *r = UData_find(a, id);
    return r == UData_itemsend(a) ? NULL : (const uint32_t *)UData_data(r);
}

/* 0x476310: the canned pieces for a hit at vel: the definition's speed bins (+0x5c, +0x60) pick a run of
   its pieces (+0x70: their counts; one, the first, below the first speed), each a QuatAnim of its second
   prototype's 'Anim' (its keys' count, its loop flag) and 'Anqt' (the keys) items at 0.1 s a key over the
   piece's playback rate, turned by the hit's direction in the object's axes and drifting with the hit's
   velocity times the velocity factor; the first piece's also the current record's. False when the
   definition has no pieces or a piece's prototype has no running animation. */
static bool canned_start(WAnimatable *a, int i, const float vel[3])
{
    WSmackEntry *e = &a->e[i];
    const WObjectDef *d = e->def;
    if (!d || !d->npieces) return false;
    float speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);   /* 0x532890 */
    int first = 0, count = 1, k = 0;
    if (0 < d->nspeeds) {
        while (k < d->nspeeds && d->speed[k] < speed) k++;
        if (0 < k) {
            k--;
            for (int j = 0; j < k; j++) first += d->speedpieces[j];
            count = d->speedpieces[k];
        }
    }
    const WPlaced *pl = &((WInstanceBundle *)a->bundle)->placed[i];
    for (int j = 0; j < count; j++) {
        if (first + j >= WOBJ_MAXPIECES || j >= e->npieces) return false;   /* (the original reads on) */
        const WObjectPiece *pc = d->piece[first + j];
        if (!pc) return false;
        UData *art = proto_article(a, pc->basegroup);
        if (!art) art = e->pieces[0].article;
        UData *art2 = proto_article(a, pc->quatgroup);   /* (its vertex animation the same article) */
        WSmackPiece *p = &e->pieces[j];
        *p = (WSmackPiece){NULL, art, art2, art2, NULL, pc->basegroup, 0};
        const uint32_t *anim = art2 ? item_words(art2, 0x416e696d) : NULL;   /* 'Anim' */
        if (!anim || (anim[0] == 0 && anim[1] == 0)) return false;
        uint8_t loop = anim[1] != 0;
        const uint32_t *keys = item_words(art2, 0x416e7174);   /* 'Anqt' */
        float rate = f32(0x5b2604) / pc->playbackrate;
        /* the hit in the object's axes: its direction's turn, the drift */
        float rt[9], lv[3];
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) rt[3 * r + c] = pl->rot[3 * c + r];   /* 0x532770 */
        for (int c = 0; c < 3; c++) lv[c] = vel[0] * rt[c] + vel[1] * rt[3 + c] + vel[2] * rt[6 + c];   /* 0x5327b0 */
        double k2pi;
        const uint8_t *kp = exe_data(0x5bb2c8, 8);
        if (kp) memcpy(&k2pi, kp, 8);
        else k2pi = 0.15915494309189535;
        float turn = (float)(atan2((double)lv[0], (double)lv[2]) * k2pi);   /* 0x5325d0 */
        float drift[3] = {lv[0] * d->velocityfactor, lv[1] * d->velocityfactor, lv[2] * d->velocityfactor};
        for (int n = 0; n < (j == 0 ? 2 : 1); n++) {
            QuatAnim *q = QuatAnim_new(anim[0], (const uint8_t *)keys, loop, pc->flags, rate, turn);   /* 0x4713f0 */
            if (!q) return false;
            memcpy(q->d->drift, drift, sizeof drift);
            QuatAnim_Start(q);   /* slot 5 */
            if (n == 0) {
                p->anim = q;
                p->canned = 1;
                e->live++;
            } else {
                anim_free(&e->cur);
                e->cur.anim = q;
                e->cur.canned = 1;
            }
        }
    }
    return true;
}

/* 0x477280: a smackable (collision kind 3, by the article's Base type) with a prototype of its placed type
   gets its body: the contact points of the prototype (a sign's by 0x476ac0, the others' 0x476e60), the
   entry's definition's mass (10 without one) and sound kind; its piece the prototype's article for the
   dynamically lit ones, the entry's own for the others; a SmackAnim for the piece and one for the current
   record. The others whose prototype loops its 'Anim' start their canned pieces (0x476310). */
static void entry_body(WAnimatable *a, int i)
{
    WInstanceBundle *in = (WInstanceBundle *)a->bundle;
    WSmackEntry *e = &a->e[i];
    UData *art = a->bundle->entries[i].article;
    const uint8_t *base = art ? article_base(art) : NULL;
    if (!base || (base[0] & 0x40)) return;
    const WObjectDef *d = WObjectDefs_find(((const WBase *)(const void *)base)->type);
    if (!d) return;
    int p = WSceneBundle_findproto((const WSceneBundle *)(const void *)in, in->placed[i].type);   /* 0x4785d0 */
    if (d->collidetype != 3) {
        /* a looping animation starts at once, as if hit standing still */
        if (p < 0) return;
        const uint32_t *anim = item_words(in->protos[p].article, 0x416e696d);   /* 'Anim' */
        if (anim && anim[1] != 0 && !e->cur.anim) canned_start(a, i, zero3);
        return;
    }
    if (p < 0) return;
    UData *proto = in->protos[p].article;   /* 0x478600 */
    const WPlaced *pl = &in->placed[i];
    EaglMatrix place;
    EAGL_MatFromRotTrans(&place, pl->rot, zero3);
    int n = 0;
    float (*pts)[3] = d->tag == 0x4e474953u ? points_sign(proto, &place, &n) : points_general(proto, &n);   /* 'SIGN' */
    const WObjectDef *ed = e->def;
    WSmackBody *b = body_new((const float (*)[3])pts, n, pl->rot, pl->pos, ed ? ed->mass : 10.0f, ed ? ed->audiotype : 0);
    free(pts);
    if (!b) return;
    if (a->nbodies == a->cap) {
        uint32_t cap = a->cap ? a->cap * 2 : 16;
        WSmackBody **q = realloc(a->bodies, cap * sizeof *q);
        if (!q) {
            body_free(b);
            return;
        }
        a->bodies = q;
        a->cap = cap;
    }
    a->bodies[a->nbodies++] = b;
    e->colour = 0xffffffffu;
    e->pieces[0] = (WSmackPiece){NULL, e->dynlight ? proto : art, NULL, NULL, b, d->tag, 0};
    e->pieces[0].anim = smackanim_new(b);
    e->cur = e->pieces[0];
    e->cur.anim = smackanim_new(b);
}

WAnimatable *WAnimatable_new(struct W3DObjectBundle *instances)   /* 0x475d80 -> 0x475e10 */
{
    WAnimatable *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->bundle = instances;
    uint32_t n = instances->count;
    a->e = calloc(n ? n : 1, sizeof *a->e);
    if (!a->e) {
        free(a);
        return NULL;
    }
    for (uint32_t i = 0; i < n; i++) a->e[i].colour = 0xffffffffu;
    for (uint32_t i = 0; i < n; i++) {
        entry_init(a, (int)i, instances->entries[i].article);
        entry_body(a, (int)i);
    }
    return a;
}

void WAnimatable_free(WAnimatable *a)   /* 0x475ff0 */
{
    if (!a) return;
    for (uint32_t i = 0; i < a->bundle->count; i++) {
        WSmackEntry *e = &a->e[i];
        for (int k = 0; e->pieces && k < e->npieces; k++) anim_free(&e->pieces[k]);
        anim_free(&e->cur);
        free(e->pieces);
    }
    for (uint32_t i = 0; i < a->nbodies; i++) body_free(a->bodies[i]);
    free(a->bodies);
    free(a->e);
    free(a);
}

void WSmackBody_Follow(WSmackBody *b, const float *rot, const float *pos)   /* 0x4832a0 */
{
    b->rot = rot;
    b->pos = pos;
    b->b1c = 0;
}

bool WAnimatable_HasAnim(const WAnimatable *a, int i, int piece)   /* 0x476730 */
{
    const WSmackEntry *e = &a->e[i];
    if (piece < 0) return e->cur.anim != NULL;
    return e->pieces && piece < e->npieces && e->pieces[piece].anim;
}

void WAnimatable_Matrix(WAnimatable *a, int i, int piece, bool still, EaglMatrix *out)   /* 0x476970 */
{
    WSmackEntry *e = &a->e[i];
    WSmackPiece *p = piece < 0 ? &e->cur : &e->pieces[piece];
    EaglMatrix m, place;
    anim_matrix(p, &m);   /* slot 1 (its state kept even when not used) */
    if (still) EAGL_MatIdentity(&m);   /* 0x5bc660 */
    const WPlaced *pl = &((WInstanceBundle *)a->bundle)->placed[i];
    EAGL_MatFromRotTrans(&place, pl->rot, pl->pos);   /* 0x539a40 */
    EAGL_MatMul(&m, &place, out);    /* 0x558f90 */
}

int WAnimatable_Pieces(const WAnimatable *a, int i) { return a->e[i].live < 1 ? 1 : a->e[i].live; }   /* 0x450220 */

int WAnimatable_HitKind(WAnimatable *a, int i, const float vel[3])   /* 0x476760 */
{
    WSmackEntry *e = &a->e[i];
    if (e->cur.anim) return 0;   /* (a body's traffic object meets the car instead; a played one is gone) */
    return 2 - canned_start(a, i, vel);
}

/* 0x4766a0: the played pieces dropped (a canned one without its loop) or stopped. */
static void pieces_done(WSmackEntry *e)
{
    for (int k = 0; k < e->live && k < e->npieces; k++) {
        WSmackPiece *p = &e->pieces[k];
        if (p->anim && !anim_b4(p) && !p->body) {
            anim_free(p);
            *p = (WSmackPiece){0};
        } else if (p->anim && !anim_b4(p)) {
            anim_reset(p);   /* slot 7 */
        } else if (!p->anim && !p->body) {
            *p = (WSmackPiece){0};
        }
    }
    e->live = 0;
}

/* The current record's animation runs whenever the entry's matrix is taken (its slot 5, 0x478050): a
   knocked object's state goes to 1 once its body is off its place. */
void WAnimatable_Tick(WAnimatable *a)   /* 0x4767a0 */
{
    W3DObjectBundle *b = a->bundle;
    for (uint32_t i = 0; i < b->count; i++) {
        WSmackEntry *e = &a->e[i];
        if (!e->cur.anim) continue;
        int state = anim_state(&e->cur);
        if (state == 2) {
            pieces_done(e);   /* 0x4766a0 */
        } else if (state == 1) {
            if (!e->sounded) {
                /* knocked off its place: its sound once, its markers fired (the draw's) */
                e->marked = 1;
                e->sounded = e->cur.article;
                float sp[4];
                b->vt->sphere(b, (int)i, sp);   /* slot 3 */
                Audio_HitSound(sp);   /* 0x4af210 */
            }
        } else {
            e->sounded = NULL;
        }
        if (e->dynlight && (e->colour == 0xffffffffu || state == 1) && anim_recent(&e->cur)) {
            /* the light at its centre from the polygon under it (a fresh tracker, 0x473060), packed */
            EaglMatrix m;
            anim_matrix(&e->cur, &m);   /* slot 1 (its result unused) */
            WSurface t;
            WSurface_Init(&t);
            float sp[4], l[4];
            b->vt->sphere(b, (int)i, sp);   /* slot 3 */
            WSurface_Light(&t, sp, l);   /* 0x474600 */
            int32_t ca = (int32_t)(l[0] * -255.0f), cr = (int32_t)(l[1] * -255.0f), cg = (int32_t)(l[2] * -255.0f);
            int32_t cb = (int32_t)(l[3] * 255.0f);   /* (0x5b38e8, 0x5b3710: the first three negated) */
            uint32_t hi = (((uint32_t)ca << 8) + (uint32_t)cr) << 8;
            hi = (hi + (uint32_t)cg) << 8;
            e->colour = (uint32_t)cb - hi;
        }
    }
}

void WAnimatable_Restart(WAnimatable *a)   /* 0x476100 */
{
    for (uint32_t i = 0; i < a->bundle->count; i++) {
        WSmackEntry *e = &a->e[i];
        if (e->cur.anim) {
            pieces_done(e);   /* 0x4766a0 */
            if (!anim_b4(&e->cur) && !e->cur.body) {
                /* a played canned object: its current record dropped, the first piece its record again */
                anim_free(&e->cur);
                if (e->pieces) e->pieces[0] = e->cur;
            } else if (!anim_b4(&e->cur)) {
                anim_reset(&e->cur);   /* slot 7 */
            }
        }
        e->sounded = NULL;
    }
}
