/* The cars' light: see carlight.h. */
#include "render/carlight.h"
#include "render/dynlight.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "game/car.h"
#include "game/road.h"
#include "game/track.h"
#include "game/wtree.h"
#include "pending.h"
#include "app.h"
#include "render/car_ini.h"
#include "render/r3dcar.h"
#include "game/race.h"
#include "render/render.h"
#include "render/rtex.h"
#include "render/sky.h"
#include "render/drive_selftest.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CarEnvMap *g_carenv;

/* ---- the environment map (R3DCar_LoadEnvMap 0x440900) ---- */

static void vec4(const IniFile *f, float out[4], uint32_t key, float a, float r, float g, float b)
{
    const float def[4] = {a, r, g, b};
    IniFile_GetVec4(f, out, exe_str(key), def);
}

/* <track>.env's textures into the map's bank (class 2, the render system's manager), then <track>.map:
   the header's count and colours (tint, ambient, interior, interior diffuse, light decal), and each
   section%d's slice. Not in the front end (0x65b928). */
void CarEnv_Load(const char *track)
{
    CarEnv_Free();
    CarEnvMap *m = calloc(1, sizeof *m);
    if (!m) return;
    RTexBank *bank = calloc(1, sizeof *bank);
    if (bank) RTexBank_ctor(bank, g_render ? g_render->textures : NULL, 2);   /* 0x44cf80 */
    m->bank = bank;
    char path[260];
    snprintf(path, sizeof path, exe_str(0x5cdaec), g_track_dir, track);   /* "%s%s.env" */
    if (bank) RTexBank_loadfile(bank, path, 0);   /* 0x44d140 */
    snprintf(path, sizeof path, exe_str(0x5cdae0), g_track_dir, track);   /* "%s%s.map" */
    IniFile f = {0};
    if (IniFile_Load(&f, path) && -1 < IniData_SelectSection(f.ini, exe_str(0x5cdad8))) {   /* header */
        int n = IniFile_GetInt(&f, exe_str(0x5cd658), 0);   /* count */
        vec4(&f, m->tint, 0x5cd714, 1, 1, 1, 1);
        vec4(&f, m->ambient, 0x5cdad0, 1, 0.3f, 0.3f, 0.3f);   /* 0x3e99999a */
        vec4(&f, m->interior, 0x5cdac4, 1, 1, 1, 1);
        vec4(&f, m->interiordiffuse, 0x5cdab4, 1, 1, 1, 1);
        vec4(&f, m->lightdecal, 0x5cdaa8, 1, 1, 1, 1);
        if (0 < n) m->slices = calloc((size_t)n, sizeof *m->slices);
        for (int i = 0; m->slices && i < n; i++) {
            char sec[32];
            snprintf(sec, sizeof sec, exe_str(0x5cda9c), i);   /* section%d */
            if (IniData_SelectSection(f.ini, sec) < 0) continue;
            CarEnvSlice *s = &m->slices[m->nslices++];
            const uint32_t ENV0 = 0x30564e45;
            s->start = IniFile_GetInt(&f, exe_str(0x5cda90), 0);
            s->end = IniFile_GetInt(&f, exe_str(0x5cda84), 0);
            s->tex[0] = (uint32_t)IniFile_GetInt(&f, exe_str(0x5cda74), (int)ENV0);   /* texture1.open */
            s->tex[3] = (uint32_t)IniFile_GetInt(&f, exe_str(0x5cda60), (int)ENV0);   /* texture1.covered */
            s->tex[1] = (uint32_t)IniFile_GetInt(&f, exe_str(0x5cda50), (int)ENV0);
            s->tex[4] = (uint32_t)IniFile_GetInt(&f, exe_str(0x5cda3c), (int)ENV0);
            s->tex[2] = (uint32_t)IniFile_GetInt(&f, exe_str(0x5cda2c), (int)ENV0);
            s->tex[5] = (uint32_t)IniFile_GetInt(&f, exe_str(0x5cda18), (int)ENV0);
            s->size = IniFile_GetFloat(&f, exe_str(0x5cda0c), 6.0f);   /* texturesize */
            s->aspect = IniFile_GetFloat(&f, exe_str(0x5cda04), 1.0f);
        }
        m->cur = m->nslices ? &m->slices[0] : NULL;
    }
    IniFile_Free(&f);
    g_carenv = m;
}

