/* The game's audio (see audio.h). */
#include "game/audio.h"
#include "exe.h"
#include "game/camera.h"
#include "pending.h"
#include "render/r3dcar.h"
#include "real/locatbig.h"
#include "snd/snd.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t g_sound_quality;

static const char *sound_dir(void) { return "GameData\\Sounds\\"; }   /* 0x65b318 (the install's) */
static int32_t opt(uint32_t va) { return (int32_t)exe_u32(va); }      /* the options' starting values */

/* 0x5d1764: a 129-step fade in; read from its end, 0x5d17e4 - i, the fade out. */
static int fade_in(int i) { return (int8_t)exe_data(0x5d1764 + (uint32_t)i, 1)[0]; }
static int fade_out(int i) { return (int8_t)exe_data((uint32_t)(0x5d17e4 - i), 1)[0]; }

/* ---- the start (0x4a6610, 0x4a6480) and the banks (0x4a6990) ---- */

static bool s_on;   /* 0x5d1734 */
static void engines_free(void);
typedef struct {
    int32_t slot;      /* 0x6564a0: the SND bank */
    uint8_t *data;     /* 0x6564a8 */
    int32_t size;      /* 0x6564ac */
} AudioBank;
static AudioBank s_banks[8];

bool Audio_Init(void)
{
    if (g_sound_quality == 3) return false;
    for (int i = 0; i < 8; i++) s_banks[i].slot = -1;
    int q = g_sound_quality;
    /* The device's rate (at most 0x5b4ae4's), its voices (0x5b4b08), the resampler (0x5b4b20). */
    int rate = (int32_t)exe_u32(0x5b4ae4 + 4u * (uint32_t)q);
    int voices = (int32_t)exe_u32(0x5b4b08 + 4u * (uint32_t)q);
    int quality = (int32_t)exe_u32(0x5b4b20 + 4u * (uint32_t)q);
    s_on = SND_Init(rate, 2, voices, quality);
    PENDING_NOTE(0x4ae080, "music, the speech and the track's sounds (Music_Init 0x4aded0, 0x4ab280)");
    return s_on;
}

void Audio_Shutdown(void)
{
    SND_Shutdown();
    engines_free();
    for (int i = 0; i < 8; i++) {
        free(s_banks[i].data);
        s_banks[i].data = NULL;
        s_banks[i].slot = -1;
    }
    s_on = false;
}

/* A bank into the library and the game's list at k (0x4a6990; the original keeps only the header part
   when the library has the samples elsewhere, status 7, which its software voices never do). */
static int add_bank(uint8_t *data, int size, int k)
{
    int slot = SND_BankAdd(data);
    if (slot < 0) printf("AudioCmn_AddBank -- DID NOT ADD BANK\n");
    s_banks[k].slot = slot;
    s_banks[k].data = data;
    s_banks[k].size = size;
    return size;
}

/* ---- the engines (0x4ac9e0, 0x4ac550, 0x4ac060) ---- */

typedef struct {
    int16_t start, end;       /* the curve's non-zero part of the 512 rpm steps */
    const int8_t *curve;
    int8_t patch;             /* -1 none; below 0x40 on the throttle, from 0x40 off it */
} EngineLayer;

typedef struct {
    uint16_t vol, pitch, pan, spread, rpm, xfade, throttle, view;
} EngineSnap;

typedef struct {
    int8_t vol;
    int16_t pitch, pan;
    int32_t handle;
} EngineVoice;

