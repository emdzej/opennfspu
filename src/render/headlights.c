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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *s_psh;               /* 0x61c164 */
static const uint8_t *s_beam;        /* 0x61e1cc: the table's bytes (the shape's +0x10) */
static Car *s_car;                   /* 0x61bcc8 */
uint32_t g_headlight_colour;         /* 0x61bd4c */
static float s_zoff, s_zscale = 1.28f, s_xscale = 3.62f;   /* 0x61bd50, 0x61bd54, 0x61bd58 (the 0x61e178,
                                                              0x61d170 copies taken once) */
static EaglMatrix s_space, s_tilted; /* 0x61bccc, 0x61bd0c */
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
    PENDING_NOTE(0x44d1f0, "the headlights' 'HEAD' texture (0x61e1d4) and the cop lights' car (0x61e180)");
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
   +0x61bd50; a copy raised by -4.8 (0x5cec54) and tilted by -17 degrees about x (0x5cec5c). (The nearest
   cop car with its lights on, within 180, for the red and blue light: pending.) */
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
    PENDING_NOTE(0x452590, "the cop cars' lights in the headlights' pass (0x61e180)");
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