void CarEnv_Free(void)   /* 0x440fd0 */
{
    CarEnvMap *m = g_carenv;
    g_carenv = NULL;
    if (!m) return;
    if (m->bank) {
        RTexBank_dtor(m->bank);
        free(m->bank);
    }
    free(m->slices);
    free(m);
}

/* The slice holding the node (0x441070), else none. */
static void select_slice(CarEnvMap *m, uint32_t node)
{
    m->cur = NULL;
    for (int i = 0; i < m->nslices; i++)
        if ((uint32_t)m->slices[i].start <= node && node <= (uint32_t)m->slices[i].end) {
            m->cur = &m->slices[i];
            return;
        }
}

/* The slice's texture of a level, open or covered (0x4410b0), NULL without a slice. */
static struct Texture *slice_texture(const CarEnvMap *m, bool covered, int level)
{
    if (!m->cur || !m->bank) return NULL;
    return RTexBank_find(m->bank, 0, m->cur->tex[level + (covered ? 3 : 0)]);
}

/* ---- the car's light (R3DCar_v15 0x43a490) ---- */

static void set4(CarLight *l, int off, float a, float r, float g, float b)
{
    float *f = &l->f[off / 4];
    f[0] = a;
    f[1] = r;
    f[2] = g;
    f[3] = b;
}

static float *at(CarLight *l, int off) { return &l->f[off / 4]; }

