/* The cars' damage (see car_damage.h). */
#include "game/car_damage.h"
#include "exe.h"
#include "game/car_drive.h"
#include "game/race_car.h"
#include "game/sim_math.h"
#include "game/sim_rand.h"
#include "pending.h"
#include <math.h>
#include <string.h>

int32_t g_race_damage;

static float exe_f32(uint32_t va)
{
    uint32_t u = exe_u32(va);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

#define K(va) exe_f32(va)

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }   /* Vec3_Dot */
static float *zone(Car *c, int z) { return CAR_F(c, 0x4e4 + 4 * z); }
static float rnd(void) { return (float)SimRand_Next(); }   /* the generator's 0..0xffff */

/* Zone a held at least at the mean of b and c. */
static void spread(Car *c, int a, int b, int d)
{
    float m = (*zone(c, b) + *zone(c, d)) * K(0x5b23f0);
    if (m < *zone(c, a)) m = *zone(c, a);
    *zone(c, a) = m;
}

/* The spin held within 0.75 turns a second (0x49bb10, as 0x496440). */
static void hold_spin(Car *c)
{
    float lim = K(0x5b49c0);
    if (!DESC_I(c, 0x518)) {
        lim = K(0x5b260c);
        if ((*CAR_I(c, CAR_FLAGS) & 0x80) && *CAR_F(c, 0x354) < K(0x5b260c)) lim = K(0x5b49bc);
    }
    for (int k = 0; k < 3; k++) {
        float *v = CAR_F(c, 0x388 + 4 * k);
        if (lim < *v) *v = lim;
        else if (*v < -lim) *v = -lim;
    }
}