/* The engine's state (0x758 bytes, 0x656aa8[slot]). */
typedef struct {
    int32_t k[13];            /* +0x00: 0x32, 0, 0x46, 0x14, 0x40, 0x80, 0x80, 0xaa, 0x50, 0x68, 0x19, 0xdc, 0x1e */
    int32_t countdown;        /* +0x34 */
    int32_t start_h, stop_h;  /* +0x38, +0x3c: the engine's start and stop sounds */
    uint8_t started;          /* +0x40 */
    int32_t vol[32];          /* +0x44: each layer's, eased */
    int32_t pan, spread, pitch;   /* +0xc4, +0xc8, +0xcc */
    int8_t bank;              /* +0xd0 */
    EngineSnap snap[32];      /* +0xd2 */
    uint8_t wr, rd;           /* +0x2d2, +0x2d3 */
    EngineLayer layer[32];    /* +0x2d4: the four tables' eight layers */
    EngineVoice v[32], v2[32];/* +0x454, +0x5d4 */
    int8_t *tables;           /* +0x754 */
} Engine;

static Engine *s_engine[2];   /* 0x656aa8 */
static bool s_tick_added;     /* 0x656ab0 */
static void engine_tick(void);

static void engines_free(void)
{
    s_tick_added = false;
    for (int i = 0; i < 2; i++) {
        if (s_engine[i]) free(s_engine[i]->tables);
        free(s_engine[i]);
        s_engine[i] = NULL;
    }
}

/* A CRDl table (version 2): +0x20 the layers' patches, +0x128 their curves' offsets from their own
   words. */
static void take_table(Engine *e, int t, const uint8_t *crd)
{
    for (int i = 0; i < 8; i++) {
        EngineLayer *l = &e->layer[t * 8 + i];
        int8_t patch = (int8_t)crd[0x20 + i];
        if (patch < 0) continue;
        int32_t off;
        memcpy(&off, crd + 0x128 + 4 * i, 4);
        const uint8_t *c = crd + 0x128 + 4 * i + off;
        l->patch = patch;
        int s = 0;
        while (s < 0x200 && c[s] == 0) s++;
        int en = s;
        while (en < 0x200 && c[en] != 0) en++;
        l->start = (int16_t)s;
        l->end = (int16_t)en;
        l->curve = (const int8_t *)c;   /* (copied below into the engine's own block) */
    }
}

bool Audio_LoadEngine(int slot, const char *sound)
{
    if (slot < 0 || 1 < slot || s_engine[slot] || !s_on) return false;
    Engine *e = calloc(1, sizeof *e);
    if (!e) return false;
    s_engine[slot] = e;
    e->start_h = -1;
    e->stop_h = -1;
    for (int i = 0; i < 32; i++) {
        e->v[i].handle = e->v2[i].handle = -1;
        e->layer[i].patch = -1;
        e->layer[i].start = e->layer[i].end = 0x200;
    }
    static const int32_t k[13] = {0x32, 0, 0x46, 0x14, 0x40, 0x80, 0x80, 0xaa, 0x50, 0x68, 0x19, 0xdc, 0x1e};
    memcpy(e->k, k, sizeof k);
    char path[160];
    size_t size = 0;
    uint8_t *viv = NULL;
    if (1 < g_sound_quality) {
        snprintf(path, sizeof path, exe_str(0x5d1b88), sound_dir(), sound);
        viv = vfs_read_all(path, &size);
    }
    if (!viv) {
        snprintf(path, sizeof path, exe_str(0x5d1b7c), sound_dir(), sound);
        viv = vfs_read_all(path, &size);
    }
    if (!viv) {
        snprintf(path, sizeof path, exe_str(1 < g_sound_quality ? 0x5d1b6c : 0x5d1b60), sound_dir());
        viv = vfs_read_all(path, &size);
    }
    if (!viv) return false;
    /* The entries by their extension: the bank, then .ect, .elt, .cct, .clt (0x5d146c, 0x5d1b3c ..). */
    static const uint32_t ext[4] = {0x5d1b3c, 0x5d1b34, 0x5d1b2c, 0x5d1b24};
    uint8_t *crd[4] = {0};
    bool bank = false;
    uint32_t n = BIG_count(viv);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = 0, len = 0;
        const char *name = BIG_locateentry(viv, NULL, (int)i, &off, &len);
        if (!name || size < off + len) continue;
        size_t nl = strlen(name);
        const char *x = nl >= 4 ? name + nl - 4 : name;
        if (!strcmp(x, exe_str(0x5d146c)) && !bank) {
            uint8_t *b = malloc(len);
            if (!b) continue;
            memcpy(b, viv + off, len);
            add_bank(b, (int)len, slot);
            e->bank = (int8_t)s_banks[slot].slot;
            bank = true;
            continue;
        }
        for (int t = 0; t < 4; t++)
            if (!crd[t] && !strcmp(x, exe_str(ext[t]))) {
                crd[t] = malloc(len);
                if (crd[t]) memcpy(crd[t], viv + off, len);
                if (crd[t] && memcmp(crd[t], "CRDl", 4)) printf("%s %s - file id incorrect\n", path, name);
                if (crd[t] && crd[t][4] != 2) printf("%s %s - file version incorrect\n", path, name);
            }
    }
    free(viv);
    if (bank && crd[0] && crd[1] && crd[3]) {
        int total = 0;
        for (int t = 0; t < 4; t++)
            if (crd[t]) take_table(e, t, crd[t]);
        for (int i = 0; i < 32; i++)
            if (e->layer[i].patch >= 0) total += e->layer[i].end - e->layer[i].start;
        e->tables = malloc((size_t)(total ? total : 1));
        int8_t *at = e->tables;
        for (int i = 0; i < 32 && at; i++) {
            EngineLayer *l = &e->layer[i];
            int len = l->end - l->start;
            if (l->patch < 0 || len == 0) continue;
            memcpy(at, l->curve + l->start, (size_t)len);
            l->curve = at;
            at += len;
        }
    }
    for (int t = 0; t < 4; t++) free(crd[t]);
    if (slot == 0) {   /* 0x4ad2a0 (Audio_LoadCollisionSounds 0x4a8aa0, after the engine) */
        if (!s_tick_added) SND_AddTick(engine_tick);
        s_tick_added = true;
    }
    return bank;
}