void CarLight_Update(CarLight *l, Car *car)
{
    const float *pos = CAR_F(car, 0x330);
    memcpy(at(l, 0xe8), pos, 12);
    if (g_r3d_frontend) {   /* 0x65b928 */
        *at(l, 0x8) = 1.0f;
        l->tex[0] = l->tex[1] = l->tex[2] = NULL;
        ((uint8_t *)l->f)[0x190] = 0;
        return;
    }
    set4(l, 0x8c, 1, 1, 1, 1);
    *at(l, 0xcc) = 5.0f;   /* 0x40a00000 */
    set4(l, 0x9c, 1, 0.2f, 0.2f, 0.2f);
    set4(l, 0xac, 1, 0.8f, 0.8f, 0.8f);
    set4(l, 0xbc, 1, 1, 1, 1);
    WSurface s = car->body;   /* (the car's tracker, +8, copied) */
    bool covered = (WSurface_FindAbove(&s, pos, NULL) & 0xc000) != 0;   /* 0x473290 */
    /* The nearest dynamic light (0x456030) when the cars' lights are on: its record (0x440850) and its
       texture (0x440840); else no texture, the record left as it was. */
    Texture *lt = NULL;
    const DynLightRec *dl = DynLights_Nearest(pos, &lt);
    if (!dl || !g_render_carlights) lt = NULL;
    else memcpy(at(l, 0x14c), dl, 0x40);
    l->lighttex = lt;
    CarEnvMap *m = g_carenv;
    if (m) select_slice(m, (uint32_t)(uint16_t)s.node);
    for (int i = 0; i < 3; i++) l->tex[i] = m && g_render_carenv ? slice_texture(m, covered, i) : NULL;   /* 0x4407f0 */
    PENDING_NOTE(0x4407f0, "the reflection textures' states (0x5377e0, 0x5361e0)");
    *at(l, 0x4) = 1.0f / (m && m->cur ? m->cur->size : 4.0f);   /* 0x4410f0 (0x5b2460: 4 without a slice) */
    *at(l, 0x8) = m && m->cur ? m->cur->aspect : 1.0f;            /* 0x441110 */
    /* the car's place across and along the road at its node, in sixths (0x5b2420) */
    if (g_road && 0 <= s.node && s.node < g_road->nnodes) {
        const uint8_t *nd = g_road->nodes + s.node * ROAD_NODE_SIZE;
        float np[3], a[3], b[3];
        memcpy(np, nd + 4, 12);
        memcpy(a, nd + 0x28, 12);
        memcpy(b, nd + 0x1c, 12);
        float d[3] = {pos[0] - np[0], pos[1] - np[1], pos[2] - np[2]};
        *at(l, 0x144) = (a[0] * d[0] + a[1] * d[1] + a[2] * d[2]) * 0.16666667f;
        *at(l, 0x148) = (b[0] * d[0] + b[1] * d[1] + b[2] * d[2]) * 0.16666667f;
    }
    /* the road's axes at the car (+0x3b8) as a matrix, transposed (0x53a3c0) */
    {
        static const float zero[3] = {0, 0, 0};
        EaglMatrix rm;
        EAGL_MatFromRotTrans(&rm, CAR_F(car, 0x3b8), zero);
        float *o = at(l, 0x104);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) o[r * 4 + c] = rm.m[c * 4 + r];
    }
    /* the light under the car (0x474600), then the map's colours */
    float g[4];
    WSurface_Light(&s, pos, g);
    static const float none[4] = {1, 1, 1, 1}, amb3[4] = {1, 0.3f, 0.3f, 0.3f};
    const float *tint = m ? m->tint : none, *amb = m ? m->ambient : amb3, *in = m ? m->interior : none,
                *ind = m ? m->interiordiffuse : none, *dec = m ? m->lightdecal : none;
    float *a0 = at(l, 0x18), *a1 = at(l, 0x28), *a2 = at(l, 0x38), *d0 = at(l, 0x58), *d1 = at(l, 0x68), *d2 = at(l, 0x78);
    a0[0] = 1.0f;
    for (int k = 1; k < 4; k++) a0[k] = g[k] * amb[k];
    for (int k = 0; k < 4; k++) a1[k] = a0[k] * in[k];
    for (int k = 0; k < 4; k++) a2[k] = a0[k] * dec[k];
    for (int k = 0; k < 4; k++) d0[k] = g[k] * tint[k];
    d1[0] = g[0];
    d0[0] = 1.0f;
    for (int k = 1; k < 4; k++) d1[k] = g[k];
    for (int k = 0; k < 4; k++) d1[k] *= ind[k];
    d1[0] = 1.0f;
    for (int k = 0; k < 4; k++) d2[k] = d0[k] * dec[k];
    uint8_t *b = (uint8_t *)l->f;
    b[0x88] = 1;
    float *dir = at(l, 0x48);
    if (!covered) {
        /* (the SPECULAR option 0x657d64 isn't read from fe.txt yet: 0) */
        b[0x89] = 2 <= g_race_view ? 1 : 0;   /* 0x657424 */
        set4(l, 0x9c, 1, 0.2f, 0.2f, 0.2f);
        set4(l, 0xac, 1, 0.8f, 0.8f, 0.8f);
        set4(l, 0xbc, 1, 1, 1, 1);
        *at(l, 0xcc) = 10.0f;   /* 0x41200000 */
        /* away from the sun (0x627e5c +0x11c), unit */
        float sun[4] = {0, 1, 0, 1};
        Sky_SunPosition(sun);
        float x = -sun[0], y = -sun[1], z = -sun[2];
        float n = 1.0f / sqrtf(x * x + y * y + z * z);   /* 0x5b2750 */
        dir[0] = n * x;
        dir[1] = n * y;
        dir[2] = n * z;
        dir[3] = 0;
    } else {
        b[0x89] = 0;
        dir[0] = 0;
        dir[1] = -1.0f;
        dir[2] = 0;
        dir[3] = 0;
    }
    PENDING_NOTE(0x43a490, "the cars' light in the mirror's view (+0x190: the drawing camera's +0x2c)");
    b[0x190] = 0;
}

static float f32(uint32_t va) { union { uint32_t u; float f; } c = {exe_u32(va)}; return c.f; }

