/* The headlights at night (render module 0x452460 .. 0x452fe0): the beam's table (night.psh's "nght", a
   128 x 128 byte image), the viewed car's headlight space each frame (0x452590), the light of a point
   (0x4527e0) and the track's vertex light layer (Headlights::LayerVertexLight 0x452fe0, rlayers.c). */
#include "render/headlights.h"
#include "eagl/eagl.h"
#include "exe.h"
#include "pending.h"
#include "vfs.h"
#include "render/r3dcar.h"
#include "render/render.h"
#include "game/camera.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float K(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint8_t *s_psh;               /* 0x61c164 */
static const uint8_t *s_beam;        /* 0x61e1cc: the table's bytes (the shape's +0x10) */
static Car *s_car;                   /* 0x61bcc8 */
uint32_t g_headlight_colour;         /* 0x61bd4c */
static float s_zoff, s_zscale = 1.28f, s_xscale = 3.62f;   /* 0x61bd50, 0x61bd54, 0x61bd58 (the 0x61e178,
                                                              0x61d170 copies taken once) */
static EaglMatrix s_space, s_tilted; /* 0x61bccc, 0x61bd0c */
static Car *s_cop;                   /* 0x61e180: the police car whose roof light shines */
static EaglMatrix s_copspace;        /* 0x61e184 */
static uint32_t s_cop_col[2];        /* 0x61e1c4 red, 0x61e1c8 blue */
static float s_cop_light[256];       /* (0x61bd60 again in the original) */
static bool s_cop_neg[256];          /* the points' side (0x61d178's z sign) */
float g_headlight_light[256];        /* 0x61bd60 */

void Headlights_Load(void)   /* 0x452460 */
{
    free(s_psh);
    s_psh = NULL;
    s_beam = NULL;
    if (g_r3d_night) {
        char path[256];
        snprintf(path, sizeof path, exe_str(0x5cec88), "GameData\\Render\\");   /* "%snight.psh" (0x65b30c) */
        /* (an "SHPP" shape file: its directory of (name, offset) pairs from +0x10, the entries' 16-byte
           headers; read as it is, the original's 0x59dab0 / 0x556110 / 0x53c3b0) */
        size_t n = 0;
        s_psh = vfs_read_all(path, &n);
        if (s_psh && 16 <= n) {
            uint32_t cnt;
            memcpy(&cnt, s_psh + 8, 4);
            for (uint32_t i = 0; i < cnt && 0x10 + 8 * (i + 1) <= n; i++) {
                uint32_t name, off;
                memcpy(&name, s_psh + 0x10 + 8 * i, 4);
                memcpy(&off, s_psh + 0x14 + 8 * i, 4);
                if (name == 0x7468676e && off + 0x10 + 0x4000 <= n) s_beam = s_psh + off + 0x10;   /* "nght" */
            }
        }
    }
    s_car = NULL;
    s_cop = NULL;
    PENDING_NOTE(0x44d1f0, "the headlights' 'HEAD' texture (0x61e1d4)");
}

void Headlights_Free(void)   /* 0x452530 */
{
    free(s_psh);
    s_psh = NULL;
    s_beam = NULL;
}

void Headlights_SetCar(Car *car)   /* 0x452550 */
{
    if (!g_r3d_night) return;
    g_headlight_colour = 0xff808080u;
    s_car = car;
    s_zoff = 0;
    s_zscale = 1.28f;   /* 0x3fa3d70a */
    s_xscale = 3.62f;   /* 0x4067ae14 */
}

/* Each frame (0x452590): the world into the car's frame (0x4660a0, 0x4660e0, 0x466140), its depth moved by
   +0x61bd50; a copy raised by -4.8 (0x5cec54) and tilted by -17 degrees about x (0x5cec5c); then the
   police car whose roof light shines. Only at night. */