/* Every third snapshot (0x4ac060): each layer's volume from its curve at the rpm, the throttle's fade
   (out for the first and third tables, in for the others), the off-throttle crossfade (fade in on the
   throttle's patches, out off it), the volume; eased 8 a time. Then each layer's voices: off at 0;
   started or set to the volume, the pitch and the pan (the off-throttle patches two voices, the pan
   less and plus the spread). */
static void engine_play(Engine *e);
static void engine_tick(void) { if (s_engine[0]) engine_play(s_engine[0]); }   /* 0x4ac060 on the library's tick */

static void engine_play(Engine *e)
{
    if (e->rd == e->wr) return;
    if (0 < --e->countdown) return;
    e->countdown = 3;
    const EngineSnap *s = &e->snap[e->rd];
    for (int i = 0; i < 32; i++) {
        const EngineLayer *l = &e->layer[i];
        if (l->patch < 0) continue;
        int v = 0;
        if ((s->view != 0 && i < 0x10) || (s->view == 0 && 0xf < i)) {
            int r = s->rpm;
            if (l->start <= r && r < l->end) {
                int c = l->curve[r - l->start];
                if (c) {
                    int f = (i < 8 || (0xf < i && i < 0x18)) ? fade_out(s->throttle) : fade_in(s->throttle);
                    c *= f;
                    if (l->patch < 0x40) v = g_sound_quality < 2 ? fade_in(s->xfade) * c : 0;
                    else if (g_sound_quality < 2) v = s->spread == 0 ? fade_out(s->xfade) * c : ((fade_out(s->xfade) * 0x5e) >> 7) * c;
                    else v = c * 0x7f;
                }
            }
        }
        v = (int)((uint32_t)((v >> 3) * (int)s->vol)) >> 0x12;
        if (0x7f < v) v = 0x7f;
        int *cur = &e->vol[i];
        if (*cur + 8 < v) *cur += 8;
        else if (v < *cur - 8) *cur -= 8;
        else *cur = v;
    }
    e->pan = s->pan;
    e->spread = s->spread;
    e->pitch = s->pitch;
    e->rd = (uint8_t)((e->rd + 1) & 0x1f);
    for (int i = 0; i < 32; i++) {
        const EngineLayer *l = &e->layer[i];
        if (l->patch < 0) continue;
        EngineVoice *a = &e->v[i], *b = &e->v2[i];
        int vol = e->vol[i];
        if (vol == 0) {
            if (a->handle != -1) {
                SND_Stop(a->handle);
                if (b->handle != -1) SND_Stop(b->handle);
                a->handle = b->handle = -1;
            }
            continue;
        }
        int16_t p1, p2;
        if (l->patch < 0x40) {
            if (e->spread < 1) {
                if (a->pan == -0x8000 && 0 < a->vol) {
                    vol = 0;
                    p1 = p2 = -0x8000;
                } else {
                    p1 = p2 = (int16_t)e->pan;
                }
            } else {
                p1 = p2 = -0x8000;
            }
        } else {
            p1 = (int16_t)(e->pan - e->spread);
            p2 = (int16_t)(e->spread + e->pan);
        }
        SndParams p;
        SND_ParamsDefault(&p);
        p.pitch = (uint16_t)e->pitch;
        p.f0b = 1;
        p.pan1a = 0;
        p.vol = (int8_t)vol;
        if (a->handle == -1) {
            p.pan18 = p1;
            a->handle = SND_Play(e->bank, l->patch, &p);
            a->vol = (int8_t)vol;
            a->pan = p1;
            a->pitch = (int16_t)e->pitch;
            continue;
        }
        if (a->vol != vol) {
            SND_SetVolume(a->handle, vol);
            a->vol = (int8_t)vol;
        }
        if (a->pan != p1) {
            SND_SetPan(a->handle, (uint16_t)p1, 0);
            a->pan = p1;
        }
        if ((uint16_t)a->pitch != (uint32_t)e->pitch) {
            SND_SetPitch(a->handle, e->pitch);
            a->pitch = (int16_t)e->pitch;
        }
        if (l->patch < 0x40 || e->spread == 0) {
            if (b->handle != -1) {
                SND_Stop(b->handle);
                b->handle = -1;
            }
        } else if (b->handle == -1) {
            p.pan18 = p2;
            b->handle = SND_Play(e->bank, l->patch, &p);
            b->vol = (int8_t)vol;
            b->pan = p2;
            b->pitch = (int16_t)e->pitch;
        } else {
            if (b->vol != vol) {
                SND_SetVolume(b->handle, vol);
                b->vol = (int8_t)vol;
            }
            if (b->pan != p2) {
                SND_SetPan(b->handle, (uint16_t)p2, 0);
                b->pan = p2;
            }
            if ((uint16_t)b->pitch != (uint32_t)e->pitch) {
                SND_SetPitch(b->handle, e->pitch);
                b->pitch = (int16_t)e->pitch;
            }
        }
    }
}