/* The +0x64 object's eases (GetTickCount): from its start to its target over its length, the target
   once past it (the length then cleared). */
static float ease(uint32_t now, uint32_t *since, uint32_t *dur, float from, float to)
{
    if (*dur == 0) return to;
    float t = (float)(now - *since) / (float)(int32_t)*dur;
    if (1.0f < t) {
        *dur = 0;
        return to;
    }
    return (to - from) * t + from;
}

/* Slot 2, the spoiler: out over 2 s above 80 km/h (22.2 m/s), back in under 50 (13.9), each move only
   once the last is done; above the speed it starts again every 2 s from where it is. */
static void spoiler(R3DCar *r, Car *car, uint32_t now)
{
    float speed = *CAR_F(car, 0xec4);
    bool restart = false;
    if (f32(0x5b3698) < speed && r->spoilerease.dur <= now - r->spoilerease.since) {
        float to = r->anim[2].frames;
        bool mid = false;
        if (r->spoilerease.dur) {
            float t = (float)(now - r->spoilerease.since) / (float)(int32_t)r->spoilerease.dur;
            if (t <= 1.0f) {
                float old = r->spoilerease.to;
                r->spoilerease.to = to;
                r->spoilerease.from = (old - r->spoilerease.from) * t + r->spoilerease.from;
                mid = true;
            } else {
                r->spoilerease.dur = 0;
            }
        }
        if (!mid) {
            r->spoilerease.from = r->spoilerease.to;
            r->spoilerease.to = to;
        }
        restart = true;
    } else if (speed < f32(0x5b3694) && r->spoilerease.dur <= now - r->spoilerease.since) {
        r->spoilerease.from = ease(now, &r->spoilerease.since, &r->spoilerease.dur, r->spoilerease.from,
                                   r->spoilerease.to);
        r->spoilerease.to = 0;
        restart = true;
    }
    if (restart) {
        r->spoilerease.since = now;
        r->spoilerease.dur = 2000;
    }
    R3DCar_AnimReset(r, 2, ease(now, &r->spoilerease.since, &r->spoilerease.dur, r->spoilerease.from,
                                r->spoilerease.to));
}

/* The slots' timed paths (+0x484): along the path's frames over its length (the original counts CPU
   cycles, rdtsc, with lengths of the CPU's clock 0x5ce908 times the seconds; here milliseconds), then
   none. */
static void paths_step(R3DCar *r, uint32_t now)
{
    for (int i = 0; i < 8; i++) {
        R3DCarPath *p = &r->path[i];
        if (!p->count || !p->table) continue;
        uint32_t el = now - p->start;
        if (el < p->dur) {
            float x = (float)el / (float)(int32_t)p->dur * (float)(int32_t)(p->count - 1);
            int ia = (int)lrintf(x - 0.5f), ib = (int)lrintf(x + 1.0f - 0.5f);
            p->a = exe_u32(p->table + 4u * (uint32_t)ia);
            p->b = exe_u32(p->table + 4u * (uint32_t)ib);
            p->t = x - (float)ia;
        } else {
            p->table = 0;
            p->count = 0;
        }
    }
}

static void path_start(R3DCar *r, int i, uint32_t count, uint32_t table, uint32_t now, uint32_t dur)
{
    R3DCarPath *p = &r->path[i];
    p->count = count;
    p->table = table;
    p->start = now;
    p->dur = dur;
    if (table && 1 < count) {
        p->a = exe_u32(table);
        p->b = exe_u32(table + 4);
    }
    p->t = 0;
}

/* A slot along its path: the path's two frames and how far (marked moved when any differs). */
static void path_follow(R3DCar *r, int i, float t)
{
    R3DCarAnim *an = &r->anim[i];
    if (an->pos != (int)r->path[i].a) an->pos = (int)r->path[i].a, an->on = 1;
    if (an->dir != (int)r->path[i].b) an->dir = (int)r->path[i].b, an->on = 1;
    if (t != an->t) an->t = t, an->on = 1;
}