void Headlights_Update(void)
{
    if (!g_r3d_night || !s_car) return;
    RFrame f;
    RFrame_Init(&f);
    memcpy(f.eye, CAR_F(s_car, 0x330), 12);
    memcpy(f.orient, CAR_F(s_car, 0x364), 36);
    /* (0x466140 transposes the orientation into the rotation; 0x4660e0 turns the eye with it) */
    const float *o = f.orient;
    float *m = f.rot;
    m[0] = o[0]; m[1] = o[3]; m[2] = o[6];
    m[3] = o[1]; m[4] = o[4]; m[5] = o[7];
    m[6] = o[2]; m[7] = o[5]; m[8] = o[8];
    float e[3] = {-f.eye[0], -f.eye[1], -f.eye[2]};
    for (int j = 0; j < 3; j++) f.trans[j] = e[0] * m[j] + e[1] * m[3 + j] + e[2] * m[6 + j];
    EAGL_MatFromRotTrans(&s_space, f.rot, f.trans);
    s_space.m[14] += s_zoff;
    s_tilted = s_space;
    s_tilted.m[13] = s_space.m[13] + -4.8f;
    EaglMatrix x;
    EAGL_MatAxisAngle(&x, -17.0f, 1.0f, 0, 0);
    EAGL_MatPostMul(&s_tilted, &x);
    /* The police car with its siren on (+0xecc bit 1) nearest the camera within 180 (octagonally), its
       light's frame turning with the clock (0.02 turns a tick about its up axis, 0x556200). */
    if (g_chasers.n <= 0) return;
    s_cop = NULL;
    float best = 180.0f;
    const float *eye = g_racecam[0].eye;
    for (int i = 0; i < g_chasers.n; i++) {
        Car *p = g_chasers.c[i];
        if (!p->b[0x7f] || !(*CAR_I(p, 0xecc) & 2)) continue;
        float dx = fabsf(CAR_F(p, 0x330)[0] - eye[0]), dz = fabsf(CAR_F(p, 0x330)[2] - eye[2]);
        float d = dx <= dz ? dx * K(0x5b2468) + dz : dz * K(0x5b2468) + dx;
        if (d < best) best = d, s_cop = p;
    }
    if (!s_cop) return;
    s_cop_col[0] = 0xffff0000u;
    s_cop_col[1] = 0xff0000ffu;
    float t = (float)g_sim_tick * K(0x5b3714) * 6.2831855f;   /* (TIMER_ticks: the simulation's clock) */
    float sn = sinf(t), cs = cosf(t);
    const float *r = CAR_F(s_cop, 0x364);
    float q[9] = {cs * r[0] - r[6] * sn, r[1] * cs - r[7] * sn, r[2] * cs - r[8] * sn, r[3], r[4], r[5],
                  sn * r[0] + r[6] * cs, r[7] * cs + r[1] * sn, r[8] * cs + r[2] * sn};
    float rot[9] = {q[0], q[3], q[6], q[1], q[4], q[7], q[2], q[5], q[8]}, tr[3];
    const float *cp = CAR_F(s_cop, 0x330);
    for (int j = 0; j < 3; j++) tr[j] = -cp[0] * rot[j] - cp[1] * rot[3 + j] - cp[2] * rot[6 + j];
    EAGL_MatFromRotTrans(&s_copspace, rot, tr);
}

bool Headlights_CopOn(void) { return s_cop != NULL; }

/* The roof light on n points (0x452920): through its frame alone (the model's matrix not applied: the
   original's), the beam's table by depth either side (|z| x 3.5, side x 3.5) less 16 x height, / 128. */
void Headlights_CopLight(int n, const float *pts)
{
    static float p[256][4];
    if (n > 256) n = 256;
    EAGL_MatTransform4(&s_copspace, n, pts, 0x10, p[0], 0x10);
    for (int i = 0; i < n; i++) {
        int v = 0;
        int iz = (int)lrintf(p[i][2] * K(0x5cec6c));
        if (s_beam && -0x7f < iz && iz < 0x7f) {
            int ix = (int)lrintf(p[i][0] * K(0x5cec6c));
            if (-0x3f < ix && ix < 0x3f) v = s_beam[(iz < 0 ? -iz : iz) * 0x80 + ix + 0x40] - (int)lrintf(p[i][1] * K(0x5cec70));
        }
        s_cop_light[i] = (float)v * K(0x5cec68);
        s_cop_neg[i] = signbit(p[i][2]) != 0;
    }
}

uint32_t Headlights_CopAdd(int i, uint32_t cv, bool flip)
{
    if (i < 0 || 255 < i) i = 255;
    float li = s_cop_light[i];
    if (!(K(0x5b23f8) < li)) return cv;
    uint32_t c = s_cop_col[s_cop_neg[i] != flip ? 1 : 0];
    int r = (int)lrintf((float)(c >> 16 & 0xff) * li) + (int)(cv >> 16 & 0xff);
    int g = (int)lrintf((float)(c >> 8 & 0xff) * li) + (int)(cv >> 8 & 0xff);
    int b = (int)lrintf(li * (float)(c & 0xff)) + (int)(cv & 0xff);
    return (uint32_t)(r < 0xff ? r : 0xff) << 16 | (uint32_t)(g < 0xff ? g : 0xff) << 8 | (uint32_t)(b < 0xff ? b : 0xff) | 0xff000000u;
}

/* The light of n points (16 bytes each) through the model matrix (when it scales) and the headlight
   space (0x4527e0): its depth x 1.28 and side x 3.62 index the beam's table (depth 1 .. 126, side
   -62 .. 62), less 32 x |height|; / 256. */
void Headlights_Light(int n, const float *pts, const float *model)
{
    EaglMatrix m = s_space;
    if (model[0] != 1.0f || model[5] != 1.0f || model[10] != 1.0f) {
        EaglMatrix mm;
        memcpy(mm.m, model, sizeof mm.m);
        EAGL_MatPreMul(&m, &mm);
    }
    static float p[256][4];
    if (n > 256) n = 256;
    EAGL_MatTransform4(&m, n, pts, 0x10, p[0], 0x10);
    for (int i = 0; i < n; i++) {
        int v = 0;
        int iz = (int)lrintf(p[i][2] * s_zscale);
        if (s_beam && 0 < iz && iz < 0x7f) {
            int ix = (int)lrintf(p[i][0] * s_xscale);
            if (-0x3f < ix && ix < 0x3f) {
                int h = (int)lrintf(p[i][1] * 32.0f);   /* 0x5cec60 */
                if (h < 0) h = -h;
                v = s_beam[iz * 0x80 + ix + 0x40] - h;
            }
        }
        g_headlight_light[i] = (float)v * 0.00390625f;   /* 0x5cec64 */
    }
}