/* The engine's snapshot (0x4ac550): its rpm step (rpm over the redline, 16.16, times k9), its pitch
   (k8 x the rpm plus 0.2 times the Doppler), its throttle, its view; the volume and crossfade by the
   view (in the car or chase: the throttle's; else the front-back mix by the car's heading to the
   listener). The engine's start (patches 6 .. 8) and stop (9 .. 11) sounds, the loops quiet under the
   start's. */
static void engine_snap(int slot, int vol, int rpm16, int throttle, int view, int pitch, int pan, int facing)
{
    Engine *e = slot >= 0 && slot < 2 ? s_engine[slot] : NULL;
    if (!e || ((e->wr + 1) & 0x1f) == e->rd) return;
    EngineSnap *s = &e->snap[e->wr];
    int r = e->k[9] * rpm16;
    s->rpm = (r & (int)0xffffc000u) < 0x7fc000 ? (uint16_t)(r >> 0xe) : 0x1ff;
    int64_t pp = (int64_t)(rpm16 + 0x3333) * pitch;
    uint32_t lo = (uint32_t)pp;
    s->pitch = (uint16_t)((e->k[8] * (int)((lo >> 0x10) + (int32_t)(pp >> 32) * 0x10000 + ((lo >> 0xf & 1) != 0))) >> 10);
    int t = throttle + (throttle >> 6) + (throttle >> 5);
    t = t < 0x80 ? (t < 0 ? 0 : t) : 0x80;
    s->throttle = (uint16_t)t;
    s->view = (uint16_t)view;
    int m;
    if (view < 2) {
        s->xfade = (uint16_t)(view == 0 ? e->k[3] : e->k[1]);
        s->pan = 0;
        s->spread = 0;
        PENDING_NOTE(0x655f34, "the engine's spread by the cars near the listener");
        m = view == 0 ? e->k[2] : e->k[0];
    } else {
        int base = e->k[4];
        int w = facing < 0 ? e->k[5] * base : (0x80 - base) * e->k[6];
        s->spread = 0;
        s->pan = (uint16_t)pan;
        s->xfade = (uint16_t)((int16_t)((((int64_t)w * facing >> 7) + ((w * facing >> 0x1f) & 0xffff)) >> 0x10) + base);
        m = e->k[7];
    }
    int v = m * vol >> 6;
    s->vol = (uint16_t)(0x7fe < v ? 0x7ff : v);
    int gain = 0x200;
    int cap = opt(0x5d1748);
    if (throttle < 0) {
        if (-1 < e->start_h) {
            SND_Stop(e->start_h);
            e->start_h = -1;
            e->started = 1;
        }
        if (e->started) {
            if (e->stop_h == -1) {
                SndParams p;
                SND_ParamsDefault(&p);
                p.vol = (int8_t)(s->vol < cap ? s->vol : cap);
                p.pan18 = (int16_t)pan;
                e->stop_h = SND_Play(e->bank, (view < 2 ? view : 2) + 9, &p);
            } else if (SND_Playing(e->stop_h)) {
                SND_SetVolume(e->stop_h, s->vol < cap ? s->vol : cap);
                SND_SetPan(e->stop_h, pan, 0);
            } else {
                e->started = 0;
                e->stop_h = -1;
            }
        }
    } else {
        if (-1 < e->stop_h) {
            SND_Stop(e->stop_h);
            e->stop_h = -1;
            e->started = 0;
        }
        if (!e->started) {
            if (e->start_h == -1) {
                SndParams p;
                SND_ParamsDefault(&p);
                p.vol = (int8_t)(s->vol < cap ? s->vol : cap);
                p.pan18 = (int16_t)pan;
                e->start_h = SND_Play(e->bank, (view < 2 ? view : 2) + 6, &p);
                gain = 0;
            } else if (!SND_Playing(e->start_h)) {
                e->started = 1;
                e->start_h = -1;
            } else {
                SND_SetVolume(e->start_h, s->vol < cap ? s->vol : cap);
                SND_SetPan(e->start_h, pan, 0);
                int left = SND_TimeLeft(e->start_h);
                gain = -1 < 0x200 - left ? 0x200 - left : 0;
            }
        }
    }
    s->vol = (uint16_t)((s->vol * gain) >> 9);
    e->wr = (uint8_t)((e->wr + 1) & 0x1f);
    engine_play(e);
}