static int wheel_frame(float wheel)   /* the steering wheel's frame, 0 .. 9 */
{
    if (wheel < 0) return 0;
    if (wheel <= f32(0x5b368c)) return (int)lrintf(wheel - 0.5f);
    return 9;
}

/* Slot 4, the driver's hand: on the wheel at its frame; at a gear change (+0xd84, its length in 1/32 s
   units of 2 s) along the path from the wheel's frame to the lever and back (0x5cd3ac .. 0x5cd4c0,
   by the wheel's frame); to the handbrake (+0xd86) and back by the three-frame paths 0x5cd2bc /
   0x5cd334 over 1/8 s; frame 19 while it holds the handbrake. */
static void hand(R3DCar *r, Car *car, float wheel, uint32_t now)
{
    R3DCarPath *p = &r->path[4];
    if (p->count && p->table) {
        path_follow(r, 4, p->t);
        return;
    }
    int8_t shift = (int8_t)car->b[0xd84];
    if (0 < shift && *CAR_I(car, 0xb50) == 0 && !g_drive_paused /* 0x606a90 (0x606a98, the game's pause, is never set here) */) {
        static const uint8_t counts[10] = {11, 11, 9, 9, 7, 7, 5, 5, 5, 5};
        static const uint32_t tables[10] = {0x5cd3ac, 0x5cd3d8, 0x5cd404, 0x5cd428, 0x5cd44c,
                                            0x5cd468, 0x5cd484, 0x5cd498, 0x5cd4ac, 0x5cd4c0};
        int k = wheel_frame(wheel);
        path_start(r, 4, counts[k], tables[k], now, (uint32_t)(shift * 2000 / 32));   /* (x 0x5b27b4) */
    }
    bool hb = car->b[0xd86] != 0;
    if ((!p->count || !p->table) && hb != (r->handbrake != 0)) {
        r->handbrake = hb;
        int k = wheel_frame(wheel);
        path_start(r, 4, 3, (hb ? 0x5cd2bcu : 0x5cd334u) + 0xcu * (uint32_t)k, now, 125);   /* 0x5b3688: 1/8 s */
    }
    if (!p->count || !p->table) R3DCar_AnimReset(r, 4, r->handbrake ? 19.0f : wheel);
}

/* Slot 6, the gear lever: by the lever's side-to-side (+0xe08, 0.1 a unit about frame 2, still within
   0.1); into and out of reverse (+0xd82 0) along the paths 0x5cd568 / 0x5cd590 over 1/4 s; frame 7 in
   reverse; pushed forward (+0xe04 < 0) between its frame and 5. */
static void lever(R3DCar *r, Car *car, uint32_t now)
{
    float x = -*CAR_F(car, 0xe08);
    if (fabsf(x) < f32(0x5b2604)) x = 0;
    float v = f32(0x5b3594) * x + f32(0x5b2580);
    R3DCarPath *p = &r->path[6];
    float t;
    if (!p->count || !p->table) {
        bool rev = car->b[0xd82] == 0;
        if ((r->reverse != 0) != rev) {
            r->reverse = rev;
            int k = v < 0 ? 0 : v <= f32(0x5b2404) ? (int)v : 4;
            uint32_t at = (rev ? 0x5cd568u : 0x5cd590u) + 8u * (uint32_t)k;
            path_start(r, 6, exe_u32(at), exe_u32(at + 4), now, 250);   /* 0x5b3684: 1/4 s */
        }
        if (p->count && p->table) return;
        if (r->reverse) {
            R3DCar_AnimReset(r, 6, 7.0f);
            return;
        }
        float y = *CAR_F(car, 0xe04);
        if (fabsf(y) < f32(0x5b2604) || 0 <= y) {
            R3DCar_AnimReset(r, 6, v);
            return;
        }
        R3DCarAnim *an = &r->anim[6];
        int a = (int)lrintf(v - 0.5f);
        if (an->pos != a) an->pos = a, an->on = 1;
        if (an->dir != 5) an->dir = 5, an->on = 1;
        t = -(f32(0x5b3598) * y);
    } else {
        t = p->t;
        R3DCarAnim *an = &r->anim[6];
        if (an->pos != (int)p->a) an->pos = (int)p->a, an->on = 1;
        if (an->dir != (int)p->b) an->dir = (int)p->b, an->on = 1;
    }
    if (t != r->anim[6].t) r->anim[6].t = t, r->anim[6].on = 1;
}