void CarSim_ZoneDamage(Car *c, float impact, int z, int kick)
{
    bool tiny = DESC_I(c, 0x518) != 0, traffic = (*CAR_I(c, CAR_FLAGS) & 0x80) != 0;
    if (g_race_damage && *CAR_I(c, 0xb50) == 0) {
        float d = impact * (g_race_derby ? K(0x5b246c) : K(0x5b23f0));
        if (K(0x5b242c) < d) d = 100.0f;
        if ((z & 1) == 0 && K(0x5b2408) < d && *zone(c, z) < K(0x5b2408))
            Car_AddEvent(c, 1, -1, 4, 3, d, 0);   /* 0x40f080: a dent's sound */
        if (d < *zone(c, z)) d = *zone(c, z);
        *zone(c, z) = d;
        switch (z) {
        case 0: spread(c, 1, 2, 0); spread(c, 7, 6, 0); break;
        case 1: spread(c, 0, 7, 1); spread(c, 2, 3, 1); break;
        case 6: spread(c, 5, 4, 6); spread(c, 7, 0, 6); break;
        case 7: spread(c, 0, 1, 7); spread(c, 6, 5, 7); break;
        case 8: break;   /* the roof */
        default: {
            /* 2 .. 5: the next from the new value and the one past it, the previous from its sides. */
            float m = (d + *zone(c, z + 2)) * K(0x5b23f0);
            if (m < *zone(c, z + 1)) m = *zone(c, z + 1);
            *zone(c, z + 1) = m;
            spread(c, z - 1, z - 2, z);
        }
        }
    }
    if (!kick) return;
    float k = 1.0f, s;
    float p = impact;
    if (g_race_mode == 3 && g_race_network) p = p * K(0x5b23f0);
    else if (tiny) p = p + p;
    float *vel = CAR_F(c, 0x33c), *w = CAR_F(c, 0x388);
    if (p <= K(0x5b26d4)) {
        s = p * K(0x5b24b8);
        if (K(0x5b3ecc) <= s) s = 0.33f;
    } else {
        /* A crash: thrown up and out of control. */
        if (!traffic) *CAR_I(c, 0x42c) = 1;
        if (!tiny && K(0x5b3d60) < p) *CAR_I(c, 0xdb0) = 0;
        p = p * K(0x5b272c);
        uint16_t one = 1;
        memcpy(c->b + 0x418, &one, 2);
        *CAR_I(c, 0x450) = 0;
        *CAR_I(c, 0x1100) = 0xc0;
        *CAR_I(c, 0x1104) = 1;
        if (*CAR_F(c, 0x424) < K(0x5b2604)) CAR_F(c, 0x330)[1] = CAR_F(c, 0x330)[1] + K(0x5b2604);
        s = p * K(0x5b24b8);
        if (K(0x5b2580) <= s) s = 2.0f;
        k = tiny ? K(0x5b2600) : K(0x5b2480);
        if (!traffic || K(0x5b24a8) <= *CAR_F(c, 0x354)) {
            p = p * K(0x5b2680);
        } else {
            float f = rnd() * K(0x5b26a0) * K(0x5b253c) + K(0x5b2604);
            float m = *CAR_F(c, 0x354);
            if (m <= f) m = f;
            p = m * p * K(0x5b3ecc);
        }
        vel[1] = p + vel[1];
        if (kick == 2 && fabsf(w[0]) < K(0x5b23f0) && fabsf(w[2]) < K(0x5b23f0)) {
            if (!traffic && !tiny) {
                w[0] = rnd() * K(0x5b2400) * K(0x5b253c) - K(0x5b2464);
                float r = rnd();
                w[1] = (r + r) * K(0x5b253c) - K(0x5b24a8);
                w[2] = rnd() * K(0x5b2400) * K(0x5b253c) - K(0x5b2464);
            } else {
                w[0] = rnd() * K(0x5b2470) * K(0x5b253c) - K(0x5b241c);
                w[1] = rnd() * K(0x5b241c) * K(0x5b253c) - K(0x5b2400);
                w[2] = rnd() * K(0x5b2470) * K(0x5b253c) - K(0x5b241c);
            }
        }
    }
    /* The spin in the car's axes, knocked about the zone's side; the forward part takes the up part's
       knock too (as the original reuses it). */
    float *l = CAR_F(c, 0xd64);
    l[0] = dot3(w, CAR_F(c, 0x364));
    l[1] = dot3(w, CAR_F(c, 0x370));
    float lf = dot3(w, CAR_F(c, 0x37c));
    float knock = k * s * K(0x5b23f0);
    l[0] = (z < 0 || z > 2) ? knock + l[0] : l[0] - knock;
    if (z < 2 || z > 4) {
        l[1] = l[1] - knock;
        lf = lf - knock;
    } else {
        l[1] = knock + l[1];
        lf = knock + lf;
    }
    l[2] = lf;
    float t[9];
    Mat3_Transpose(CAR_F(c, 0x364), t);
    if (*CAR_I(c, 0x434) == 0) {
        /* (0x532550, 0x532560: the angle in radians rounded to a float, the sine unrounded until the
           product; single precision after) */
        double a = (float)((double)l[2] * 6.283185307179586);
        float q[3] = {-(float)(sin(a) * *CAR_F(c, 0x3a8)), -(float)(cos(a) * *CAR_F(c, 0x3ac)), 0};
        CAR_F(c, 0x330)[1] = dot3(q, t + 3) * K(0x5b24b8) + CAR_F(c, 0x330)[1];
    }
    w[0] = dot3(l, t);
    w[1] = dot3(l, t + 3);
    w[2] = dot3(l, t + 6);
    if (kick != 2) hold_spin(c);
}

void CarSim_ImpactDamage(Car *c, const float n[3], int kick)
{
    float r = dot3(n, CAR_F(c, 0x364)), u = dot3(n, CAR_F(c, 0x370)), f = dot3(n, CAR_F(c, 0x37c));
    float impact = *CAR_F(c, 0x430);
    if (impact <= K(0x5b241c)) return;
    int z = 9;
    if (u < K(0x5b2448) && !(*CAR_I(c, CAR_FLAGS) & 0x80)) {
        z = 8;
        CarSim_ZoneDamage(c, impact, 8, kick);
    }
    float lo = K(0x5b2604), nlo = K(0x5b2444);   /* 0.1, -0.1 */
    if (fabsf(r) < lo && f < nlo) z = 1;
    else if (fabsf(r) < lo && lo < f) z = 5;
    else if (fabsf(f) < lo && lo < r) z = 7;
    else if (fabsf(f) < lo && r < nlo) z = 3;
    else if (f < nlo && lo < r) z = 0;
    else if (f < nlo && r < nlo) z = 2;
    else if (lo < f && lo < r) z = 6;
    else if (lo < f && r < nlo) z = 4;
    else if (z > 7) return;
    CarSim_ZoneDamage(c, *CAR_F(c, 0x430), z, kick);
}