/* ---- the cars' sounds (0x4a9e20, 0x4a7430) ---- */

static uint8_t s_throttle[CAR_MAX];   /* 0x6563c8: each car's eased throttle */

/* a / b in 16.16 (0x4a5f30). */
static int fixdiv(int a, int b) { return (int)((((int64_t)(a % b)) << 16) / b) + (a / b) * 0x10000; }

/* The engine's part of the car's sounds (0x4a9e20): the volume by the distance (2048 / (d + 2)), the
   rpm's lift, the gear changes; the throttle eased (up by half the gap, down by an eighth); the view
   (1 in the car, 2 chase, 3 others). The wind, rain, gearbox whine and the rest are pending. */
static void car_sounds(Car *c, int dist, int rpmvol, int pitch, int pan, int heading, int speedv, int facing)
{
    (void)heading;
    int att = fixdiv(0x8000000, dist + 0x20000);
    int a = (att + ((att >> 31) & 0xffff)) >> 0x10;
    int idx = *CAR_I(c, 0x520);
    if (idx < 0 || CAR_MAX <= idx) idx = 0;
    int thr = *CAR_I(c, 0xd98) == 0 ? c->b[0xd7f] : c->b[0xd80];
    int e = s_throttle[idx];
    if (e < thr) {
        int d = (thr - e) / 2;
        s_throttle[idx] = (uint8_t)(e + (d < 2 ? 1 : d));
    } else if (thr < e) {
        int d = (thr - e) / 8;   /* ((d + (d >> 31 & 7)) >> 3: towards zero */
        s_throttle[idx] = (uint8_t)(e + (-2 < d ? -1 : d));
    }
    int eased = s_throttle[idx] < 0xff ? s_throttle[idx] : 0xff;
    int lift = rpmvol < 0x97 ? (rpmvol < 0 ? 0 : rpmvol) : 0x96;
    int up = ((lift + 0x28) * a + (((lift + 0x28) * a >> 31) & 0x7f)) >> 7;
    int v = up <= a ? a : up;
    int vol = (v * 0x7f) >> 7;
    if (speedv != 0 || g_racecam[0].mode == 0xb) vol = (vol << 0x10) / pitch;
    int8_t shift = (int8_t)c->b[0xd84];
    if (shift && (int8_t)c->b[0xd81] < (int8_t)c->b[0xd82] && (int8_t)c->b[0xd81] != 1 && eased) {
        vol = ((shift * vol) / *SPEC_I(c, 0x54) + vol) >> 1;
        int q = eased >> 2;
        if (shift == 5) eased -= q;
        else if (shift == 4) eased >>= 1;
        else if (shift == 3) eased = q;
        else if (shift < 3) eased = 0;
        s_throttle[idx] = (uint8_t)eased;
    }
    int view;
    switch (g_racecam[0].mode) {
    case 0:
    case 0xf: view = 1; break;
    case 2:
    case 3:
    case 4: view = 2; break;
    default:
        view = (g_racecam[0].flags2 & 8) ? 0 : 3;   /* (0x443e10 for the car's interior is pending) */
        break;
    }
    int rpm16 = (int)(*CAR_F(c, 0xdb4) / *SPEC_F(c, 0x94) * 65536.0f);   /* 0x5b3e68 */
    int throttle = *CAR_I(c, 0xdb0) == 0 ? -1 : *CAR_I(c, 0x548) == 0 ? eased >> 1 : 0x7f;
    int engvol = ((opt(0x5d1748) * opt(0x5d14c8) * 0x55 >> 7) * vol) >> 0xe;
    PENDING_NOTE(0x4a99e0, "the car's wind, rain, gearbox whine, tyres and gear sounds");
    engine_snap(0, engvol, rpm16, throttle, view, DESC_I(c, 0x518) ? pitch * 2 : pitch, pan, facing);
}