/* ---- the car's hinges (R3DCar_Animate, slot 9, 0x438370) ----

   Per wheel (the simulation's wheel records, 0xc4 bytes from +0x7f8): its spin (+0x48, negated) about x,
   its steering (+0x74, or in a replay the recorded +0xac8 / +0xacc of the front pair) x 1/216 (negated in
   the mirror) about y, its +0x30 about z. The steering wheel (5) turns by the fronts' sum, within +-4.5/23;
   the rev counter's needle (0xb) by the description's scale (+0x528) x rpm / 1000 + its zero (+0x524), the
   speedometer's (0xc) by +0x530 x speed x 0.144 + +0x52c; the four shadows (0x12 .. 0x15) take their
   wheel's angles without the spin; 0x16 turns with the clock (1/128 a tick). In the front end's showroom
   (car NULL) only the lids, the headlights and the timed paths. */
void CarAnim_Update(R3DCar *r, Car *car)
{
    float (*h)[3] = r->hinge;
    if (!h) return;
    if (g_r3d_frontend) {
        /* The showroom's: hinge 0x10 from +0x57c .. +0x588 and +0x74, which nothing writes (0); the
           lids and the pop-up headlights eased; then the low detail's hand and lever, or the timed paths */
        uint32_t now = app_ticks_ms();   /* GetTickCount */
        h[0x10][0] = 0;
        h[8][0] = ease(now, &r->hoodease.since, &r->hoodease.dur, r->hoodease.from, r->hoodease.to);
        h[0x11][0] = ease(now, &r->headease.since, &r->headease.dur, r->headease.from, r->headease.to);
        h[9][0] = ease(now, &r->trunkease.since, &r->trunkease.dur, r->trunkease.from, r->trunkease.to);
        if (g_r3d_quality < 1) {
            R3DCar_AnimReset(r, 4, 4.5f);
            R3DCar_AnimReset(r, 5, 4.5f);
            R3DCar_AnimReset(r, 6, 5.0f);
            return;
        }
        paths_step(r, now);
        return;
    }
    float k = g_r3d_mirror ? -0.0046296297f : 0.0046296297f;   /* 1/216 */
    if (g_race_view < 2) {
        h[1][1] = k * *CAR_F(car, 0x86c);
        h[2][1] = k * *CAR_F(car, 0x930);
    } else {
        h[1][1] = k * *CAR_F(car, 0xac8);
        h[2][1] = k * *CAR_F(car, 0xacc);
    }
    h[1][2] = *CAR_F(car, 0x828);
    h[2][2] = *CAR_F(car, 0x8ec);
    h[3][2] = *CAR_F(car, 0x9b0);
    h[4][2] = *CAR_F(car, 0xa74);
    const uint8_t *d = CAR_DESCB(car);
    float f;
    memcpy(&f, d + 0x528, 4);
    float z;
    memcpy(&z, d + 0x524, 4);
    h[0xb][2] = f * *CAR_F(car, 0xdb4) * 0.001f + z;   /* 0x5b3060 */
    memcpy(&f, d + 0x530, 4);
    memcpy(&z, d + 0x52c, 4);
    h[0xc][2] = f * *CAR_F(car, 0xec4) * 0.144f + z;   /* 0x5b36a4 */
    float s = h[2][1] + h[1][1], lo = -4.5f / 23.0f, hi = 4.5f / 23.0f;   /* 0x5b36a0, 0x5b369c, 0x5b3590 */
    h[5][2] = s < lo ? lo : hi < s ? hi : s;
    h[1][0] = -*CAR_F(car, 0x840);
    h[2][0] = -*CAR_F(car, 0x904);
    h[3][0] = -*CAR_F(car, 0x9c8);
    h[4][0] = -*CAR_F(car, 0xa8c);
    for (int w = 0; w < 4; w++) {
        memcpy(h[0x12 + w], h[1 + w], sizeof h[0]);
        h[0x12 + w][0] = 0;
    }
    h[0x16][1] = (float)g_sim_tick * 0.0078125f;   /* (TIMER_ticks x 0x5b273c; the port's clock is the simulation's) */
    uint32_t now = app_ticks_ms();   /* GetTickCount */
    h[8][0] = ease(now, &r->hoodease.since, &r->hoodease.dur, r->hoodease.from, r->hoodease.to);
    h[9][0] = ease(now, &r->trunkease.since, &r->trunkease.dur, r->trunkease.from, r->trunkease.to);
    h[0x11][0] = ease(now, &r->headease.since, &r->headease.dur, r->headease.from, r->headease.to);
    spoiler(r, car, now);
    if (DESC_I(car, 0x518)) {   /* slot 7 by the speed, none in reverse */
        float v = 0;
        if (car->b[0xd82]) {
            v = *CAR_F(car, 0xec4) * f32(0x5b3690);
            if (1.0f < v) v = 1.0f;
        }
        R3DCar_AnimReset(r, 7, v);
    }
    if (g_r3d_quality < 1) {   /* the cockpit's hand on the wheel's middle, the lever in neutral */
        R3DCar_AnimReset(r, 4, 4.5f);
        R3DCar_AnimReset(r, 5, 4.5f);
        R3DCar_AnimReset(r, 6, 5.0f);
        return;
    }
    paths_step(r, now);
    float wheel = f32(0x5b3590) * h[5][2] + f32(0x5b369c);   /* the steering wheel's frame: 23 x turn + 4.5 */
    R3DCar_AnimReset(r, 5, wheel);
    hand(r, car, wheel, now);
    lever(r, car, now);
}

