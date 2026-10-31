/* The track's animations: QuatAnim (0x4713f0 .. 0x471640, keys 0x470df0 / 0x470ed0) and the animated
   bundle's entries (0x477780). */
#include "game/anim.h"
#include "exe.h"
#include "game/objdefs.h"
#include "game/world.h"
#include "render/r3dcar.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern int32_t g_sim_tick;   /* (0x628938's clock: the address of 0x606ac4, Track_NewCamAnims) */
static const float CLOCK = 1.0f / 64.0f;   /* 0x3c800000 */

static float exe_f32(uint32_t va) { uint32_t u = exe_u32(va); float f; memcpy(&f, &u, 4); return f; }
static float kf(const uint8_t *k, int i) { float v; memcpy(&v, k + 4 * i, 4); return v; }

/* The matrix between two keys (0x470ed0): the quaternions and positions blended (0x48a080), the matrix
   of the blend as it is (0x48a110), its rows scaled by the blended scale (0x532710), at the position;
   flag 8 turns it about y after, flag 0x10 spins it about y down to nothing at the run's end. */
static void keys_matrix(const QuatAnimData *d, EaglMatrix *out, uint32_t a, uint32_t b, float f)
{
    const uint8_t *ka = d->keys + a * 0x34, *kb = d->keys + b * 0x34;
    float q[4], p[3], s[3];
    for (int i = 0; i < 4; i++) q[i] = (kf(kb, i) - kf(ka, i)) * f + kf(ka, i);
    for (int i = 0; i < 3; i++) p[i] = (kf(kb, 4 + i) - kf(ka, 4 + i)) * f + kf(ka, 4 + i);
    float g = 1.0f - f;   /* 0x5b24a8 */
    for (int i = 0; i < 3; i++) s[i] = f * kf(kb, 8 + i) + g * kf(ka, 8 + i);
    float x2 = q[0] + q[0], y2 = q[1] + q[1], z2 = q[2] + q[2], xw = x2 * q[3];
    float r[9];
    r[0] = (1.0f - y2 * q[1]) - z2 * q[2];
    r[1] = y2 * q[0] + z2 * q[3];
    r[2] = z2 * q[0] - y2 * q[3];
    r[3] = y2 * q[0] - z2 * q[3];
    float t = 1.0f - x2 * q[0];
    r[4] = t - z2 * q[2];
    r[5] = z2 * q[1] + xw;
    r[6] = z2 * q[0] + y2 * q[3];
    r[7] = z2 * q[1] - xw;
    r[8] = t - y2 * q[1];
    for (int i = 0; i < 9; i++) r[i] *= s[i / 3];
    EAGL_MatFromRotTrans(out, r, p);
    if (d->flags & 8) {
        EaglMatrix m, o;
        EAGL_MatAxisAngle(&m, d->turn * 360.0f, 0, 1.0f, 0);   /* 0x5b25cc */
        EAGL_MatMul(out, &m, &o);   /* 0x558f90 */
        *out = o;
    }
    float spin = 0;
    if (d->flags & 0x10) spin = d->turn * exe_f32(0x5b45b4) * (1.0f - ((float)a + f) / (float)d->nkeys);
    EaglMatrix m, o;
    EAGL_MatAxisAngle(&m, spin, 0, 1.0f, 0);
    EAGL_MatMul(&m, out, &o);
    *out = o;
}

bool QuatAnim_At(const QuatAnim *a, int32_t tick, EaglMatrix *out)   /* 0x470df0 */
{
    const QuatAnimData *d = a->d;
    float r = d->rate / CLOCK;
    if (tick < 0) tick = 0;
    float t = (float)tick, fr = t / r;
    uint32_t k = (uint32_t)(int64_t)fr;
    float f = fr - (float)k;
    if (d->nkeys < 2) {
        EAGL_MatIdentity(out);
        return false;
    }
    if (d->flags & 1) k = d->nkeys > 2 ? k % (d->nkeys - 2) : 0;
    else if (d->nkeys - 2 <= k) {
        k = d->nkeys - 1;
        f = 0;
    }
    /* (past the last key the original blends with the key after it at weight 0; the port reads the last
       twice) */
    keys_matrix(d, out, k, k + 1 < d->nkeys ? k + 1 : k, f);
    float s = t * r;
    for (int i = 0; i < 3; i++) out->m[12 + i] += d->drift[i] * s;
    return k + 1 < d->nkeys;
}

