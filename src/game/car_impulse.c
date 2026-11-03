/* The car body's collision impulses (see car_impulse.h). */
#include "game/car_impulse.h"
#include "exe.h"
#include "game/car_damage.h"
#include "pending.h"
#include <math.h>
#include <string.h>

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static float bits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

#define K(va) exe_f32(va)

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }   /* Vec3_Dot */

static void cross3(const float a[3], const float b[3], float out[3])   /* 0x5325f0 */
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

static void scale3(float *v, float k)   /* Vec3_ScaleArray */
{
    for (int i = 0; i < 3; i++) v[i] *= k;
}

void CarSim_ClampSpin(Car *c)
{
    float lim = K(0x5b49c0);
    if (!DESC_I(c, 0x518)) {   /* not TINYCAR */
        lim = K(0x5b260c);
        if ((*CAR_I(c, CAR_FLAGS) & 0x80) && *CAR_F(c, 0x354) < K(0x5b260c)) lim = K(0x5b49bc);
    }
    float *w = CAR_F(c, 0x388);
    for (int k = 0; k < 3; k++) {
        if (w[k] > lim) w[k] = lim;
        else if (w[k] < -lim) w[k] = -lim;
    }
}

void CarSim_ContactImpulse(Car *c, const float at[3], const float v[3], const float n[3], float fr)
{
    float t[3] = {v[0], v[1], v[2]}, tl = 0;
    float *vel = CAR_F(c, 0x33c), *w = CAR_F(c, 0x388);
    float vn = dot3(n, vel);
    const float *pos = CAR_F(c, 0x330);
    float r[3] = {at[0] - pos[0], at[1] - pos[1], at[2] - pos[2]}, rn[3];
    cross3(r, n, rn);
    float im = *CAR_F(c, 0x358), ii = *CAR_F(c, 0x398);
    /* (in single precision, as the x87 runs) */
    float num = dot3(w, rn) + vn;
    float len = sqrtf(rn[0] * rn[0] + rn[1] * rn[1] + rn[2] * rn[2]);   /* 0x532890 */
    float den = (ii + ii) * len * K(0x5b23f0) + im * K(0x5b23f0) * K(0x5b23f0);
    float j = -(num / den);
    float k = K(0x5b25f0), jn;
    bool scaled = false;
    if (*CAR_I(c, CAR_FLAGS) & 0x80) {
        /* Traffic: by its largest half size past 1.5. */
        float e = *CAR_F(c, 0x3a8);
        if (e <= *CAR_F(c, 0x3ac)) e = *CAR_F(c, 0x3ac);
        if (e <= *CAR_F(c, 0x3b0)) e = *CAR_F(c, 0x3b0);
        k = K(0x5b261c);
        if (K(0x5b246c) < e) {
            jn = j * K(0x5b49b8) / e;
            scaled = true;
        }
    }
    if (!scaled) jn = j * k;
    *CAR_I(c, 0x434) = 0;
    *CAR_F(c, 0x430) = fabsf(vn);
    *CAR_I(c, 0x438) = *CAR_I(c, 0x464) | 0x30000;
    memcpy(CAR_F(c, 0x440), at, 12);
    if (K(0x5b23f8) < fr) {
        if (t[0] != K(0x5b23f8) || t[1] != K(0x5b23f8) || t[2] != K(0x5b23f8)) {
            /* The sliding direction: the point's velocity in the plane, reversed. */
            float d = dot3(t, n);
            for (int i = 0; i < 3; i++) t[i] = t[i] - n[i] * d;
            tl = sqrtf(dot3(t, t));   /* 0x532540 */
            float s = tl <= K(0x5b27a8) ? -1.0f : -(K(0x5b23f0) / (tl * K(0x5b23f0)));
            scale3(t, s);
        }
        float f = jn * fr;
        float a[3] = {t[0], t[1], t[2]}, b[3];
        scale3(a, f * im * K(0x5b23f0));
        cross3(t, r, b);
        cross3(b, r, b);
        scale3(b, f * ii + f * ii);
        for (int i = 0; i < 3; i++) a[i] = a[i] + b[i];
        float q = dot3(a, t);
        if (tl < q) f = tl / q * f;
        scale3(t, f);
    }
    if (K(0x5b27b4) <= jn) {
        float imp[3] = {n[0] * jn + t[0], n[1] * jn + t[1], n[2] * jn + t[2]}, dv[3], dw[3];
        for (int i = 0; i < 3; i++) dv[i] = imp[i] * (im * K(0x5b23f0));
        for (int i = 0; i < 3; i++) vel[i] = vel[i] + dv[i];
        cross3(r, imp, dw);
        scale3(dw, ii + ii);
        for (int i = 0; i < 3; i++) w[i] = w[i] + dw[i];
        bool slow = (*CAR_I(c, CAR_FLAGS) & 0x80) || *CAR_F(c, 0x3a0) < K(0x5b23f4);
        if (slow && *CAR_F(c, 0x428) < K(0x5b2738) && jn < K(0x5b241c) && fabsf(w[0]) < K(0x5b24a8) &&
            fabsf(w[1]) < K(0x5b24a8) && fabsf(w[2]) < K(0x5b24a8)) {
            scale3(w, bits(0x3f400000));
            scale3(vel, bits(0x3f733333));
        }
        return;
    }
    scale3(w, bits(0x3f666666));
    scale3(vel, bits(0x3f666666));
}

void CarSim_BodyImpulse(Car *c, const float n[3], const float at[3], float fr)
{
    int np = *CAR_I(c, 0x32c);
    if (np <= 0) return;
    float deepest = -1.0f, cnt = 0, sum[3] = {0, 0, 0};
    for (int i = 0; i < np; i++) {
        const float *p = CAR_F(c, 0x1dc + 12 * i);
        float d[3] = {p[0] - at[0], p[1] - at[1], p[2] - at[2]};
        float depth = -dot3(d, n);
        if (K(0x5b49c4) < depth) {
            cnt = cnt + K(0x5b24a8);
            if (deepest < depth) deepest = depth;
            for (int k = 0; k < 3; k++) sum[k] = sum[k] + p[k];
        }
    }
    if (!(K(0x5b23f8) < cnt)) return;
    if (deepest < K(0x5b23f8)) deepest = 0;
    scale3(sum, K(0x5b24a8) / cnt);
    float *w = CAR_F(c, 0x388), *pos = CAR_F(c, 0x330);
    scale3(w, bits(0x40c90fd0));   /* turns to radians */
    float r[3] = {sum[0] - pos[0], sum[1] - pos[1], sum[2] - pos[2]}, v[3];
    cross3(w, r, v);
    for (int k = 0; k < 3; k++) v[k] = v[k] + CAR_F(c, 0x33c)[k];
    CarSim_ContactImpulse(c, sum, v, n, fr);   /* 0x495ea0 */
    float push[3] = {n[0] * deepest, n[1] * deepest, n[2] * deepest};
    for (int k = 0; k < 3; k++) pos[k] = pos[k] + push[k];
    for (int wl = 0; wl < 4; wl++)
        for (int k = 0; k < 3; k++) CAR_F(c, 0x7f8 + 0xc4 * wl)[k] += push[k];
    scale3(w, bits(0x3e22f987));   /* back to turns */
    CarSim_ClampSpin(c);   /* 0x496440 */
    *CAR_F(c, 0x430) = fabsf(dot3(n, CAR_F(c, 0x33c))) * K(0x5b2404);
    CarSim_ImpactDamage(c, n, 0);   /* 0x494d60 */
}