/* ---- the lights' state (R3DCar_v6 0x439290) ---- */

static void on(R3DCar *r, int id, bool v) { r->lighton[id] = v; }       /* 0x4435a0 */
static void level(R3DCar *r, int id, float v) { r->lightlevel[id] = v; }   /* 0x4435d0 */

/* Ids: 0, 1 the headlights (by the car's lights switch, +0xd87 bit 0; their level the pop-ups' extent
   eased over a second, or 1), 2, 3 the brake lights (the brake, or in reverse the throttle), 4 .. 7 the
   indicators (bits 3 and 4; both blinking on a wreck, state 3; every 64 ticks), 8 reversing (gear 0),
   10, 11 the rear lights' halves (an indicator's blink, else the brake), 12, 13 always; 9 never. With the
   headlights on the rear lights glow at 0.6 unbraked. The damage of a corner over 30 puts its lights
   out. The front end's showroom: headlights 0.8, rear lights 0.6, reversing at 0. A switch while the
   fade runs is let pass (the switch kept, the fade not restarted), as the original. */
void R3DCar_SetLights(R3DCar *r, Car *c)
{
    r->road = *CAR_F(c, 0x41c);
    uint8_t *b = c->b;
    int last = 0xb;
    if (g_r3d_frontend) {
        on(r, 0, 1), level(r, 0, f32(0x5b359c));
        on(r, 1, 1), level(r, 1, f32(0x5b359c));
        on(r, 2, 1), level(r, 2, f32(0x5b35a4));
        on(r, 3, 1), level(r, 3, f32(0x5b35a4));
        on(r, 10, 1), level(r, 10, f32(0x5b35a4));
        on(r, 11, 1), level(r, 11, f32(0x5b35a4));
        on(r, 8, 1), level(r, 8, f32(0x5b35a0));
        last = 9;
    } else {
        bool brake = (*CAR_I(c, 0xd98) == 0 ? b[0xd80] : b[0xd7f]) != 0;
        on(r, 8, b[0xd82] == 0);
        on(r, 2, brake);
        on(r, 3, brake);
        int32_t ticks = (int32_t)(uint32_t)((uint64_t)app_ticks_ms() * 128u / 1000u);   /* TIMER_ticks */
        uint32_t blink = (uint32_t)(ticks / (128 >> 1));   /* (the timer's rate 0x5deb48, 128, halved) */
        uint32_t sw = (uint32_t)(int32_t)(int8_t)b[0xd87];
        bool right = (sw >> 4) & 1, left = (sw >> 3) & 1;
        uint32_t v4;
        if (*CAR_I(c, 0xb50) == 3) {
            left = right = true;
            v4 = blink & 1;
        } else {
            v4 = left ? blink & 1 : 0;
        }
        on(r, 4, v4);
        on(r, 6, left ? ~blink & 1 : 0);
        on(r, 5, right ? blink & 1 : 0);
        on(r, 7, right ? ~blink & 1 : 0);
        on(r, 10, left ? blink & 1 : brake);
        on(r, 11, right ? blink & 1 : brake);
        uint8_t head = b[0xd87] & 1;
        on(r, 0, head);
        on(r, 1, head);
        on(r, 9, 0);
        on(r, 12, 1);
        on(r, 13, 1);
        uint32_t now = app_ticks_ms();   /* GetTickCount */
        if (r->headon != head) {
            r->headon = head;
            if (r->headease.dur <= now - r->headease.since) {
                if (!head) {
                    r->headease.from = ease(now, &r->headease.since, &r->headease.dur, r->headease.from, r->headease.to);
                    r->headease.to = 0;
                } else {
                    float to = r->headease.to;
                    if (r->headease.dur != 0) {
                        float t = (float)(now - r->headease.since) / (float)(int32_t)r->headease.dur;
                        if (t <= 1.0f) r->headease.from = (to - r->headease.from) * t + r->headease.from;
                        else {
                            r->headease.dur = 0;
                            r->headease.from = to;
                        }
                    } else {
                        r->headease.from = to;
                    }
                    r->headease.to = r->headlightextent;
                }
                r->headease.since = now;
                r->headease.dur = 1000;
            }
        }
        float lv;
        if (r->headlightextent == 0) {
            lv = head ? 1.0f : 0.0f;
        } else {
            lv = ease(now, &r->headease.since, &r->headease.dur, r->headease.from, r->headease.to) / r->headlightextent;
            if (!head && 0 < lv) {
                on(r, 0, 1);
                on(r, 1, 1);
            }
        }
        level(r, 0, lv);
        level(r, 1, lv);
        if (!head || brake) {
            level(r, 2, 1.0f);
            level(r, 3, 1.0f);
            level(r, 10, 1.0f);
            level(r, 11, 1.0f);
            last = -1;
        } else {
            level(r, 2, f32(0x5b35a8));
            level(r, 3, f32(0x5b35a8));
            level(r, 10, left ? 1.0f : f32(0x5b35a8));
            level(r, 11, right ? 1.0f : f32(0x5b35a8));
            on(r, 2, 1);
            on(r, 3, 1);
            if (!left) on(r, 10, 1);
            if (right) last = -1;
        }
    }
    if (0 <= last) on(r, last, 1);
    float lim = f32(0x5b2408);   /* 30 */
    if (lim < *CAR_F(c, 0x4e4)) on(r, 0, 0), on(r, 4, 0);
    if (lim < *CAR_F(c, 0x4ec)) on(r, 1, 0), on(r, 5, 0);
    if (lim < *CAR_F(c, 0x4f4)) on(r, 3, 0), on(r, 7, 0), on(r, 11, 0);
    if (lim < *CAR_F(c, 0x4fc)) on(r, 2, 0), on(r, 6, 0), on(r, 10, 0);
    if (lim < *CAR_F(c, 0x4f8)) on(r, 8, 0);
    if (lim < *CAR_F(c, 0x504)) on(r, 12, 0), on(r, 13, 0);
}