static float s_listener[3], s_lastlistener[3];   /* 0x655e50, 0x655f44 */
static float s_lright[3], s_lfwd[3];              /* 0x655e5c, 0x655e74 */

/* An angle from (x, y) in 1024 steps (0x565030: by octant from the exe's arctangent bytes 0x5e24ac). */
static int angle(int x, int y)
{
    int base = 0;
    int neg = y < 0;
    if (neg) {
        y = -y;
        base = 0x200;
    }
    if (x < 0) {
        neg = !neg;
        x = -x;
        base = -base;
    }
    int lo = x, hi = y;
    if (y < x) {
        base += neg ? -0x100 : 0x100;
        neg = !neg;
        lo = y;
        hi = x;
    }
    int r;
    if (x == y) r = 0x80;
    else {
        uint32_t q;
        if (((uint32_t)lo & 0xff800000u) == 0) {
            q = ((uint32_t)(hi >> 1) + (uint32_t)lo * 0x100) / (uint32_t)hi;
        } else {   /* 0x58dd60, 0x58db30: the 32-bit fraction, rounded to 8 bits */
            uint32_t f = (uint32_t)(((uint64_t)(uint32_t)lo << 32) / (uint32_t)hi);
            q = (f >> 24) + ((f & 0x800000) != 0);
        }
        r = exe_data(0x5e24ac + q, 1)[0];
    }
    return neg ? base - r : r + base;
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* The player's car (0x4a7430): its distance from the listener, the Doppler (340 / (340 + the closing
   speed)), its pan, how it faces the listener, its rpm's volume lift. */