QuatAnim *QuatAnim_new(uint32_t nkeys, const uint8_t *keys, uint8_t b4, uint32_t flags, float rate, float turn)
{
    QuatAnim *a = calloc(1, sizeof *a);
    QuatAnimData *d = calloc(1, sizeof *d);
    if (!a || !d) {
        free(a);
        free(d);
        return NULL;
    }
    d->flags = flags;
    d->rate = rate;
    d->nkeys = nkeys;
    d->keys = keys;
    d->turn = turn;   /* (the drift: the zero vector 0x5bb2b0) */
    a->d = d;
    a->b4 = b4;
    return a;
}

void QuatAnim_free(QuatAnim *a)
{
    if (!a) return;
    free(a->d);
    free(a);
}

void QuatAnim_Start(QuatAnim *a)
{
    if (a && a->state == 0) {
        a->state = 1;
        a->start = g_sim_tick;
    }
}

int QuatAnim_Matrix(QuatAnim *a, EaglMatrix *out)   /* 0x4714d0 */
{
    if (a->state == 0) {
        keys_matrix(a->d, out, 0, 0, 0);
        return 0;
    }
    int32_t t = g_sim_tick - a->start;
    a->state = QuatAnim_At(a, t < 0 ? 0 : t, out) ? 1 : 2;
    return a->state;
}

/* 0x477780's part: per entry its animation ('Anqt': its keys and their count) made and started, its
   type's sounds (a CAudioElem record each: the sound, its range, the inner radius from FadeIn, the
   period from Trigger and its random part from FadeOut, the repeats, the volume). */
WAnimEntry *WAnim_create(W3DObjectBundle *b)
{
    const uint32_t ANQT = 0x416e7174, BASE = 0x42617365;
    WAnimEntry *es = calloc(b->count ? b->count : 1, sizeof *es);
    if (!es) return NULL;
    for (uint32_t i = 0; i < b->count; i++) {
        UData *art = b->entries[i].article;
        UData *q = UData_find(art, ANQT);
        if (q == UData_itemsend(art)) continue;
        UData *base = UData_find(art, BASE);
        if (base != UData_itemsend(art)) memcpy(&es[i].type, (const uint8_t *)UData_data(base) + 0x44, 4);
        es[i].anim = QuatAnim_new(q->count, UData_data(q), 0, exe_u32(0x5b45a4), 0.1f, 0);   /* 0x3dcccccd */
        QuatAnim_Start(es[i].anim);
        const WAudioDef *ad = es[i].type ? WObjectDefs_findaudio(es[i].type) : NULL;
        if (!ad || ad->n == 0) continue;
        es[i].sounds = calloc((size_t)ad->n, sizeof *es[i].sounds);
        if (!es[i].sounds) continue;
        es[i].nsounds = (uint32_t)ad->n;
        for (int k = 0; k < ad->n; k++) {
            const WSoundDef *sd = &ad->sound[k];
            SceneElem *e = &es[i].sounds[k];
            uint8_t *r = e->b + 0x7c;
            e->kind = SCENE_AUDIO;
            e->size = 0xa4;
            memcpy(r + 0x14, &sd->fadein, 4);
            memcpy(r + 0x18, &sd->trigger, 2);
            memcpy(r + 0x1c, &sd->minrepeat, 2);
            memcpy(r + 0x0e, &sd->patch, 2);
            memcpy(r + 0x1a, &sd->fadeout, 2);
            memcpy(r + 0x1e, &sd->randomrepeat, 2);
            memcpy(r + 0x10, &sd->range, 4);
            memcpy(r + 0x20, &sd->volume, 2);
            int16_t none = -1;
            memcpy(r + 0x24, &none, 2);   /* (the original's array is uninitialised: its kind and voice
                                             whatever the heap held; 0x4aed00 drops a stale voice) */
        }
    }
    return es;
}

void WAnim_free(WAnimEntry *e, uint32_t n)
{
    if (!e) return;
    for (uint32_t i = 0; i < n; i++) {
        QuatAnim_free(e[i].anim);
        free(e[i].sounds);
    }
    free(e);
}
