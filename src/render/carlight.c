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
#include "render/car_ini.h"
#include "render/r3dcar.h"
#include "game/race.h"
#include "render/render.h"
#include "render/rtex.h"
#include "render/sky.h"
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

/* ---- the car's hinges (R3DCar_Animate, slot 9, 0x438370: the race's part) ----

   Per wheel (the simulation's wheel records, 0xc4 bytes from +0x7f8): its spin (+0x48, negated) about x,
   its steering (+0x74, or in a replay the recorded +0xac8 / +0xacc of the front pair) x 1/216 (negated in
   the mirror) about y, its +0x30 about z. The steering wheel (5) turns by the fronts' sum, within +-4.5/23;
   the rev counter's needle (0xb) by the description's scale (+0x528) x rpm / 1000 + its zero (+0x524), the
   speedometer's (0xc) by +0x530 x speed x 0.144 + +0x52c; the four shadows (0x12 .. 0x15) take their
   wheel's angles without the spin; 0x16 turns with the clock (1/128 a tick). */
void CarAnim_Update(R3DCar *r, Car *car)
{
    float (*h)[3] = r->hinge;
    if (!h || g_r3d_frontend) {
        PENDING_NOTE(0x438370, "R3DCar_Animate's showroom part (the timed doors, lids, roof)");
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
    PENDING_NOTE(0x438370, "R3DCar_Animate's doors, lids, wipers and texture animations (+0x64's timers, 0x490)");
}
