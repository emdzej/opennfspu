/* The simulation's matrix and vector helpers (see sim_math.h). */
#include "game/sim_math.h"
#include "exe.h"
#include "render/camera_math.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static double two_pi(void)
{
    uint64_t u = (uint64_t)exe_u32(0x5bb2c4) << 32 | exe_u32(0x5bb2c0);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

/* 0x532570: the sine and cosine of an angle in turns (0x532550 / 0x532560). */
static void sincos_turns(float t, float *s, float *c)
{
    *s = (float)sin((double)t * two_pi());
    *c = (float)cos((double)t * two_pi());
}

void Vec3_AddArray(int n, const float *src, const float add[3], float *dst)
{
    for (; n > 0; n--, src += 3, dst += 3) {
        float a = src[1], b = add[1], c = src[2], d = add[2];
        dst[0] = src[0] + add[0];
        dst[1] = a + b;
        dst[2] = c + d;
    }
}

void Mat3_RotX(float m[9], float t)
{
    float s, c;
    sincos_turns(t, &s, &c);
    m[4] = c;
    m[1] = m[2] = m[3] = m[6] = 0;
    m[7] = -s;
    m[0] = 1;
    m[5] = s;
    m[8] = c;
}

void Mat3_RotY(float m[9], float t)
{
    float s, c;
    sincos_turns(t, &s, &c);
    m[0] = c;
    m[1] = m[3] = m[5] = m[7] = 0;
    m[4] = 1;
    m[2] = -s;
    m[6] = s;
    m[8] = c;
}

void Mat3_RotZ(float m[9], float t)
{
    float s, c;
    sincos_turns(t, &s, &c);
    m[0] = c;
    m[1] = s;
    m[3] = -s;
    m[2] = 0;
    m[4] = c;
    m[5] = m[6] = m[7] = 0;
    m[8] = 1;
}

void Mat3_Transpose(const float m[9], float o[9])
{
    float t[9] = {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
    memcpy(o, t, sizeof t);
}

void Mat3_Orthonormalize(float m[9])
{
    float id[9];
    for (int i = 0; i < 9; i++) id[i] = exe_f32(0x5d1068 + 4u * (unsigned)i);   /* the identity */
    for (int round = 0; round < 4; round++) {
        float mt[9], mtm[9], e[9], pw[9], sum[9];
        Mat3_Transpose(m, mt);
        Mat3_Mul(mt, m, mtm);
        for (int i = 0; i < 9; i++) e[i] = mtm[i] - id[i];
        memcpy(pw, id, sizeof pw);
        memcpy(sum, id, sizeof sum);
        for (uint32_t va = 0x5d1040; va < 0x5d104c; va += 4) {
            Mat3_Mul(pw, e, pw);
            float k = exe_f32(va);
            for (int i = 0; i < 9; i++) sum[i] = sum[i] + pw[i] * k;
        }
        Mat3_Mul(m, sum, m);
    }
}