static void player_tick(Car *c)
{
    const float *p = CAR_F(c, 0x330);
    float d[3] = {p[0] - s_listener[0], p[1] - s_listener[1], p[2] - s_listener[2]};
    float closing = 0;
    if (d[0] != 0 || d[1] != 0 || d[2] != 0) {
        float u[3];
        float l = sqrtf(dot3(d, d));
        for (int k = 0; k < 3; k++) u[k] = d[k] / l;
        closing = dot3(u, CAR_F(c, 0x33c));
        PENDING_NOTE(0x655e44, "the listener's own speed in the Doppler");
    }
    int pitch = (int)(340.0 / ((double)closing + 340.0) * 65536.0);   /* 0x5b4b40, 0x5b4b38 */
    int pan = 0;
    if (opt(0x5d1760) && g_racecam[0].mode != 0) {
        int x = (int)(dot3(d, s_lright) * 65536.0f), y = (int)(dot3(d, s_lfwd) * 65536.0f);
        if (g_r3d_mirror) x = -x;
        pan = (angle(x >> 8, y >> 8) & 0x3ff) << 6;
    }
    int dist = (int)(sqrtf(dot3(d, d)) * 65536.0f);   /* 0x532110 */
    int facing = 0;
    if (dist != 0) {
        int f = fixdiv((int)(dot3(d, CAR_F(c, 0x37c)) * 65536.0f), dist);
        facing = f < 0x10000 ? (f <= -0x10000 ? -0x10000 : f) : 0x10000;
    }
    float red = *SPEC_F(c, 0x94) + (DESC_I(c, 0xc) == 1 ? 1000.0f : 2000.0f);   /* (+0x558: the description) */
    if (5 < *(uint16_t *)(void *)(c->b + 0x418)) red -= 500.0f;
    int rpmvol = (int)(*CAR_F(c, 0xdb4) * 127.0f / red);
    int speedv = abs((int)(*CAR_F(c, 0xec0) * 65536.0f));
    int heading = 0;
    if (opt(0x5d1760)) {
        int x = (int)(dot3(CAR_F(c, 0x3d0), s_lright) * 65536.0f), y = (int)(dot3(CAR_F(c, 0x3d0), s_lfwd) * 65536.0f);
        if (g_r3d_mirror) x = -x;
        heading = (angle(x >> 8, y >> 8) & 0x3ff) << 6;
    }
    car_sounds(c, dist, rpmvol, pitch, pan, heading, speedv, facing);
}

void Audio_Tick(void)
{
    if (!s_on) return;
    /* 0x465360: the listener at the first camera's eye, its axes (the view's adjustments of the eye,
       0x464e40's, are left out). */
    const RaceCamera *cam = &g_racecam[0];
    memcpy(s_listener, cam->eye, sizeof s_listener);
    memcpy(s_lright, cam->orient, sizeof s_lright);
    memcpy(s_lfwd, cam->orient + 6, sizeof s_lfwd);
    PENDING_NOTE(0x4b07f0, "the streamed sounds (0x4b07f0), 0x4a8150, 0x4a7b90");
    if (g_humans.n > 0) player_tick(g_humans.c[0]);
    PENDING_NOTE(0x4a6e50, "the opponents' sounds (0x4a6e50)");
    memcpy(s_lastlistener, s_listener, sizeof s_lastlistener);
}
